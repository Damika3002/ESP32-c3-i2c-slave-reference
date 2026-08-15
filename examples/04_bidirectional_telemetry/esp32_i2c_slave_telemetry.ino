/*
  ============================================================================
  ESP32-C3 I2C SLAVE + USB CDC TELEMETRY - WITH RADXA TEMPERATURE RECEPTION
  Radxa ROCK 5C (/dev/i2c-6) <-> ESP32-C3 (I2C slave at 0x08 + USB CDC)
  ============================================================================

  PURPOSE
  -------
  Demonstrate practical bidirectional communication:
    - I2C: Diagnostic status, commands, link health, temperature frames FROM Radxa
    - USB CDC: High-speed telemetry (temperature, fan RPM, etc.)

  HARDWARE
  --------
    Radxa ROCK 5C: I2C master (/dev/i2c-6) + USB host
    ESP32-C3:      I2C slave 0x08 + USB CDC device
    ESP32-C3 LED:  GPIO8 (blinks twice on each temperature frame received)

  ARDUINO ENVIRONMENT
  -------------------
    Board:              ESP32C3_DEV
    Arduino-ESP32 core: 3.3.8
    USB CDC on boot:    enabled

  COMPILE-TIME CONFIGURATION
  ---------------------------
    LED_ACTIVE_HIGH:
      - true  : LED turns ON when GPIO is HIGH (3.3V)
      - false : LED turns ON when GPIO is LOW (GND) - common for many boards

    ENABLE_JSON_TELEMETRY:
      - true  : Send JSON telemetry over USB CDC Serial
      - false : Disable JSON output (cleaner logs, less USB traffic)

    ENABLE_I2C_FRAME_LOGS:
      - true  : Print each I2C temperature frame as received
      - false : Silence I2C frame logs (cleaner logs)

    ENABLE_STATUS_PRINT:
      - true  : Print I2C status summary every 5 seconds
      - false : Disable status prints (minimal output)

  PROTOCOL
  --------
    I2C commands (Radxa -> ESP32):
      0x01 0x99 -> Initialize/link test
      0x01 0x2A -> Status request
      0x01 0xB0 -> Read temperature (simulated)
      0x01 0xB1 -> Read fan RPM (simulated)
      0x02 <sensorId> <tempHi> <tempLo> <checksum> 0x03 -> Temperature frame (6 bytes)

    I2C response (ESP32 -> Radxa): 6 bytes
      byte 0: 0xA5 (marker)
      byte 1: status
      byte 2: command echo
      byte 3: value
      byte 4: sequence counter
      byte 5: XOR checksum

    USB CDC telemetry (ESP32 -> Radxa):
      JSON-like text at 1 Hz (when enabled):
      {"t":25.5,"r":1200,"c":123,"i2c_rx":45,"i2c_tx":45,"radxa_temps":[...]}

  TEMPERATURE FRAME FORMAT (from Radxa)
  -------------------------------------
    byte 0: 0x02 (START)
    byte 1: sensorId (0x01-0x07)
    byte 2: tempHi (temperature * 10, high byte)
    byte 3: tempLo (temperature * 10, low byte)
    byte 4: checksum (XOR of bytes 1-3)
    byte 5: 0x03 (END)

  LED BEHAVIOR
  ------------
    On each valid temperature frame received:
      - LED blinks twice (200ms ON, 200ms OFF, 200ms ON, 200ms OFF)
      - Non-blocking: uses millis() timing, no delay()
      - Only triggers on sensor 1 (first frame of sweep)

  ============================================================================
*/

#include <Arduino.h>
#include <Wire.h>

// -----------------------------------------------------------------------------
// COMPILE-TIME CONFIGURATION - CHANGE THESE VALUES
// -----------------------------------------------------------------------------

// LED polarity
static constexpr bool LED_ACTIVE_HIGH = false;  // false = active LOW (common)

// Serial output control
static constexpr bool ENABLE_JSON_TELEMETRY = true;    // true = send JSON, false = no JSON
static constexpr bool ENABLE_I2C_FRAME_LOGS = false;   // true = log each frame, false = silent
static constexpr bool ENABLE_STATUS_PRINT = true;      // true = status every 5s, false = no status

// Telemetry timing
static constexpr uint32_t TELEMETRY_INTERVAL_MS = 1000;  // 1000 = 1 Hz, 100 = 10 Hz

// -----------------------------------------------------------------------------
// Hardware configuration
// -----------------------------------------------------------------------------
static constexpr uint8_t I2C_ADDRESS = 0x08;
static constexpr int SDA_PIN = 4;
static constexpr int SCL_PIN = 5;
static constexpr int LED_PIN = 8;

static constexpr uint32_t I2C_FREQUENCY_HZ = 50000;
static constexpr uint8_t REQUEST_MARKER = 0x01;
static constexpr uint8_t RESPONSE_MARKER = 0xA5;
static constexpr uint8_t RESPONSE_SIZE = 6;

// Frame markers for temperature data from Radxa
static constexpr uint8_t FRAME_START = 0x02;
static constexpr uint8_t FRAME_END = 0x03;
static constexpr uint8_t FRAME_LENGTH = 6;

// Commands
static constexpr uint8_t CMD_INIT = 0x99;
static constexpr uint8_t CMD_STATUS = 0x2A;
static constexpr uint8_t CMD_TEMP = 0xB0;
static constexpr uint8_t CMD_RPM = 0xB1;

// Status codes
static constexpr uint8_t STATUS_OK = 0x00;
static constexpr uint8_t STATUS_UNKNOWN = 0x01;
static constexpr uint8_t STATUS_INVALID = 0xEE;

// -----------------------------------------------------------------------------
// State variables
// -----------------------------------------------------------------------------
volatile uint32_t i2c_rx_count = 0;
volatile uint32_t i2c_tx_count = 0;
volatile uint32_t i2c_invalid = 0;
volatile uint32_t temp_frames_received = 0;

uint8_t response[RESPONSE_SIZE] = {
  RESPONSE_MARKER, STATUS_INVALID, 0x00, 0xE1, 0x00, 0x00
};

// Simulated sensors
float simulated_temp = 25.0;
uint16_t simulated_rpm = 1200;
uint32_t telemetry_counter = 0;

// Temperature data from Radxa (up to 7 sensors)
struct RadxaTemp {
  uint8_t sensorId;
  float temperature;
  uint32_t lastUpdate;
  bool valid;
};

RadxaTemp radxaTemps[8] = {};  // Index by sensorId

// -----------------------------------------------------------------------------
// LED Helper Functions (handles active-high/active-low)
// -----------------------------------------------------------------------------
static inline void setLed(bool on) {
  digitalWrite(LED_PIN, on ? (LED_ACTIVE_HIGH ? HIGH : LOW) : (LED_ACTIVE_HIGH ? LOW : HIGH));
}

static inline bool getLedState() {
  return digitalRead(LED_PIN) == (LED_ACTIVE_HIGH ? HIGH : LOW);
}

// -----------------------------------------------------------------------------
// LED blink state machine (non-blocking)
// -----------------------------------------------------------------------------
enum LedBlinkState {
  LED_IDLE,
  LED_BLINK_1_ON,
  LED_BLINK_1_OFF,
  LED_BLINK_2_ON,
  LED_BLINK_2_OFF
};

volatile LedBlinkState ledState = LED_IDLE;
volatile uint32_t ledStateChangeTime = 0;
constexpr uint32_t LED_BLINK_INTERVAL_MS = 50;

// Call this from loop() to update LED state (non-blocking)
void updateLedBlink() {
  if (ledState == LED_IDLE) return;

  uint32_t now = millis();
  if (now - ledStateChangeTime >= LED_BLINK_INTERVAL_MS) {
    ledStateChangeTime = now;

    switch (ledState) {
      case LED_BLINK_1_ON:
        setLed(true);  // Turn LED ON
        ledState = LED_BLINK_1_OFF;
        break;

      case LED_BLINK_1_OFF:
        setLed(false);  // Turn LED OFF
        ledState = LED_BLINK_2_ON;
        break;

      case LED_BLINK_2_ON:
        setLed(true);  // Turn LED ON
        ledState = LED_BLINK_2_OFF;
        break;

      case LED_BLINK_2_OFF:
        setLed(false);  // Turn LED OFF
        ledState = LED_IDLE;
        break;

      default:
        ledState = LED_IDLE;
        break;
    }
  }
}

// Trigger a double-blink (call from receiveEvent when sensorId==1)
void triggerLedBlink() {
  if (ledState == LED_IDLE) {  // Only if not already blinking
    ledState = LED_BLINK_1_ON;
    ledStateChangeTime = millis();
    setLed(true);  // Start with ON
  }
}

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------
static uint8_t calcChecksum(uint8_t m, uint8_t s, uint8_t c, uint8_t v, uint8_t seq) {
  return m ^ s ^ c ^ v ^ seq;
}

static void setResponse(uint8_t status, uint8_t cmd, uint8_t value) {
  response[0] = RESPONSE_MARKER;
  response[1] = status;
  response[2] = cmd;
  response[3] = value;
  response[4]++;
  response[5] = calcChecksum(response[0], response[1], response[2], response[3], response[4]);
}

// Process a 6-byte temperature frame from Radxa
void processTempFrame(uint8_t* frame) {
  if (frame[0] != FRAME_START || frame[5] != FRAME_END) {
    if (ENABLE_I2C_FRAME_LOGS) {
      Serial.println("[I2C] Invalid temp frame markers");
    }
    return;
  }

  uint8_t sensorId = frame[1];
  if (sensorId < 1 || sensorId > 7) {
    if (ENABLE_I2C_FRAME_LOGS) {
      Serial.printf("[I2C] Invalid sensor ID: 0x%02X\n", sensorId);
    }
    return;
  }

  // Verify checksum
  uint8_t expectedChecksum = frame[1] ^ frame[2] ^ frame[3];
  if (frame[4] != expectedChecksum) {
    if (ENABLE_I2C_FRAME_LOGS) {
      Serial.printf("[I2C] Temp frame checksum mismatch: got 0x%02X, expected 0x%02X\n", 
                    frame[4], expectedChecksum);
    }
    return;
  }

  // Decode temperature (stored as temp * 10 in 16 bits)
  uint16_t tempX10 = (frame[2] << 8) | frame[3];
  float temperature = tempX10 / 10.0;

  // Store in array
  radxaTemps[sensorId].sensorId = sensorId;
  radxaTemps[sensorId].temperature = temperature;
  radxaTemps[sensorId].lastUpdate = millis();
  radxaTemps[sensorId].valid = true;

  temp_frames_received++;

  if (ENABLE_I2C_FRAME_LOGS) {
    Serial.printf("[I2C] Temp frame: sensor=%d, temp=%.1fC\n", sensorId, temperature);
  }

  // ONLY blink on sensor 1 (first frame of each 2-second sweep)
  if (sensorId == 1) {
    triggerLedBlink();
  }
}

// -----------------------------------------------------------------------------
// I2C receive callback (Radxa writes data)
// -----------------------------------------------------------------------------
void receiveEvent(int count) {
  (void)count;
  i2c_rx_count++;

  // Read all available bytes
  uint8_t buf[8] = {};
  uint8_t len = 0;

  while (Wire.available() && len < sizeof(buf)) {
    buf[len++] = Wire.read();
  }

  // Check if this is a temperature frame (6 bytes, starts with 0x02)
  if (len == FRAME_LENGTH && buf[0] == FRAME_START) {
    processTempFrame(buf);
    // Don't send response for temperature frames - they're one-way data
    return;
  }

  // Otherwise treat as 2-byte command (0x01 + command)
  if (len != 2 || buf[0] != REQUEST_MARKER) {
    i2c_invalid++;
    setResponse(STATUS_INVALID, (len >= 2) ? buf[1] : 0x00, 0xE1);
    return;
  }

  uint8_t cmd = buf[1];

  switch (cmd) {
    case CMD_INIT:
      setResponse(STATUS_OK, cmd, 0x11);
      break;

    case CMD_STATUS:
      setResponse(STATUS_OK, cmd, 0x2B);
      break;

    case CMD_TEMP:
      setResponse(STATUS_OK, cmd, (uint8_t)simulated_temp);
      break;

    case CMD_RPM:
      setResponse(STATUS_OK, cmd, (simulated_rpm >> 8) & 0xFF);
      break;

    default:
      i2c_invalid++;
      setResponse(STATUS_UNKNOWN, cmd, 0x00);
      break;
  }
}

// -----------------------------------------------------------------------------
// I2C request callback (Radxa reads response)
// -----------------------------------------------------------------------------
void requestEvent() {
  i2c_tx_count++;
  setLed(!getLedState());  // Toggle LED

  for (uint8_t i = 0; i < RESPONSE_SIZE; i++) {
    Wire.write(response[i]);
  }
}

// -----------------------------------------------------------------------------
// Setup
// -----------------------------------------------------------------------------
void setup() {
  pinMode(LED_PIN, OUTPUT);
  setLed(false);  // Start with LED OFF

  // USB CDC (appears as /dev/ttyACM* on Radxa)
  Serial.begin(115200);
  delay(2000);

  Serial.println("=== ESP32-C3 I2C+USB CDC Ready ===");
  Serial.printf("LED Configuration: GPIO%d, Active-%s\n", 
                LED_PIN, LED_ACTIVE_HIGH ? "HIGH" : "LOW");
  Serial.printf("JSON Telemetry: %s\n", ENABLE_JSON_TELEMETRY ? "ENABLED" : "DISABLED");
  Serial.printf("I2C Frame Logs: %s\n", ENABLE_I2C_FRAME_LOGS ? "ENABLED" : "DISABLED");
  Serial.printf("Status Print: %s\n", ENABLE_STATUS_PRINT ? "ENABLED" : "DISABLED");
  Serial.printf("Telemetry Rate: %lu Hz\n", (unsigned long)(1000 / TELEMETRY_INTERVAL_MS));
  Serial.println("Accepts: commands (0x01+cmd) and temp frames (0x02+data)");
  Serial.println("LED blinks twice on sensor 1 frame (start of sweep)");

  // I2C slave
  Wire.begin(I2C_ADDRESS, SDA_PIN, SCL_PIN, I2C_FREQUENCY_HZ);
  Wire.onReceive(receiveEvent);
  Wire.onRequest(requestEvent);

  Serial.println("I2C slave @ 0x08, USB CDC active");
}

// -----------------------------------------------------------------------------
// Main loop
// -----------------------------------------------------------------------------
void loop() {
  static uint32_t last_telemetry_ms = 0;
  static uint32_t last_sensor_update_ms = 0;
  static uint32_t last_status_ms = 0;

  // Update LED blink state machine (non-blocking)
  updateLedBlink();

  // Update simulated sensors every 500ms
  if (millis() - last_sensor_update_ms >= 500) {
    last_sensor_update_ms = millis();
    simulated_temp += (random(-5, 6) / 10.0);
    if (simulated_temp < 20.0) simulated_temp = 20.0;
    if (simulated_temp > 40.0) simulated_temp = 40.0;
    simulated_rpm = 1000 + random(0, 500);
  }

  // Print status every 5 seconds
  if (ENABLE_STATUS_PRINT && millis() - last_status_ms >= 5000) {
    last_status_ms = millis();
    
    Serial.println("\n--- I2C Status ---");
    Serial.printf("RX=%lu TX=%lu INVALID=%lu TEMP_FRAMES=%lu\n",
      (unsigned long)i2c_rx_count,
      (unsigned long)i2c_tx_count,
      (unsigned long)i2c_invalid,
      (unsigned long)temp_frames_received
    );

    // Print Radxa temperatures
    Serial.println("Radxa Temps:");
    for (uint8_t i = 1; i <= 7; i++) {
      if (radxaTemps[i].valid) {
        uint32_t age = millis() - radxaTemps[i].lastUpdate;
        Serial.printf("  Sensor %d: %.1fC (age %lums)\n", 
                      i, radxaTemps[i].temperature, (unsigned long)age);
      }
    }
  }

  // Send USB CDC telemetry
  if (ENABLE_JSON_TELEMETRY && millis() - last_telemetry_ms >= TELEMETRY_INTERVAL_MS) {
    last_telemetry_ms = millis();
    telemetry_counter++;

    // Build JSON with Radxa temperatures
    String json = String("{\"t\":") + String(simulated_temp, 1) + 
                  String(",\"r\":") + String(simulated_rpm) +
                  String(",\"c\":") + String(telemetry_counter) +
                  String(",\"i2c_rx\":") + String(i2c_rx_count) +
                  String(",\"i2c_tx\":") + String(i2c_tx_count) +
                  String(",\"temp_frames\":") + String(temp_frames_received) +
                  String(",\"radxa_temps\":[");
    
    bool first = true;
    for (uint8_t i = 1; i <= 7; i++) {
      if (radxaTemps[i].valid) {
        if (!first) json += ",";
        json += String("{\"id\":") + String(i) + 
                String(",\"temp\":") + String(radxaTemps[i].temperature, 1) + 
                String("}");
        first = false;
      }
    }
    json += "]}";

    Serial.println(json);
  }

  delay(10);
}