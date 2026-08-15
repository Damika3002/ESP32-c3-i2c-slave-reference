# Troubleshooting Guide

Comprehensive troubleshooting guide for the ESP32-C3 I2C slave reference implementation. This document covers every issue encountered during development, along with symptoms, root causes, and verified solutions.

---

## Table of Contents

- [Issue Classification](#issue-classification)
- [Issue 1: Shifted Response Bytes](#issue-1-shifted-response-bytes)
- [Issue 2: Double-Queued Data](#issue-2-double-queued-data)
- [Issue 3: 0xFF Fill Bytes](#issue-3-0xff-fill-bytes)
- [Issue 4: No Such Device or Address](#issue-4-no-such-device-or-address)
- [Issue 5: GPIO8 LED Conflict](#issue-5-gpio8-led-conflict)
- [Issue 6: Native ESP-IDF Driver Issues](#issue-6-native-esp-idf-driver-issues)
- [Issue 7: Intermittent Bus Detection](#issue-7-intermittent-bus-detection)
- [Issue 8: Incorrect Checksum](#issue-8-incorrect-checksum)
- [Issue 9: Wrong I2C Frequency](#issue-9-wrong-i2c-frequency)
- [Issue 10: Device Tree Overlay Not Applied](#issue-10-device-tree-overlay-not-applied)
- [Issue 11: No Serial Output](#issue-11-no-serial-output)
- [Issue 12: Compilation Errors](#issue-12-compilation-errors)
- [Debugging Tools and Techniques](#debugging-tools-and-techniques)
- [Decision Flowchart](#decision-flowchart)

---

## Issue Classification

| Category | Issues | Severity |
|----------|--------|----------|
| Data integrity | 1, 2, 3, 8 | Critical |
| Bus communication | 4, 7, 9, 10 | High |
| Hardware | 5, 11 | Medium |
| Software/API | 6, 12 | High |

---

## Issue 1: Shifted Response Bytes

### Symptoms

The master receives a response where the first byte is from the previous transaction instead of the current one:

```diff
- 0x11 0xa5 0x00 0x99 0x11 0x01
```

Expected:

```diff
+ 0xa5 0x00 0x99 0x11 0x01 0x2c
```

The leading `0x11` is byte 3 (response value) from the previous `0x99` command response.

### Root Cause

`Wire.slaveWrite()` was used in the `requestEvent()` callback. This function writes directly to the ESP32 I2C hardware TX FIFO. The FIFO is not automatically cleared between transactions. New data is appended after old data, and the master reads a mixture of old and new bytes.

### Verification

Check the `requestEvent()` function in your code:

```cpp
// WRONG - causes this issue
void requestEvent() {
    Wire.slaveWrite(response, sizeof(response));
}
```

### Solution

Replace `Wire.slaveWrite()` with `Wire.write()`:

```cpp
// CORRECT - uses txBuffer which is auto-cleared
void requestEvent() {
    for (uint8_t i = 0; i < RESPONSE_SIZE; i++) {
        Wire.write(response[i]);
    }
}
```

### Technical Explanation

The Arduino Wire library's `onRequestService()` function:

1. Sets `txLength = 0` (clears software buffer)
2. Calls `requestEvent()` (your callback)
3. Checks if `txLength > 0`
4. If yes, sends `txBuffer` to hardware

`Wire.write()` fills `txBuffer` (cleared in step 1). `Wire.slaveWrite()` bypasses `txBuffer` and writes directly to hardware FIFO (not cleared).

---

## Issue 2: Double-Queued Data

### Symptoms

The master receives 12 or more bytes of mixed data instead of 6 clean bytes. The response contains fragments of two different responses concatenated together.

### Root Cause

`Wire.slaveWrite()` was called in both `receiveEvent()` and `requestEvent()`. The first call queues 6 bytes when the command is received. The second call queues 6 more bytes when the master reads. The FIFO now contains 12 bytes.

### Verification

Check both callback functions:

```cpp
// WRONG - double-queuing
void receiveEvent(int count) {
    // ... process command ...
    Wire.slaveWrite(response, sizeof(response));  // queues here
}

void requestEvent() {
    Wire.slaveWrite(response, sizeof(response));  // queues again here
}
```

### Solution

Only prepare the `response[]` array in `receiveEvent()`. Only send data from `requestEvent()` using `Wire.write()`:

```cpp
// CORRECT
void receiveEvent(int count) {
    // ... process command ...
    // Prepare response[] array only
    // Do NOT call Wire.slaveWrite() or Wire.write()
}

void requestEvent() {
    for (uint8_t i = 0; i < RESPONSE_SIZE; i++) {
        Wire.write(response[i]);
    }
}
```

---

## Issue 3: 0xFF Fill Bytes

### Symptoms

The master receives responses filled with `0xFF` bytes:

```diff
- 0x01 0xff 0xff 0xff 0xff 0xff
- 0xa5 0x00 0x99 0x11 0x01 0xff
```

### Root Cause

The TX FIFO is empty or misaligned. The I2C hardware returns `0xFF` when there is no data to send. This happens when:

1. The master reads before the slave has prepared a response
2. The FIFO was not loaded with fresh data
3. `Wire.slaveWrite()` was used but the FIFO was in an inconsistent state

### Solution

Using `Wire.write()` in `requestEvent()` ensures the `txBuffer` is always populated before the hardware sends. The Wire library handles FIFO loading correctly.

Also ensure the master always sends a write command before attempting a read:

```bash
# CORRECT - write then read
sudo i2ctransfer -y 6 w2@0x08 0x01 0x99
sleep 0.50
sudo i2ctransfer -y 6 r6@0x08

# WRONG - read without write
sudo i2ctransfer -y 6 r6@0x08
```

---

## Issue 4: No Such Device or Address

### Symptoms

```diff
- Error: Sending messages failed: No such device or address
```

### Root Cause

The ESP32-C3 slave did not ACK the address byte (`0x08`) during the I2C address phase. Possible causes:

1. ESP32-C3 is not powered or not running firmware
2. SDA or SCL wiring is loose or incorrect
3. I2C bus frequency is too high for the slave
4. ESP32-C3 is busy processing a previous transaction
5. Missing or incorrect pull-up resistors
6. Common ground is not connected between master and slave

### Diagnostic Steps

1. **Check power:** Verify ESP32-C3 is powered (LED on, serial output visible)
2. **Check wiring:** Verify SDA to GPIO4, SCL to GPIO5, GND to GND
3. **Scan the bus:**
   ```bash
   sudo i2cdetect -y 6
   ```
   If `08` does not appear, the slave is not responding to its address.
4. **Check pull-ups:** Verify 2.2k ohm resistors on SDA and SCL to 3.3V
5. **Lower frequency:** Try 10 kHz in the ESP32 firmware:
   ```cpp
   Wire.begin(0x08, 4, 5, 10000);
   ```
6. **Add delay:** Increase the delay between transactions:
   ```bash
   sudo i2ctransfer -y 6 w2@0x08 0x01 0x99
   sleep 1.00
   sudo i2ctransfer -y 6 r6@0x08
   ```

### Solution

- Use 50 kHz bus frequency
- Use 2.2k ohm pull-up resistors
- Ensure common ground
- Allow at least 500ms between write and read
- Avoid rapid repeated transactions

---

## Issue 5: GPIO8 LED Conflict

### Symptoms

- Status LED on GPIO8 does not toggle
- ESP32-C3 fails to boot or boots into wrong mode
- I2C communication is unstable when LED code is active

### Root Cause

GPIO8 is a strapping pin on the ESP32-C3. It must be held HIGH during reset to enter normal boot mode. Using GPIO8 for an LED or other output can:

1. Pull the pin LOW during boot, causing boot failure
2. Interfere with the I2C peripheral (GPIO8 is the default I2C SDA on some boards)
3. Cause unpredictable behavior during reset

### Solution

Use GPIO2 or another non-strapping pin for the status LED:

```cpp
// WRONG
static constexpr int LED_PIN = 8;

// CORRECT
static constexpr int LED_PIN = 2;
```

### ESP32-C3 Strapping Pins

| Pin | Boot Function | Safe for I/O |
|-----|---------------|-------------|
| GPIO2 | Must be LOW at boot | Yes (with caution) |
| GPIO8 | Must be HIGH at boot | No |
| GPIO9 | Must be HIGH at boot (download mode) | No |

---

## Issue 6: Native ESP-IDF Driver Issues

### Symptoms

When using the native ESP-IDF `i2c_slave.h` driver (Version 1 API):

- Same stale/shifted data as `Wire.slaveWrite()`
- Compilation errors with `i2c_slave_config_t` fields
- Callback signature mismatches
- `eventData->length` does not exist

### Root Cause

The native ESP-IDF driver's `i2c_slave_transmit()` function also writes to the hardware FIFO/ringbuffer without clearing it first. The installed API (Arduino-ESP32 3.3.8) differs from online examples.

### API Differences in Arduino-ESP32 3.3.8

| Feature | Online Examples | Installed API (3.3.8) |
|---------|----------------|----------------------|
| Config field | `slave_addr_7bit` | `slave_addr` |
| Config field | `receive_buf_depth` | Not available |
| Config field | `enable_internal_pullup` | Not available |
| Callback | `on_request` | Not available (V1) |
| Callback | `on_receive` | `on_recv_done` (V1) |
| Callback return | `void` | `bool` |
| Event field | `eventData->length` | Not available |
| Event field | `eventData->buffer` | Available |
| Transmit function | `i2c_slave_write()` | `i2c_slave_transmit()` |

### Solution

For reliability, use the Arduino `Wire` library with `Wire.write()` instead of the native ESP-IDF driver. The Wire library provides better buffer management for the ESP32-C3 slave use case.

If the native driver is required, carefully match the API to the installed header at:

```
packages/esp32/tools/esp32c3-libs/3.3.8/include/esp_driver_i2c/include/driver/i2c_slave.h
```

---

## Issue 7: Intermittent Bus Detection

### Symptoms

`i2cdetect` sometimes shows `0x08` and sometimes does not:

```bash
sudo i2cdetect -y 6
# First scan: 08 appears
sudo i2cdetect -y 6
# Second scan: 08 does not appear
sudo i2cdetect -y 6
# Third scan: 08 appears again
```

### Root Cause

The ESP32-C3 slave is not maintaining a stable ACK response. This can be caused by:

1. Bus frequency too high
2. ESP32-C3 busy in a callback when the scan arrives
3. Pull-up resistors too weak
4. Signal integrity problems (long wires, noise)
5. ESP32-C3 interrupt latency

### Solution

1. Reduce bus frequency to 50 kHz (use device tree overlay on Radxa)
2. Use 2.2k ohm pull-up resistors (not weaker)
3. Keep wire lengths under 30 cm
4. Avoid running `i2cdetect` repeatedly in rapid succession
5. Ensure the ESP32-C3 firmware is running and callbacks are fast

---

## Issue 8: Incorrect Checksum

### Symptoms

The response frame has a valid marker (`0xA5`) but the checksum byte does not match:

```diff
- 0xa5 0x00 0x99 0x11 0x01 0xff
```

Expected:

```diff
+ 0xa5 0x00 0x99 0x11 0x01 0x2c
```

### Root Cause

1. Checksum calculation error in the firmware
2. Data corruption on the I2C bus
3. Sequence counter not incrementing correctly
4. Response buffer overwritten during transmission

### Verification

Calculate the expected checksum manually:

```bash
python3 -c "print(hex(0xA5 ^ 0x00 ^ 0x99 ^ 0x11 ^ 0x01))"
# Should output: 0x2c
```

### Solution

Verify the checksum function in firmware:

```cpp
uint8_t makeChecksum(
  uint8_t marker,
  uint8_t status,
  uint8_t command,
  uint8_t value,
  uint8_t sequence
) {
  return marker ^ status ^ command ^ value ^ sequence;
}
```

Ensure `response[4]` (sequence) is incremented before calculating the checksum, not after.

---

## Issue 9: Wrong I2C Frequency

### Symptoms

Communication is unreliable, and the actual SCL frequency does not match the requested value.

### Root Cause

The device tree `clock-frequency` property is a requested value. The actual SCL frequency depends on:

1. Clock divider rounding in the RK3588S I2C controller
2. Bus capacitance (wire length, number of devices)
3. Pull-up resistor values
4. Clock stretching (not supported by ESP32-C3 slave)

### Verification

Software check:

```bash
NODE=$(readlink -f /sys/class/i2c-adapter/i2c-6/device/of_node)
od -An -tu4 "$NODE/clock-frequency"
```

Hardware check (requires logic analyzer or oscilloscope):

- Measure SCL period
- At 50 kHz: period should be approximately 20 microseconds

### Solution

If the actual frequency is too high, use a lower requested value or stronger pull-up resistors. If the requested value is not reflected, verify the device tree overlay is loaded correctly.

---

## Issue 10: Device Tree Overlay Not Applied

### Symptoms

After installing the overlay and rebooting, the frequency still shows the default value:

```bash
od -An -tu4 "$NODE/clock-frequency"
# Outputs: 100000 (default) instead of 50000
```

### Root Cause

1. The overlay was not compiled correctly
2. The overlay file is in the wrong directory
3. The boot configuration does not reference the overlay
4. The bootloader does not support overlays
5. The overlay target label (`i2c6`) does not exist in the base device tree

### Diagnostic Steps

1. Verify the overlay compiled:
   ```bash
   ls -lh i2c6-50k-overlay.dtbo
   file i2c6-50k-overlay.dtbo
   ```

2. Verify the overlay is in the correct directory:
   ```bash
   find /boot -name '*.dtbo' -print
   ```

3. Verify the boot configuration references it:
   ```bash
   grep -RniE 'i2c6|50k|overlay' /boot/armbianEnv.txt /boot/extlinux/extlinux.conf 2>/dev/null
   ```

4. Check kernel messages for overlay loading:
   ```bash
   sudo dmesg | grep -i overlay
   ```

### Solution

- Ensure the overlay name in `armbianEnv.txt` matches the filename (without `.dtbo`)
- Try the fragment-style overlay if the label `i2c6` is not available
- Check that the bootloader supports device tree overlays
- See `radxa/PROCEDURE.md` for the complete procedure

---

## Issue 11: No Serial Output

### Symptoms

After uploading firmware to the ESP32-C3FH4, the Arduino Serial Monitor shows nothing.

### Root Cause

1. USB CDC On Boot is not enabled
2. Wrong board selected in Arduino IDE
3. USB cable is power-only (no data lines)
4. Wrong baud rate in Serial Monitor
5. ESP32-C3 did not boot after programming

### Solution

1. Verify board selection: ESP32C3_DEV
2. Enable USB CDC On Boot in Arduino IDE settings
3. Use a data-capable USB cable (not a charging-only cable)
4. Set Serial Monitor baud rate to 115200
5. Press the reset button on the ESP32-C3 after upload
6. Verify the USB connection appears as a COM port (Windows) or /dev/ttyACM* (Linux)

---

## Issue 12: Compilation Errors

### Symptoms

### Error: slave_addr_7bit not found

```text
error: 'i2c_slave_config_t' has no non-static data member named 'slave_addr_7bit'
```

**Cause:** The installed API uses `slave_addr`, not `slave_addr_7bit`.

**Solution:**

```cpp
// WRONG
.slave_addr_7bit = 0x08,

// CORRECT
.slave_addr = 0x08,
```

### Error: designator order does not match

```text
error: designator order for field 'i2c_slave_config_t::i2c_port' does not match
```

**Cause:** C++ designated initializers must follow the struct declaration order.

**Solution:** Reorder fields to match the struct definition in `i2c_slave.h`:

```cpp
i2c_slave_config_t config = {
    .i2c_port = I2C_NUM_0,
    .sda_io_num = (gpio_num_t)4,
    .scl_io_num = (gpio_num_t)5,
    .clk_source = I2C_CLK_SRC_DEFAULT,
    .send_buf_depth = 32,
    .slave_addr = 0x08,
    .addr_bit_len = I2C_ADDR_BIT_LEN_7,
    .intr_priority = 0,
    .flags = { .allow_pd = 0 },
};
```

### Error: callback return type mismatch

```text
error: invalid conversion from 'void (*)(...)' to 'bool (*)(...)'
```

**Cause:** The callback must return `bool`, not `void`.

**Solution:**

```cpp
// WRONG
void on_recv_done(...) { ... }

// CORRECT
bool on_recv_done(...) {
    ...
    return false;  // No high-priority task woken
}
```

### Error: eventData->length not found

```text
error: 'const struct i2c_slave_rx_done_event_data_t' has no member named 'length'
```

**Cause:** The `length` field only exists when `CONFIG_I2C_ENABLE_SLAVE_DRIVER_VERSION_2` is defined. The installed API (Version 1) does not have it.

**Solution:** Use `eventData->buffer` only. Do not access `eventData->length`.

---

## Debugging Tools and Techniques

### 1. Serial Monitor

Open the Arduino IDE Serial Monitor at 115200 baud. The diagnostic examples print:

- Every received command (RX lines)
- Every decoded command (CMD lines)
- Every prepared response (RESP lines)
- Every transmitted response (TX lines)
- Periodic statistics (every 2 seconds)

### 2. i2cdetect

Scan the bus to verify the slave is present:

```bash
sudo i2cdetect -y 6
```

### 3. i2ctransfer

Perform manual transactions:

```bash
sudo i2ctransfer -y 6 w2@0x08 0x01 0x99
sudo i2ctransfer -y 6 r6@0x08
```

### 4. Logic Analyzer

Connect to SDA, SCL, and GND. Verify:

- SCL frequency matches the device tree setting
- SDA and SCL idle high
- Address byte is correct (0x08 with R/W bit)
- ACK after address byte
- ACK after each data byte
- STOP condition between write and read
- Six bytes returned during read

### 5. Oscilloscope

Measure SCL period to verify actual frequency:

- At 50 kHz: period = 20 microseconds
- At 100 kHz: period = 10 microseconds

### 6. dmesg

Check kernel messages for I2C errors:

```bash
sudo dmesg | grep -i i2c
```

### 7. Test Script

Run the automated test script:

```bash
cd radxa
chmod +x test_i2c.sh
./test_i2c.sh cycle
```

---

## Decision Flowchart

```
Problem detected
    |
    v
Is the response shifted (first byte wrong)?
    |-- YES --> Check requestEvent(): using Wire.write()?
    |              |-- NO  --> Change to Wire.write()
    |              |-- YES --> Check receiveEvent(): calling slaveWrite()?
    |                          |-- YES --> Remove slaveWrite() from receiveEvent()
    |                          |-- NO  --> Continue debugging
    |
    |-- NO
        |
        v
Is "No such device or address" error?
    |-- YES --> Check wiring (SDA, SCL, GND)
    |           Check pull-up resistors (2.2k ohm)
    |           Check ESP32-C3 is powered and running
    |           Scan bus: sudo i2cdetect -y 6
    |           Lower frequency to 50 kHz or 10 kHz
    |
    |-- NO
        |
        v
Is response filled with 0xFF?
    |-- YES --> Master is reading without writing first
    |           Ensure one write before each read
    |           Check that receiveEvent() prepares response
    |
        |-- NO
            |
            v
Is checksum wrong?
    |-- YES --> Verify checksum calculation
    |           Verify sequence increments before checksum
    |           Check for bus noise (shorter wires, pull-ups)
    |
    |-- NO
        |
        v
Is LED not blinking?
    |-- YES --> Check if using GPIO8 (strapping pin)
    |           Change to GPIO2
    |
    |-- NO
        |
        v
Is serial output empty?
    |-- YES --> Enable USB CDC On Boot
    |           Check USB cable (data capable)
    |           Verify baud rate is 115200
    |           Press reset after upload
    |
    |-- NO
        |
        v
Compilation error?
    |-- YES --> Check API fields match installed header
    |           Check struct field order
    |           Check callback returns bool
    |           See Issue 12 for specific errors
    |
    |-- NO
        |
        v
Use logic analyzer to capture SDA/SCL
and verify bus timing and signal integrity.
```
