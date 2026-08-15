# Example 01: Basic I2C Slave

Minimal implementation demonstrating the `Wire.write()` fix for stale data on ESP32-C3 I2C slave.

---

## Table of Contents

- [Overview](#overview)
- [Hardware Requirements](#hardware-requirements)
- [Wiring Diagram](#wiring-diagram)
- [Pull-up Resistors](#pull-up-resistors)
- [Arduino IDE Configuration](#arduino-ide-configuration)
- [Protocol](#protocol)
- [Expected Output](#expected-output)
- [Testing from Radxa](#testing-from-radxa)
- [What This Example Does NOT Include](#what-this-example-does-not-include)
- [How It Works](#how-it-works)
- [Common Mistakes](#common-mistakes)

---

## Overview

This is the simplest working example of an ESP32-C3 I2C slave. It receives a 2-byte command from the master and responds with a 6-byte frame. There is no serial output, no statistics, no LED, and no application logic.

Use this example to validate that the I2C link works correctly before adding complexity.

---

## Hardware Requirements

| Component | Specification |
|-----------|---------------|
| Microcontroller | ESP32-C3FH4 (4 MB internal flash) |
| Master | Radxa ROCK 5C or any Linux I2C master |
| I2C Address | 0x08 (7-bit) |
| SDA Pin | GPIO4 |
| SCL Pin | GPIO5 |
| Bus Frequency | 50 kHz |

---

## Wiring Diagram

```
Radxa ROCK 5C              ESP32-C3FH4
---------------             ------------
I2C6 SDA pin  -------->  GPIO4 (SDA)
I2C6 SCL pin  -------->  GPIO5 (SCL)
GND           -------->  GND
3.3V (optional) ------->  3V3
```

Common ground between the Radxa and ESP32-C3 is required.

---

## Pull-up Resistors

The I2C bus requires pull-up resistors on both SDA and SCL lines. The ESP32-C3 has internal pull-ups, but they are weak (approximately 45k ohm) and insufficient for reliable I2C communication.

### Recommended Values

| Resistor Value | Suitability | Notes |
|----------------|-------------|-------|
| 1.5k ohm | Good for long bus / high capacitance | Stronger pull-up, more current |
| 2.2k ohm | Recommended (used in this project) | Validated working at 50 kHz |
| 4.7k ohm | Acceptable for short bus / low capacitance | Standard value, lower current |

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

The 2.2k ohm value was validated during testing and produced reliable results at 50 kHz with short wire lengths (under 30 cm). If your bus has longer wires or more devices, consider 1.5k ohm. If power consumption is critical and the bus is short, 4.7k ohm is acceptable.

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

| Command | Write Bytes | Expected Response |
|---------|------------|-------------------|
| Initialize | `01 99` | `A5 00 99 11 <seq> <checksum>` |
| Status | `01 2A` | `A5 02 2A 2B <seq> <checksum>` |

---

## Expected Output

### Command 0x99 (Initialize)

After sending:
```bash
sudo i2ctransfer -y 6 w2@0x08 0x01 0x99
sleep 0.50
sudo i2ctransfer -y 6 r6@0x08
```

The master should receive:

```diff
+ 0xa5 0x00 0x99 0x11 0x01 0x2c
```

Breakdown:
- `0xa5` = Response marker (correct)
- `0x00` = Status OK (correct)
- `0x99` = Command echo (correct)
- `0x11` = Response value for initialize (correct)
- `0x01` = Sequence counter (first transaction)
- `0x2c` = Checksum: 0xA5 ^ 0x00 ^ 0x99 ^ 0x11 ^ 0x01 = 0x2C (correct)

### Command 0x2A (Status)

After sending:
```bash
sudo i2ctransfer -y 6 w2@0x08 0x01 0x2a
sleep 0.50
sudo i2ctransfer -y 6 r6@0x08
```

The master should receive:

```diff
+ 0xa5 0x02 0x2a 0x2b 0x02 0xa4
```

Breakdown:
- `0xa5` = Response marker (correct)
- `0x02` = Status reply (correct)
- `0x2a` = Command echo (correct)
- `0x2b` = Response value for status (correct)
- `0x02` = Sequence counter (second transaction)
- `0xa4` = Checksum: 0xA5 ^ 0x02 ^ 0x2A ^ 0x2B ^ 0x02 = 0xA4 (correct)

### Incorrect Output (What NOT to Expect)

If you see any of the following, something is wrong:

```diff
- 0x11 0xa5 0x00 0x99 0x11 0x01    (shifted - stale FIFO data)
- 0x01 0xff 0xff 0xff 0xff 0xff    (empty FIFO, uninitialized)
- Error: Sending messages failed: No such device or address    (slave not responding)
- 0xa5 0xee 0x00 0xe1 0x01 0xab    (invalid frame response)
```

---

## Testing from Radxa

### Step 1: Verify the slave is on the bus

```bash
sudo i2cdetect -y 6
```

You should see `08` in the output grid:

```
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
00:          -- -- -- -- -- 08 -- -- -- -- -- -- --
```

### Step 2: Send command 0x99

```bash
sudo i2ctransfer -y 6 w2@0x08 0x01 0x99
sleep 0.50
sudo i2ctransfer -y 6 r6@0x08
```

### Step 3: Send command 0x2A

```bash
sudo i2ctransfer -y 6 w2@0x08 0x01 0x2a
sleep 0.50
sudo i2ctransfer -y 6 r6@0x08
```

### Step 4: Verify sequence incrementing

Send the same command twice and check that byte 4 (sequence) increments:

```bash
sudo i2ctransfer -y 6 w2@0x08 0x01 0x99
sleep 0.30
sudo i2ctransfer -y 6 r6@0x08
# Sequence should be 0x01

sudo i2ctransfer -y 6 w2@0x08 0x01 0x99
sleep 0.30
sudo i2ctransfer -y 6 r6@0x08
# Sequence should be 0x02
```

---

## What This Example Does NOT Include

This basic example intentionally omits:

- Serial debug output (no `Serial.print()` calls)
- Statistics counters (no rx/tx/invalid tracking)
- Status LED (no GPIO toggling)
- Error recovery logic
- Timeout handling
- Fan control logic
- Temperature sensing
- Application-specific commands

For these features, see:
- `examples/02_Diagnostic_Slave/` - Adds serial output and statistics
- `examples/03_Fan_Controller/` - Adds fan control and temperature sensing

---

## How It Works

### receiveEvent() Callback

This function is called automatically by the Wire library when the master writes data to the slave.

1. Read all available bytes from the Wire buffer
2. Validate that exactly 2 bytes were received and byte 0 is 0x01
3. Look up the command and determine the status and value
4. Build the 6-byte response in the `response[]` array
5. Calculate the XOR checksum and store it in byte 5

This function does NOT send any data. It only prepares the response.

### requestEvent() Callback

This function is called automatically by the Wire library when the master reads data from the slave.

1. Call `Wire.write()` for each byte of the response
2. The Wire library collects these into its internal `txBuffer`
3. After this function returns, the Wire library sends `txBuffer` to the I2C hardware

The key point is that the Wire library clears `txBuffer` before calling `requestEvent()`. This ensures that only fresh data is sent, with no stale bytes from previous transactions.

### Transaction Flow

```
Master                          Slave
------                          -----
1. Write 01 99            -->   receiveEvent() called
                                response[] prepared

2. Wait 500ms                  (slave processes)

3. Read 6 bytes           <--  requestEvent() called
                                Wire.write() fills txBuffer
                                Wire library sends txBuffer
```

---

## Common Mistakes

### Mistake 1: Using Wire.slaveWrite() in requestEvent()

```cpp
// WRONG - causes stale/shifted data
void requestEvent() {
    Wire.slaveWrite(response, sizeof(response));
}
```

This bypasses the txBuffer and writes directly to the hardware FIFO. Old data remains and produces shifted frames.

### Mistake 2: Calling Wire.slaveWrite() in receiveEvent()

```cpp
// WRONG - double-queues data
void receiveEvent(int count) {
    // ... process command ...
    Wire.slaveWrite(response, sizeof(response));  // queues too early
}
```

This queues data before the master has requested a read. Combined with `requestEvent()` also writing data, this produces stale FIFO output.

### Mistake 3: Using GPIO8 for LED

```cpp
// WRONG - GPIO8 is a strapping pin on ESP32-C3
static constexpr int LED_PIN = 8;
```

GPIO8 must be HIGH during boot. Using it for an LED can prevent the ESP32-C3 from booting correctly or interfere with the I2C peripheral.

### Mistake 4: Repeated Reads Without Write

```bash
# WRONG - reads stale data from FIFO
sudo i2ctransfer -y 6 r6@0x08
sudo i2ctransfer -y 6 r6@0x08
sudo i2ctransfer -y 6 r6@0x08
```

Each read without a preceding write consumes the same queued frame or returns stale/empty FIFO data. Always send a write command before each read.

### Mistake 5: No Pull-up Resistors

Relying on the ESP32-C3 internal pull-ups is insufficient. The internal pull-ups are approximately 45k ohm, which is too weak for reliable I2C. External pull-up resistors of 1.5k to 4.7k ohm are required on both SDA and SCL lines.
