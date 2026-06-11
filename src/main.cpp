/* 
 6-11-26
 
1. Open wifi setting on device and  connect to "Cummins_Live_Dashboard" with password "12345678"
2. Open any browser go to address "192.168.4.1"

*/

#include <Arduino.h>
#include "driver/twai.h"
#include "esp_wifi.h"
#include "esp_bt.h"
#include <WiFi.h>
#include <WebServer.h>
#include <Update.h>
#include <stdarg.h> // Required for va_list / vsnprintf inside logMessage
#include <ArduinoOTA.h>
#include <LittleFS.h>

// Connection to 
#define CTX_PIN GPIO_NUM_1
#define CRX_PIN GPIO_NUM_0

#define STATUS_LED_PIN GPIO_NUM_8  

// Heartbeat tracking variables
unsigned long lastLedToggle = 0;
unsigned long lastDataReceivedTime = 0;
bool ledState = false;

// Global Variables
String webLogBuffer = "";
WebServer server(80);
bool isUpdating = false;

// Global Counter tracking total events since ESP32 powered up
unsigned long globalLogEntryCounter = 0;

// FreeRTOS Synchronization Handles
TaskHandle_t xTwaiTaskHandle = NULL;
SemaphoreHandle_t logMutex = NULL;

// Unified Thread-Safe Logger with Flash File Storage
void logMessage(const char* format, ...) {
    // 1. Calculate ESP32 System Uptime in HH:MM:SS format
    unsigned long totalSeconds = millis() / 1000;
    unsigned int seconds = totalSeconds % 60;
    unsigned int minutes = (totalSeconds / 60) % 60;
    unsigned int hours = (totalSeconds / 3600);

    // 2. Format the custom timestamp header string
    char header_buf[32];
    // Check if the message is a new line or a continuation block
    // This adds "[#123 @ 01:23:45] " to the beginning of log messages
    if (format[0] != '\n' && format[0] != '-' && format[0] != '_') {
        globalLogEntryCounter++;
        snprintf(header_buf, sizeof(header_buf), "[#%lu @ %02u:%02u:%02u] ", 
                 globalLogEntryCounter, hours, minutes, seconds);
    } else {
        header_buf[0] = '\0'; // Leave empty for raw breaks or separators
    }

    // 3. Format the actual text payload
    char payload_buf[256];
    va_list arg;
    va_start(arg, format);
    vsnprintf(payload_buf, sizeof(payload_buf), format, arg);
    va_end(arg);

    // 4. Combine header and payload into a single localized buffer
    char final_buf[300];
    snprintf(final_buf, sizeof(final_buf), "%s%s", header_buf, payload_buf);

    // 5. Output to local USB Serial Monitor
    Serial.print(final_buf);

    // 6. Thread-Safe Append to Dynamic Web Buffer
    if (logMutex != NULL && xSemaphoreTake(logMutex, pdMS_TO_TICKS(10)) == pdTRUE) {
        webLogBuffer += String(final_buf);
        if(webLogBuffer.length() > 6000) {
            webLogBuffer = webLogBuffer.substring(webLogBuffer.length() - 3000);
        }
        xSemaphoreGive(logMutex);
    }

    // 7. Append directly to the LittleFS Flash .txt file
    File logFile = LittleFS.open("/log.txt", FILE_APPEND);
    if (logFile) {
        logFile.print(final_buf);
        logFile.close();
    }

    // 8. Size Safety Guard Loop (Keeps flash usage clean under 50KB)
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



// Fixed Web Interface Layout (Added Download & Wipe capabilities with safe HTML entities)
const char* htmlDashboard = "<!DOCTYPE html><html><head>"
"<meta name='viewport' content='width=device-width, initial-scale=1'>"
"<style>"
"body{font-family:sans-serif; background:#121212; color:#e0e0e0; padding:20px; text-align:center;}"
"h2, h3{color:#00adb5;} .box{background:#1e1e1e; padding:15px; border-radius:8px; margin: 0 auto 20px auto; max-width:700px; border:1px solid #333;}"
"pre{background:#000; color:#0f0; padding:15px; border-radius:5px; overflow-y:scroll; height:400px; font-family:monospace; text-align:left; white-space:pre-wrap; margin-bottom:15px;}"
"input[type=file]{background:#2d2d2d; padding:8px; border-radius:4px; color:#fff; border:1px solid #444; margin-right:10px;}"
"input[type=button], .btn-action{background:#00adb5; color:#fff; border:none; padding:10px 20px; border-radius:4px; cursor:pointer; font-weight:bold; font-size:14px; text-decoration:none; display:inline-block; margin:5px;}"
"input[type=button]:hover, .btn-action:hover{background:#01c3cc;}"
".btn-clear{background:#d9534f;}"
".btn-clear:hover{background:#c9302c;}"
".progress-container{width:100%; background-color:#2d2d2d; border-radius:4px; margin-top:15px; display:none; border:1px solid #444;}"
".progress-bar{width:0%; height:20px; background-color:#00adb5; border-radius:4px; text-align:center; line-height:20px; color:white; font-size:12px; transition: width 0.1s linear;}"
"#status-msg{margin-top:10px; font-weight:bold; color:#ffb703;}"
"</style></head><body>"
"<h2>Cummins HGLCA Diagnostic Dashboard v1.3</h2>"
"<div class='box'>"
"  <h3>Live Telemetry Monitor</h3>"
"  <pre id='terminal'>Connecting to telemetry engine...</pre>"
"  <a href='/download-log' download='onan_generator_log.txt' class='btn-action'>&#128190; Download Log (.txt)</a>" /* Safe Floppy Disk Entity */
"  <button onclick='clearSystemLog()' class='btn-action btn-clear'>&#128465; Wipe Saved Log</button>"     /* Safe Trash Can Entity */
"</div>"
"<div class='box'>"
"  <h3>Wireless Firmware Management</h3>"
"  <form id='upload-form' enctype='multipart/form-data'>"
"    <input type='file' id='file-input' name='update' accept='.bin' required> "
"    <input type='button' value='Upload New Code (.bin)' onclick='uploadFile()'>"
"  </form>"
"  <div class='progress-container' id='prg-wrapper'><div class='progress-bar' id='prg-bar'>0%</div></div>"
"  <div id='status-msg'></div>"
"</div>"
"<script>"
"  var term = document.getElementById('terminal');"
"  var jsUpdating = false;"
"  function pollTelemetry() {"
"    if(jsUpdating) return;"
"    fetch('/telemetry')"
"    .then(response => response.text())"
"    .then(text => {"
"      if(text.trim() !== '') {"
"        term.innerHTML = text;"
"        term.scrollTop = term.scrollHeight;"
"      }"
"    })"
"    .catch(() => {"
"      term.innerHTML = '<br><span style=\"color:red;\">[Network Polling Offline - Resetting...]</span>';"
"    });"
"  }"
"  var intervalId = setInterval(pollTelemetry, 500);"
"  function clearSystemLog() {"
"    if(confirm('Are you sure you want to permanently erase the flash log history?')) {"
"      fetch('/clear-log', {method: 'POST'})"
"      .then(res => { if(res.ok) { alert('Log successfully cleared from flash memory!'); term.innerHTML = ''; } });"
"    }"
"  }"
"  function uploadFile() {"
"    var fileInput = document.getElementById('file-input');"
"    if(fileInput.files.length === 0) { alert('Please select a .bin file first!'); return; }"
"    jsUpdating = true;"
"    var file = fileInput.files[0];"
"    var formData = new FormData();"
"    formData.append('update', file);"
"    var xhr = new XMLHttpRequest();"
"    xhr.open('POST', '/update', true);"
"    document.getElementById('prg-wrapper').style.display = 'block';"
"    document.getElementById('status-msg').innerText = 'Uploading firmware payload...';"
"    xhr.upload.addEventListener('progress', function(e) {"
"      if(e.lengthComputable) {"
"        var percent = Math.round((e.loaded / e.total) * 100);"
"        document.getElementById('prg-bar').style.width = percent + '%';"
"        document.getElementById('prg-bar').innerText = percent + '%';"
"        if(percent === 100) { document.getElementById('status-msg').innerText = 'Writing flash memory... Please wait.'; }"
"      }"
"    });"
"    xhr.onload = function() {"
"      if(xhr.status === 200) {"
"        document.getElementById('status-msg').style.color = '#00ff00';"
"        document.getElementById('status-msg').innerHTML = '&#9989; Update Success! Microcontroller is rebooting now...';" /* Safe Check Mark HTML Entity */
"      } else {"
"        document.getElementById('status-msg').style.color = '#ff0000';"
"        document.getElementById('status-msg').innerText = '&#10060; Update Failed: ' + xhr.responseText;" /* Safe X Cross HTML Entity */
"        jsUpdating = false;"
"      }"
"    };"
"    xhr.onerror = function() { "
"      document.getElementById('status-msg').style.color = '#ff0000';"
"      document.getElementById('status-msg').innerText = '&#10060; Connection lost during flash procedure.';" /* Safe X Cross HTML Entity */
"    };"
"    xhr.send(formData);"
"  }"
"</script></body></html>";

// ============================================================================
// CUMMINS ONAN HGLCA FAULT DATABASE Configuration (Chapter 10 Service Manual)
// ============================================================================

struct OnanFaultMapping {
    uint16_t faultNumber;
    const char* displayLabel;
};

// Define text strings individually in flash memory to completely protect SRAM
const char f_msg_01[] PROGMEM = "Engine Temperature Exceeded Limit";
const char f_msg_04[] PROGMEM = "Over Crank Fault";
const char f_msg_06[] PROGMEM = "Low Oil Level / Pressure Failure";
const char f_msg_12[] PROGMEM = "Over Voltage Control Circuit Shutdown";
const char f_msg_13[] PROGMEM = "Under Voltage Power Generation Interruption";
const char f_msg_14[] PROGMEM = "Over Frequency Operational Limit Exceeded";
const char f_msg_15[] PROGMEM = "Under Frequency Operational Control Limit";
const char f_msg_19[] PROGMEM = "Governor Actuator Configuration Sensor Fault";
const char f_msg_22[] PROGMEM = "Governor Overload Engine Stalled Under Load";
const char f_msg_27[] PROGMEM = "Voltage Capture Control Read Error";
const char f_msg_29[] PROGMEM = "High Battery Voltage Warning Limit";
const char f_msg_32[] PROGMEM = "Low Battery Charge State Cranking Warning";
const char f_msg_35[] PROGMEM = "Microprocessor Control Core Memory Failure";
const char f_msg_36[] PROGMEM = "Engine Uncommanded Shutdown Mechanical Stall";
const char f_msg_37[] PROGMEM = "Invalid Inverter Configuration Core Readout";
const char f_msg_38[] PROGMEM = "Field Overload Exciter Output Saturation";
const char f_msg_41[] PROGMEM = "Inverter Rotor/Stator Signal Loss";
const char f_msg_43[] PROGMEM = "Control Board Internal Temperature Trip";
const char f_msg_45[] PROGMEM = "Speed Sense Core Frequency Sensor Missing";
const char f_msg_47[] PROGMEM = "Ignition Circuit Timing Feedback Error";
const char f_msg_48[] PROGMEM = "Generator Field Sense Return Interrupted";
const char f_msg_52[] PROGMEM = "Fuel Injector Driver Circuit Open/Short";
const char f_msg_54[] PROGMEM = "Manifold Air Temperature (MAT) Sensor Fault";
const char f_msg_56[] PROGMEM = "Manifold Absolute Pressure (MAP) Sensor Fault";
const char f_msg_57[] PROGMEM = "Over Prime / Fuel Pressure Fault / Solenoid Error";
const char f_msg_58[] PROGMEM = "Exhaust Gas Temperature Exhaust Limit Tripped";
const char f_msg_73[] PROGMEM = "AC Output Circuit Overcurrent Fault";

// Master Table reference layout
const OnanFaultMapping ONAN_FAULT_TABLE[] PROGMEM = {
    {1,  f_msg_01},
    {4,  f_msg_04},
    {6,  f_msg_06},
    {12, f_msg_12},
    {13, f_msg_13},
    {14, f_msg_14},
    {15, f_msg_15},
    {19, f_msg_19},
    {22, f_msg_22},
    {27, f_msg_27},
    {29, f_msg_29},
    {32, f_msg_32},
    {35, f_msg_35},
    {36, f_msg_36},
    {37, f_msg_37},
    {38, f_msg_38},
    {41, f_msg_41},
    {43, f_msg_43},
    {45, f_msg_45},
    {47, f_msg_47},
    {48, f_msg_48},
    {52, f_msg_52},
    {54, f_msg_54},
    {56, f_msg_56},
    {57, f_msg_57},
    {58, f_msg_58},
    {73, f_msg_73}
};

// Dynamically scale database size configuration automatically
const int ONAN_DB_COUNT = sizeof(ONAN_FAULT_TABLE) / sizeof(ONAN_FAULT_TABLE[0]);

// System Prototypes
void processHglcaNetworkFrame(twai_message_t msg);
void twaiBackgroundEngine(void *pvParameters);


void setup() {
  Serial.begin(115200);
  delay(500);

    // Initialize LittleFS
    if (!LittleFS.begin(true)) { // "true" formats the file system if it fails to mount
        Serial.println("❌ LittleFS Mount Failed!");
    } else {
        Serial.println("📂 LittleFS Mounted Successfully.");
    }  

    // Set up the indicator LED pin target
  pinMode(STATUS_LED_PIN, OUTPUT);

  logMutex = xSemaphoreCreateMutex();

  logMessage("=============================================\n");
  logMessage("    Cummins J1939 Threaded Engine Active    \n");
  logMessage("=============================================\n");

  esp_bt_controller_disable();
  esp_bt_controller_deinit();

  WiFi.mode(WIFI_AP);
  WiFi.softAP("Cummins_Live_Dashboard", "12345678");

  // Add these lines right after WiFi.softAP
ArduinoOTA.onStart([]() {
    isUpdating = true; // Safely pauses your TWAI background loop engine
    vTaskDelay(pdMS_TO_TICKS(50));
    twai_stop();
    twai_driver_uninstall();
    Serial.println("VS Code OTA Flash Initiated...");
});

ArduinoOTA.onEnd([]() {
    Serial.println("\nVS Code OTA Complete. Rebooting...");
});

ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    Serial.printf("Progress: %u%%\r", (progress / (total / 100)));
});

ArduinoOTA.onError([](ota_error_t error) {
    Serial.printf("Error[%u]: ", error);
    isUpdating = false; // Resume TWAI network loop if upload fails
});

ArduinoOTA.begin();


  logMessage("Hotspot: Cummins_Live_Dashboard\n");
  logMessage("URL:     http://%s\n", WiFi.softAPIP().toString().c_str());

  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", htmlDashboard);
  });

  // Telemetry Snapshot Endpoint
  server.on("/telemetry", HTTP_GET, []() {
    String logPayload = "";
    if (logMutex != NULL && xSemaphoreTake(logMutex, pdMS_TO_TICKS(20)) == pdTRUE) {
      logPayload = webLogBuffer;
      xSemaphoreGive(logMutex);
    }
    logPayload.replace("\n", "<br>");
    server.send(200, "text/plain", logPayload);
  });

  // OTA Firmware Receiver Route
  server.on("/update", HTTP_POST, []() {
    server.sendHeader("Connection", "close");
    if (Update.hasError()) {
      server.send(500, "text/plain", "Flash verification failed!");
    } else {
      server.send(200, "text/plain", "OK");
      delay(2000); 
      ESP.restart();
    }
  }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      isUpdating = true;
      vTaskDelay(pdMS_TO_TICKS(50));
      twai_stop();
      twai_driver_uninstall();
      Serial.printf("Flashing Firmware Image: %s\n", upload.filename.c_str());
      if (!Update.begin(UPDATE_SIZE_UNKNOWN)) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
        Update.printError(Serial);
      }
    } else if (upload.status == UPLOAD_FILE_END) {
      if (Update.end(true)) {
        Serial.printf("Flash Process Finalized: %u bytes written.\n", upload.totalSize);
      } else {
        Update.printError(Serial);
      }
    }
  });


// 1. Download log Route
server.on("/download-log", HTTP_GET, []() {
    if (LittleFS.exists("/log.txt")) {
        File file = LittleFS.open("/log.txt", FILE_READ);
        server.sendHeader("Content-Disposition", "attachment; filename=onan_generator_log.txt");
        server.streamFile(file, "text/plain");
        file.close();
    } else {
        server.send(404, "text/plain", "Log is currently empty.");
    }
});

// 2. Clear/Wipe log Route
server.on("/clear-log", HTTP_POST, []() {
    LittleFS.remove("/log.txt");
    
    if (logMutex != NULL && xSemaphoreTake(logMutex, pdMS_TO_TICKS(50)) == pdTRUE) {
        webLogBuffer = ""; 
        globalLogEntryCounter = 0; // <--- Resets your line numbering index back to 0
        xSemaphoreGive(logMutex);
    }
    server.send(200, "text/plain", "OK");
});



  server.begin();

  // Initialize TWAI Subsystem Architecture
  twai_general_config_t g_config = TWAI_GENERAL_CONFIG_DEFAULT(CTX_PIN, CRX_PIN, TWAI_MODE_LISTEN_ONLY);
  g_config.rx_queue_len = 64; 
  twai_timing_config_t t_config = TWAI_TIMING_CONFIG_250KBITS();
  twai_filter_config_t f_config = TWAI_FILTER_CONFIG_ACCEPT_ALL();

  if (twai_driver_install(&g_config, &t_config, &f_config) == ESP_OK && twai_start() == ESP_OK) {
    logMessage("Status: High-Priority CAN Core Instantiated.\n");
    
    xTaskCreate(
      twaiBackgroundEngine,   
      "TWAI_Reader_Task",     
      3072,                   
      NULL,                   
      3,                      
      &xTwaiTaskHandle        
    );
  } else {
    Serial.println("Error: Critical Network Driver Fault!");
  }
}

void loop() {
  server.handleClient();
  ArduinoOTA.handle();
  
  // --- Non-Blocking Dynamic Heartbeat Engine ---
  unsigned long currentMillis = millis();
  unsigned int flashInterval = 1000; // Default Idle Heartbeat rate (1 second)

  // If a data frame has arrived within the last 2000 milliseconds, ramp up the speed
  if (currentMillis - lastDataReceivedTime < 2000) {
    flashInterval = 150; // High-speed flash rate for active data monitoring
  }

  // Toggle state window execution check
  if (currentMillis - lastLedToggle >= flashInterval) {
    lastLedToggle = currentMillis;
    ledState = !ledState;
    digitalWrite(STATUS_LED_PIN, ledState ? LOW : HIGH); 
  }
  
  vTaskDelay(pdMS_TO_TICKS(2));
}

void twaiBackgroundEngine(void *pvParameters) {
  twai_message_t msg;
  while (1) {
    if (isUpdating) {
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }

    if (twai_receive(&msg, pdMS_TO_TICKS(15)) == ESP_OK) {
      if (msg.extd) {
        uint32_t pgn = (msg.identifier >> 8) & 0x3FFFF;
        if (pgn == 65280) {
          lastDataReceivedTime = millis(); // <--- Update timestamp here! LED data status
          processHglcaNetworkFrame(msg);
        }
      }
    }
  }
}

void processHglcaNetworkFrame(twai_message_t msg) {
    // 1. Isolate the true operational state from byte 0
    uint8_t engineState = msg.data[0]; 
    static uint8_t lastEngineState = 0xFF;

    if (engineState != lastEngineState) {
        lastEngineState = engineState;
        logMessage("\n⚡ [GENSET STATE CHANGE] Status: ");
        
        switch(engineState) {
            case 0:  logMessage("Ready / Standby (AC Disconnected)\n"); break;
            case 1:  logMessage("Stopped / Engine Inactive\n"); break;
            case 2:  logMessage("Starting / Cranking Engine\n"); break;
            case 3:  logMessage("Running / Producing AC Power\n"); break;
            case 4:  logMessage("Warm-up Mode / Automatic Choke Active\n"); break;
            case 5:  logMessage("FUEL PRIMING RUNNING (Lift Pump Engaged)\n"); break;
            case 6:  logMessage("CRITICAL CRASH / FAULT SHUTDOWN TRIGGERED\n"); break; // Corrected Definition
            case 15: logMessage("Internal Use Mode / Core Initializing\n"); break;
            default: logMessage("Unknown State (0x%02X)\n", engineState); break;
        }
    }

    // 2. Safely parse out the 16-bit Fault Code from Bytes 2 and 3
    uint16_t activeFaultCode = (msg.data[3] << 8) | msg.data[2];
    static uint16_t lastFaultCode = 0x0000;

    // Filter out J1939 blank/padding blocks
    if (activeFaultCode == 0x0000 || activeFaultCode == 0xFFFF || activeFaultCode == 0x00FF || activeFaultCode == 0xFF00) {
        // Only clear out the fault flag if the generator drops out of Fault State 6
        if (lastFaultCode != 0 && engineState != 6) {
            logMessage("\n✔ [DIAGNOSTIC] Faults Cleared. System Normal.\n");
            lastFaultCode = 0;
        }
        return; 
    }

    // 3. Process the fault code if it is a real number and has changed
    if (activeFaultCode != lastFaultCode) {
        lastFaultCode = activeFaultCode;
        logMessage("\n------------------------------------------------\n");
        logMessage("⚠ [HGLCA INVERTER FAULT CODE ENCOUNTERED]\n");
        logMessage("Raw Stream Payload Matrix: ");
        for(int i = 0; i < 8; i++) {
            logMessage("%02X ", msg.data[i]);
        }
        logMessage("\n");

        bool matchFound = false;
        for (int i = 0; i < ONAN_DB_COUNT; i++) {
            uint16_t tableFault = pgm_read_word(&(ONAN_FAULT_TABLE[i].faultNumber));
            
            if (tableFault == activeFaultCode) {
                const char* label = (const char*)pgm_read_ptr(&(ONAN_FAULT_TABLE[i].displayLabel));
                logMessage("Factory Mapping Description: %s\n", label);
                logMessage("Manual Reference Integer : Code %d\n", activeFaultCode);
                matchFound = true;
                break;
            }
        }

        if (!matchFound) {
            logMessage("Alert: Unmapped Inverter Code -> Code %d\n", activeFaultCode);
            uint16_t reverseParsedCode = (msg.data[2] << 8) | msg.data[3];
            logMessage("Alternative Big-Endian Fallback : Code %d\n", reverseParsedCode);
        }
        logMessage("------------------------------------------------\n");
    }
}

