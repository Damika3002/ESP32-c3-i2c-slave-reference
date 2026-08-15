# Example 04: Bidirectional Temperature Telemetry

This example demonstrates full bidirectional communication between Radxa ROCK 5C and ESP32-C3 with real-time thermal monitoring and stress testing capabilities.

## Overview

- **I2C (Radxa to ESP32):** 7 thermal sensors broadcast every 2 seconds
- **USB CDC (ESP32 to Radxa):** JSON telemetry at 1 Hz with Radxa temperatures embedded
- **LED feedback:** GPIO8 blinks twice on each sensor sweep
- **Stress test ready:** Monitor CPU temperature rise under load

## Hardware Requirements

- ESP32-C3 development board
- Radxa ROCK 5C (or compatible Linux board with I2C)
- 2.2k ohm pull-up resistors x2 (for SDA and SCL)
- Jumper wires

## Wiring

```
Radxa ROCK 5C          ESP32-C3
--------------         --------
I2C6 SDA      ----->   GPIO4 (SDA)
I2C6 SCL      ----->   GPIO5 (SCL)
GND         ----->   GND
USB         ----->   USB-C (for CDC telemetry)
```

## Quick Start

### Step 1: Upload ESP32 Sketch

1. Open `esp32_i2c_slave_telemetry.ino` in Arduino IDE
2. Select board: **ESP32C3_DEV**
3. Configure:
   - USB CDC On Boot: Enabled
   - Upload Speed: 921600
4. Upload to ESP32-C3
5. Open Serial Monitor at 115200 baud

### Step 2: Compile Radxa Script

```bash
cd examples/04_bidirectional_telemetry/radxa_i2c_temp_broadcast
g++ -o i2c_temp_check i2c_temp_check.cpp -lpthread
chmod +x i2c_temp_check
```

### Step 3: Run Radxa Script

```bash
sudo ./i2c_temp_check
```

When prompted, enter `7` for `/dev/i2c-6`.

## Expected Output

### Radxa Terminal

```
I2C device opened: /dev/i2c-6 (address 0x08)

--- Current Thermal Status ---
soc-thermal       : 45.20 C
bigcore0-thermal  : 42.50 C
bigcore1-thermal  : 43.10 C
littlecore-thermal: 38.90 C
center-thermal    : 41.20 C
gpu-thermal       : 39.80 C
npu-thermal       : 40.50 C

Starting I2C temperature broadcast every 2000ms (Ctrl+C to stop)...
```

### ESP32 Serial Monitor

```
=== ESP32-C3 I2C+USB CDC Ready ===
LED Configuration: GPIO8, Active-LOW
JSON Telemetry: ENABLED
I2C Frame Logs: DISABLED
Status Print: ENABLED
Telemetry Rate: 1 Hz

{"t":25.5,"r":1200,"c":1,"i2c_rx":7,"temp_frames":7,"radxa_temps":[...]}

--- I2C Status ---
RX=7 TX=0 INVALID=0 TEMP_FRAMES=7
Radxa Temps:
  Sensor 1: 45.2C (age 150ms)
  Sensor 2: 42.5C (age 140ms)
  Sensor 3: 43.1C (age 130ms)
  Sensor 4: 38.9C (age 120ms)
  Sensor 5: 41.2C (age 110ms)
  Sensor 6: 39.8C (age 100ms)
  Sensor 7: 40.5C (age 90ms)
```

**LED Behavior:** GPIO8 blinks twice (200ms ON, 200ms OFF, 200ms ON, 200ms OFF) at the start of each 2-second sensor sweep.

## Stress Test: CPU Load and Temperature Monitoring

### Baseline Measurement

1. Start `i2c_temp_check` on Radxa
2. Note initial temperatures in ESP32 Serial Monitor
3. Expected idle temperatures: 40-50C depending on ambient

### Run Stress Test

```bash
# Install stress tool
sudo apt install stress

# Run 4-worker CPU stress test for 5 minutes
stress --cpu 4 --timeout 300
```

### Expected Results

**During stress test (0-60 seconds):**
- SOC temperature rises from ~45C to ~55C
- JSON telemetry shows increasing values
- All sensors continue updating (age < 2000ms)

**Peak temperature (1-5 minutes):**
- SOC stabilizes at 60-70C (depends on cooling)
- Other cores show moderate increase
- No I2C communication errors

**After stress test ends:**
- Temperatures gradually return to idle over 2-3 minutes
- I2C communication remains stable throughout

### Example Stress Test Output

```json
{"t":25.5,"r":1200,"c":45,"i2c_rx":210,"temp_frames":210,"radxa_temps":[
  {"id":1,"temp":65.3},
  {"id":2,"temp":58.2},
  {"id":3,"temp":59.1},
  {"id":4,"temp":52.4},
  {"id":5,"temp":54.8},
  {"id":6,"temp":53.5},
  {"id":7,"temp":54.2}
]}
```

## Configuration

### ESP32 Compile-Time Settings

Edit the top section of `esp32_i2c_slave_telemetry.ino`:

```cpp
// LED polarity (test to determine your board)
static constexpr bool LED_ACTIVE_HIGH = false;  // false = active LOW

// Serial output control
static constexpr bool ENABLE_JSON_TELEMETRY = true;    // JSON output
static constexpr bool ENABLE_I2C_FRAME_LOGS = false;   // Frame logs
static constexpr bool ENABLE_STATUS_PRINT = true;      // Status every 5s

// Telemetry rate
static constexpr uint32_t TELEMETRY_INTERVAL_MS = 1000;  // 1000 = 1 Hz
```

**Recommended for production:**
- `ENABLE_JSON_TELEMETRY = true`
- `ENABLE_I2C_FRAME_LOGS = false` (reuces Serial output)
- `ENABLE_STATUS_PRINT = true`
- `TELEMETRY_INTERVAL_MS = 1000`

### Radxa Broadcast Interval

Edit `BROADCAST_INTERVAL_MS` in `i2c_temp_check.cpp`:

```cpp
constexpr int BROADCAST_INTERVAL_MS = 2000;  // 2 seconds
```

**Faster updates:** Reduce to 1000ms (1 Hz)  
**Slower updates:** Increase to 5000ms (0.2 Hz)

## Protocol Specification

### I2C Frame Format (Radxa to ESP32)

Each temperature frame is 6 bytes:

| Byte | Name        | Value     | Description                    |
|------|-------------|-----------|--------------------------------|
| 0    | START       | 0x02      | Frame start marker             |
| 1    | Sensor ID   | 0x01-0x07 | Sensor identifier (1-7)        |
| 2    | Temp Hi     | variable  | Temperature x 10, high byte    |
| 3    | Temp Lo     | variable  | Temperature x 10, low byte     |
| 4    | Checksum    | XOR       | XOR of bytes 1-3               |
| 5    | END         | 0x03      | Frame end marker               |

**Example:** 33.3C from sensor 2

```
0x02 0x02 0x01 0x4D 0x4C 0x03
     |   |    |    |    |
     |   |    |    |    +-- END marker
     |   |    |    +------- Checksum (0x02 ^ 0x01 ^ 0x4D = 0x4C)
     |   |    +------------ Temp Lo (333 = 0x014D)
     |   +----------------- Temp Hi
     +--------------------- Sensor ID (2)
```

### USB CDC Telemetry (ESP32 to Radxa)

JSON format at 1 Hz:

```json
{
  "t": 25.5,
  "r": 1200,
  "c": 123,
  "i2c_rx": 45,
  "i2c_tx": 0,
  "temp_frames": 45,
  "radxa_temps": [
    {"id": 1, "temp": 45.2},
    {"id": 2, "temp": 42.5},
    {"id": 3, "temp": 43.1},
    {"id": 4, "temp": 38.9},
    {"id": 5, "temp": 41.2},
    {"id": 6, "temp": 39.8},
    {"id": 7, "temp": 40.5}
  ]
}
```

**Fields:**
- `t`: ESP32 simulated temperature (Celsius)
- `r`: ESP32 simulated fan RPM
- `c`: Telemetry counter (increments every second)
- `i2c_rx`: Total I2C frames received
- `temp_frames`: Valid temperature frames received
- `radxa_temps`: Array of latest Radxa sensor readings

## Troubleshooting

### No I2C frames received

**Symptoms:** ESP32 shows `RX=0 TEMP_FRAMES=0`

**Solutions:**
1. Verify wiring (SDA/SCL not swapped)
2. Check pull-up resistors (2.2k ohm to 3.3V)
3. Confirm I2C address: `sudo i2cdetect -y 6` should show `08`
4. Test with: `sudo i2ctransfer -y 6 w2@0x08 0x01 0x99`

### Stale data (age greater than 2000ms)

**Symptoms:** `Sensor 1: 45.2C (age 3500ms)`

**Diagnosis:** Radxa broadcast interval too slow or I2C errors

**Solution:** Verify `i2c_temp_check` is running, check `BROADCAST_INTERVAL_MS = 2000`

### LED always on or does not blink

**Symptoms:** GPIO8 LED stays lit constantly or never blinks

**Solution:** Change `LED_ACTIVE_HIGH` from `false` to `true` (or vice versa)

### Checksum errors

**Symptoms:** `[I2C] Temp frame checksum mismatch: got 0x4C, expected 0x4D`

**Diagnosis:** Data corruption on I2C bus

**Solution:**
1. Check pull-up resistors (2.2k ohm recommended)
2. Reduce I2C speed to 50 kHz
3. Shorten I2C cables
4. Check for loose connections

## References

- [Arduino-ESP32 I2C Documentation](https://docs.espressif.com/projects/arduino-esp32/en/latest/api/i2c.html)
- [ESP-IDF I2C Slave API](https://docs.espressif.com/projects/esp-idf/en/stable/esp32/api-reference/peripherals/i2c.html)
- [Radxa ROCK 5C Documentation](https://radxa.com/rock-5c)
- [Linux I2C Documentation](https://www.kernel.org/doc/html/latest/i2c/index.html)


---

For additional support, refer to the main repository documentation or open an issue.
