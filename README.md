<div align="center">

# ⚡ Cummins Onan HGLCA J1939 Digital Control Interface

An ESP32 and high-speed CAN transceiver module implementation providing full bi-directional diagnostic telemetry tracking and real-time remote powertrain control loops for Cummins HGLCA inverter generators.

---

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

## 🔧 Hardware Architecture & Secure 5-Wire Pinout

The physical layer utilizes an automotive-grade CAN transceiver module to step up the ESP32's 3.3V digital logic to the robust 5.0V differential CAN signal standard required by heavy industrial vehicle networks.

To bypass critical microcontroller boot configuration requirements (strapping pin logic vectors) and maintain high-speed wireless processing stability without data logic line saturation, the hardware array must be wired exactly as follows:

| High-Speed CAN Transceiver Pin | Direction | ESP32 Controller Micro Pin | Context / Safety Rule |
| :--- | :---: | :--- | :--- |
| **`VCC`** | ──> | **`5V Main Power`** | Powers the high-output CAN transmission coils |
| **`GND`** | <─> | **`Common GND`** | Common ground reference plane bridge |
| **`TXD`** | <── | **`Hardware TX Pin`** | Sends web command pulses out to the transceiver |
| **`RXD`** | ──> | **`Hardware RX Pin`** | **Safe Pin Selection:** Bypasses silent bootloader locking loops |
| **`VIO / V_LEVEL`** | <─> | **`3.3V Rail Out`** | **Crucial:** Safely clamps transceiver logic signals down to 3.3V |

---


---

## 🎯 J1939 Network Telemetry & Multi-PGN Parsing Matrix

The Cummins HGLCA platform splits real-time metrics across distinct Parameter Group Numbers (PGNs). The firmware operates an **Accept All Pass Filter**, utilizing Little Endian multi-byte realignment to populate the telemetry indicators simultaneously:

*   **Genset State / Status (PGN 65280)**: Byte 0. Mapped states: `1`=Stopped, `2`=Cranking, `3`=Running, `5`=Priming, `6`=Fault Target.
*   **Engine Speed / RPM (PGN 61444)**: Bytes 4-5. Resolution: `0.125 RPM/bit`.
*   **Inverter Temperature (PGN 64409)**: Byte 3. Resolution: `1 °C/bit` with a `-40 °C` offset.
*   **AC RMS Output Voltage (PGN 65030)**: Bytes 3-4. Resolution: `1 V/bit`.
*   **AC Line Frequency (PGN 65030)**: Bytes 5-6. Resolution: `1/128 Hz/bit`.
*   **DC Battery System Input (PGN 65271)**: Bytes 5-6. Resolution: `0.05 V/bit`.

---

## 📡 Remote Control Automation Engine (PGN 65281)

Remote operation is achieved entirely over software data streams. Commands bypass safety blocks by utilizing **Source Address `0x27`** (Authorized Service/Diagnostic Tool profile) targeting the dedicated control registration pipeline explicitly:

```text
       CUMMINS ONAN REMOTE OPERATION CONTROL FRAME
+-------------------------------------------------------+

|  CAN Extended ID: 0x0CFF0127                          |
|  PGN Type       : Proprietary B PGN 65281 (0xFF01)    |
|  Target Offset  : Byte 1 (Index 0), Lower 4-Bit Nibble|
+-------------------------------------------------------+
```

### 📋 Bitmask Control Key Layout (SPN 65281)

Per core product specifications, commands occupy the lower nibble of Byte 1, while the upper bits are maintained with `0xF` masking padding. Transitions occur via high-speed, non-blocking **50ms decoupled pulse streams**:

*   **🚀 Crank Start (`0xF2`)**: Transmits raw code `2` to SPN 65281 for up to 4 seconds or until a running feedback loop is achieved, then cleanly drops.
*   **🛑 Kill Engine (`0xF1`)**: Transmits raw code `1` to SPN 65281 for 2 seconds, forcing an immediate mechanical shutdown sequence.
*   **💽 Fuel Priming (`0xF1` Prolonged Hold)**: Transmits raw code `1` continuously for 12 seconds. Holding the "Stop" channel low while the machine is fully inactive satisfies the ECU logic gate to engage the low-pressure lift pump.
*   **🔓 None / Button Release (`0xF0`)**: Protocol compliance requires actively broadcasting code `0` to release the digital button hook, returning the line to idle safely without causing an `SPN 524032` loss-of-communication fault trigger.

---

## 🚨 Advanced Trouble Code Diagnosis & Manual Mapping

The system actively listens to **PGN 65226 (DM1 Active Diagnostic Trouble Codes)**. When a powertrain fault triggers, the background parser intercepts the raw frame, extracts the SPN and FMI bits dynamically, and pushes explicit maintenance guidelines straight from the repair manual to the web console buffer:

*   **SPN 110 | FMI 2** ➔ *Code 36 (Abnormal Shutdown)*: Uncommanded Mechanical Stall/Fuel Loss. Check fuel level, lines, and IPM pump relays.
*   **SPN 1268 | FMI 4** ➔ *Code 36 (Abnormal Shutdown)*: Primary Winding Open Circuit on Ignition Coil. Check plugs and measure coil resistance.
*   **SPN 931 | FMI 2** ➔ *Code 52 (Fuel Circuit Fault)*: IPM Fuel Pump open circuit/short. Replace pump or generator ECU driver.
*   **SPN 651 | FMI 2** ➔ *Code 52 (Fuel Circuit Fault)*: Fuel Injector circuit outlier/overcurrent. Inspect injector module pins.
*   **SPN 4083 | FMI 8** ➔ *Code 57 (Overprime Warning)*: Fuel prime active for over 3 continuous minutes. Release Stop key immediately.
*   **SPN 1390 | FMI 2** ➔ *Code 57 (Pressure Sensor Fault)*: High or low LP sensor voltage outlier. Verify tank pressure (9-13 in WC).
---
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

---

### 🎯 J1939 Network Telemetry & Multi-PGN Parsing Matrix

The Cummins HGLCA platform splits real-time metrics across distinct Parameter Group Numbers (PGNs). The firmware operates an **Accept All Pass Filter**, utilizing Little Endian multi-byte realignment to populate the telemetry indicators simultaneously:

*   **Engine Speed / RPM (PGN 61444)**: Bytes 4-5. Resolution: `0.125 RPM/bit`.
*   **Inverter Temperature (PGN 64409)**: Byte 3. Resolution: `1 °C/bit` with a `-40 °C` offset.
*   **AC RMS Output Voltage (PGN 65030)**: Bytes 3-4. Resolution: `1 V/bit`.
*   **AC Line Frequency (PGN 65030)**: Bytes 5-6. Resolution: `1/128 Hz/bit`.
*   **DC Battery System Input (PGN 65271)**: Bytes 5-6. Resolution: `0.05 V/bit`.

---

### 📑 Protocol Frame Breakdown: Proprietary Status & Fault Code Vectoring

All critical operational states and baseline diagnostic fault vectors are broadcasted continuously over an extended 29-bit identifier framework under **Proprietary PGN 65280 (0xFF00)**. 

*   **CAN Identifier**: `0x0CFF00XX` (Where `XX` represents the dynamic Source Address of the controller node, typically `0x21`)
*   **Data Length Code (DLC)**: 8 Bytes
*   **Transmission Rate**: 100ms (Continuous Cyclic Loop)

```text
 [ Byte 0 ]     [ Byte 1 ]     [ Byte 2 ]     [ Byte 3 ]     [ Byte 4 ]     [ Bytes 5-7 ]
+------------+ +------------+ +------------+ +------------+ +------------+ +-------------+

|   State    | |  Reserved  | |   Fault    | |   Fault    | |  Reserved  | |   Unused    |
|  ID (Int)  | |   (0x00)   | |  Dig1(Ch)  | |  Dig2(Ch)  | |   (0x00)   | |   (0xFF)    |
+------------+ +------------+ +------------+ +------------+ +------------+ +-------------+
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

---

### 📡 Remote Control Automation Engine (PGN 65281)

Remote operation is achieved entirely over software data streams. Commands bypass safety blocks by utilizing **Source Address `0x27`** (Authorized Service/Diagnostic Tool profile) targeting the dedicated control registration pipeline explicitly:

```text
       CUMMINS ONAN REMOTE OPERATION CONTROL FRAME
+-------------------------------------------------------+

|  CAN Extended ID: 0x0CFF0127                          |
|  PGN Type       : Proprietary B PGN 65281 (0xFF01)    |
|  Target Offset  : Byte 1 (Index 0), Lower 4-Bit Nibble|
+-------------------------------------------------------+
```

### 📋 Bitmask Control Key Layout (SPN 65281)

Per core product specifications, commands occupy the lower nibble of Byte 1, while the upper bits are maintained with `0xF` masking padding. Transitions occur via high-speed, non-blocking **50ms decoupled pulse streams**:

*   **🚀 Crank Start (`0xF2`)**: Transmits raw code `2` to SPN 65281 for up to 4 seconds or until a running feedback loop is achieved, then cleanly drops.
*   **🛑 Kill Engine (`0xF1`)**: Transmits raw code `1` to SPN 65281 for 2 seconds, forcing an immediate mechanical shutdown sequence.
*   **💽 Fuel Priming (`0xF1` Prolonged Hold)**: Transmits raw code `1` continuously for 12 seconds. Holding the "Stop" channel low while the machine is fully inactive satisfies the ECU logic gate to engage the low-pressure lift pump.
*   **🔓 None / Button Release (`0xF0`)**: Protocol compliance requires actively broadcasting code `0` to release the digital button hook, returning the line to idle safely without causing an `SPN 524032` loss-of-communication fault trigger.


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

### 🔌 Hardware Schematics & Pinout Mapping

This project utilizes an **ESP32** microcontroller to interface with a high-speed CAN network through a **TJA1051T/3** transceiver breakout. The system uses an external **3.3V Buck Converter** to safely step down raw automotive battery voltages to provide a single, unified logic power rail.

### 🖼️ Schematic Overview

```text
   [ 12V / 24V Battery ]
       │            │
       ▼ (+)        ▼ (-)
  ┌─────────┐  ┌──────────┐
  │ Buck IN │  │ Buck GND │──────┐
  └─────────┘  └──────────┘      │
       │                         │
       ▼ (+3.3V Output)          │
 ┌──────────┐                    │
 │  ESP32   │                    ││                    │
 └──────────┘                    ▼
  │  │  │  │               ┌───────────┐
  │  │  │  └──────────────>│ Transceiver│ ─── [ CAN_H / CAN_L ]
  │  │  └─────────────────>│ TJA1051T/3 │      (Generator Port)
  ▼  ▼                     └───────────┘
[ Logic Link: RX / TX ]
```


### ⚙️ Important Hardware Notes

* **Unified 3.3V Power Architecture**: In this configuration, the external buck converter supplies regulated `3.3V` directly to the `3.3` power rails of both the ESP32-C3 and the **TJA1051T/3**. The `5V` pin on the ESP32 is left completely disconnected.
* **Safe USB Concurrent Hookup**: Because external power bypasses the ESP32's internal 5V-to-3.3V low-dropout (LDO) linear regulator completely, you can safely connect your computer's USB-C cable for debugging and code flashing while the vehicle battery is actively connected.
* **Silent Mode Control**: To establish seamless bidirectional telemetry, the transceiver's `SLNT` pin is looped through the automotive plug terminal block directly back to common `GND` via a static hardware bridge. This overrides passive mode configurations and forces an active, normal-write system state.


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



### 🔌 Safe 5-Wire Interfacing Pinout

To protect the ESP32-C3 from high-voltage logic leakage and bypass critical bootloader strapping pin locks (like `GPIO0`), the hardware loop must be wired **exactly** as follows:

| Adafruit CAN Pal Pin | ESP32-C3 Dev Board Pin | Signal Type | Purpose |
| :--- | :--- | :--- | :--- |
| **`VCC`** | **`5V` / `VBUS`** | 5V Power Supply | Drives high-output CAN transmission coils |
| **`GND`** | **`GND`** | Ground Reference | Common ground plane bridge |
| **`TXD`** | **`GPIO1` (TX)** | 3.3V Logic Input | Passes outgoing web dashboard command states |
| **`RXD`** | **`GPIO0` (RX)** | 3.3V Logic Output | **Safe Pin 0:** Alt GPIO2 if bootloader locks |
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
                        [ ESP32 MULTI-PGN ENGINE ]
```


