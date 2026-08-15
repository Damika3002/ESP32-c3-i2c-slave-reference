# Protocol Reference

Detailed specification of the I2C communication protocol used between the Radxa ROCK 5C (master) and the ESP32-C3FH4 (slave).

---

## Table of Contents

- [Overview](#overview)
- [Frame Format](#frame-format)
- [Master Write Frame](#master-write-frame)
- [Slave Response Frame](#slave-response-frame)
- [Checksum Calculation](#checksum-calculation)
- [Sequence Counter](#sequence-counter)
- [Command Reference](#command-reference)
- [Status Codes](#status-codes)
- [Transaction Rules](#transaction-rules)
- [Examples](#examples)
- [Byte-Level Transaction Analysis](#byte-level-transaction-analysis)
- [Error Handling](#error-handling)
- [Protocol Design Rationale](#protocol-design-rationale)

---

## Overview

The protocol uses a simple request-response model. The master writes exactly 2 bytes (a marker and a command). The slave responds with exactly 6 bytes (a structured frame with checksum validation).

This protocol is designed for:

- Minimal bus occupancy (2-byte write, 6-byte read)
- Error detection (XOR checksum, marker validation)
- Sequence tracking (incrementing counter)
- Command echo (verification that the slave processed the correct command)

---

## Frame Format

### Master Write (2 bytes)

```
+--------+--------+
| Marker | Command|
| 0x01   | 0xNN   |
+--------+--------+
```

### Slave Response (6 bytes)

```
+--------+--------+--------+--------+--------+----------+
| Marker | Status | Command| Value  |Sequence |Checksum |
| 0xA5   | 0xNN   | 0xNN   | 0xNN   | 0xNN   | 0xNN     |
+--------+--------+--------+--------+--------+----------+
```

---

## Master Write Frame

### Byte 0: Request Marker

| Value | Description |
|-------|-------------|
| 0x01 | Valid request marker |

The slave validates that byte 0 is `0x01`. Any other value triggers an `INVALID_FRAME` response.

### Byte 1: Command

The command byte tells the slave what action to perform or what data to return.

| Command | Name | Description |
|---------|------|-------------|
| 0x99 | Initialize | Heartbeat or initialization request |
| 0x2A | Status | Request system status bitmask |
| 0x10 | Read Temperature | Request current temperature |
| 0x11 | Read Fan Speed | Request current fan RPM |
| 0x20 | Fan Override ON | Force fan to full speed |
| 0x21 | Fan Override OFF | Restore automatic thermal control |
| 0x30 | Read eFuse Status | Request TPS25940 fault status |
| 0x40 | Read Error Counters | Request accumulated error flags |

---

## Slave Response Frame

### Byte 0: Response Marker

| Value | Description           |
|-------|-----------------------|
| 0xA5  | Valid response marker |

The master should validate that byte 0 is `0xA5`. Any other value indicates stale or shifted data.

### Byte 1: Status

The status byte indicates whether the command was accepted and what type of response is being returned.

### Byte 2: Command Echo

The slave echoes the command byte it received. This allows the master to verify that the slave processed the correct command.

### Byte 3: Value

The value byte contains the response data. The meaning depends on the command.

### Byte 4: Sequence

The sequence counter increments by 1 for each accepted write command. It wraps from `0xFF` to `0x00`. This allows the master to detect missed or duplicate transactions.

### Byte 5: Checksum

The checksum is the XOR of bytes 0 through 4:

```
checksum = marker ^ status ^ command ^ value ^ sequence
```

---

## Checksum Calculation

### Formula

```
checksum = response[0] ^ response[1] ^ response[2] ^ response[3] ^ response[4]
```

### Implementation

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

### Verification (Python)

```bash
python3 -c "print(hex(0xA5 ^ 0x00 ^ 0x99 ^ 0x11 ^ 0x01))"
# Output: 0x2c
```

### Verification (Bash)

```bash
echo $((0xA5 ^ 0x00 ^ 0x99 ^ 0x11 ^ 0x01))
# Output: 44 (decimal) = 0x2C (hex)
```

---

## Sequence Counter

The sequence counter (byte 4) increments by 1 for each accepted write command. It is not incremented for:

- Invalid frames (wrong marker or length)
- Read transactions without a preceding write

The counter wraps from `0xFF` to `0x00`.

### Sequence Validation

The master should check that the sequence counter increments correctly:

```
Transaction 1: seq = 0x01
Transaction 2: seq = 0x02
Transaction 3: seq = 0x03
...
Transaction 255: seq = 0xFF
Transaction 256: seq = 0x00
```

If the master receives a sequence that does not increment, it indicates a missed transaction or stale FIFO data.

---

## Command Reference

### 0x99 - Initialize / Heartbeat

| Field | Value |
|-------|-------|
| Write | `01 99` |
| Response | `A5 00 99 11 <seq> <checksum>` (Example 01/02) |
| Response | `A5 00 99 <fan_duty> <seq> <checksum>` (Example 03) |
| Status byte | 0x00 (STATUS_OK) |
| Value byte | 0x11 (fixed) or current fan PWM duty |
| Use case | Periodic heartbeat to verify slave is alive |

### 0x2A - Status Request

| Field | Value |
|-------|-------|
| Write | `01 2A` |
| Response | `A5 02 2A <status_bitmask> <seq> <checksum>` |
| Status byte | 0x02 (STATUS_STATUS_REPLY) |
| Value byte | System status bitmask |
| Use case | Quick overall system health check |

### 0x10 - Read Temperature

| Field | Value |
|-------|-------|
| Write | `01 10` |
| Response | `A5 00 10 <temp_c> <seq> <checksum>` |
| Status byte | 0x00 (STATUS_OK) |
| Value byte | Temperature in degrees Celsius (integer) |
| Use case | Monitor current temperature |

### 0x11 - Read Fan Speed

| Field | Value |
|-------|-------|
| Write | `01 11` |
| Response | `A5 00 11 <rpm_byte> <seq> <checksum>` |
| Status byte | 0x00 (STATUS_OK) |
| Value byte | Fan RPM divided by 100 |
| Use case | Verify fan is spinning at expected speed |

### 0x20 - Fan Override ON

| Field | Value |
|-------|-------|
| Write | `01 20` |
| Response | `A5 00 20 01 <seq> <checksum>` |
| Status byte | 0x00 (STATUS_OK) |
| Value byte | 0x01 (acknowledgement) |
| Effect | Forces fan to full speed, disables thermal policy |
| Use case | Emergency cooling or manual override |

### 0x21 - Fan Override OFF

| Field | Value |
|-------|-------|
| Write | `01 21` |
| Response | `A5 00 21 01 <seq> <checksum>` |
| Status byte | 0x00 (STATUS_OK) |
| Value byte | 0x01 (acknowledgement) |
| Effect | Clears override, restores automatic thermal policy |
| Use case | Return to normal operation after override |

### 0x30 - Read eFuse Status

| Field | Value |
|-------|-------|
| Write | `01 30` |
| Response | `A5 00 30 <efuse_status> <seq> <checksum>` |
| Status byte | 0x00 (STATUS_OK) |
| Value byte | 0x00 = OK, 0x01 = fault |
| Use case | Check TPS25940 eFuse health |

### 0x40 - Read Error Counters

| Field | Value |
|-------|-------|
| Write | `01 40` |
| Response | `A5 00 40 <error_bitmask> <seq> <checksum>` |
| Status byte | 0x00 (STATUS_OK) |
| Value byte | Error bitmask |
| Use case | Diagnose accumulated errors |

Error bitmask:

| Bit | Mask | Error |
|-----|------|-------|
| 0 | 0x01 | Temperature sensor error |
| 1 | 0x02 | eFuse fault occurred |
| 2 | 0x04 | I2C invalid frame received |
| 3 | 0x08 | Thermal warning reached |

---

## Status Codes

| Code | Name | Description |
|------|------|-------------|
| 0x00 | STATUS_OK | Command accepted and processed successfully |
| 0x01 | STATUS_UNKNOWN_COMMAND | Command byte not recognized |
| 0x02 | STATUS_STATUS_REPLY | Status reply for command 0x2A |
| 0x10 | STATUS_EFUSE_FAULT | eFuse fault is active |
| 0x20 | STATUS_THERMAL_WARNING | Temperature is in warning range |
| 0x40 | STATUS_THERMAL_CRITICAL | Temperature is critical |
| 0xEE | STATUS_INVALID_FRAME | Wrong marker byte or wrong frame length |

---

## Transaction Rules

### Rule 1: One Write Before One Read

Always send a write command before attempting a read. The slave prepares the response during the write callback and sends it during the read callback.

```
WRITE (01 99) --> [slave prepares response] --> READ (6 bytes)
```

Do NOT perform repeated reads:

```
READ (6 bytes)  -->  stale data from FIFO
READ (6 bytes)  -->  empty FIFO, 0xFF bytes
```

### Rule 2: Wait Between Write and Read

Allow at least 500 milliseconds between the write and the read. This gives the slave time to process the command and prepare the response.

```bash
sudo i2ctransfer -y 6 w2@0x08 0x01 0x99
sleep 0.50
sudo i2ctransfer -y 6 r6@0x08
```

### Rule 3: Validate the Response

The master should validate:

1. Byte 0 is `0xA5` (response marker)
2. Byte 2 matches the sent command (command echo)
3. Byte 5 equals the XOR of bytes 0-4 (checksum)
4. Byte 4 increments by 1 from the previous transaction (sequence)

### Rule 4: Handle Errors Gracefully

If the master receives:

- "No such device or address" - retry the transaction
- A frame not starting with `0xA5` - stale data, send a new write
- A checksum mismatch - bus error, retry

---

## Examples

### Example 1: Initialize Command

```
Master writes:  01 99
Slave responds: A5 00 99 11 01 2C
```

Checksum verification: `0xA5 ^ 0x00 ^ 0x99 ^ 0x11 ^ 0x01 = 0x2C`

### Example 2: Status Command

```
Master writes:  01 2A
Slave responds: A5 02 2A 2B 02 A4
```

Checksum verification: `0xA5 ^ 0x02 ^ 0x2A ^ 0x2B ^ 0x02 = 0xA4`

### Example 3: Temperature Read

```
Master writes:  01 10
Slave responds: A5 00 10 2A 03 98
```

Temperature is 42 degrees C (0x2A = 42 decimal).

Checksum verification: `0xA5 ^ 0x00 ^ 0x10 ^ 0x2A ^ 0x03 = 0x98`

### Example 4: Invalid Frame

```
Master writes:  00 FF  (wrong marker)
Slave responds: A5 EE FF E1 01 4B
```

Status is `0xEE` (INVALID_FRAME). The slave echoes the command byte `0xFF` and returns error value `0xE1`.

---

## Byte-Level Transaction Analysis

### Write Transaction (master writes 01 99)

```
SCL: __|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|__

SDA:    S  0  0  0  1  0  0  0  ACK 0  0  0  0  0  0  0  1  ACK 1  0  0  1  1  0  0  1  ACK P
        |  |----- address 0x08 ---|   |----- byte 0 = 0x01 --|   |----- byte 1 = 0x99 --|   |
        START                                              (slave ACKs each byte)        STOP
```

### Read Transaction (master reads 6 bytes)

```
SCL: __|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|_|__

SDA:    S  0  0  0  1  0  0  1  ACK A5 00 99 11 01 2C NACK P
        |  |----- address 0x08 ---|   |--- 6 bytes from slave --|   |
        START  (R/W=1 = read)         (master NACKs last byte)        STOP
```

### Complete Transaction Sequence

```
1. START
2. Address 0x08 (write mode)
3. ACK from slave
4. Data byte 0x01 (marker)
5. ACK from slave
6. Data byte 0x99 (command)
7. ACK from slave
8. STOP

--- slave processes command in receiveEvent() ---

9. START
10. Address 0x08 (read mode)
11. ACK from slave
12. Data byte 0xA5 (response marker) - slave sends
13. ACK from master
14. Data byte 0x00 (status) - slave sends
15. ACK from master
16. Data byte 0x99 (command echo) - slave sends
17. ACK from master
18. Data byte 0x11 (value) - slave sends
19. ACK from master
20. Data byte 0x01 (sequence) - slave sends
21. ACK from master
22. Data byte 0x2C (checksum) - slave sends
23. NACK from master (last byte)
24. STOP
```

---

## Error Handling

### Master-Side Error Handling

The master should implement the following error handling:

1. **No ACK (No such device or address):** Retry the transaction up to 3 times with 200ms delay between retries.

2. **Invalid marker (byte 0 != 0xA5):** The response is stale or shifted. Send a new write command and re-read.

3. **Checksum mismatch:** Data corruption occurred. Retry the transaction.

4. **Wrong command echo:** The slave processed a different command. Check for race conditions or bus collisions.

5. **Sequence counter did not increment:** The slave may be sending stale data. Verify `Wire.write()` is being used.

### Slave-Side Error Handling

The slave handles these error cases:

1. **Wrong frame length:** Returns `STATUS_INVALID_FRAME` (0xEE) with the received command byte echoed.

2. **Wrong marker byte:** Returns `STATUS_INVALID_FRAME` (0xEE) with byte 1 of the received frame echoed.

3. **Unknown command:** Returns `STATUS_UNKNOWN_COMMAND` (0x01) with the command echoed and value 0x00.

---

## Protocol Design Rationale

### Why 2-byte write?

Minimizes bus occupancy. The marker byte (0x01) provides basic frame validation without adding significant overhead.

### Why 6-byte response?

Provides enough fields for status, command echo, value, sequence, and checksum while keeping the response short enough for the ESP32-C3 FIFO.

### Why XOR checksum?

XOR is simple to calculate, requires no lookup tables, and detects single-bit errors. It is not as robust as CRC, but sufficient for a short 5-byte payload over a reliable I2C bus.

### Why a sequence counter?

Allows the master to detect missed transactions, duplicate responses, or stale FIFO data. If the sequence does not increment, the master knows something is wrong.

### Why a command echo?

Confirms that the slave processed the correct command. If the echo does not match the sent command, there is a protocol or bus error.

### Why a response marker (0xA5)?

Provides immediate validation that the response is aligned correctly. If byte 0 is not `0xA5`, the response is shifted or stale.
