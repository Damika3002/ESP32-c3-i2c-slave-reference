/*
  ============================================================================
  ESP32-C3 I2C Slave - Diagnostic Reference Implementation
  ============================================================================
  
  Full-featured implementation with serial debug output, statistics counters,
  frame validation, and status LED. Use this for development, debugging, and
  as a reference for production code.
  
  Hardware:
    - ESP32-C3FH4 (4 MB internal flash)
    - I2C address: 0x08
    - SDA: GPIO4
    - SCL: GPIO5
    - Status LED: GPIO2 (do NOT use GPIO8 - strapping pin)
    - Frequency: 50 kHz
  
  Protocol:
    Master Write:  01 <command>
    Slave Response: A5 <status> <command> <value> <sequence> <checksum>
  
  Commands:
    0x99 -> A5 00 99 11 <sequence> <checksum>  (Initialize/Heartbeat)
    0x2A -> A5 02 2A 2B <sequence> <checksum>  (Status)
  
  Pull-up Resistors:
    SDA: 2.2k ohm to 3.3V (user validated)
    SCL: 2.2k ohm to 3.3V (user validated)
    Acceptable range: 1.5k to 4.7k ohm
  
  Created by: Damika3002
  GitHub: https://github.com/Damika3002
  Repository: https://github.com/Damika3002/ESP32-c3-i2c-slave-reference
  License: MIT
  ============================================================================
*/

#include <Arduino.h>
#include <Wire.h>

// ============================================================================
// Configuration
// ============================================================================
static constexpr uint8_t I2C_ADDRESS = 0x08;
static constexpr int SDA_PIN = 4;
static constexpr int SCL_PIN = 5;
static constexpr int LED_PIN = 2;

static constexpr uint32_t I2C_FREQUENCY_HZ = 50000;
static constexpr uint8_t REQUEST_MARKER = 0x01;
static constexpr uint8_t RESPONSE_MARKER = 0xA5;
static constexpr uint8_t RESPONSE_SIZE = 6;

// Command definitions
static constexpr uint8_t CMD_INITIALIZE = 0x99;
static constexpr uint8_t CMD_STATUS = 0x2A;

// Status codes
static constexpr uint8_t STATUS_OK = 0x00;
static constexpr uint8_t STATUS_UNKNOWN_COMMAND = 0x01;
static constexpr uint8_t STATUS_STATUS_REPLY = 0x02;
static constexpr uint8_t STATUS_INVALID_FRAME = 0xEE;

// ============================================================================
// Statistics (volatile for ISR safety)
// ============================================================================
// These counters are updated inside I2C callbacks which run in interrupt
// context. The main loop reads them under a brief noInterrupts()/interrupts()
// section to avoid reading partially-updated 32-bit values.
// ============================================================================
volatile uint32_t rx_count = 0;
volatile uint32_t request_count = 0;
volatile uint32_t tx_count = 0;
volatile uint32_t invalid_count = 0;
volatile uint32_t success_99 = 0;
volatile uint32_t success_2a = 0;

// Snapshot of last received bytes for diagnostics
volatile uint8_t last_rx[8] = {};
volatile uint8_t last_rx_len = 0;

// ============================================================================
// Response Buffer
// ============================================================================
// The response is prepared after every valid or invalid write.
// It is emitted when the master performs its following read transaction.
// ============================================================================
uint8_t response[RESPONSE_SIZE] = {
  RESPONSE_MARKER,
  STATUS_INVALID_FRAME,
  0x00,
  0xE1,
  0x00,
  0x00
};

// ============================================================================
// Helper Functions
// ============================================================================
static uint8_t makeChecksum(
  uint8_t marker,
  uint8_t status,
  uint8_t command,
  uint8_t value,
  uint8_t sequence
) {
  return marker ^ status ^ command ^ value ^ sequence;
}

static void setResponse(uint8_t status, uint8_t command, uint8_t value) {
  response[0] = RESPONSE_MARKER;
  response[1] = status;
  response[2] = command;
  response[3] = value;
  response[4]++;  // Wraps naturally after 0xFF
  response[5] = makeChecksum(
    response[0], response[1], response[2], response[3], response[4]
  );
}

// ============================================================================
// I2C Receive Callback
// ============================================================================
// Called when the master writes data to the slave.
//
// This function:
//   1. Reads all available bytes from the Wire buffer
//   2. Stores a snapshot for diagnostics
//   3. Validates the frame (must be 2 bytes, marker 0x01)
//   4. Processes the command
//   5. Prepares the response[] array
//
// This function does NOT call Wire.slaveWrite() or Wire.write().
// The response is sent later from requestEvent().
// ============================================================================
void receiveEvent(int count) {
  (void)count;
  rx_count++;

  uint8_t local[8] = {};
  uint8_t length = 0;

  while (Wire.available() && length < sizeof(local)) {
    local[length++] = static_cast<uint8_t>(Wire.read());
  }

  // Store snapshot for diagnostics
  last_rx_len = length;
  for (uint8_t i = 0; i < length; ++i) {
    last_rx[i] = local[i];
  }

  // Debug output
  Serial.print("RX: ");
  for (uint8_t i = 0; i < length; i++) {
    Serial.printf("%02X ", local[i]);
  }
  Serial.println();

  // Validate: must be exactly 2 bytes, marker must be 0x01
  if (length != 2 || local[0] != REQUEST_MARKER) {
    invalid_count++;
    Serial.printf("INVALID: len=%u, b0=%02X, b1=%02X\n",
      length, local[0], (length >= 2) ? local[1] : 0x00);

    setResponse(
      STATUS_INVALID_FRAME,
      (length >= 2) ? local[1] : 0x00,
      0xE1
    );
    return;
  }

  // Valid command received
  const uint8_t command = local[1];

  switch (command) {
    case CMD_INITIALIZE:
      success_99++;
      setResponse(STATUS_OK, command, 0x11);
      Serial.println("CMD: 0x99 (Initialize)");
      break;

    case CMD_STATUS:
      success_2a++;
      setResponse(STATUS_STATUS_REPLY, command, 0x2B);
      Serial.println("CMD: 0x2A (Status)");
      break;

    default:
      invalid_count++;
      setResponse(STATUS_UNKNOWN_COMMAND, command, 0x00);
      Serial.printf("CMD: 0x%02X (Unknown)\n", command);
      break;
  }

  Serial.printf("RESP: %02X %02X %02X %02X %02X %02X\n",
    response[0], response[1], response[2],
    response[3], response[4], response[5]
  );
}

// ============================================================================
// I2C Request Callback
// ============================================================================
// Called when the master reads data from the slave.
//
// CRITICAL: Use Wire.write() here, NOT Wire.slaveWrite().
//
// Wire.write() fills the internal txBuffer which is automatically cleared
// by the Wire library before each onRequest() callback. After this function
// returns, the Wire library sends the txBuffer contents to the I2C hardware.
//
// Wire.slaveWrite() bypasses txBuffer and writes directly to the hardware
// FIFO. The FIFO is NOT cleared between transactions, causing stale data.
//
// See: https://github.com/espressif/arduino-esp32/issues/5906
// ============================================================================
void requestEvent() {
  request_count++;
  tx_count++;

  // Toggle status LED
  digitalWrite(LED_PIN, !digitalRead(LED_PIN));

  // Send response using Wire.write() - the correct approach
  for (uint8_t i = 0; i < RESPONSE_SIZE; ++i) {
    Wire.write(response[i]);
  }

  Serial.printf("TX: %02X %02X %02X %02X %02X %02X\n",
    response[0], response[1], response[2],
    response[3], response[4], response[5]
  );
}

// ============================================================================
// Setup
// ============================================================================
void setup() {
  // Configure status LED
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, LOW);

  // Initialize Serial for debug output
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("========================================");
  Serial.println("ESP32-C3 I2C Slave - Diagnostic");
  Serial.println("========================================");
  Serial.printf("Address: 0x%02X\n", I2C_ADDRESS);
  Serial.printf("SDA: GPIO%d | SCL: GPIO%d | LED: GPIO%d\n",
    SDA_PIN, SCL_PIN, LED_PIN);
  Serial.printf("Frequency: %lu Hz\n",
    static_cast<unsigned long>(I2C_FREQUENCY_HZ));
  Serial.println();

  // Initialize I2C slave
  const bool started = Wire.begin(
    I2C_ADDRESS,
    SDA_PIN,
    SCL_PIN,
    I2C_FREQUENCY_HZ
  );

  if (!started) {
    Serial.println("FATAL: Wire.begin() failed");
    while (true) {
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
      delay(200);
    }
  }

  // Register callbacks
  Wire.onReceive(receiveEvent);
  Wire.onRequest(requestEvent);

  Serial.println("I2C slave ready.");
  Serial.println("Rule: one master write, then one master read.");
  Serial.println("========================================");
  Serial.println();
}

// ============================================================================
// Main Loop
// ============================================================================
// I2C behavior is callback-driven. The main loop only prints periodic
// statistics. In production, disable or reduce serial output.
// ============================================================================
void loop() {
  static uint32_t last_report_ms = 0;

  if (millis() - last_report_ms >= 2000) {
    last_report_ms = millis();

    // Disable interrupts briefly to safely read 32-bit volatile variables
    noInterrupts();
    const uint32_t rx = rx_count;
    const uint32_t req = request_count;
    const uint32_t tx = tx_count;
    const uint32_t invalid = invalid_count;
    const uint32_t cmd_99 = success_99;
    const uint32_t cmd_2a = success_2a;
    const uint8_t rx_len = last_rx_len;
    const uint8_t rx0 = last_rx[0];
    const uint8_t rx1 = last_rx[1];
    interrupts();

    Serial.println("--- I2C STATUS ---");
    Serial.printf("RX=%lu REQ=%lu TX=%lu INVALID=%lu\n",
      static_cast<unsigned long>(rx),
      static_cast<unsigned long>(req),
      static_cast<unsigned long>(tx),
      static_cast<unsigned long>(invalid)
    );
    Serial.printf("CMD_99=%lu CMD_2A=%lu LAST_RX_LEN=%u LAST_RX=%02X %02X\n",
      static_cast<unsigned long>(cmd_99),
      static_cast<unsigned long>(cmd_2a),
      rx_len, rx0, rx1
    );
    Serial.printf("NEXT_RESPONSE=%02X %02X %02X %02X %02X %02X\n",
      response[0], response[1], response[2],
      response[3], response[4], response[5]
    );
  }

  delay(10);
}
