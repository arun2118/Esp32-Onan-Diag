/* 6-17-26--- sniffng dashboard data

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

// --- Live Powertrain Metrics Buffers ---
volatile float liveBatteryVoltage = 0.0;
volatile uint16_t liveEngineRPM = 0;
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

// Master Diagnostic Matrix mappings
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
// HTML UI with responsive gauge elements and unified parsing strings
const char htmlDashboard[] PROGMEM = "<!DOCTYPE html><html><head>"
"<meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1'>"
"<style>body{font-family:sans-serif; background:#121212; color:#e0e0e0; padding:15px; text-align:center;}"
"h2, h3{color:#00adb5; margin:10px 0;} .box{background:#1e1e1e; padding:15px; border-radius:8px; margin:0 auto 15px auto; max-width:750px; border:1px solid #333;}"
".grid{display:flex; flex-wrap:wrap; gap:10px; justify-content:center; max-width:750px; margin:0 auto 15px auto;}"
".metric-card{background:#1e1e1e; border:1px solid #333; border-radius:6px; padding:12px; width:130px; text-align:center; box-sizing:border-box;}"
".val{font-size:20px; font-weight:bold; color:#00adb5; margin-top:5px;}"
"pre{background:#000; color:#0f0; padding:12px; border-radius:5px; overflow-y:scroll; height:250px; font-family:monospace; text-align:left; white-space:pre-wrap; margin-bottom:10px;}"
"input[type=file]{background:#2d2d2d; padding:6px; border-radius:4px; color:#fff; border:1px solid #444;}"
"input[type=button], .btn-action{background:#00adb5; color:#fff; border:none; padding:10px 15px; border-radius:4px; cursor:pointer; font-weight:bold; text-decoration:none; display:inline-block; margin:4px;}"
".btn-clear{background:#3d3d3d;}.btn-start{background:#5cb85c;}.btn-stop{background:#d9534f;}.btn-prime{background:#f0ad4e; color:#222;}"
".progress-container{width:100%; background-color:#2d2d2d; border-radius:4px; margin-top:10px; display:none;}"
".progress-bar{width:0%; height:18px; background-color:#00adb5; border-radius:4px; text-align:center; line-height:18px; color:white; font-size:11px;}"
"#status-msg{margin-top:8px; font-weight:bold; color:#ffb703;}</style></head><body>"
"<h2>Cummins HGLCA Pro Diagnostic System</h2>"
"<div class='grid'>"
" <div class='metric-card'><div>🔋 Battery</div><div class='val' id='m-volts'>0.0V</div></div>"
" <div class='metric-card'><div>⚙️ Engine</div><div class='val' id='m-rpm'>0 RPM</div></div>"
" <div class='metric-card'><div>🔥 Inverter</div><div class='val' id='m-temp'>0&deg;C</div></div>"
" <div class='metric-card'><div>⚡ AC Power</div><div class='val' id='m-acv'>0V</div></div>"
" <div class='metric-card'><div>🌀 Frequency</div><div class='val' id='m-hz'>0.0Hz</div></div>"
"</div>"
"<div class='box'><h3>Live System Console Logs</h3><pre id='terminal'>Awaiting telemetry synchronization...</pre>"
"<a href='/download-log' download='onan_generator_log.txt' class='btn-action'>💾 Download Log</a>"
"<button onclick='clearSystemLog()' class='btn-action btn-clear'>🗑 Wipe Saved Log</button></div>"
"<div class='box'><h3>⚡ Remote Powertrain Control Panel</h3>"
"<button onclick='controlGenerator(\"/gen-prime\")' class='btn-action btn-prime'>💽 Prime Fuel</button>"
"<button onclick='controlGenerator(\"/gen-start\")' class='btn-action btn-start'>🚀 Crank Start</button>"
"<button onclick='controlGenerator(\"/gen-stop\")' class='btn-action btn-stop'>🛑 Kill Engine</button></div>"
"<div class='box'><h3>Wireless Firmware Management</h3><form id='upload-form' enctype='multipart/form-data'>"
"<input type='file' id='file-input' name='update' accept='.bin' required> "
"<input type='button' value='Flash Payload (.bin)' onclick='uploadFile()'></form>"
"<div class='progress-container' id='prg-wrapper'><div class='progress-bar' id='prg-bar'>0%</div></div><div id='status-msg'></div></div>"
"<script>var term = document.getElementById('terminal'); var jsUpdating = false;"
"function pollTelemetry() { if(jsUpdating) return;"
" fetch('/telemetry-json').then(r => r.json()).then(data => {"
" document.getElementById('m-volts').innerText = data.v + 'V';"
" document.getElementById('m-rpm').innerText = data.r + ' RPM';"
" document.getElementById('m-temp').innerText = data.t + '°C';"
" document.getElementById('m-acv').innerText = data.av + 'V';"
" document.getElementById('m-hz').innerText = data.hz + 'Hz';"
" });"
" fetch('/telemetry').then(r => r.text()).then(text => { if(text.trim()!==''){ term.innerHTML=text; term.scrollTop=term.scrollHeight; } });"
"}"
"setInterval(pollTelemetry, 500); function controlGenerator(route){ fetch(route, {method:'POST'}); }"
"function clearSystemLog(){ if(confirm('Wipe saved flash logs?')){ fetch('/clear-log',{method:'POST'}).then(() => { term.innerHTML=''; }); } }"
"function uploadFile(){ var fi=document.getElementById('file-input'); if(fi.files.length===0){alert('Select .bin!');return;} jsUpdating=true; var fd=new FormData(); fd.append('update',fi.files[0]); var xhr=new XMLHttpRequest(); xhr.open('POST','/update',true); document.getElementById('prg-wrapper').style.display='block'; document.getElementById('status-msg').innerText='Uploading firmware...';"
"xhr.upload.addEventListener('progress',function(e){ if(e.lengthComputable){ var p=Math.round((e.loaded/e.total)*100); document.getElementById('prg-bar').style.width=p+'%'; document.getElementById('prg-bar').innerText=p+'%'; } });"
"xhr.onload=function(){ if(xhr.status===200){ document.getElementById('status-msg').style.color='#00ff00'; document.getElementById('status-msg').innerText='✅ Success! Rebooting...'; }else{ document.getElementById('status-msg').innerText='❌ Failed: '+xhr.responseText; jsUpdating=false; } }; xhr.send(fd); }</script></body></html>";

void setup() {
    Serial.begin(115200);
    LittleFS.begin(true);
    pinMode(STATUS_LED_PIN, OUTPUT);
    logMutex = xSemaphoreCreateMutex();

    WiFi.mode(WIFI_AP);
    WiFi.softAP("Cummins_Live_Dashboard", "12345678", 6, false, 2);
    WiFi.setTxPower(WIFI_POWER_13dBm);
    ArduinoOTA.begin();

    server.on("/", HTTP_GET, []() { server.send(200, "text/html", htmlDashboard); });
    server.on("/telemetry", HTTP_GET, []() {
        String p = ""; if (logMutex != NULL && xSemaphoreTake(logMutex, pdMS_TO_TICKS(10)) == pdTRUE) { p = webLogBuffer; xSemaphoreGive(logMutex); }
        p.replace("\n", "<br>"); server.send(200, "text/plain", p);
    });

    // Unified structured API channel driving our custom card variables 
    server.on("/telemetry-json", HTTP_GET, []() {
        char j_buf[128];
        snprintf(j_buf, sizeof(j_buf), "{\"v\":%.1f,\"r\":%u,\"t\":%d,\"hz\":%.1f,\"av\":%u}", 
                 liveBatteryVoltage, liveEngineRPM, liveInverterTemp, liveACFrequency, liveACVoltage);
        server.send(200, "application/json", j_buf);
    });

    server.on("/update", HTTP_POST, []() { server.send(200, "text/plain", "OK"); delay(1000); ESP.restart(); }, []() {
        HTTPUpload& u = server.upload();
        if (u.status == UPLOAD_FILE_START) { isUpdating = true; twai_stop(); twai_driver_uninstall(); Update.begin(UPDATE_SIZE_UNKNOWN); }
        else if (u.status == UPLOAD_FILE_WRITE) { Update.write(u.buf, u.currentSize); }
    });

    server.on("/download-log", HTTP_GET, []() {
        File f = LittleFS.open("/log.txt", FILE_READ); server.streamFile(f, "text/plain"); f.close();
    });
    server.on("/clear-log", HTTP_POST, []() { LittleFS.remove("/log.txt"); webLogBuffer = ""; server.send(200, "text/plain", "OK"); });

    server.on("/gen-start", HTTP_POST, []() { currentActiveCommand = CMD_START; server.send(200, "text/plain", "PENDING"); });
    server.on("/gen-stop", HTTP_POST, []() { currentActiveCommand = CMD_STOP; server.send(200, "text/plain", "PENDING"); });
    server.on("/gen-prime", HTTP_POST, []() { currentActiveCommand = CMD_PRIME; server.send(200, "text/plain", "PENDING"); });

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
    tx_msg.identifier = 0x0CE0FF01; // Change to 0x0CE0FF27 if utilizing Source Address 27
    
    unsigned long lastTxTime = 0;
    unsigned long commandStartTime = 0;
    GenControlCommand previousCommand = CMD_RELEASE;

    while (1) {
        if (isUpdating) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }
        GenControlCommand activeCmd = currentActiveCommand;

        if (activeCmd != CMD_RELEASE && previousCommand == CMD_RELEASE) { 
            commandStartTime = millis(); 
        }
        previousCommand = activeCmd;

        if (activeCmd != CMD_RELEASE && (millis() - commandStartTime >= 3000)) { 
            currentActiveCommand = CMD_RELEASE; 
            activeCmd = CMD_RELEASE; 
            logMessage("\n⚠️ [REMOTE] No response from generator. Command line cleared to IDLE.\n");
        }

        unsigned long now = millis();
        if (now - lastTxTime >= 100) {
            lastTxTime = now;
            for(int i = 1; i < 8; i++) { tx_msg.data[i] = 0xFF; }
            tx_msg.data[0] = (uint8_t)activeCmd; // ✅ FIXED: Explicit index 0 target for tx

            if (activeCmd != CMD_RELEASE) { 
                // ✅ FIXED: Forces a raw string carriage breakdown so the Web terminal displays the active broadcast frame
                logMessage("\n📡 [REMOTE] Transmitting active J1939 Command: 0x%02X\n", tx_msg.data[0]);
                twai_transmit(&tx_msg, pdMS_TO_TICKS(5)); 
            }
        }

        if (twai_receive(&rx_msg, pdMS_TO_TICKS(5)) == ESP_OK) {
            if (rx_msg.extd) {
                uint32_t pgn = (rx_msg.identifier >> 8) & 0x3FFFF;
                if (pgn == 65280) { 
                    lastDataReceivedTime = millis();
                    processHglcaNetworkFrame(rx_msg);
                }
            }
        }
        vTaskDelay(pdMS_TO_TICKS(5));
    }
}

void processHglcaNetworkFrame(twai_message_t msg) {
    // 1. Instantly log out the precise raw HEX byte map coming from the engine
    logMessage("\n🔍 [J1939 SNIFFER] PGN: 65280 | Hex Map: ");
    for(int i = 0; i < 8; i++) { 
        logMessage("[%d]:0x%02X ", i, msg.data[i]); 
    }
    logMessage("\n");

    // 2. Keep the basic engine state state tracking operational so you can run comparisons
    uint8_t engineState = msg.data[0];
    globalGensetState = engineState;
    
    static uint8_t lastState = 0xFF;
    if (engineState != lastState) {
        lastState = engineState;
        logMessage(" G_STATE: %d\n", engineState);
    }
}
