/* 
   v3.1 Rssi added

   7-24-26 
   --- Cummins Multi-PGN Deep Diagnostic & Vehicle Control Interface
   1. Wire Connection: TX to GPIO1, RX to GPIO0 (via logic shifter), Grounded RS Pin.
   2. Open Wi-Fi on device and connect to "Cummins_Live_Dashboard" with password "12345678"
   3. Open browser and access address "192.168.4.1"
*/
#include <Arduino.h>
#include "driver/twai.h"
#include "esp_wifi.h"
#include "esp_bt.h"
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <stdarg.h>
#include <ArduinoOTA.h>
#include <LittleFS.h>

#define CTX_PIN GPIO_NUM_1
#define CRX_PIN GPIO_NUM_0
#define STATUS_LED_PIN GPIO_NUM_8

unsigned long lastLedToggle = 0;
unsigned long lastDataReceivedTime = 0;
bool ledState = false;

String webLogBuffer = "";
WebServer server(80);
bool isUpdating = false;
unsigned long globalLogEntryCounter = 0;

enum GenControlCommand { CMD_RELEASE = 0, CMD_START = 1, CMD_STOP = 2, CMD_PRIME = 5 };
volatile GenControlCommand currentActiveCommand = CMD_RELEASE;

// --- Live Telemetry Variables Decoded from Cummins Matrix ---
volatile float liveBatteryVoltage = 0.0;
volatile float liveEngineRPM = 0.0;
volatile int16_t liveInverterTemp = 0;
volatile float liveACFrequency = 0.0;
volatile uint16_t liveACVoltage = 0;
volatile uint8_t globalGensetState = 1; 

TaskHandle_t xTwaiTaskHandle = NULL;
SemaphoreHandle_t logMutex = NULL;

void logMessage(const char* format, ...) {
    unsigned long totalSeconds = millis() / 1000;
    unsigned int seconds = totalSeconds % 60;
    unsigned int minutes = (totalSeconds / 60) % 60;
    unsigned int hours = (totalSeconds / 3600);
    char header_buf[32];

    if (format[0] != '\n' && format[0] != '-' && format[0] != '_') {
        globalLogEntryCounter++;
        snprintf(header_buf, sizeof(header_buf), "[#%lu @ %02u:%02u:%02u] ", globalLogEntryCounter, hours, minutes, seconds);
    } else {
        header_buf[0] = '\0';
    }

    char payload_buf[256];
    va_list arg;
    va_start(arg, format);
    vsnprintf(payload_buf, sizeof(payload_buf), format, arg);
    va_end(arg);

    char final_buf[300];
    snprintf(final_buf, sizeof(final_buf), "%s%s", header_buf, payload_buf);
    Serial.print(final_buf);

    if (logMutex != NULL && xSemaphoreTake(logMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        webLogBuffer += String(final_buf);
        if(webLogBuffer.length() > 6000) {
            webLogBuffer = webLogBuffer.substring(webLogBuffer.length() - 3000);
        }
        xSemaphoreGive(logMutex);
    }

    File logFile = LittleFS.open("/log.txt", FILE_APPEND);
    if (logFile) { logFile.print(final_buf); logFile.close(); }
}

struct OnanFaultMapping { uint16_t faultNumber; uint32_t spn; uint8_t fmi; const char* displayLabel; };
const char f_msg_01[] PROGMEM = "Engine Temperature Exceeded Limit";
const char f_msg_04[] PROGMEM = "Over Crank Fault";
const char f_msg_06[] PROGMEM = "Low Oil Level / Pressure Failure";
const char f_msg_12[] PROGMEM = "Over Voltage Control Circuit Shutdown";
const char f_msg_13[] PROGMEM = "Under Voltage Power Generation Interruption";
const char f_msg_14[] PROGMEM = "Over Frequency Operational Limit Exceeded";
const char f_msg_15[] PROGMEM = "Under Frequency Operational Control Limit";
const char f_msg_19[] PROGMEM = "Governor Actuator Configuration Sensor Fault";
const char f_msg_27[] PROGMEM = "Voltage Capture Control PMA Read Error";
const char f_msg_29[] PROGMEM = "High Battery Voltage Warning Limit";
const char f_msg_31[] PROGMEM = "Engine Over Speed Mechanical Safety Cutout";
const char f_msg_34[] PROGMEM = "Inverter Temperature Exceeded Limit";
const char f_msg_36[] PROGMEM = "Abnormal Genset Uncommanded Shutdown";
const char f_msg_38[] PROGMEM = "Field Overload Exciter Output Saturation";
const char f_msg_43[] PROGMEM = "Control Board Internal ECU Memory Failure";
const char f_msg_57[] PROGMEM = "Over Prime / Fuel Pressure / LPG Valve Fault";
const char f_msg_73[] PROGMEM = "AC Output Circuit Overcurrent Fault";

const OnanFaultMapping ONAN_FAULT_TABLE[] PROGMEM = {
    {1, 110, 0, f_msg_01}, {4, 1213, 7, f_msg_04}, {6, 98, 1, f_msg_06},
    {12, 1795, 0, f_msg_12}, {13, 1795, 1, f_msg_13}, {14, 1797, 0, f_msg_14},
    {15, 1797, 1, f_msg_15}, {19, 1479, 7, f_msg_19}, {27, 4220, 2, f_msg_27},
    {29, 168, 0, f_msg_29}, {31, 190, 0, f_msg_31}, {34, 1798, 0, f_msg_34},
    {36, 1213, 3, f_msg_36}, {38, 1799, 0, f_msg_38}, {43, 611, 12, f_msg_43},
    {57, 1213, 5, f_msg_57}, {73, 1795, 6, f_msg_73}
};
const int ONAN_DB_COUNT = sizeof(ONAN_FAULT_TABLE) / sizeof(ONAN_FAULT_TABLE[0]);

void processHglcaNetworkFrame(twai_message_t msg);
void twaiBackgroundEngine(void *pvParameters);
// ============================================================================
// 📦 SECTION 2: RE-ALIGNED HTML COMPILATION LAYOUT (Restores Upload Form Box)
// ============================================================================
const char htmlDashboard[] PROGMEM = "<!DOCTYPE html><html><head>"
"<meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1'>"
"<style>body{font-family:sans-serif; background:#121212; color:#e0e0e0; padding:15px; text-align:center;}"
"h2, h3{color:#00adb5; margin:10px 0;} .box{background:#1e1e1e; padding:15px; border-radius:8px; margin:0 auto 15px auto; max-width:750px; border:1px solid #333;}"
".grid{display:flex; flex-wrap:wrap; gap:10px; justify-content:center; max-width:750px; margin:0 auto 15px auto;}"
".metric-card{background:#1e1e1e; border:1px solid #333; border-radius:6px; padding:12px; width:115px; height:85px; text-align:center; box-sizing:border-box; display:flex; flex-direction:column; justify-content:space-between;}"
".lbl{font-size:11px; color:#aaa; white-space:nowrap; overflow:hidden; text-overflow:ellipsis;}"
".val{font-size:16px; font-weight:bold; color:#00adb5; margin-top:2px;}"
"pre{background:#000; color:#0f0; padding:12px; border-radius:5px; overflow-y:scroll; height:200px; font-family:monospace; text-align:left; white-space:pre-wrap; margin-bottom:10px;}"
"input[type=file]{background:#2d2d2d; padding:6px; border-radius:4px; color:#fff; border:1px solid #444;}"
"input[type=button], .btn-action{background:#00adb5; color:#fff; border:none; padding:10px 15px; border-radius:4px; cursor:pointer; font-weight:bold; text-decoration:none; display:inline-block; margin:4px;}"
".btn-clear{background:#3d3d3d;}.btn-start{background:#5cb85c;}.btn-stop{background:#d9534f;}.btn-prime{background:#f0ad4e; color:#222;}"
".progress-container{width:100%; background-color:#2d2d2d; border-radius:4px; margin-top:10px; display:none;}"
".progress-bar{width:0%; height:18px; background-color:#00adb5; border-radius:4px; text-align:center; line-height:18px; color:white; font-size:11px;}"
"#status-msg{margin-top:8px; font-weight:bold; color:#ffb703;}</style></head><body>"
"<h2>Cummins HGLCA Smart Control Interface</h2>"
"<div class='grid'>"
" <div class='metric-card'><div class='lbl'>🔋 Battery Input</div><div class='val' id='m-volts'>0.0V</div></div>"
" <div class='metric-card'><div class='lbl'>⚙️ Engine Speed</div><div class='val' id='m-rpm'>0 RPM</div></div>"
" <div class='metric-card'><div class='lbl'>🔥 Inverter Core</div><div class='val' id='m-temp'>0&deg;C</div></div>"
" <div class='metric-card'><div class='lbl'>⚡ AC Output</div><div class='val' id='m-acv'>0V</div></div>"
" <div class='metric-card'><div class='lbl'>🌀 Frequency</div><div class='val' id='m-hz'>0.0Hz</div></div>"
" <div class='metric-card'><div class='lbl'>📶 Signal Status</div><div class='val' id='m-rssi'>N/A</div></div>"
"</div>"
"<div class='box'><h3>Diagnostic Telemetry Stream</h3><pre id='terminal'>Awaiting PGN frame updates...</pre>"
"<a href='/download-log' download='onan_generator_log.txt' class='btn-action'>💾 Download Log</a>"
"<button onclick='clearSystemLog()' class='btn-action btn-clear'>🗑 Wipe Saved Log</button></div>"
"<div class='box'><h3>⚡ Hybrid Remote Controller Panel</h3>"
"<button onclick='sendCmd(\"/gen-prime\")' class='btn-action btn-prime'>💽 Prime Fuel (12s)</button>"
"<button onclick='sendCmd(\"/gen-start\")' class='btn-action btn-start'>🚀 Crank Start (4s)</button>"
"<button onclick='sendCmd(\"/gen-stop\")' class='btn-action btn-stop'>🛑 Kill Engine (2s)</button></div>"
"<div class='box'><h3>Wireless Firmware Management</h3><form id='upload-form' enctype='multipart/form-data'>"
"<input type='file' id='file-input' name='update' accept='.bin' required> "
"<input type='button' value='Flash Payload (.bin)' onclick='uploadFile()'></form>"
"<div class='progress-container' id='prg-wrapper'><div class='progress-bar' id='prg-bar'>0%</div></div><div id='status-msg'></div></div>"
"<script>var term = document.getElementById('terminal'); var jsUpdating = false;"
"function poll(){ if(jsUpdating) return;"
" fetch('/telemetry-json').then(r=>r.json()).then(d=>{"
" document.getElementById('m-volts').innerText=d.v.toFixed(1)+'V'; document.getElementById('m-rpm').innerText=Math.round(d.r)+' RPM';"
" document.getElementById('m-temp').innerText=d.t+'°C'; document.getElementById('m-acv').innerText=d.av+'V'; document.getElementById('m-hz').innerText=d.hz.toFixed(1)+'Hz';"
" let rStr = 'Disconn.'; if(d.sig !== 0) { rStr = d.sig + ' dBm'; }"
" document.getElementById('m-rssi').innerText = rStr;"
" });"
" fetch('/telemetry').then(r=>r.text()).then(t=>{ if(t.trim()!==''){ term.innerHTML=t; term.scrollTop=term.scrollHeight; } });"
"} setInterval(poll,500); function sendCmd(url){ fetch(url,{method:'POST'}); }"
"function clearSystemLog(){ if(confirm('Wipe saved flash logs?')){ fetch('/clear-log',{method:'POST'}).then(() => { term.innerHTML=''; }); } }"
"function uploadFile(){ var fi=document.getElementById('file-input'); if(fi.files.length===0){alert('Select .bin!');return;} jsUpdating=true; var fd=new FormData(); fd.append('update',fi.files[0]); var xhr=new XMLHttpRequest(); xhr.open('POST','/update',true); document.getElementById('prg-wrapper').style.display='block'; document.getElementById('status-msg').innerText='Uploading firmware...';"
"xhr.upload.addEventListener('progress',function(e){ if(e.lengthComputable){ var p=Math.round((e.loaded/e.total)*100); document.getElementById('prg-bar').style.width=p+'%'; document.getElementById('prg-bar').innerText=p+'%'; } });"
"xhr.onload=function(){ if(xhr.status===200){ document.getElementById('status-msg').style.color='#00ff00'; document.getElementById('status-msg').innerText='✅ Success! Rebooting...'; }else{ document.getElementById('status-msg').innerText='❌ Failed: '+xhr.responseText; jsUpdating=false; } }; xhr.send(fd); }</script></body></html>";


// ============================================================================
// 📦 SECTION 3: CORE SETUP & DYNAMIC ARRAY JSON HANDLER (Fixed Memory Bug)
// ============================================================================
void setup() {
    Serial.begin(115200);
    delay(200);

    WiFi.mode(WIFI_AP);
    delay(100);
    if (WiFi.softAP("Cummins_Live_Dashboard", "12345678")) {
        Serial.println("📡 Wi-Fi Access Point Online!");
    }

    esp_wifi_set_ps(WIFI_PS_NONE); 
    WiFi.setTxPower(WIFI_POWER_13dBm);
    delay(200);

    if (LittleFS.begin(true)) {
        Serial.println("📂 Storage Mounted Successfully.");
    }

    pinMode(STATUS_LED_PIN, OUTPUT);
    logMutex = xSemaphoreCreateMutex();
    
    if (logMutex != NULL && xSemaphoreTake(logMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        webLogBuffer = ""; webLogBuffer.reserve(4096); xSemaphoreGive(logMutex);
    }
    ArduinoOTA.begin();

    server.on("/", HTTP_GET, []() { server.send(200, "text/html", htmlDashboard); });
    server.on("/telemetry", HTTP_GET, []() {
        String p = ""; if (logMutex != NULL && xSemaphoreTake(logMutex, pdMS_TO_TICKS(10)) == pdTRUE) { p = webLogBuffer; xSemaphoreGive(logMutex); }
        p.replace("\n", "<br>"); server.send(200, "text/plain", p);
    });

    // ✅ FIXED: Re-allocated full 128-byte array buffer stack size to absorb telemetry
    server.on("/telemetry-json", HTTP_GET, []() {
        int32_t activeRssiSignal = 0;
        wifi_sta_list_t connectedStations;
        if (esp_wifi_ap_get_sta_list(&connectedStations) == ESP_OK) {
            if (connectedStations.num > 0) { activeRssiSignal = connectedStations.sta[0].rssi; }
        }

        char j_buf[128]; // Fixed memory buffer constraint boundary
        snprintf(j_buf, sizeof(j_buf), "{\"v\":%.2f,\"r\":%.2f,\"t\":%d,\"hz\":%.2f,\"av\":%u,\"sig\":%d}", 
                 liveBatteryVoltage, liveEngineRPM, liveInverterTemp, liveACFrequency, liveACVoltage, activeRssiSignal);
        server.send(200, "application/json", j_buf);
    });

    server.on("/update", HTTP_POST, []() { server.send(200, "text/plain", "OK"); delay(1000); ESP.restart(); }, []() {
        HTTPUpload& u = server.upload();
        if (u.status == UPLOAD_FILE_START) { isUpdating = true; twai_stop(); twai_driver_uninstall(); Update.begin(UPDATE_SIZE_UNKNOWN); }
        else if (u.status == UPLOAD_FILE_WRITE) { Update.write(u.buf, u.currentSize); }
    });
    server.on("/download-log", HTTP_GET, []() { File f = LittleFS.open("/log.txt", FILE_READ); server.streamFile(f, "text/plain"); f.close(); });
    server.on("/clear-log", HTTP_POST, []() { LittleFS.remove("/log.txt"); webLogBuffer = ""; server.send(200, "text/plain", "OK"); });

    server.on("/gen-stop",  HTTP_POST, []() { currentActiveCommand = CMD_STOP;  server.send(200, "text/plain", "OK"); });
    server.on("/gen-start", HTTP_POST, []() { currentActiveCommand = CMD_START; server.send(200, "text/plain", "OK"); });
    server.on("/gen-prime", HTTP_POST, []() { currentActiveCommand = CMD_PRIME; server.send(200, "text/plain", "OK"); });

    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CTX_PIN, CRX_PIN, TWAI_MODE_NORMAL);
    g_config.rx_queue_len = 64;
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_250KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK && twai_start() == ESP_OK) {
        xTaskCreate(twaiBackgroundEngine, "TWAI_Task", 4096, NULL, 3, &xTwaiTaskHandle);
    }
    server.begin();
}


void loop() {
    server.handleClient();
    ArduinoOTA.handle();

    unsigned long currentMillis = millis();
    unsigned int flashInterval = (currentMillis - lastDataReceivedTime < 2000) ? 150 : 1000;

    if (currentMillis - lastLedToggle >= flashInterval) {
        lastLedToggle = currentMillis;
        ledState = !ledState;
        digitalWrite(STATUS_LED_PIN, ledState ? LOW : HIGH);
    }
    vTaskDelay(pdMS_TO_TICKS(2));
}

void twaiBackgroundEngine(void *pvParameters) {
    twai_message_t rx_msg;
    twai_message_t tx_msg;
    
    tx_msg.extd = 1;
    tx_msg.rtr = 0;
    tx_msg.data_length_code = 8;
    
    // ✅ TRUE CUMMINS CONTROL IDENTIFIER (PGN 65281 / 0xFF01 from Source Address 0x27)
    // Try changing 27 to 11 or 17 if the ECU filters out the service tool address slot.
    tx_msg.identifier = 0x0CFF0127; 
    
    unsigned long commandStartTime = 0;
    bool isCommandActive = false;

    while (1) {
        if (isUpdating) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
        GenControlCommand activeCmd = currentActiveCommand;

        // --- 1. LIGHTWEIGHT ONE-SHOT PULSE TRANSMITTER ---
        if (activeCmd != CMD_RELEASE) {
            if (!isCommandActive) {
                isCommandActive = true;
                commandStartTime = millis();
                logMessage("\n📡 [J1939] Targeting PGN 65281: Sending active control stream...\n");
            }

            // Fill bytes 1-7 with standard J1939 unused data padding
            for(int i = 1; i < 8; i++) { tx_msg.data[i] = 0xFF; }
            
            // ✅ IMAGE ANALYSIS COMPLIANCE: Target Byte 1 (Index 0) lower nibble
            if (activeCmd == CMD_STOP)        { tx_msg.data[0] = 0xF1; } // Hex 1 -> Stop Engine
            else if (activeCmd == CMD_START)  { tx_msg.data[0] = 0xF2; } // Hex 2 -> Start Engine
            else if (activeCmd == CMD_PRIME)  { tx_msg.data[0] = 0xF1; } // Prime uses a prolonged Stop signal hold

            twai_transmit(&tx_msg, pdMS_TO_TICKS(5));
            
            // Maintain transmission bursts for 4 seconds for Start, and 12 seconds for Prime
            unsigned long maxRuntime = (activeCmd == CMD_PRIME) ? 12000 : 4000;
            if (millis() - commandStartTime >= maxRuntime) {
                logMessage("\n⚠️ [J1939] Runtime complete. Sending Table 8 'None' release state.\n");
                
                tx_msg.data[0] = 0xF0; // Actively command 'None' (Hex 0) to release the relay line
                twai_transmit(&tx_msg, pdMS_TO_TICKS(5));
                
                currentActiveCommand = CMD_RELEASE;
                isCommandActive = false;
            }
            vTaskDelay(pdMS_TO_TICKS(50)); // Fast 50ms transmission pulse interval
        } else {
            isCommandActive = false; 
        }

        // --- 2. DE-COUPLED HIGH-SPEED RECEIVER ENGINE ---
        while (twai_receive(&rx_msg, pdMS_TO_TICKS(1)) == ESP_OK) {
            if (rx_msg.extd) { processHglcaNetworkFrame(rx_msg); }
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}


void processHglcaNetworkFrame(twai_message_t msg) {
    uint32_t pgn = (msg.identifier >> 8) & 0x3FFFF;
    if (((msg.identifier >> 16) & 0xFF) < 240) { pgn = (msg.identifier >> 8) & 0x3FF00; }

    switch(pgn) {
        case 65226: { // 🚨 PGN 65226 - J1939 DM1 Active Diagnostic Trouble Codes
            lastDataReceivedTime = millis();
            
            // Extract standard J1939 SPN and FMI bitfields from the array
            uint32_t parsedSpn = ((msg.data[2] & 0xE0) << 11) | (msg.data[1] << 8) | msg.data[0];
            uint8_t parsedFmi = msg.data[2] & 0x1F;

            static uint32_t lastParsedSpn = 0;
            static uint8_t lastParsedFmi = 0;

            if (parsedSpn != lastParsedSpn || parsedFmi != lastParsedFmi) {
                lastParsedSpn = parsedSpn;
                lastParsedFmi = parsedFmi;

                logMessage("\n⚠️ [J1939 FAULT EVENT] Active DTC -> SPN: %lu | FMI: %d \n", parsedSpn, parsedFmi);
                logMessage("🔧 [MANUAL REPAIR GUIDE DATA MATCHED]:\n");

                // --- 10.2.15 CODE 36 MATRIX ---
                if (parsedSpn == 110 && parsedFmi == 2) {
                    logMessage(" >> Factory Code : Code 36 (Abnormal Genset Shutdown)\n");
                    logMessage(" >> Root Cause   : Uncommanded Mechanical Stall / Fuel Loss.\n");
                    logMessage(" >> Action Plan  : 1. Check fuel supply. 2. Verify lines. 3. Check lift/IPM pump.\n");
                } 
                else if (parsedSpn == 1268 && parsedFmi == 4) {
                    logMessage(" >> Factory Code : Code 36 (Abnormal Genset Shutdown)\n");
                    logMessage(" >> Root Cause   : Primary Winding Open Circuit on Ignition Coil.\n");
                    logMessage(" >> Action Plan  : 1. Check connectors. 2. Check coil per section 8.6.1.\n");
                } 
                else if (parsedSpn == 1268 && parsedFmi == 3) {
                    logMessage(" >> Factory Code : Code 36 (Abnormal Genset Shutdown)\n");
                    logMessage(" >> Root Cause   : Overcurrent from ECU for Ignition Coil.\n");
                    logMessage(" >> Action Plan  : 1. Replace ECU. 2. Replace the wiring harness.\n");
                } 
                
                // --- 10.2.19 CODE 52 MATRIX ---
                else if (parsedSpn == 931 && parsedFmi == 2) {
                    logMessage(" >> Factory Code : Code 52 (Fuel Injector / IPM Pump Circuit Fault)\n");
                    logMessage(" >> Root Cause   : Fuel pump open circuit, overcurrent, short, or driver over-temp.\n");
                    logMessage(" >> Action Plan  : 1. Replace the fuel pump. 2. Replace the ECU.\n");
                }
                else if (parsedSpn == 651 && parsedFmi == 2) {
                    logMessage(" >> Factory Code : Code 52 (Fuel Injector / IPM Pump Circuit Fault)\n");
                    logMessage(" >> Root Cause   : Injector open circuit, overcurrent, short, or driver over-temp.\n");
                    logMessage(" >> Action Plan  : 1. Replace the fuel injector. 2. Replace the ECU.\n");
                }

                // --- 10.2.22 CODE 57 MATRIX ---
                else if (parsedSpn == 4083 && parsedFmi == 8) {
                    logMessage(" >> Factory Code : Code 57 (Over Prime / Fuel Pressure / LPG Solenoid Valve Fault)\n");
                    logMessage(" >> Root Cause   : Prime fuel active for more than 3 minutes continuously.\n");
                    logMessage(" >> Action Plan  : 1. Release STOP key to stop priming fuel immediately.\n");
                }
                else if (parsedSpn == 1390 && parsedFmi == 2) {
                    logMessage(" >> Factory Code : Code 57 (Over Prime / Fuel Pressure / LPG Solenoid Valve Fault)\n");
                    logMessage(" >> Root Cause   : High or low LP fuel pressure sensor circuit voltage outlier.\n");
                    logMessage(" >> Action Plan  : 1. Check LPG tank pressure (9-13 in WC). 2. Replace LP pressure sensor. 3. Replace ECU.\n");
                }
                else if (parsedSpn == 6181 && parsedFmi == 2) {
                    logMessage(" >> Factory Code : Code 57 (Over Prime / Fuel Pressure / LPG Solenoid Valve Fault)\n");
                    logMessage(" >> Root Cause   : LP Solenoid Valve open circuit, short to ground, overcurrent, or overheat.\n");
                    logMessage(" >> Action Plan  : 1. Check solenoid coil for opens/shorts. 2. Check wiring connections. 3. Replace solenoid if current exceeds 1.6A-3A. 4. Replace ECU if driver temp exceeds 155-185C.\n");
                }
                
                // --- ✅ NEW: REMOTE INTERFACE CONFIGURATION GATEWAY ---
                else if (parsedSpn == 524032 && parsedFmi == 31) {
                    logMessage(" >> Factory Status: Standby Awaiting Handshake\n");
                    logMessage(" >> Condition     : Generator is resting in remote standby mode.\n");
                    logMessage(" >> Action Plan   : Push 'Prime Fuel' or 'Crank Start' to execute a remote J1939 automation handshake.\n");
                }

                
                // --- FALLBACK UNMAPPED ALERTS ---
                else {
                    logMessage(" >> Factory Code : Unknown / Variant Sub-Fault Code\n");
                    logMessage(" >> Action Plan  : Lookup standard manual criteria for SPN %lu, FMI %d.\n", parsedSpn, parsedFmi);
                }
                logMessage("----------------------------------------------------------------------\n");
            }
            break;
        }


        case 65280: { // Proprietary Status Loop
            lastDataReceivedTime = millis();
            uint8_t engineState = msg.data[0];
            globalGensetState = engineState;

            static uint8_t lastEngineState = 0xFF;
            if (engineState != lastEngineState) {
                lastEngineState = engineState;
                logMessage("\n⚡ [STATE CHANGE] Status: ");
                switch(engineState) {
                    case 0: logMessage("Ready / Standby (AC Disconnected)\n"); break;
                    case 1: logMessage("Stopped / Engine Inactive\n"); break;
                    case 2: logMessage("Starting / Cranking Engine\n"); break;
                    case 3: logMessage("Running / Producing AC Power\n"); break;
                    case 4: logMessage("Warm-up Mode / Automatic Choke\n"); break;
                    case 5: logMessage("FUEL PRIMING RUNNING (Lift Pump Engaged)\n"); break;
                    case 6: logMessage("CRITICAL CRASH / FAULT SHUTDOWN TRIGGERED\n"); break;
                    default: logMessage("Unknown (0x%02X)\n", engineState); break;
                }

                if ((engineState == 3 && currentActiveCommand == CMD_START) ||
                    (engineState == 1 && currentActiveCommand == CMD_STOP)) {
                    currentActiveCommand = CMD_RELEASE;
                    logMessage("✔ [REMOTE] Control Handshake Complete.\n");
                }
            }
            break;
        }

        case 61444: { // Engine Speed
            lastDataReceivedTime = millis();
            if (msg.data[3] != 0xFF && msg.data[4] != 0xFF) {
                uint16_t rawRPM = (msg.data[4] << 8) | msg.data[3];
                if (rawRPM < 0xFA00) { liveEngineRPM = rawRPM * 0.125; } else { liveEngineRPM = 0.0; }
            } else { liveEngineRPM = 0.0; }
            break;
        }

        case 64409: { // Inverter Temp
            lastDataReceivedTime = millis();
            if (msg.data[2] != 0xFF) { liveInverterTemp = (int16_t)msg.data[2] - 40; } else { liveInverterTemp = 0; }
            break;
        }

        case 65030: { // AC Voltage and Frequency
            lastDataReceivedTime = millis();
            if (msg.data[2] != 0xFF && msg.data[3] != 0xFF) { liveACVoltage = (msg.data[3] << 8) | msg.data[2]; } else { liveACVoltage = 0; }
            if (msg.data[4] != 0xFF && msg.data[5] != 0xFF) {
                uint16_t rawHz = (msg.data[5] << 8) | msg.data[4];
                if (rawHz < 0xFA00) { liveACFrequency = rawHz * (1.0 / 128.0); } else { liveACFrequency = 0.0; }
            } else { liveACFrequency = 0.0; }
            break;
        }

        case 65271: { // Battery Voltage
            lastDataReceivedTime = millis();
            if (msg.data[4] != 0xFF && msg.data[5] != 0xFF) {
                uint16_t rawVolts = (msg.data[5] << 8) | msg.data[4];
                if (rawVolts < 0xFA00) { liveBatteryVoltage = rawVolts * 0.05; } else { liveBatteryVoltage = 0.0; }
            } else { liveBatteryVoltage = 0.0; }
            break;
        }
    }
}
