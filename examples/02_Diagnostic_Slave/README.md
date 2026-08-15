# Example 02: Diagnostic I2C Slave

Full-featured I2C slave implementation with serial debug output, statistics counters, frame validation, and status LED. Use this example for development, debugging, and as a reference for production code.

---

## Table of Contents

- [Overview](#overview)
- [Hardware Requirements](#hardware-requirements)
- [Wiring Diagram](#wiring-diagram)
- [Pull-up Resistors](#pull-up-resistors)
- [Arduino IDE Configuration](#arduino-ide-configuration)
- [Protocol](#protocol)
- [Features](#features)
- [Serial Output](#serial-output)
- [Expected Output](#expected-output)
- [Statistics Counters](#statistics-counters)
- [How It Works](#how-it-works)
- [The Wire.write() Fix Explained](#the-wirewrite-fix-explained)
- [Issues Encountered During Development](#issues-encountered-during-development)
- [Testing from Radxa](#testing-from-radxa)
- [Difference from Example 01](#difference-from-example-01)
- [Troubleshooting](#troubleshooting)

---

## Overview

This example extends the basic slave (Example 01) with full diagnostic capabilities. It prints every received command, every transmitted response, and periodic statistics to the serial monitor. It also validates incoming frames and tracks invalid transactions separately.

This is the recommended starting point for any real application. The serial output allows you to verify exactly what the ESP32-C3 receives and sends, making it easy to diagnose bus problems, timing issues, and protocol errors.

---

## Hardware Requirements

| Component | Specification |
|-----------|---------------|
| Microcontroller | ESP32-C3FH4 (4 MB internal flash) |
| Master | Radxa ROCK 5C or any Linux I2C master |
| I2C Address | 0x08 (7-bit) |
| SDA Pin | GPIO4 |
| SCL Pin | GPIO5 |
| Status LED | GPIO2 (do NOT use GPIO8) |
| Bus Frequency | 50 kHz |
| USB Connection | For serial monitor debug output |

---

## Wiring Diagram

```
Radxa ROCK 5C              ESP32-C3FH4
---------------             ------------
I2C6 SDA pin  -------->  GPIO4 (SDA)
I2C6 SCL pin  -------->  GPIO5 (SCL)
GND           -------->  GND
                             |
                           GPIO2
                             |
                           LED (330 ohm)
                             |
                            GND

USB Cable ---------> PC (for Serial Monitor)
```

Common ground between the Radxa and ESP32-C3 is required.

---

## Pull-up Resistors

The I2C bus requires external pull-up resistors on both SDA and SCL lines. The ESP32-C3 internal pull-ups are too weak for reliable communication.

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

The 2.2k ohm value was validated during testing and produced reliable results at 50 kHz with short wire lengths (under 30 cm).

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

**USB CDC On Boot** must be enabled to see serial output over USB when using the ESP32-C3FH4.

---

## Protocol

### Master Write (2 bytes)

```
Byte 0: 0x01              (request marker)
Byte 1: <command>         (0x99 or 0x2A)
```

### Slave Response (6 bytes)

```
Byte 0: 0xA5              (response marker)
Byte 1: <status>          (status code)
Byte 2: <command echo>    (echo of received command)
Byte 3: <value>            (response value)
Byte 4: <sequence>         (increments per transaction)
Byte 5: <checksum>         (XOR of bytes 0-4)
```

### Commands

| Command | Name | Write Bytes | Expected Response |
|---------|------|-------------|-------------------|
| 0x99 | Initialize | `01 99` | `A5 00 99 11 <seq> <checksum>` |
| 0x2A | Status | `01 2A` | `A5 02 2A 2B <seq> <checksum>` |

### Status Codes

| Code | Name | Description |
|------|------|-------------|
| 0x00 | STATUS_OK | Command accepted and processed successfully |
| 0x01 | STATUS_UNKNOWN_COMMAND | Command byte not recognized |
| 0x02 | STATUS_STATUS_REPLY | Status reply for command 0x2A |
| 0xEE | STATUS_INVALID_FRAME | Wrong marker byte or wrong frame length |

---

## Features

### Serial Debug Output

Every I2C transaction is printed to the serial monitor:

- **RX:** lines show received bytes from the master
- **CMD:** lines show the decoded command
- **RESP:** lines show the prepared response before sending
- **TX:** lines show the response actually transmitted
- **INVALID:** lines show validation failures with details

### Statistics Counters

Six counters track I2C activity:

| Counter | Description |
|---------|-------------|
| rx_count | Total receive events (master writes) |
| request_count | Total request events (master reads) |
| tx_count | Total bytes transmitted |
| invalid_count | Total invalid frames received |
| success_99 | Total successful 0x99 commands |
| success_2a | Total successful 0x2A commands |

### Frame Validation

The receive callback validates:

1. Frame length must be exactly 2 bytes
2. Byte 0 must be the request marker 0x01
3. Command must be recognized (0x99 or 0x2A)

Invalid frames are counted and an error response with status 0xEE is prepared.

### Status LED

GPIO2 toggles on every master read request. This provides visual confirmation of I2C activity without needing the serial monitor.

---

## Serial Output

### Startup Message

When the ESP32-C3 boots, it prints:

```
========================================
ESP32-C3 I2C Slave - Diagnostic
========================================
Address: 0x08
SDA: GPIO4 | SCL: GPIO5 | LED: GPIO2
Frequency: 50000 Hz

I2C slave ready.
Rule: one master write, then one master read.
========================================
```

### Transaction Output

When a command is received and a response is sent:

```
RX: 01 99
CMD: 0x99 (Initialize)
RESP: A5 00 99 11 01 2C
TX: A5 00 99 11 01 2C
```

### Statistics Output (every 2 seconds)

```
--- I2C STATUS ---
RX=1 REQ=1 TX=1 INVALID=0
CMD_99=1 CMD_2A=0 LAST_RX_LEN=2 LAST_RX=01 99
NEXT_RESPONSE=A5 00 99 11 01 2C
```

### Invalid Frame Output

When an invalid frame is received:

```
RX: 00 FF
INVALID: len=2, b0=00, b1=FF
```

---

## Expected Output

### Command 0x99 (Initialize)

After sending:
```bash
sudo i2ctransfer -y 6 w2@0x08 0x01 0x99
sleep 0.50
sudo i2ctransfer -y 6 r6@0x08
```

**Radxa terminal:**

```diff
+ 0xa5 0x00 0x99 0x11 0x01 0x2c
```

**ESP32 serial monitor:**

```
RX: 01 99
CMD: 0x99 (Initialize)
RESP: A5 00 99 11 01 2C
TX: A5 00 99 11 01 2C
```

### Command 0x2A (Status)

After sending:
```bash
sudo i2ctransfer -y 6 w2@0x08 0x01 0x2a
sleep 0.50
sudo i2ctransfer -y 6 r6@0x08
```

**Radxa terminal:**

```diff
+ 0xa5 0x02 0x2a 0x2b 0x02 0xa4
```

**ESP32 serial monitor:**

```
RX: 01 2A
CMD: 0x2A (Status)
RESP: A5 02 2A 2B 02 A4
TX: A5 02 2A 2B 02 A4
```

### Incorrect Output (Problem Indicators)

If you see any of the following on the Radxa terminal, something is wrong:

```diff
- 0x11 0xa5 0x00 0x99 0x11 0x01    (shifted - stale FIFO data)
- 0x01 0xff 0xff 0xff 0xff 0xff    (empty FIFO, uninitialized)
- Error: Sending messages failed: No such device or address    (slave not responding)
- 0xa5 0xee 0x00 0xe1 0x01 0xab    (invalid frame response)
```

On the ESP32 serial monitor, an invalid frame would show:

```
RX: 00 FF
INVALID: len=2, b0=00, b1=FF
RESP: A5 EE FF E1 01 4B
TX: A5 EE FF E1 01 4B
```

---

## Statistics Counters

The statistics are printed every 2 seconds and provide a quick health check:

| Counter | Healthy Value | Problem Indicator |
|---------|---------------|-------------------|
| RX | Equals number of writes sent | 0 means no writes received |
| REQ | Equals number of reads sent | 0 means master not reading |
| TX | Equals REQ | Less than REQ means send failures |
| INVALID | 0 or very low | High means bus noise or wrong protocol |
| CMD_99 | Increments per 0x99 sent | 0 means 0x99 never received |
| CMD_2A | Increments per 0x2A sent | 0 means 0x2A never received |
| LAST_RX_LEN | 2 | Not 2 means wrong frame size |
| LAST_RX | 01 99 or 01 2A | Other values mean corruption |

---

## How It Works

### Transaction Flow

```
Master (Radxa)                         Slave (ESP32-C3)
---------------                        ----------------
1. START + Address 0x08 (write)  -->   I2C hardware ACKs address
2. Write byte 0x01               -->   I2C hardware stores in RX FIFO
3. Write byte 0x99               -->   I2C hardware stores in RX FIFO
4. STOP                                receiveEvent() callback fires
                                       - Reads 2 bytes from Wire buffer
                                       - Validates marker and length
                                       - Looks up command
                                       - Prepares response[] array
                                       - Prints RX and RESP to serial

5. START + Address 0x08 (read)   -->   I2C hardware ACKs address
6. Read 6 bytes                  <--   requestEvent() callback fires
                                         - Calls Wire.write() 6 times
                                         - Wire library collects into txBuffer
                                         - After callback, Wire sends txBuffer
                                         - Prints TX to serial
                                         - Toggles LED
7. STOP
```

### Callback Architecture

The ESP32 Arduino Wire library uses two callbacks:

**receiveEvent():** Called when the master writes to the slave. This is where you read incoming data and prepare your response. This function should be fast and should NOT call `Wire.slaveWrite()`.

**requestEvent():** Called when the master reads from the slave. This is where you send your response using `Wire.write()`. The Wire library has already cleared its internal `txBuffer` before calling this function, so only fresh data is sent.

### Volatile Variables and Interrupt Safety

The statistics counters are declared `volatile` because they are modified inside interrupt callbacks and read from the main loop. The main loop uses a `noInterrupts()` / `interrupts()` pair around the reads to ensure that 32-bit values are not read while being updated.

---

## The Wire.write() Fix Explained

### The Problem

The ESP32 I2C hardware has a TX FIFO that is not automatically cleared between transactions. When data is written to the FIFO, it remains there until the master reads it. If new data is written before the old data is fully consumed, the new data is appended, and the master reads a mixture of old and new bytes.

### Why Wire.slaveWrite() Causes Stale Data

`Wire.slaveWrite()` calls the ESP32 HAL function `i2cSlaveWrite()` directly. This function writes to the hardware TX FIFO and the internal TX queue. It does not clear the FIFO first. If old data remains, new data is appended.

The Arduino Wire library's `onRequestService()` function works like this:

```
1. txLength = 0                    (clear software buffer length)
2. user_onRequest()                (call your callback)
3. if (txLength > 0)               (check if you wrote data)
4.     slaveWrite(txBuffer, txLength)  (send to hardware)
```

When you call `Wire.slaveWrite()` inside your callback (step 2), it writes directly to the hardware FIFO, bypassing the `txBuffer` clearing in step 1. Old data remains, and your new data is appended.

### Why Wire.write() Works Correctly

When you call `Wire.write()` inside your callback, it fills the software `txBuffer`. Because `txLength` was set to 0 in step 1, only your new data is in the buffer. In step 4, the Wire library calls `slaveWrite()` with the `txBuffer`, which contains only fresh data.

The hardware FIFO still has old data, but the Wire library's `slaveWrite()` implementation resets the TX queue before writing, ensuring the old FIFO data is flushed.

### Summary

| Function | Path | FIFO Cleared | Result |
|----------|------|-------------|--------|
| `Wire.write()` | txBuffer (software) | Yes (by Wire library) | Fresh data only |
| `Wire.slaveWrite()` | Hardware FIFO (direct) | No | Stale data mixed with new |

---

## Issues Encountered During Development

### Issue 1: Shifted Response Bytes

**Symptom:** The master received `11 A5 00 99 11 01` instead of `A5 00 99 11 01 2C`.

**Cause:** `Wire.slaveWrite()` was used in `requestEvent()`, bypassing the `txBuffer` management. Old response bytes remained in the hardware FIFO and were prepended to the new response.

**Fix:** Replace `Wire.slaveWrite()` with `Wire.write()` in `requestEvent()`.

### Issue 2: Double-Queued Data

**Symptom:** The master received 12 bytes of mixed data instead of 6 clean bytes.

**Cause:** `Wire.slaveWrite()` was called in both `receiveEvent()` and `requestEvent()`, queuing the response twice.

**Fix:** Only prepare the `response[]` array in `receiveEvent()`. Only call `Wire.write()` in `requestEvent()`.

### Issue 3: 0xFF Fill Bytes

**Symptom:** The master received `01 FF FF FF FF FF` or `A5 00 99 11 01 FF`.

**Cause:** The TX FIFO was empty or misaligned, and the hardware returned `0xFF` for unread positions.

**Fix:** Using `Wire.write()` ensures the `txBuffer` is always populated before the hardware sends, preventing empty FIFO reads.

### Issue 4: No Such Device or Address

**Symptom:** `Error: Sending messages failed: No such device or address`

**Cause:** The ESP32-C3 slave failed to ACK the address byte. This can happen when the slave is busy processing a previous transaction, when the I2C frequency is too high, or when there are signal integrity problems.

**Fix:** Use 50 kHz bus frequency, ensure proper pull-up resistors (2.2k ohm), and avoid rapid repeated transactions. Allow at least 500ms between write and read.

### Issue 5: GPIO8 LED Conflict

**Symptom:** Status LED on GPIO8 does not blink or the ESP32-C3 fails to boot.

**Cause:** GPIO8 is a strapping pin on the ESP32-C3. It must be HIGH during boot. Using it for an LED can interfere with the boot process.

**Fix:** Use GPIO2 for the status LED instead.

### Issue 6: Native ESP-IDF Driver Issues

**Symptom:** The native `i2c_slave.h` driver (Version 1 API) produced the same stale data issues as `Wire.slaveWrite()`.

**Cause:** The native driver's `i2c_slave_transmit()` function also writes to the hardware FIFO/ringbuffer without clearing it first. The `i2c_slave_rx_done_event_data_t` structure does not expose a `length` field in the installed API version.

**Fix:** The Arduino Wire library with `Wire.write()` remains the most reliable approach for ESP32-C3 I2C slave. The native ESP-IDF driver requires more careful FIFO management.

---

## Testing from Radxa

### Step 1: Verify the slave is on the bus

```bash
sudo i2cdetect -y 6
```

You should see `08` in the output grid.

### Step 2: Send command 0x99 and read response

```bash
sudo i2ctransfer -y 6 w2@0x08 0x01 0x99
sleep 0.50
sudo i2ctransfer -y 6 r6@0x08
```

### Step 3: Send command 0x2A and read response

```bash
sudo i2ctransfer -y 6 w2@0x08 0x01 0x2a
sleep 0.50
sudo i2ctransfer -y 6 r6@0x08
```

### Step 4: Run 10-cycle test

```bash
for i in {1..10}; do
  echo "=== Cycle $i ==="
  sudo i2ctransfer -y 6 w2@0x08 0x01 0x99
  sleep 0.30
  sudo i2ctransfer -y 6 r6@0x08
  sudo i2ctransfer -y 6 w2@0x08 0x01 0x2a
  sleep 0.30
  sudo i2ctransfer -y 6 r6@0x08
  sleep 0.50
done
```

### Step 5: Check serial monitor

Open the Arduino IDE Serial Monitor at 115200 baud. You should see RX, CMD, RESP, and TX lines for each transaction, plus status reports every 2 seconds.

---

## Difference from Example 01

| Feature | Example 01 (Basic) | Example 02 (Diagnostic) |
|---------|---------------------|-------------------------|
| Serial output | No | Yes (RX, CMD, RESP, TX) |
| Statistics counters | No | Yes (6 counters) |
| Frame validation | Basic | Full with error messages |
| Status LED | No | Yes (GPIO2) |
| Invalid frame handling | Sets error response | Sets error response and prints details |
| Periodic status report | No | Yes (every 2 seconds) |
| Volatile ISR safety | Not needed | Yes (noInterrupts/interrupts) |
| Code size | Small | Larger |
| Use case | Quick validation | Development and debugging |

---

## Troubleshooting

### No Serial Output

If the serial monitor shows nothing after upload:

1. Verify USB CDC On Boot is enabled in Arduino IDE settings
2. Verify the correct board (ESP32C3_DEV) is selected
3. Try pressing the reset button after upload
4. Check the USB cable (some cables are power-only)
5. Verify the baud rate is 115200

### LED Not Blinking

If the GPIO2 LED does not toggle:

1. Verify the LED polarity (anode to GPIO2, cathode through 330 ohm to GND)
2. Verify GPIO2 is not used by another peripheral
3. Check that `pinMode(LED_PIN, OUTPUT)` is in setup()

### Statistics Show INVALID > 0

If the invalid counter is incrementing:

1. Check that the master is sending exactly 2 bytes
2. Check that byte 0 is 0x01 (request marker)
3. Look at the serial output for the INVALID line to see what was received
4. Check for bus noise (use shorter wires, add pull-ups)

### Statistics Show RX = 0

If no receive events are counted:

1. Run `sudo i2cdetect -y 6` to verify the slave is detected
2. Check SDA and SCL wiring
3. Verify common ground between master and slave
4. Check pull-up resistors are installed
5. Verify the ESP32-C3 firmware is running (check serial monitor for startup message)

### Statistics Show REQ = 0 but RX > 0

If writes are received but reads are not requested:

1. The master is writing but not reading. Ensure your test sequence includes a read after each write.
2. Check that the master is using the correct read command (`r6@0x08`)

### Sequence Counter Not Incrementing

If byte 4 of the response does not change:

1. The slave is not processing new commands. Check if RX counter is incrementing.
2. The master may be reading stale FIFO data. Verify you are using `Wire.write()` not `Wire.slaveWrite()`.
3. Ensure each read is preceded by a new write command.
