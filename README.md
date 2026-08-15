# ESP32-C3 I2C Slave Reference Implementation

Reference implementation for ESP32-C3 as an I2C slave device, demonstrating the correct use of `Wire.write()` in the `onRequest()` callback to avoid stale and shifted data on the ESP32 I2C slave FIFO.

---

## Table of Contents

- [Overview](#overview)
- [Problem Statement](#problem-statement)
- [Root Cause Analysis](#root-cause-analysis)
- [Solution](#solution)
- [Hardware Configuration](#hardware-configuration)
- [Protocol Specification](#protocol-specification)
- [Quick Start](#quick-start)
- [Repository Structure](#repository-structure)
- [Examples](#examples)
- [Testing and Validation](#testing-and-validation)
- [Radxa ROCK 5C Configuration](#radxa-rock-5c-configuration)
- [Arduino IDE Configuration](#arduino-ide-configuration)
- [Troubleshooting](#troubleshooting)
- [References](#references)
- [License](#license)
- [Author](#author)

---

## Overview

This repository provides a tested, documented reference implementation for operating the ESP32-C3 as an I2C slave. It addresses a well-known issue where the ESP32 I2C slave returns stale or shifted bytes when the Arduino `Wire` library is used incorrectly.

The implementation was developed and validated as part of a fan controller project where the Radxa ROCK 5C acts as the I2C master and the ESP32-C3 acts as the slave providing diagnostic and status data.

### Key Facts

- The ESP32-C3 I2C slave peripheral does not support hardware clock stretching in slave mode.
- The ESP32 I2C hardware FIFO does not automatically clear between transactions.
- The Arduino `Wire` library provides a software buffer (`txBuffer`) that is cleared before each `onRequest()` callback, but only `Wire.write()` uses this buffer.
- `Wire.slaveWrite()` bypasses this buffer management and writes directly to the hardware FIFO, which retains stale data.

---

## Problem Statement

Many developers report stale or shifted bytes when using the ESP32 as an I2C slave. This issue is documented across multiple platforms:

- GitHub Issue [#5906](https://github.com/espressif/arduino-esp32/issues/5906): I2C Slave data stale
- GitHub Issue [#10145](https://github.com/espressif/arduino-esp32/issues/10145): ESP32 S3 I2C issues when working as slave
- Arduino Forum (January 2026): ESP32 slave I2C responds with previous value
- ProductionESP32.com: ESP32 I2C Slave Issues

### Observed Symptoms

When using `Wire.slaveWrite()` in the `onRequest()` callback, the master receives shifted data:

```
Expected response:  A5 00 99 11 01 2C
Actual response:    11 A5 00 99 11 01
```

The leading byte (`0x11`) is byte 3 of the previous response. This happens because the hardware FIFO retains old data and the new response is appended after it rather than replacing it.

Additional symptoms include:

- Frames beginning with `0xEE`, `0x01`, `0xE1`, or `0x04` instead of `0xA5`
- Trailing `0xFF` bytes when the TX FIFO is empty or misaligned
- Intermittent `No such device or address` errors from the Linux master

---

## Root Cause Analysis

### The Incorrect Approach

When `Wire.slaveWrite()` is called inside the `onRequest()` callback, it bypasses the Arduino `Wire` library's internal `txBuffer` management and writes directly to the hardware FIFO.

```cpp
void requestEvent() {
    // INCORRECT: Bypasses txBuffer, causes stale FIFO data
    Wire.slaveWrite(response, sizeof(response));
}
```

The ESP32 I2C hardware FIFO is not cleared automatically between transactions. Old response bytes remain in the FIFO. When new data is written, it is appended. The master then reads a mixture of old and new bytes, producing shifted frames.

### Why Wire.slaveWrite() Causes Stale Data

The Arduino-ESP32 `Wire` library implements the slave request path as follows:

1. `onRequestService()` is called when the master begins a read.
2. It sets `wire->txLength = 0` to reset the software buffer.
3. It calls the user's `onRequest()` callback.
4. After the callback returns, it checks if `wire->txLength > 0`.
5. If so, it sends the `txBuffer` contents to the slave HAL.

When you call `Wire.write()` inside the callback, data goes into `txBuffer`. This buffer was already cleared in step 2, so only fresh data is sent.

When you call `Wire.slaveWrite()` inside the callback, data goes directly to the hardware FIFO. This bypasses the cleared `txBuffer`, and old FIFO data is not removed.

---

## Solution

Use `Wire.write()` in the `onRequest()` callback instead of `Wire.slaveWrite()`. This leverages the Arduino `Wire` library's automatic `txBuffer` clearing and submission.

### The Correct Approach

```cpp
void requestEvent() {
    // CORRECT: Uses txBuffer which is automatically cleared
    // before each onRequest() call
    for (uint8_t i = 0; i < RESPONSE_SIZE; i++) {
        Wire.write(response[i]);
    }
}
```

### Additional Rules

1. Do not call `Wire.slaveWrite()` from `receiveEvent()`. Only prepare the `response[]` array there.
2. Do not call `Wire.slaveWrite()` from both `receiveEvent()` and `requestEvent()`. This double-queues data and produces stale FIFO output.
3. Always send one write command before one read. Do not perform repeated reads without a new write command.

---

## Hardware Configuration

### Microcontroller

| Parameter | Value |
|-----------|-------|
| Chip | ESP32-C3FH4 |
| Package | QFN-32 |
| Flash | 4 MB Internal (Embedded) |
| RAM | 400 KB SRAM |
| CPU | 32-bit RISC-V single-core, 160 MHz |
| WiFi | 802.11b/g/n |
| Bluetooth | Bluetooth 5 (LE) |
| I2C Controllers | 1 |

### I2C Bus Configuration

| Parameter | Value |
|-----------|-------|
| Master | Radxa ROCK 5C |
| Linux Adapter | /dev/i2c-6 |
| Slave | ESP32-C3FH4 |
| Slave Address | 0x08 (7-bit) |
| SDA Pin | GPIO4 |
| SCL Pin | GPIO5 |
| Bus Frequency | 50 kHz (recommended) |
| Status LED | GPIO2 (do not use GPIO8) |

### Wiring

```
Radxa ROCK 5C          ESP32-C3FH4
-------------          -----------
I2C6 SDA          ---> GPIO4 (SDA)
I2C6 SCL          ---> GPIO5 (SCL)
GND               ---> GND
3.3V (optional)   ---> 3V3
```

External pull-up resistors of 4.7k ohm to 3.3V are recommended on both SDA and SCL lines. The ESP32-C3 internal pull-ups are weak and not sufficient for reliable I2C communication.

### GPIO8 Warning

GPIO8 on the ESP32-C3 is a strapping pin. It must be held HIGH during boot to enter normal boot mode. Using GPIO8 for an LED or other output can interfere with the boot process or the I2C peripheral. Use GPIO2 or another non-strapping pin for status indicators.

---

## Protocol Specification

### Master Write Frame (2 bytes)

| Byte | Offset | Value | Description |
|------|--------|-------|-------------|
| 0 | 0x01 | Request marker | Identifies a valid command frame |
| 1 | 0x99 or 0x2A | Command code | The command to execute |

### Slave Response Frame (6 bytes)

| Byte | Offset | Value | Description |
|------|--------|-------|-------------|
| 0 | 0xA5 | Response marker | Identifies a valid response frame |
| 1 | 0x00-0xFF | Status | Status code for the command |
| 2 | 0x00-0xFF | Command echo | Echo of the received command |
| 3 | 0x00-0xFF | Value | Response value |
| 4 | 0x00-0xFF | Sequence | Incremented per accepted write command |
| 5 | 0x00-0xFF | Checksum | XOR of bytes 0 through 4 |

### Checksum Formula

```
checksum = marker ^ status ^ command ^ value ^ sequence
```

### Defined Commands

| Command | Name | Request | Expected Response |
|---------|------|---------|-------------------|
| 0x99 | Initialize | 01 99 | A5 00 99 11 <seq> <checksum> |
| 0x2A | Status | 01 2A | A5 02 2A 2B <seq> <checksum> |

### Status Codes

| Code | Name | Description |
|------|------|-------------|
| 0x00 | STATUS_OK | Command accepted and processed |
| 0x01 | STATUS_UNKNOWN_COMMAND | Command not recognized |
| 0x02 | STATUS_STATUS_REPLY | Status reply response |
| 0xEE | STATUS_INVALID_FRAME | Invalid frame (wrong marker or length) |

---

## Quick Start

### Step 1: Upload Firmware to ESP32-C3

1. Open Arduino IDE
2. Select Board: ESP32C3_DEV
3. Configure settings (see Arduino IDE Configuration section)
4. Upload `examples/01_Basic_Slave/01_Basic_Slave.ino`

### Step 2: Configure Radxa ROCK 5C

```bash
sudo apt update
sudo apt install -y i2c-tools
```

### Step 3: Test Communication

```bash
# Scan for the slave
sudo i2cdetect -y 6

# Should show 0x08 on the bus

# Send command 0x99 and read response
sudo i2ctransfer -y 6 w2@0x08 0x01 0x99
sleep 0.50
sudo i2ctransfer -y 6 r6@0x08

# Expected: 0xa5 0x00 0x99 0x11 0x01 0x2c
```

---

## Repository Structure

```
esp32-c3-i2c-slave-reference/
|
|-- README.md                              Main documentation (this file)
|-- LICENSE                                MIT License
|-- .gitignore                             Git ignore rules
|
|-- examples/
|   |-- 01_Basic_Slave/
|   |   |-- 01_Basic_Slave.ino             Minimal working example
|   |   `-- README.md
|   |
|   |-- 02_Diagnostic_Slave/
|   |   |-- 02_Diagnostic_Slave.ino        Full diagnostic implementation
|   |   `-- README.md
|   |
|   `-- 03_Fan_Controller/
|       |-- 03_Fan_Controller.ino          Production-ready example
|       `-- README.md
|
|-- radxa/
|   |-- README.md                          Radxa configuration guide
|   |-- PROCEDURE.md                       Detailed 50 kHz configuration procedure
|   |-- test_i2c.sh                        Automated test script
|   `-- i2c6-50k-overlay.dts              Device tree overlay source
|
`-- docs/
    |-- TROUBLESHOOTING.md                 Comprehensive troubleshooting guide
    |-- PROTOCOL_REFERENCE.md             Detailed protocol documentation
    `-- HARDWARE_GUIDE.md                  Wiring and hardware guide
```

---

## Examples

### Example 1: Basic Slave

Minimal implementation demonstrating the `Wire.write()` fix. No serial output, no statistics, no LED. Suitable for quick validation.

**Location:** `examples/01_Basic_Slave/`

### Example 2: Diagnostic Slave

Full implementation with serial debug output, statistics counters, frame validation, and status LED. Suitable for development and debugging.

**Location:** `examples/02_Diagnostic_Slave/`

### Example 3: Fan Controller

Production-ready example integrating I2C slave communication with fan PWM control, temperature sensing, and thermal policy. Suitable as a starting point for actual fan controller projects.

**Location:** `examples/03_Fan_Controller/`

---

## Testing and Validation

### Automated Test Script

```bash
cd radxa
chmod +x test_i2c.sh
./test_i2c.sh cycle    # Run 10 cycles (20 transactions)
./test_i2c.sh single   # Single transaction test
./test_i2c.sh stress   # 100 cycles stress test
```

### Validation Results

The implementation was validated with the following results:

- 20 consecutive successful transactions (10 x command 0x99, 10 x command 0x2A)
- All frames began with marker 0xA5
- All command echoes were correct
- All XOR checksums were correct
- Sequence counter incremented once per accepted write command
- No shifted bytes observed
- No "No such device or address" errors during the test run

### Validation Checklist

- 20 or more consecutive successful transactions
- No shifted bytes (first byte is always 0xA5)
- Correct checksums on all frames
- Sequence counter increments correctly
- No "No such device or address" errors

---

## Radxa ROCK 5C Configuration

### Device Tree Overlay

To configure I2C6 to 50 kHz, compile and apply a device tree overlay:

```bash
# Compile the overlay
dtc -@ -I dts -O dtbo -o i2c6-50k.dtbo i2c6-50k-overlay.dts

# Copy to overlay directory
sudo cp i2c6-50k.dtbo /boot/dtb/rockchip/overlay/

# Add to boot configuration
sudo nano /boot/armbianEnv.txt
# Add: user_overlays=i2c6-50k

# Reboot
sudo reboot

# Verify the frequency
NODE=$(readlink -f /sys/class/i2c-adapter/i2c-6/device/of_node)
od -An -tu4 "$NODE/clock-frequency"
# Should output: 50000
```

### Detailed Procedure

See `radxa/PROCEDURE.md` for the complete configuration procedure including backup, restore, and verification steps.

---

## Arduino IDE Configuration

### Board Settings

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

### Library Dependencies

| Library | Version | Required |
|---------|---------|----------|
| Wire | 3.3.8 (built-in) | Yes |
| Arduino | 3.3.8 (built-in) | Yes |

No external libraries are required for the basic and diagnostic examples.

---

## Troubleshooting

### Common Issues

| Issue | Symptom | Cause | Solution |
|-------|---------|-------|----------|
| Shifted bytes | `11 A5 00 99...` | Using `Wire.slaveWrite()` | Use `Wire.write()` in `onRequest()` |
| No device | "No such device or address" | Slave not ACKing | Check wiring, power, pull-ups |
| Wrong checksum | Checksum mismatch | Data corruption or calc error | Verify checksum formula |
| LED not blinking | GPIO8 LED inactive | GPIO8 is a strapping pin | Use GPIO2 instead |
| Stale data | Previous response returned | FIFO not cleared | Use `Wire.write()` not `Wire.slaveWrite()` |
| 0xFF bytes | `01 FF FF FF FF FF` | TX FIFO empty | Ensure `receiveEvent()` prepares response |

### Debugging Steps

1. Verify wiring: SDA, SCL, GND, and 3.3V connections
2. Open ESP32 Serial Monitor at 115200 baud
3. Scan the bus: `sudo i2cdetect -y 6` (should show 08)
4. Use a logic analyzer on SDA and SCL
5. Measure ESP32 VCC (should be stable 3.3V)
6. Check for external pull-up resistors

See `docs/TROUBLESHOOTING.md` for the comprehensive troubleshooting guide.

---

## References

- [Arduino-ESP32 I2C API Documentation](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/i2c.html)
- [ESP-IDF I2C Documentation](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/i2c.html)
- [GitHub Issue #5906: I2C Slave data stale](https://github.com/espressif/arduino-esp32/issues/5906)
- [GitHub Issue #10145: ESP32 S3 I2C issues](https://github.com/espressif/arduino-esp32/issues/10145)
- [ProductionESP32: ESP32 I2C Slave Issues](https://productionesp32.com/posts/esp32-i2c-slave-issue/)
- [ESP32-C3 Datasheet](https://www.espressif.com/sites/default/files/documentation/esp32-c3_datasheet_en.pdf)

---

## License

MIT License. See the [LICENSE](LICENSE) file for details.

---

## Author

**Damika3002**

- GitHub: [https://github.com/Damika3002](https://github.com/Damika3002)
- Repository: [https://github.com/Damika3002/ESP32-c3-i2c-slave-reference](https://github.com/Damika3002/ESP32-c3-i2c-slave-reference)

### Project Context

This implementation was developed as part of the `fancontrol` project. The Radxa ROCK 5C serves as the I2C master and the ESP32-C3FH4 serves as the I2C slave. The I2C link provides diagnostic and status monitoring. The safety-critical fan control path uses UART.

### Hardware Used

- ESP32-C3FH4 (4 MB internal flash, QFN-32 package)
- Radxa ROCK 5C (RK3588S, I2C6 controller)
- External 4.7k ohm pull-up resistors on SDA and SCL

### Contributions

Contributions are welcome. Please submit improvements via Pull Requests or report issues via GitHub Issues.

---

**Important:** This implementation is intended for diagnostic and status monitoring purposes. The ESP32-C3 I2C slave has hardware limitations including no clock stretching support and manual FIFO management requirements. For safety-critical applications, use UART or a microcontroller with full I2C slave hardware support such as the STM32F407.
