/* 6-15-26 v2 --- J1939 Active Control & SPN/FMI Deep Diagnostic Engine


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
#include <esp_task_wdt.h>

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

TaskHandle_t xTwaiTaskHandle = NULL;
SemaphoreHandle_t logMutex = NULL;

// Unified Thread-Safe Logger (FIXED BUFFER STRINGS)
void logMessage(const char* format, ...) {
    unsigned long totalSeconds = millis() / 1000;
    unsigned int seconds = totalSeconds % 60;
    unsigned int minutes = (totalSeconds / 60) % 60;
    unsigned int hours = (totalSeconds / 3600);
    char header_buf[32]; // ✅ FIXED: Proper array sizing

    if (format[0] != '\n' && format[0] != '-' && format[0] != '_') {
        globalLogEntryCounter++;
        snprintf(header_buf, sizeof(header_buf), "[#%lu @ %02u:%02u:%02u] ", globalLogEntryCounter, hours, minutes, seconds);
    } else {
        header_buf[0] = '\0'; // ✅ FIXED: Proper array character handling
    }

    char payload_buf[256]; // ✅ FIXED: Proper array sizing
    va_list arg;
    va_start(arg, format);
    vsnprintf(payload_buf, sizeof(payload_buf), format, arg);
    va_end(arg);

    char final_buf[300]; // ✅ FIXED: Proper array sizing
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
    if (logFile) {
        logFile.print(final_buf);
        logFile.close();
    }

    static unsigned long lastSizeCheck = 0;
    if (millis() - lastSizeCheck > 30000) {
        lastSizeCheck = millis();
        File checkFile = LittleFS.open("/log.txt", FILE_READ);
        if (checkFile) {
            size_t fileSize = checkFile.size();
            checkFile.close();
            if (fileSize > 50000) {
                File readFile = LittleFS.open("/log.txt", FILE_READ);
                if (readFile) {
                    readFile.seek(fileSize - 20000);
                    String truncatedData = readFile.readString();
                    readFile.close();
                    File writeFile = LittleFS.open("/log.txt", FILE_WRITE);
                    if (writeFile) {
                        writeFile.print(truncatedData);
                        writeFile.close();
                    }
                }
            }
        }
    }
}

const char htmlDashboard[] PROGMEM = "<!DOCTYPE html><html><head>"
"<meta charset='utf-8'><meta name='viewport' content='width=device-width, initial-scale=1'>"
"<style>body{font-family:sans-serif; background:#121212; color:#e0e0e0; padding:20px; text-align:center;}"
"h2, h3{color:#00adb5;} .box{background:#1e1e1e; padding:15px; border-radius:8px; margin: 0 auto 20px auto; max-width:700px; border:1px solid #333;}"
"pre{background:#000; color:#0f0; padding:15px; border-radius:5px; overflow-y:scroll; height:400px; font-family:monospace; text-align:left; white-space:pre-wrap; margin-bottom:15px;}"
"input[type=file]{background:#2d2d2d; padding:8px; border-radius:4px; color:#fff; border:1px solid #444; margin-right:10px;}"
"input[type=button], .btn-action{background:#00adb5; color:#fff; border:none; padding:10px 20px; border-radius:4px; cursor:pointer; font-weight:bold; font-size:14px; text-decoration:none; display:inline-block; margin:5px;}"
"input[type=button]:hover, .btn-action:hover{background:#01c3cc;}.btn-clear{background:#3d3d3d;}.btn-clear:hover{background:#4d4d4d;}"
".btn-start{background:#5cb85c;}.btn-start:hover{background:#4cae4c;}.btn-stop{background:#d9534f;}.btn-stop:hover{background:#c9302c;}"
".btn-prime{background:#f0ad4e; color:#222;}.btn-prime:hover{background:#f0b95e;}.progress-container{width:100%; background-color:#2d2d2d; border-radius:4px; margin-top:15px; display:none; border:1px solid #444;}"
".progress-bar{width:0%; height:20px; background-color:#00adb5; border-radius:4px; text-align:center; line-height:20px; color:white; font-size:12px; transition: width 0.1s linear;}"
"#status-msg{margin-top:10px; font-weight:bold; color:#ffb703;}</style></head><body>"
"<h2>Cummins HGLCA Diagnostic Dashboard v1.7</h2>"
"<div class='box'><h3>Live Telemetry & J1939 SPN/FMI Monitor</h3><pre id='terminal'>Awaiting connection to CAN powertrain loop...</pre>"
"<a href='/download-log' download='onan_generator_log.txt' class='btn-action'>💾 Download Log (.txt)</a>"
"<button onclick='clearSystemLog()' class='btn-action btn-clear'>🗑 Wipe Saved Log</button></div>"
"<div class='box'><h3>⚡ Remote Powertrain Control Panel</h3>"
"<button onclick='controlGenerator(\"/gen-prime\")' class='btn-action btn-prime'>💽 Prime Fuel System</button>"
"<button onclick='controlGenerator(\"/gen-start\")' class='btn-action btn-start'>🚀 Crank Engine / Start</button>"
"<button onclick='controlGenerator(\"/gen-stop\")' class='btn-action btn-stop'>🛑 Kill Engine / Stop</button></div>"
"<div class='box'><h3>Wireless Firmware Management</h3><form id='upload-form' enctype='multipart/form-data'>"
"<input type='file' id='file-input' name='update' accept='.bin' required>"
"<input type='button' value='Upload New Code (.bin)' onclick='uploadFile()'></form>"
"<div class='progress-container' id='prg-wrapper'><div class='progress-bar' id='prg-bar'>0%</div></div><div id='status-msg'></div></div>"
"<script>var term = document.getElementById('terminal'); var jsUpdating = false;"
"function pollTelemetry() { if(jsUpdating) return; fetch('/telemetry').then(response => response.text()).then(text => { if(text.trim() !== '') { term.innerHTML = text; term.scrollTop = term.scrollHeight; } }).catch(() => { term.innerHTML = '<br><span style=\"color:red;\">[Network Polling Offline - Resetting...]</span>'; }); }"
"var intervalId = setInterval(pollTelemetry, 500); function controlGenerator(urlRoute) { fetch(urlRoute, {method: 'POST'}); }"
"function clearSystemLog() { if(confirm('Are you sure you want to permanently erase the flash log history?')) { fetch('/clear-log', {method: 'POST'}).then(res => { if(res.ok) { alert('Log successfully cleared from flash memory!'); term.innerHTML = ''; } }); } }"
"function uploadFile() { var fileInput = document.getElementById('file-input'); if(fileInput.files.length === 0) { alert('Please select a .bin file first!'); return; } jsUpdating = true; var fileBlob = fileInput.files[0]; var formData = new FormData(); formData.append('update', fileBlob); var xhr = new XMLHttpRequest(); xhr.open('POST', '/update', true); document.getElementById('prg-wrapper').style.display = 'block'; document.getElementById('status-msg').innerText = 'Uploading firmware payload...';"
"xhr.upload.addEventListener('progress', function(e) { if(e.lengthComputable) { var percent = Math.round((e.loaded / e.total) * 100); document.getElementById('prg-bar').style.width = percent + '%'; document.getElementById('prg-bar').innerText = percent + '%'; if(percent === 100) { document.getElementById('status-msg').innerText = 'Writing flash memory... Please wait.'; } } });"
"xhr.onload = function() { if(xhr.status === 200) { document.getElementById('status-msg').style.color = '#00ff00'; document.getElementById('status-msg').innerHTML = '✅ Update Success! Microcontroller is rebooting now...'; } else { document.getElementById('status-msg').style.color = '#ff0000'; document.getElementById('status-msg').innerText = '❌ Update Failed: ' + xhr.responseText; jsUpdating = false; } };"
"xhr.onerror = function() { document.getElementById('status-msg').style.color = '#ff0000'; document.getElementById('status-msg').innerText = '❌ Connection lost during flash procedure.'; }; xhr.send(formData); }</script></body></html>";

struct OnanFaultMapping { uint16_t faultNumber; uint32_t spn; uint8_t fmi; const char* displayLabel; };
const char f_msg_01[] PROGMEM = "Engine Temperature Exceeded Limit"; //
const char f_msg_04[] PROGMEM = "Over Crank Fault"; //
const char f_msg_06[] PROGMEM = "Low Oil Level / Pressure Failure"; //
const char f_msg_12[] PROGMEM = "Over Voltage Control Circuit Shutdown"; //
const char f_msg_13[] PROGMEM = "Under Voltage Power Generation Interruption"; //
const char f_msg_14[] PROGMEM = "Over Frequency Operational Limit Exceeded"; //
const char f_msg_15[] PROGMEM = "Under Frequency Operational Control Limit"; //
const char f_msg_19[] PROGMEM = "Governor Actuator Configuration Sensor Fault"; //
const char f_msg_25[] PROGMEM = "Alternator Over Voltage Protection Trip"; //
const char f_msg_26[] PROGMEM = "Alternator Under Voltage Power Loss"; //
const char f_msg_27[] PROGMEM = "Voltage Capture Control PMA Read Error"; //
const char f_msg_29[] PROGMEM = "High Battery Voltage Warning Limit"; //
const char f_msg_31[] PROGMEM = "Engine Over Speed Mechanical Safety Cutout"; //
const char f_msg_34[] PROGMEM = "Inverter Temperature Exceeded Limit"; //
const char f_msg_36[] PROGMEM = "Abnormal Genset Uncommanded Shutdown"; //
const char f_msg_38[] PROGMEM = "Field Overload Exciter Output Saturation"; //
const char f_msg_43[] PROGMEM = "Control Board Internal ECU Memory Failure"; //
const char f_msg_45[] PROGMEM = "Speed Sense Core Frequency Sensor Missing"; //
const char f_msg_52[] PROGMEM = "Fuel Injector Driver/IPM Pump Circuit Fault"; //
const char f_msg_53[] PROGMEM = "Oil Temperature Sensor Circuit Fault"; //
const char f_msg_54[] PROGMEM = "Manifold Air Temperature (MAT) Sensor Fault"; //
const char f_msg_57[] PROGMEM = "Over Prime / Fuel Pressure / LPG Valve Fault"; //
const char f_msg_73[] PROGMEM = "AC Output Circuit Overcurrent Fault"; //
const char f_msg_81[] PROGMEM = "Alternator Stator Circuit Phase Fault"; //
const char f_msg_85[] PROGMEM = "Oxygen Sensor Circuit Open/Short Fault"; //

// Advanced Lookup table linking Code to explicit J1939 diagnostic variables
const OnanFaultMapping ONAN_FAULT_TABLE[] PROGMEM = {
    {1,  110,  0, f_msg_01}, {4,  1213, 7, f_msg_04}, {6,  98,   1, f_msg_06},
    {12, 1795, 0, f_msg_12}, {13, 1795, 1, f_msg_13}, {14, 1797, 0, f_msg_14},
    {15, 1797, 1, f_msg_15}, {19, 1479, 7, f_msg_19}, {25, 1796, 0, f_msg_25},
    {26, 1796, 1, f_msg_26}, {27, 4220, 2, f_msg_27}, {29, 168,  0, f_msg_29},
    {31, 190,  0, f_msg_31}, {34, 1798, 0, f_msg_34}, {36, 1213, 3, f_msg_36},
    {38, 1799, 0, f_msg_38}, {43, 611, 12, f_msg_43}, {45, 723,  2, f_msg_45},
    {52, 1268, 7, f_msg_52}, {53, 175,  2, f_msg_53}, {54, 105,  2, f_msg_54},
    {57, 1213, 5, f_msg_57}, {73, 1795, 6, f_msg_73}, {81, 4221, 7, f_msg_81},
    {85, 3216, 2, f_msg_85}
};
const int ONAN_DB_COUNT = sizeof(ONAN_FAULT_TABLE) / sizeof(ONAN_FAULT_TABLE[0]);

void processHglcaNetworkFrame(twai_message_t msg);
void twaiBackgroundEngine(void *pvParameters);
void setup() {
    Serial.begin(115200);
    delay(500);

    if (!LittleFS.begin(true)) {
        Serial.println("❌ LittleFS Mount Failed!");
    } else {
        Serial.println("📂 LittleFS Mounted Successfully.");
    }

    pinMode(STATUS_LED_PIN, OUTPUT);
    logMutex = xSemaphoreCreateMutex();
    logMessage("=============================================\n");
    logMessage(" Cummins Diagnostic & Control System Booted   \n");
    logMessage("=============================================\n");

    esp_bt_controller_disable();
    esp_bt_controller_deinit();
    WiFi.mode(WIFI_AP);
    WiFi.softAP("Cummins_Live_Dashboard", "12345678", 6, false, 2);
    WiFi.setTxPower(WIFI_POWER_13dBm);

    ArduinoOTA.onStart([]() {
        isUpdating = true;
        vTaskDelay(pdMS_TO_TICKS(50));
        twai_stop();
        twai_driver_uninstall();
        Serial.println("VS Code OTA Flash Initiated...");
    });
    ArduinoOTA.onEnd([]() { Serial.println("\nVS Code OTA Complete. Rebooting..."); });
    ArduinoOTA.begin();

    server.on("/", HTTP_GET, []() { server.send(200, "text/html", htmlDashboard); });
    
    server.on("/telemetry", HTTP_GET, []() {
        String logPayload = "";
        if (logMutex != NULL && xSemaphoreTake(logMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
            logPayload = webLogBuffer;
            xSemaphoreGive(logMutex);
        }
        logPayload.replace("\n", "<br>");
        server.send(200, "text/plain", logPayload);
    });

    server.on("/update", HTTP_POST, []() {
        server.sendHeader("Connection", "close");
        if (Update.hasError()) { server.send(500, "text/plain", "Flash verification failed!"); }
        else { server.send(200, "text/plain", "OK"); delay(2000); ESP.restart(); }
    }, []() {
        HTTPUpload& upload = server.upload();
        if (upload.status == UPLOAD_FILE_START) {
            isUpdating = true;
            vTaskDelay(pdMS_TO_TICKS(50));
            twai_stop();
            twai_driver_uninstall();
            if (!Update.begin(UPDATE_SIZE_UNKNOWN)) { Update.printError(Serial); }
        } else if (upload.status == UPLOAD_FILE_WRITE) {
            yield();
            if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) { Update.printError(Serial); }
            yield();
        } else if (upload.status == UPLOAD_FILE_END) {
            Update.end(true);
        }
    });

    server.on("/download-log", HTTP_GET, []() {
        if (LittleFS.exists("/log.txt")) {
            File file = LittleFS.open("/log.txt", FILE_READ);
            server.sendHeader("Content-Disposition", "attachment; filename=onan_generator_log.txt");
            server.streamFile(file, "text/plain");
            file.close();
        } else { server.send(404, "text/plain", "Log is empty."); }
    });

    server.on("/clear-log", HTTP_POST, []() {
        LittleFS.remove("/log.txt");
        if (logMutex != NULL && xSemaphoreTake(logMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
            webLogBuffer = ""; globalLogEntryCounter = 0;
            xSemaphoreGive(logMutex);
        }
        server.send(200, "text/plain", "OK");
    });

    server.on("/gen-start", HTTP_POST, []() { currentActiveCommand = CMD_START; server.send(200, "text/plain", "START_PENDING"); });
    server.on("/gen-stop", HTTP_POST, []() { currentActiveCommand = CMD_STOP; server.send(200, "text/plain", "STOP_PENDING"); });
    server.on("/gen-prime", HTTP_POST, []() { currentActiveCommand = CMD_PRIME; server.send(200, "text/plain", "PRIME_PENDING"); });

    // Active controller requires normal driver loop to allow frame writing
    twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CTX_PIN, CRX_PIN, TWAI_MODE_NORMAL);
    g_config.rx_queue_len = 64;
    twai_timing_config_t t_config = TWAI_TIMING_CONFIG_250KBITS();
    twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK && twai_start() == ESP_OK) {
        logMessage("Status: Transceiver Core Initialized (NORMAL MODE).\n");
        xTaskCreate(twaiBackgroundEngine, "TWAI_Engine_Task", 4096, NULL, 3, &xTwaiTaskHandle);
    } else {
        Serial.println("Error: Critical Network Driver Fault!");
    }
    server.begin();
}
void loop() {
    server.handleClient();
    ArduinoOTA.handle();

    unsigned long currentMillis = millis();
    unsigned int flashInterval = 1000;

    if (currentMillis - lastDataReceivedTime < 2000) { flashInterval = 150; }

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
    tx_msg.identifier = 0x0CE0FF01; 
    
    unsigned long lastTxTime = 0;

    while (1) {
        if (isUpdating) { vTaskDelay(pdMS_TO_TICKS(100)); continue; }

        unsigned long now = millis();
        if (now - lastTxTime >= 100) {
            lastTxTime = now;
            for(int i = 1; i < 8; i++) { tx_msg.data[i] = 0xFF; }
            
            GenControlCommand activeCmd = currentActiveCommand;
            tx_msg.data[0] = (uint8_t)activeCmd;

            if (activeCmd != CMD_RELEASE) {
                logMessage("📡 [REMOTE] Broadcasting J1939 Command Flag: 0x%02X\n", tx_msg.data[0]);
            }
            twai_transmit(&tx_msg, pdMS_TO_TICKS(5));
        }

        if (twai_receive(&rx_msg, pdMS_TO_TICKS(10)) == ESP_OK) {
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
    uint8_t engineState = msg.data[0];
    static uint8_t lastEngineState = 0xFF;

    if (engineState != lastEngineState) {
        lastEngineState = engineState;
        logMessage("\n⚡ [GENSET STATE CHANGE] Status: ");
        switch(engineState) {
            case 0: logMessage("Ready / Standby (AC Disconnected)\n"); break;
            case 1: logMessage("Stopped / Engine Inactive\n"); break;
            case 2: logMessage("Starting / Cranking Engine\n"); break;
            case 3: logMessage("Running / Producing AC Power\n"); break;
            case 4: logMessage("Warm-up Mode / Automatic Choke Active\n"); break;
            case 5: logMessage("FUEL PRIMING RUNNING (Lift Pump Engaged)\n"); break;
            case 6: logMessage("CRITICAL CRASH / FAULT SHUTDOWN TRIGGERED\n"); break;
            case 15: logMessage("Internal Use Mode / Core Initializing\n"); break;
            default: logMessage("Unknown State (0x%02X)\n", engineState); break;
        }

        if ((engineState == 3 && currentActiveCommand == CMD_START) ||
            (engineState == 1 && currentActiveCommand == CMD_STOP)  ||
            (engineState == 5 && currentActiveCommand == CMD_PRIME)) {
            currentActiveCommand = CMD_RELEASE;
            logMessage("✔ [REMOTE] Target state achieved. Command line released to idle.\n");
        }
    }

    uint16_t activeFaultCode = 0;
    if (msg.data[2] >= 0x30 && msg.data[2] <= 0x39) { activeFaultCode = msg.data[2] - 0x30; } 
    else { activeFaultCode = msg.data[2]; }

    if (engineState == 6 && activeFaultCode == 0) { activeFaultCode = 53; }

    static uint16_t lastFaultCode = 0x0000;
    static uint8_t lastLoggedStateForMatrix = 0xFF;

    if (engineState != 6 && (activeFaultCode == 0x00 || msg.data[2] == 0xFF)) {
        if (lastFaultCode != 0) {
            logMessage("\n✔ [DIAGNOSTIC] Faults Cleared. System Normal.\n");
            lastFaultCode = 0;
        }
        lastLoggedStateForMatrix = engineState;
        return;
    }

    if (activeFaultCode != lastFaultCode || engineState != lastLoggedStateForMatrix) {
        lastFaultCode = activeFaultCode;
        lastLoggedStateForMatrix = engineState;
        logMessage("\n------------------------------------------------\n");
        if (engineState == 6) { logMessage("🚨 [CRASH MATRIX DUMP - FAULT ACTIVE]\n"); } 
        else { logMessage("⚠ [HGLCA INVERTER FAULT CODE ENCOUNTERED]\n"); }
        
        logMessage("Raw Stream Payload Matrix: ");
        for(int i = 0; i < 8; i++) { logMessage("%02X ", msg.data[i]); }
        logMessage("\n");

        bool matchFound = false;
        for (int i = 0; i < ONAN_DB_COUNT; i++) {
            uint16_t tableFault = pgm_read_word(&(ONAN_FAULT_TABLE[i].faultNumber));
            if (tableFault == activeFaultCode) {
                uint32_t diagnosticSpn = pgm_read_dword(&(ONAN_FAULT_TABLE[i].spn));
                uint8_t diagnosticFmi = pgm_read_byte(&(ONAN_FAULT_TABLE[i].fmi));
                const char* label = (const char*)pgm_read_ptr(&(ONAN_FAULT_TABLE[i].displayLabel));
                
                logMessage("Manual Reference: Code %d\n", activeFaultCode); //
                logMessage("J1939 Mapping   : SPN %lu, FMI %d\n", diagnosticSpn, diagnosticFmi);
                logMessage("Description     : %s\n", label);
                matchFound = true;
                break;
            }
        }
        if (!matchFound) { logMessage("Alert: Unmapped Inverter Code -> Code %d\n", activeFaultCode); }
        logMessage("------------------------------------------------\n");
    }
}
