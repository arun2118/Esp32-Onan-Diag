# Cummins Onan HGLCA Diagnostic Engine & Telematics Gateway

A lightweight, high-performance ESP32 application designed to interface with the digital controller of a **Cummins Onan HGLCA (Gasoline/Inverter)** RV generator. By tapping into the vehicle's secondary CAN bus framework via custom protocol reverse-engineering, this system maps proprietary operational state transitions, tracks heavy-duty diagnostic parameters, and serves a thread-safe live diagnostic dashboard right to your web browser.

---

## 🚀 Key Features

* **Real-Time Telemetry Mapping**: Deeply decodes proprietary state matrices directly off the controller layer, identifying system modes like Warm-up/Choke, Fuel Priming, and active AC generation.
* **Persistent Diagnostic Logging**: Saves your generator's state logs into the ESP32’s flash memory using an optimized **LittleFS** file layout designed to safely survive sudden power drops.
* **Smart Flash Memory Truncation**: A rolling memory supervisor actively limits log file boundaries to ~50KB to preserve device stability and protect internal flash chips from over-wear.
* **Multithreaded Thread-Safety**: Uses FreeRTOS binary locks (`Mutexes`) to securely split workloads across both processing cores—ensuring rapid CAN processing doesn't collide with the web server.
* **Wireless Firmware Management (OTA)**: Built-in Over-the-Air updates with client-side flags to let you flash new binaries (`.bin`) without pulling the ESP32 out of your coach.

---

## 📟 Decoded System States (Gasoline Profile)

Unlike standard J1939 frameworks, the HGLCA digital architecture treats its primary status byte as a single linear tracking sequence. This project cleanly maps out those states to accurately match gasoline engine properties:

* **State 0**: Ready / Standby (AC Field Disconnected)
* **State 1**: Stopped / Mechanical Engine Inactive
* **State 2**: Starting / Active Cranking Sequence
* **State 3**: Running / Actively Producing AC Power
* **State 4**: Warm-up Mode / Electronic Automatic Choke Active
* **State 5**: Fuel Priming Running (Lift Pump Active via Stop hold > 3s)
* **State 6**: Fault Shutdown Active (Controller forces a protective engine kill)

---

## 🛠️ Hardware Requirements

1. **Microcontroller**: ESP32 Development Board (NodeMCU / ESP32-WROOM-32).
2. **CAN Bus Transceiver**: SN65HVD230, TJA1050, or ISO1050 isolated transceiver module.
3. **Connection Interface**: Tap into the generator's network terminal blocks (CAN_H / CAN_L).

---

## 📁 Repository Codebase Architecture

The application layout handles background tasks and network traffic using the standard Arduino IDE / ESP-IDF framework:

```text
├── htmlDashboard     # Embedded CSS/HTML UI matrix using safe HTML Entities
├── twaiBackground    # Dedicated Core-0 FreeRTOS thread monitoring CAN inputs
├── logMessage Engine # Variadic thread-safe ring-buffer with timestamp headers
└── LittleFS Service  # Local flash partition structure handling /log.txt exports
```

---

## 🔌 Web Endpoint API Directory

The embedded web server exposes direct endpoints allowing external scripts, automation nodes, or tools to interact with your generator data:

* `GET /` : Serves the main visual Telemetry and Update Dashboard.
* `GET /telemetry` : Exposes the active web text buffer for live terminal polling.
* `GET /download-log` : Triggers an immediate download of the historical `onan_generator_log.txt` file.
* `POST /clear-log` : Instructs LittleFS to wipe flash memory logs and reset log entry counters.

---

## 📝 Example Log Stream Structure

The tracking header prepends an incremental line counter alongside system uptime (`HH:MM:SS`) to give exact duration intervals for system events:

```text
[#1 @ 00:00:02] ⚡ [GENSET STATE CHANGE] Status: Stopped / Engine Inactive
[#2 @ 00:01:15] ⚡ [GENSET STATE CHANGE] Status: FUEL PRIMING RUNNING (Lift Pump Engaged)
[#3 @ 00:01:45] ⚡ [GENSET STATE CHANGE] Status: Starting / Cranking Engine
[#4 @ 00:01:48] ⚡ [GENSET STATE CHANGE] Status: Warm-up Mode / Automatic Choke Active
[#5 @ 00:01:54] ⚡ [GENSET STATE CHANGE] Status: Running / Producing AC Power
```
