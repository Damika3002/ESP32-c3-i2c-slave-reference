# Example 03: Fan Controller

Production-ready example integrating I2C slave communication with fan PWM control, temperature sensing, thermal policy, and eFuse monitoring. This example demonstrates how to combine diagnostic I2C with real hardware control on the ESP32-C3FH4.

---

## Table of Contents

- [Overview](#overview)
- [Hardware Requirements](#hardware-requirements)
- [Wiring Diagram](#wiring-diagram)
- [Pull-up Resistors](#pull-up-resistors)
- [Arduino IDE Configuration](#arduino-ide-configuration)
- [Protocol](#protocol)
- [Thermal Policy](#thermal-policy)
- [System Status Bitmask](#system-status-bitmask)
- [Features](#features)
- [Serial Output](#serial-output)
- [Expected Output](#expected-output)
- [How It Works](#how-it-works)
- [Command Reference](#command-reference)
- [Testing from Radxa](#testing-from-radxa)
- [Safety Considerations](#safety-considerations)
- [Difference from Example 02](#difference-from-example-02)
- [Troubleshooting](#troubleshooting)

---

## Overview

This example extends the diagnostic slave (Example 02) with a complete fan controller implementation. It includes:

- 8 I2C commands for monitoring and control
- Automatic thermal policy with 5 temperature thresholds
- Fan override mode for manual control
- eFuse (TPS25940) fault detection
- System status bitmask for compact reporting
- Error counters for diagnostics
- 1 Hz control loop

The I2C link serves as the diagnostic interface. The UART link remains the authoritative path for safety-critical fan control.

---

## Hardware Requirements

| Component | Specification |
|-----------|---------------|
| Microcontroller | ESP32-C3FH4 (4 MB internal flash) |
| Master | Radxa ROCK 5C (/dev/i2c-6) |
| I2C Address | 0x08 (7-bit) |
| SDA Pin | GPIO4 |
| SCL Pin | GPIO5 |
| Status LED | GPIO2 |
| Fan PWM | GPIO10 |
| Temperature Sensor | GPIO0 (ADC1_CH0) |
| eFuse Fault Input | GPIO9 (TPS25940, active low) |
| Bus Frequency | 50 kHz |

---

## Wiring Diagram

```
Radxa ROCK 5C              ESP32-C3FH4
---------------             ------------
I2C6 SDA pin  -------->  GPIO4 (SDA)
I2C6 SCL pin  -------->  GPIO5 (SCL)
GND           -------->  GND

                           ESP32-C3FH4          External Components
                           ------------          -------------------
                           GPIO2  ------>  LED (330 ohm) ----> GND
                           GPIO10 ------>  Fan PWM (via NPN/NMOS driver)
                           GPIO0  ------>  Temperature sensor (analog)
                           GPIO9  ------>  TPS25940 eFuse FAULT pin

USB Cable ---------> PC (for Serial Monitor)
```

### Fan PWM Driver

The ESP32-C3 GPIO10 cannot drive a fan directly. Use an NPN transistor or N-channel MOSFET as a low-side switch:

```
3.3V or 5V
   |
  Fan (+)
   |
  Fan (-)
   |
  Collector (NPN) or Drain (NMOS)
   |
  Emitter (NPN) or Source (NMOS) ----> GND
   |
  Base (NPN) or Gate (NMOS) <---- 330 ohm <---- GPIO10
```

---

## Pull-up Resistors

The I2C bus requires external pull-up resistors on both SDA and SCL lines.

### Recommended Values

| Resistor Value | Suitability | Notes |
|----------------|-------------|-------|
| 1.5k ohm | Good for long bus / high capacitance | Stronger pull-up, more current draw |
| 2.2k ohm | Recommended (used in this project) | Validated working at 50 kHz |
| 4.7k ohm | Acceptable for short bus / low capacitance | Standard value, lower current draw |

### Configuration Used

```
3.3V
 |
2.2k ohm
 |
 +---> SDA (GPIO4)

3.3V
 |
2.2k ohm
 |
 +---> SCL (GPIO5)
```

The eFuse fault input (GPIO9) uses the ESP32-C3 internal pull-up (`INPUT_PULLUP`). If the TPS25940 FAULT pin is open-drain, the internal pull-up is sufficient.

---

## Arduino IDE Configuration

| Setting | Value |
|---------|-------|
| Board | ESP32C3_DEV |
| Arduino-ESP32 Core | 3.3.8 |
| Upload Speed | 921600 |
| CPU Frequency | 160 MHz |
| Flash Frequency | 80 MHz |
| Flash Mode | QIO |
| Flash Size | 4 MB |
| Partition Scheme | Default |
| USB CDC On Boot | Enabled |
| Debug Level | None |

---

## Protocol

### Master Write (2 bytes)

```
Byte 0: 0x01              (request marker)
Byte 1: <command>         (command code)
```

### Slave Response (6 bytes)

```
Byte 0: 0xA5              (response marker)
Byte 1: <status>          (status code)
Byte 2: <command echo>    (echo of received command)
Byte 3: <value>            (response value, command-dependent)
Byte 4: <sequence>         (increments per transaction)
Byte 5: <checksum>         (XOR of bytes 0-4)
```

---

## Thermal Policy

The fan controller implements a 5-level thermal policy. When fan override is active, the thermal policy is bypassed.

| Temperature Range | Fan Duty | PWM Value | Description |
|-------------------|----------|-----------|-------------|
| Below 35.0 C | Off | 0 | No cooling needed |
| 35.0 C to 44.9 C | Low | 80 | Minimal cooling (~31%) |
| 45.0 C to 54.9 C | Medium | 140 | Moderate cooling (~55%) |
| 55.0 C to 64.9 C | High | 200 | Aggressive cooling (~78%) |
| 65.0 C to 84.9 C | Full | 255 | Maximum cooling (100%) |
| 85.0 C and above | Full | 255 | Critical - full speed + warning |

### eFuse Fault Behavior

If the TPS25940 eFuse signals a fault (GPIO9 LOW), the fan immediately goes to full speed regardless of the thermal policy. This ensures maximum cooling in case of a power protection event.

### Fan Override

The master can override the thermal policy by sending command 0x20. This sets the fan to full speed and disables the automatic thermal policy. Command 0x21 clears the override and restores automatic control.

---

## System Status Bitmask

Command 0x2A returns a status bitmask in byte 3 of the response:

| Bit | Mask | Name | Description |
|-----|------|------|-------------|
| 0 | 0x01 | Fan running | Fan PWM duty is greater than 0 |
| 1 | 0x02 | Fan override | Fan override is active |
| 2 | 0x04 | Thermal warning | Temperature is 55 C or above |
| 3 | 0x08 | Thermal critical | Temperature is 85 C or above |
| 4 | 0x10 | eFuse fault | TPS25940 fault pin is LOW |
| 5 | 0x20 | I2C error | Invalid frames have been received |
| 6 | 0x40 | Reserved | Not used |
| 7 | 0x80 | Reserved | Not used |

---

## Features

### I2C Diagnostic Commands (8 commands)

| Command | Name | Returns | Description |
|---------|------|---------|-------------|
| 0x99 | Initialize | Fan duty | Heartbeat, returns current fan PWM duty |
| 0x2A | Status | Status bitmask | Returns system status byte |
| 0x10 | Read Temperature | Temp in C | Returns temperature as integer |
| 0x11 | Read Fan Speed | RPM / 100 | Returns fan RPM divided by 100 |
| 0x20 | Fan Override ON | 0x01 (ack) | Force fan to full speed |
| 0x21 | Fan Override OFF | 0x01 (ack) | Restore automatic thermal control |
| 0x30 | Read eFuse | 0x00 or 0x01 | Returns eFuse fault status |
| 0x40 | Read Errors | Error bitmask | Returns accumulated error flags |

### Automatic Thermal Policy

- Reads temperature every 1 second
- Adjusts fan speed based on 5 thermal thresholds
- Responds to eFuse faults immediately
- Tracks thermal warning events

### Fan Override Mode

- Master can force fan to full speed via I2C
- Override bypasses thermal policy
- Override clears on command or can be left active

### Error Tracking

| Error Type | Counter | Description |
|------------|---------|-------------|
| Temperature sensor | temp_error_count | Reading outside -10 to 150 C range |
| eFuse fault | efuse_fault_count | TPS25940 FAULT pin went LOW |
| Invalid I2C frame | invalid_count | Wrong marker or length |
| Thermal warning | thermal_warning_count | Temperature reached 85 C |

---

## Serial Output

### Startup Message

```
========================================
ESP32-C3 I2C Slave - Fan Controller
========================================
I2C Address: 0x08
SDA: GPIO4 | SCL: GPIO5 | LED: GPIO2
Fan PWM: GPIO10 | eFuse: GPIO9
Frequency: 50000 Hz

Fan controller ready.
I2C for diagnostics. UART for safety-critical control.
========================================
```

### Status Report (every 1 second)

```
--- FAN CONTROLLER STATUS ---
Temp: 42.3 C | Fan: 80/255 | RPM: 1568
Status: 0x01 | Override: OFF
I2C: rx=5 req=5 tx=5 inv=0
Faults: efuse=0 thermal=0 temp_err=0
------------------------------
```

### Transaction Output

```
RX: 01 99
CMD: 0x99 (Initialize)
RESP: A5 00 99 50 01 6D
TX: A5 00 99 50 01 6D
```

---

## Expected Output

### Command 0x99 (Initialize/Heartbeat)

After sending:
```bash
sudo i2ctransfer -y 6 w2@0x08 0x01 0x99
sleep 0.50
sudo i2ctransfer -y 6 r6@0x08
```

```diff
+ 0xa5 0x00 0x99 0x50 0x01 0x6d
```

- Byte 3 = 0x50 (80 decimal = current fan duty at low speed)

### Command 0x2A (Status)

```bash
sudo i2ctransfer -y 6 w2@0x08 0x01 0x2a
sleep 0.50
sudo i2ctransfer -y 6 r6@0x08
```

```diff
+ 0xa5 0x02 0x2a 0x01 0x02 0x8a
```

- Byte 3 = 0x01 (fan running, no other issues)

### Command 0x10 (Read Temperature)

```bash
sudo i2ctransfer -y 6 w2@0x08 0x01 0x10
sleep 0.50
sudo i2ctransfer -y 6 r6@0x08
```

```diff
+ 0xa5 0x00 0x10 0x2a 0x03 0x98
```

- Byte 3 = 0x2A (42 decimal = 42 degrees C)

### Command 0x20 (Fan Override ON)

```bash
sudo i2ctransfer -y 6 w2@0x08 0x01 0x20
sleep 0.50
sudo i2ctransfer -y 6 r6@0x08
```

```diff
+ 0xa5 0x00 0x20 0x01 0x04 0x80
```

- Byte 3 = 0x01 (acknowledgement)

### Command 0x21 (Fan Override OFF)

```bash
sudo i2ctransfer -y 6 w2@0x08 0x01 0x21
sleep 0.50
sudo i2ctransfer -y 6 r6@0x08
```

```diff
+ 0xa5 0x00 0x21 0x01 0x05 0x85
```

- Byte 3 = 0x01 (acknowledgement)

### Incorrect Output

```diff
- 0x11 0xa5 0x00 0x99 0x50 0x01    (shifted - using slaveWrite)
- Error: Sending messages failed: No such device or address    (slave not responding)
- 0xa5 0xee 0x20 0xe1 0x04 0x70    (invalid frame response)
```

---

## How It Works

### Architecture

```
Radxa ROCK 5C (Master)                    ESP32-C3FH4 (Slave)
- I2C6 (/dev/i2c-6)                      - I2C slave @ 0x08
- Sends diagnostic commands              - Receives and processes commands
- Reads status responses                 - Returns diagnostic data

                                         - GPIO10: Fan PWM output
                                         - GPIO0:  Temperature sensor input
                                         - GPIO9:  eFuse fault input
                                         - GPIO2:  Status LED

                                         - 1 Hz control loop:
                                           1. Read temperature
                                           2. Apply thermal policy
                                           3. Update RPM estimate
                                           4. Update system status

Radxa ROCK 5C (Master)
- UART                                   ESP32-C3FH4
- Safety-critical fan control            - UART receiver
- Temperature monitoring                 - Fan control execution
- Emergency shutdown                     - Failsafe response
```

### Control Loop

The main loop runs at 1 Hz (once per second). Each cycle:

1. Read temperature from ADC
2. Check eFuse fault pin
3. Apply thermal policy or fan override
4. Update estimated fan RPM
5. Update system status bitmask
6. Print status to serial

### I2C Transaction Flow

```
Master writes 01 99            -->   receiveEvent() fires
                                      - Reads command
                                      - Looks up fan duty
                                      - Prepares response with fan duty as value

Master reads 6 bytes           <--   requestEvent() fires
                                      - Wire.write() sends response
                                      - LED toggles
```

### Fan Override Flow

```
Master sends 01 20            -->   receiveEvent() fires
                                      - Sets fan_override = true
                                      - Sets fan_override_duty = 255
                                      - Returns ack

Control loop                       -->   applyThermalPolicy()
                                      - Checks fan_override
                                      - If true: setFanSpeed(fan_override_duty)
                                      - Thermal policy bypassed

Master sends 01 21            -->   receiveEvent() fires
                                      - Sets fan_override = false
                                      - Returns ack
                                      - Thermal policy resumes
```

---

## Command Reference

### 0x99 - Initialize / Heartbeat

| Field | Value |
|-------|-------|
| Write | 01 99 |
| Response | A5 00 99 <fan_duty> <seq> <checksum> |
| Value (byte 3) | Current fan PWM duty (0-255) |
| Use case | Periodic heartbeat to verify slave is alive |

### 0x2A - Status Request

| Field | Value |
|-------|-------|
| Write | 01 2A |
| Response | A5 02 2A <status_bitmask> <seq> <checksum> |
| Value (byte 3) | System status bitmask (see Status Bitmask section) |
| Use case | Quick overall system health check |

### 0x10 - Read Temperature

| Field | Value |
|-------|-------|
| Write | 01 10 |
| Response | A5 00 10 <temp_c> <seq> <checksum> |
| Value (byte 3) | Temperature in degrees Celsius (integer) |
| Use case | Monitor current temperature |

### 0x11 - Read Fan Speed

| Field | Value |
|-------|-------|
| Write | 01 11 |
| Response | A5 00 11 <rpm_byte> <seq> <checksum> |
| Value (byte 3) | Fan RPM divided by 100 (e.g., 200 = 20,000 RPM max range) |
| Use case | Verify fan is spinning at expected speed |

### 0x20 - Fan Override ON

| Field | Value |
|-------|-------|
| Write | 01 20 |
| Response | A5 00 20 01 <seq> <checksum> |
| Value (byte 3) | 0x01 (acknowledgement) |
| Effect | Forces fan to full speed, disables thermal policy |
| Use case | Emergency cooling or manual override |

### 0x21 - Fan Override OFF

| Field | Value |
|-------|-------|
| Write | 01 21 |
| Response | A5 00 21 01 <seq> <checksum> |
| Value (byte 3) | 0x01 (acknowledgement) |
| Effect | Clears override, restores automatic thermal policy |
| Use case | Return to normal operation after override |

### 0x30 - Read eFuse Status

| Field | Value |
|-------|-------|
| Write | 01 30 |
| Response | A5 00 30 <efuse_status> <seq> <checksum> |
| Value (byte 3) | 0x00 = OK, 0x01 = fault |
| Use case | Check TPS25940 eFuse health |

### 0x40 - Read Error Counters

| Field | Value |
|-------|-------|
| Write | 01 40 |
| Response | A5 00 40 <error_bitmask> <seq> <checksum> |
| Value (byte 3) | Error bitmask (see below) |
| Use case | Diagnose accumulated errors |

Error bitmask:

| Bit | Mask | Error |
|-----|------|-------|
| 0 | 0x01 | Temperature sensor error |
| 1 | 0x02 | eFuse fault occurred |
| 2 | 0x04 | I2C invalid frame received |
| 3 | 0x08 | Thermal warning reached |

---

## Testing from Radxa

### Step 1: Verify the slave is on the bus

```bash
sudo i2cdetect -y 6
```

### Step 2: Send heartbeat

```bash
sudo i2ctransfer -y 6 w2@0x08 0x01 0x99
sleep 0.50
sudo i2ctransfer -y 6 r6@0x08
```

### Step 3: Read temperature

```bash
sudo i2ctransfer -y 6 w2@0x08 0x01 0x10
sleep 0.50
sudo i2ctransfer -y 6 r6@0x08
```

### Step 4: Read system status

```bash
sudo i2ctransfer -y 6 w2@0x08 0x01 0x2a
sleep 0.50
sudo i2ctransfer -y 6 r6@0x08
```

### Step 5: Enable fan override

```bash
sudo i2ctransfer -y 6 w2@0x08 0x01 0x20
sleep 0.50
sudo i2ctransfer -y 6 r6@0x08
```

### Step 6: Clear fan override

```bash
sudo i2ctransfer -y 6 w2@0x08 0x01 0x21
sleep 0.50
sudo i2ctransfer -y 6 r6@0x08
```

### Step 7: Full diagnostic sequence

```bash
for cmd in 99 2a 10 11 30 40; do
  echo "=== Command 0x$cmd ==="
  sudo i2ctransfer -y 6 w2@0x08 0x01 0x$cmd
  sleep 0.30
  sudo i2ctransfer -y 6 r6@0x08
  sleep 0.30
done
```

---

## Safety Considerations

### I2C Is Not Safety-Critical

The ESP32-C3 I2C slave has hardware limitations:

- No clock stretching support
- FIFO management requires careful API usage
- Occasional address ACK failures

### UART Is the Authority

The Radxa ROCK 5C must use UART for:

- Emergency fan shutdown
- Critical temperature alerts
- Safety-critical fan speed commands
- System-wide thermal policy decisions

### eFuse Fault Response

If the TPS25940 eFuse signals a fault, the ESP32-C3 immediately sets the fan to full speed. This happens in the 1 Hz control loop, not waiting for I2C commands.

### Thermal Policy Is a Fallback

The thermal policy in this example is a local fallback. The Radxa ROCK 5C may override it via UART or I2C. If the I2C link fails, the ESP32-C3 continues to operate autonomously using the local thermal policy.

### Temperature Sensor Fallback

If the temperature sensor reads outside the -10 C to 150 C range, the reading is discarded and 25 C (ambient) is used as a fallback. This prevents runaway fan behavior from sensor failures.

---

## Difference from Example 02

| Feature | Example 02 (Diagnostic) | Example 03 (Fan Controller) |
|---------|------------------------|----------------------------|
| Commands | 2 (0x99, 0x2A) | 8 (0x99, 0x2A, 0x10, 0x11, 0x20, 0x21, 0x30, 0x40) |
| Fan control | No | Yes (PWM on GPIO10) |
| Temperature sensing | No | Yes (ADC on GPIO0) |
| eFuse monitoring | No | Yes (GPIO9) |
| Thermal policy | No | Yes (5 thresholds) |
| Fan override | No | Yes (commands 0x20/0x21) |
| System status bitmask | No | Yes (8-bit) |
| Error bitmask | No | Yes (8-bit) |
| Control loop | No (callbacks only) | Yes (1 Hz in loop) |
| RPM estimation | No | Yes (simulated) |
| Safety fallback | No | Yes (eFuse, thermal, sensor) |

---

## Troubleshooting

### Fan Not Spinning

1. Verify GPIO10 is connected to the fan driver transistor/MOSFET
2. Check that the fan driver circuit is powered
3. Verify temperature is above the fan-on threshold (35 C)
4. Check serial monitor for fan duty value
5. Try fan override: `sudo i2ctransfer -y 6 w2@0x08 0x01 0x20`

### Temperature Reads 25 C Always

1. Verify temperature sensor is connected to GPIO0
2. Check sensor wiring and power
3. Check serial monitor for temperature readings
4. Verify sensor type matches the conversion formula in the code
5. If readings are outside -10 to 150 C, the fallback to 25 C is triggered

### eFuse Fault Always Active

1. Verify TPS25940 FAULT pin is connected to GPIO9
2. Check if the eFuse has actually tripped (check power supply)
3. Verify GPIO9 is configured as INPUT_PULLUP
4. The FAULT pin may be active-low; ensure polarity matches

### I2C Commands Return Invalid Frame

1. Check that the master is sending exactly 2 bytes
2. Verify byte 0 is 0x01 (request marker)
3. Check for bus noise (shorter wires, pull-up resistors)
4. Verify the I2C address is 0x08

### Fan Override Does Not Clear

1. Send command 0x21 to clear override
2. If still not clearing, reset the ESP32-C3
3. Check serial monitor for override status
4. Verify the control loop is running (status reports every 1 second)
