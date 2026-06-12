<div align="center">

# Cummins Onan HGLCA Diagnostic Engine & Telematics Gateway

A lightweight, high-performance ESP32 application designed to interface with the digital controller of a **Cummins Onan HGLCA (Gasoline/Inverter)** RV generator. By tapping into the vehicle's secondary CAN bus framework via custom protocol reverse-engineering, this system maps proprietary operational state transitions, tracks heavy-duty diagnostic parameters, and serves a thread-safe live diagnostic dashboard right to your web browser.

---

### 🚧 DEVELOPMENT STATUS: WORK IN PROGRESS 🚧
*This project is currently under active development and field-testing. The protocol mapping is being reverse-engineered sequentially; as a result, **not all factory fault codes, operational sub-states, or diagnostic variables have been mapped out yet.** Features and code structures are subject to changes as new network frames are cataloged.*

---

<table> 
<tr> 
<td align="center" valign="top"> 
<img src="https://github.com" alt="circuit_image" height="250" /><br /> 
<sub><b>Circuit Diagram</b></sub> 
</td> 
<td align="center" valign="top"> 
<img src="https://github.com" alt="Screenshot 2026-06-11 135819" height="250" /><br /> 
<sub><b>Onan CAN</b></sub> 
</td> 
</tr> 
</table> 

</div>


<div align="center"> 
<table> 
<tr> 
<td align="center" valign="top"> 
<img src="https://github.com/user-attachments/assets/8c418f4c-76ec-45f3-a1aa-15ad97b0558e" alt="circuit_image" height="250" /><br /> 
<sub><b>Circuit Diagram</b></sub> 
</td> 
<td align="center" valign="top"> 
<img src="https://github.com/user-attachments/assets/a7a3e850-8a35-4710-a52f-0cc2abd3cd3e" alt="Screenshot 2026-06-11 135819" height="250" /><br /> 
<sub><b>Onan CAN</b></sub> 
</td> 
</tr> 
</table> 
</div>

---

## 🚀 Key Features

* **Real-Time Telemetry Mapping**: Deeply decodes proprietary state matrices directly off the controller layer, identifying system modes like Warm-up/Choke, Fuel Priming, Engine running, and active AC generation. 
* **Persistent Diagnostic Logging**: Saves your generator's state logs into the ESP32’s flash memory using an optimized **LittleFS** file layout designed to safely survive sudden power drops. 
* **Smart Flash Memory Truncation**: A rolling memory supervisor actively limits log file boundaries to ~50KB to preserve device stability and protect internal flash chips from over-wear. 
* **Multithreaded Thread-Safety**: Uses FreeRTOS binary locks (`Mutexes`) to securely split workloads across both processing cores—ensuring rapid CAN processing doesn't collide with the web server. 
* **Wireless Firmware Management (OTA)**: Built-in Over-the-Air updates with client-side flags to let you flash new binaries (`.bin`) without pulling the ESP32 out of your system.

---

## 📡 J1939 CAN Bus Telemetry & Decoding Architecture

While standard heavy-duty commercial J1939 networks rely heavily on standard Parameter Group Numbers (PGNs) like **Electronic Engine Controller 1 (EEC1 - PGN 61444)** for RPM, the Cummins Onan HGLCA digital controller utilizes a proprietary application layer footprint optimized for inverter-generator topologies.

### 🔢 Protocol Frame Breakdown

All critical operational states and diagnostic fault vectors are broadcasted continuously over an extended 29-bit identifier framework under **Proprietary PGN 65280 (0xFF00)**. 

* **CAN Identifier**: `0x18FF00XX` (Where `XX` represents the dynamic Source Address of the controller node)
* **Data Length Code (DLC)**: 8 Bytes
* **Transmission Rate**: 100ms (Continuous Loop)

```text
 [ Byte 0 ]   [ Byte 1 ]   [ Byte 2 ]   [ Byte 3 ]   [ Byte 4 ]   [ Bytes 5-7 ]
 ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌───────────┐
 │ State   │  │Reserved │  │ Fault   │  │ Fault   │  │Reserved │  │  Unused   │
 │ ID (Int)│  │ (0x00)  │  │ Dig1(Ch)│  │ Dig2(Ch)│  │ (0x00)  │  │  (0xFF)   │
 └─────────┘  └─────────┘  └─────────┘  └─────────┘  └─────────┘  └───────────┘
```

| Byte Index | Data Field Assignment | Format Type | Description / Context |
| :--- | :--- | :--- | :--- |
| **Byte 0** | Engine Operational State ID | Raw Integer | Maps linear active sequences (States 0-6, 15) |
| **Byte 1** | Factory Network Reserved | Padding | Static boundary placeholder (`0x00`) |
| **Byte 2** | Fault Sequence Digit 1 | ASCII Text | Streams primary code character (e.g., `'5'`) |
| **Byte 3** | Fault Sequence Digit 2 | ASCII Text | Streams secondary code character (e.g., `'3'`) |
| **Byte 4** | Factory Network Reserved | Padding | Static boundary placeholder (`0x00`) |
| **Byte 5** | Unused Data Slot | J1939 Standard | Empty padding byte (`0xFF`) |
| **Byte 6** | Unused Data Slot | J1939 Standard | Empty padding byte (`0xFF`) |
| **Byte 7** | Unused Data Slot | J1939 Standard | Empty padding byte (`0xFF`) |

### 🚨 Diagnostic Streaming Logic & Bug Resolution

The key breakthrough in this project lies in how the Cummins firmware encodes and streams fault data across the network bus, which differs by scenario:

#### Scenario A: The Active Memory Loop (Historical / Intermittent Status)
When faults are broadcasted during non-crash states, the data is pushed out as literal **Sequential ASCII Characters** inside **Byte 2**. 
* For example, to broadcast **Fault Code 36**, the engine outputs `0x33` (ASCII character `'3'`) in one frame, instantly followed by `0x36` (ASCII character `'6'`) in the next. 
* The parsing engine captures these characters, applies an ASCII-to-integer conversion scale (`rawByte - 0x30`), and evaluates them only when a distinct new digit rolls onto the bus to eliminate sequential frame repetition spam.

#### Scenario B: The Live Runtime Crash Override (Sensor Disconnection)
When a critical sensor circuit fails during engine runtime (such as pulling the **Oil Temperature Sensor wire**), the controller undergoes a hard safety split:
1. It immediately forces Byte 0 into **`0x06` (Fault Shutdown)**.
2. It simultaneously **wipes the data stream bytes clean** to zero (`06 00 00 FF FF FF FF FF`).

Because the raw network streams display an empty `0x00` during this runtime state change, standard J1939 index algorithms default to showing an unmapped `Code 0`. This software implements a **Context-Aware Safety Interpreter**. When a hard `0x06` drop is detected alongside an empty payload, the code evaluates the preceding stable state layer. If a crash cuts directly out of engine execution sequences with zeroed data, the ESP32 intercepts the frame, maps it straight to **Code 53 (Oil Temperature Sensor Circuit Fault)**, and prints the corresponding manual description from `PROGMEM` flash storage.

---

## 📟 Decoded System States (Gasoline Profile)

Unlike standard J1939 frameworks, the HGLCA CAN digital architecture treats its primary status byte as a single linear tracking sequence. This project cleanly maps out those states to accurately match gasoline engine properties:

* **State 0**: Ready / Standby (AC Field Disconnected)
* **State 1**: Stopped / Mechanical Engine Inactive
* **State 2**: Starting / Active Cranking Sequence
* **State 3**: Running / Actively Producing AC Power
* **State 4**: Warm-up Mode / Electronic Automatic Choke Active
* **State 5**: Fuel Priming Running (Lift Pump Active via Stop hold > 3s)
* **State 6**: Fault Shutdown Active (Controller forces a protective engine kill)

---

## 🔌 Hardware Schematics & Pinout Mapping

The system is powered directly via the vehicle 12V battery system. A high-efficiency step-down buck converter supplies the microcontroller, while the isolated CAN transceiver utilizes the micro-controller's internal 3.3V regulator to maintain clean logic-level matching across the data bus lines.

### 📍 Hardware Pin Connection Index

| Component A (Source) | Terminal / Pin Name | Component B (Destination) | Wire Color in Print | Purpose / Context |
| :--- | :--- | :--- | :--- | :--- |
| **12V Battery** | **[ + ] Positive** | 5V Buck Converter (IN+) | 🔴 Red | Raw Power Supply Input |
| **12V Battery** | **[ - ] Negative** | 5V Buck Converter (IN-) | ⚫ Black | System Ground Reference |
| **5V Buck Converter** | **[ + ] Output** | ESP32-C3 SM (**5V**) | 🧠 Pink / Magenta | Main Microcontroller Power |
| **5V Buck Converter** | **[ - ] Output** | ESP32-C3 SM (**G**) | 🟢 Green | Common Power Ground |
| **ESP32-C3 SuperMini** | **3.3V** | SN65HVD230 (**3V3**) | 🟠 Orange | Transceiver Power Rail |
| **ESP32-C3 SuperMini** | **G (GND)** | SN65HVD230 (**GND**) | 🟢 Green | Integrated Logic Ground |
| **ESP32-C3 SuperMini** | **GPIO 2** | SN65HVD230 (**CTX**) | ⚪ White / Light Blue | CAN Transmit (TX Line) |
| **ESP32-C3 SuperMini** | **GPIO 3** | SN65HVD230 (**CRX**) | 🧬 Mint / Teal | CAN Receive (RX Line) |
| **SN65HVD230 Board** | **CANH** | Deutsch DT-3 Pin (Pin 1) | 🍏 Olive Green | High Differential Data Line |
| **SN65HVD230 Board** | **CANL** | Deutsch DT-3 Pin (Pin 2) | 🔵 Dark Blue | Low Differential Data Line |
| **Deutsch Connector** | **Pin 3 (Ground)** | ESP32-C3 SM (**G**) | 🟢 Green | Network Shield / Common Ground |

---

## 🛠️ Hardware Requirements

1. **Microcontroller**: ESP32 Development Board (NodeMCU / ESP32-C3 SuperMini / ESP32-WROOM-32).
2. **CAN Bus Transceiver**: SN65HVD230, TJA1050, or ISO1050 isolated transceiver module.
3. **Connection Interface**: Tap into the generator's network terminal blocks (CAN_H / CAN_L) via Deutsch DT-3 connector.

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
