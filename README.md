<div align="center">

# Cummins Onan HGLCA Diagnostic Engine & Telematics Gateway

A lightweight, high-performance ESP32 application designed to interface with the digital controller of a **Cummins Onan HGLCA (Gasoline/Inverter)** RV generator. By tapping into the vehicle's secondary CAN bus framework via custom protocol reverse-engineering, this system maps proprietary operational state transitions, tracks heavy-duty diagnostic parameters, and serves a thread-safe live diagnostic dashboard right to your web browser.

---

### 🚧 DEVELOPMENT STATUS: WORK IN PROGRESS 🚧
*This project is currently under active development and field-testing. The protocol mapping is being reverse-engineered sequentially; as a result, **not all factory fault codes, operational sub-states, or diagnostic variables have been mapped out yet.** Features and code structures are subject to changes as new network frames are cataloged.*

---

</div>

<div align="center"> 
<table> 
<tr> 
<td align="center" valign="top"> 
<img src="https://github.com/user-attachments/assets/60e86fe0-6b3c-4bd5-9706-b0d6310e5aa3" alt="circuit_image" height="250" /><br /> 
<sub><b>Circuit Diagram</b></sub> 
</td> 
<td align="center" valign="top"> 
<img src="https://github.com/user-attachments/assets/a7a3e850-8a35-4710-a52f-0cc2abd3cd3e" alt="Screenshot 2026-06-11 135819" height="250" /><br /> 
<sub><b>Onan CAN</b></sub> 
</td>

<td align="center" valign="bottom"> 
<img src="https://github.com/user-attachments/assets/5b97c472-69fc-492f-8583-920183ec214e" alt="Screenshot 2026-06-26 9 34 40 AM" height="250" /><br /> 
<sub><b>Web Dashboard</b></sub> 
</td> 
 
</tr> 
</table> 
</div>
<img width="1076" height="785" alt="Screenshot 2026-06-26 9 34 40 AM" src="https://github.com/user-attachments/assets/5b97c472-69fc-492f-8583-920183ec214e" />

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
## 🧠 ESP32-C3 Memory & Partition Mapping

The system utilizes a custom partition scheme optimized for **4MB (4194304 bytes)** of physical flash memory. It balances large dual application slots for safe **Over-The-Air (OTA) updates** with a dedicated flash file system partition for system logging.

### 📊 Partition Layout Overview

| Partition Name | Type | SubType | Offset Address | Size (Hex) | Size (Decimal) | Purpose |
| :--- | :--- | :--- | :--- | :--- | :--- | :--- |
| **`nvs`** | data | nvs | `0x9000` | `0x5000` | 20 KB | Non-Volatile Storage (WiFi credentials, system variables) |
| **`otadata`** | data | ota | `0xe000` | `0x2000` | 8 KB | OTA Rollback control register & boot slot indicator |
| **`app0`** | app | ota_0 | `0x10000` | `0x1C0000` | 1,792 KB (1.75 MB) | Active Factory/Primary Firmware Execution Slot |
| **`app1`** | app | ota_1 | `0x1D0000` | `0x1C0000` | 1,792 KB (1.75 MB) | Passive Target Firmware Storage Slot for OTA updates |
| **`spiffs`** | data | spiffs | `0x390000` | `0x60000` | 384 KB | **LittleFS File System** for storing active generator logs (`log.txt`) |

### 🛠️ Visual Flash Memory Allocation

```text
[nvs/ota] [==== app0 (Active App) ====] [==== app1 (OTA Target) ====] [LittleFS Log]
 (28 KB)           (1,792 KB)                     (1,792 KB)             (384 KB)

|------------------------------------ Total: 4,000 KB (~4.0 MB) ----------------------|
```

### 🔒 Safety and OTA Headroom Analysis

* **Zero Size Risks**: A standard diagnostic powertrain application of this scale compiles to roughly **850 KB - 950 KB**. 
* **Dual-Slot Matrix**: Because both `app0` and `app1` are isolated into identical 1.75 MB containers, the new binary can download fully into the passive slot before the ESP32-C3 reboots and validates the flash.
* **Storage Buffer Guard**: If an OTA stream fails midway through transmission, the active slot remains untouched and functional.

### ⚙️ Implementation Files

#### `partitions.csv`
```csv
# Name,   Type, SubType, Offset,   Size,     Flags
nvs,      data, nvs,     0x9000,   0x5000,
otadata,  data, ota,     0xe000,   0x2000,
app0,     app,  ota_0,   0x10000,  0x1C0000,
app1,     app,  ota_1,   0x1D0000, 0x1C0000,
spiffs,   data, spiffs,  0x390000, 0x60000,
```


--------------------- 6/26/2026--------------------------


## 🔧 Hardware Upgrade: Adafruit CAN Pal Deployment

The system has been upgraded from the passive 3.3V SN65HVD230 breakout board to the robust, vehicle-grade **[Adafruit CAN Pal (TJA1051T/3)](https://adafruit.com)**. This single-board transceiver solution bridges the 3.3V logic of the ESP32-C3 with the heavy industrial 5.0V differential signaling constraints required by the Cummins HGLCA generator.

### 🔌 Safe 5-Wire Interfacing Pinout

To protect the ESP32-C3 from high-voltage logic leakage and bypass critical bootloader strapping pin locks (like `GPIO0`), the hardware loop must be wired **exactly** as follows:

| Adafruit CAN Pal Pin | ESP32-C3 Dev Board Pin | Signal Type | Purpose |
| :--- | :--- | :--- | :--- |
| **`VCC`** | **`5V` / `VBUS`** | 5V Power Supply | Drives high-output CAN transmission coils |
| **`GND`** | **`GND`** | Ground Reference | Common ground plane bridge |
| **`TXD`** | **`GPIO1` (TX)** | 3.3V Logic Input | Passes outgoing web dashboard command states |
| **`RXD`** | **`GPIO2` (RX)** | 3.3V Logic Output | **Safe Pin 2:** Prevents silent bootloader locking |
| **`VIO` / `V_LEV`** | **`3.3V`** | 3.3V Logic Ref | **Crucial:** Clamps RX line signals safely to 3.3V |

> ⚠️ **Power Architecture Rule**: The `STBY` (Standby) pin on the Adafruit CAN Pal package is internally tied down to `GND` via a surface-mount resistor trace on the circuit board layout. **The transceiver chip is permanently physically awake and unlocked.** Do not manually ground it.

---

## 📊 Deep J1939 Multi-PGN Telemetry Layout

Instead of tracking parameters inside a single proprietary message, the Cummins HGLCA generator distributes real-time metrics across distinct standard SAE J1939 Parameter Group Numbers (PGNs). The firmware uses an **Accept All Pass Filter** to decode the complete engine profile seamlessly:

```text
               [ CUMMINS INVERTER POWERTRAIN NETWORK CAN-BUS TRUNK ]
                                        |
      +-----------------+---------------+----------------+-----------------+

      |                 |               |                |                 |
  PGN 65280         PGN 61444       PGN 64409        PGN 65030         PGN 65271
(Genset State)    (Engine Speed)  (Inverter Temp)  (Basic AC Quant)   (Vehicle Power)
  [Byte 0]         [Bytes 4-5]       [Byte 2]       [Bytes 2-5]        [Bytes 4-5]

      |                 |               |                |                 |
      +-----------------+---------------+----------------+-----------------+
                                        |
                        [ ESP32-C3 MULTI-PGN ENGINE ]
```

### 🔍 Decoded Parameter Matrix

1. **Genset State / Operational Status (PGN 65280)**
   * **Byte Location**: Byte 0 (Index `0`)
   * **States**: `1` = Stopped / Inactive, `2` = Starting / Cranking, `3` = Running / Producing AC, `5` = Fuel Priming, `6` = Fault Tripped.
2. **Engine Speed / RPM (PGN 61444 - EEC1)**
   * **Byte Location**: Bytes 4 and 5 (Indices `3` and `4`)
   * **Resolution**: `0.125 RPM/bit`, Little Endian Byte Alignment.
3. **Inverter Module Temperature (PGN 64409 - DCAC_AI1_T)**
   * **Byte Location**: Byte 3 (Index `2`)
   * **Resolution**: `1 °C/bit`, Offset: `-40 °C`.
4. **Line-to-Neutral AC RMS Voltage (PGN 65030 - GAAC)**
   * **Byte Location**: Bytes 3 and 4 (Indices `2` and `3`)
   * **Resolution**: `1 V/bit`, Little Endian Byte Alignment.
5. **Average AC Frequency (PGN 65030 - GAAC)**
   * **Byte Location**: Bytes 5 and 6 (Indices `4` and `5`)
   * **Resolution**: `1/128 Hz/bit`, Little Endian Byte Alignment.
6. **Battery Potential / DC Input Voltage (PGN 65271 - VEP1)**
   * **Byte Location**: Bytes 5 and 6 (Indices `4` and `5`)
   * **Resolution**: `0.05 V/bit`, Little Endian Byte Alignment.

---

## 🔒 Automated Control Fallback & Table 7 Bitmask Validation

Remote control commands override physical switches using **PGN 59904 (Request Network)** directed explicitly toward the inverter controller at target address `0x21` from source tool address `0x27`. 

* **Crank Start Execution**: Broadcasts `0xF2` (Table 7 Operational Command Key `2`).
* **Kill Engine Execution**: Broadcasts `0xF1` (Table 7 Operational Command Key `1`).
* **Fuel Lift Pump Priming**: Holds down `0xF1` (Table 7 Operational Command Key `1`), mimicking a long physical button hold to engage low-pressure priming pathways.
* **Safety Isolation**: If a command is triggered but the generator network fails to achieve a handshake verification state within **3 seconds**, an internal watchdog auto-releases the transmission loop to `0xF0` (Idle) to prevent starter motor burnouts or system locks.
