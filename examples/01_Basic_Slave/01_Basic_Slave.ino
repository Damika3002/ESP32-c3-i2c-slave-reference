/*
  ============================================================================
  ESP32-C3 I2C Slave - Basic Example
  ============================================================================
  
  Minimal implementation demonstrating the Wire.write() fix for stale data.
  
  This is the simplest working example. It does not include serial output,
  statistics, or LED logic. Use this to validate that the I2C link works
  before adding application logic.
  
  Hardware:
    - ESP32-C3FH4 (4 MB internal flash)
    - I2C address: 0x08
    - SDA: GPIO4
    - SCL: GPIO5
    - Frequency: 50 kHz
  
  Protocol:
    Master Write:  01 <command>
    Slave Response: A5 <status> <command> <value> <sequence> <checksum>
  
  Commands:
    0x99 -> A5 00 99 11 <sequence> <checksum>
    0x2A -> A5 02 2A 2B <sequence> <checksum>
  
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
static constexpr uint32_t I2C_FREQUENCY_HZ = 50000;

// Protocol constants
static constexpr uint8_t REQUEST_MARKER = 0x01;
static constexpr uint8_t RESPONSE_MARKER = 0xA5;
static constexpr uint8_t RESPONSE_SIZE = 6;

// ============================================================================
// Response Buffer
// ============================================================================
uint8_t response[RESPONSE_SIZE] = {0xA5, 0x00, 0x00, 0x00, 0x00, 0x00};

// ============================================================================
// Helper: Calculate XOR Checksum
// ============================================================================
uint8_t makeChecksum() {
  return response[0] ^ response[1] ^ response[2] ^ response[3] ^ response[4];
}

// ============================================================================
// I2C Receive Callback
// ============================================================================
// Called when the master writes data to the slave.
// This function ONLY prepares the response[] array.
// It does NOT call Wire.slaveWrite() or Wire.write().
// The response is sent later when the master performs a read.
// ============================================================================
void receiveEvent(int count) {
  uint8_t local[8] = {};
  uint8_t length = 0;

  while (Wire.available() && length < sizeof(local)) {
    local[length++] = static_cast<uint8_t>(Wire.read());
  }

  // Validate: must be exactly 2 bytes, first byte must be 0x01
  if (length != 2 || local[0] != REQUEST_MARKER) {
    response[0] = RESPONSE_MARKER;
    response[1] = 0xEE;  // Invalid frame
    response[2] = (length >= 2) ? local[1] : 0x00;
    response[3] = 0xE1;
    response[4]++;
    response[5] = makeChecksum();
    return;
  }

  // Process valid command
  uint8_t command = local[1];

  switch (command) {
    case 0x99:
      response[0] = RESPONSE_MARKER;
      response[1] = 0x00;  // Status OK
      response[2] = command;
      response[3] = 0x11;  // Response value
      response[4]++;
      response[5] = makeChecksum();
      break;

    case 0x2A:
      response[0] = RESPONSE_MARKER;
      response[1] = 0x02;  // Status reply
      response[2] = command;
      response[3] = 0x2B;  // Response value
      response[4]++;
      response[5] = makeChecksum();
      break;

    default:
      response[0] = RESPONSE_MARKER;
      response[1] = 0x01;  // Unknown command
      response[2] = command;
      response[3] = 0x00;
      response[4]++;
      response[5] = makeChecksum();
      break;
  }
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
// ============================================================================
void requestEvent() {
  for (uint8_t i = 0; i < RESPONSE_SIZE; i++) {
    Wire.write(response[i]);
  }
}

// ============================================================================
// Setup
// ============================================================================
void setup() {
  Wire.begin(
    I2C_ADDRESS,
    SDA_PIN,
    SCL_PIN,
    I2C_FREQUENCY_HZ
  );

  Wire.onReceive(receiveEvent);
  Wire.onRequest(requestEvent);
}

// ============================================================================
// Main Loop
// ============================================================================
// All I2C work happens in callbacks. The main loop does nothing.
// In a real application, this is where you would handle fan control,
// temperature sensing, or other application logic.
// ============================================================================
void loop() {
  delay(10);
}
