/*
  ============================================================================
  ESP32-C3 I2C Slave - Fan Controller Example
  ============================================================================
  
  Production-ready example integrating I2C slave communication with fan
  PWM control, temperature sensing, and a thermal policy. This example
  demonstrates how to combine diagnostic I2C with real hardware control.
  
  Hardware:
    - ESP32-C3FH4 (4 MB internal flash)
    - I2C address: 0x08
    - SDA: GPIO4
    - SCL: GPIO5
    - Status LED: GPIO2
    - Fan PWM: GPIO10
    - Temperature sensor (analog): GPIO3 (ADC1_CH3)
    - eFuse fault input: GPIO9 (TPS25940, active low)
    - Boost enable: GPIO3 (alternative use)
  
  Protocol:
    Master Write:  01 <command>
    Slave Response: A5 <status> <command> <value> <sequence> <checksum>
  
  Commands:
    0x99 -> Initialize/Heartbeat -> A5 00 99 <fan_duty> <seq> <checksum>
    0x2A -> Status               -> A5 02 2A <system_status> <seq> <checksum>
    0x10 -> Read Temperature     -> A5 00 10 <temp_c> <seq> <checksum>
    0x11 -> Read Fan Speed       -> A5 00 11 <rpm_byte> <seq> <checksum>
    0x20 -> Set Fan Override     -> A5 00 20 <ack> <seq> <checksum>
    0x21 -> Clear Fan Override   -> A5 00 21 <ack> <seq> <checksum>
    0x30 -> Read eFuse Status    -> A5 00 30 <efuse_flags> <seq> <checksum>
    0x40 -> Read Error Counters  -> A5 00 40 <error_byte> <seq> <checksum>
  
  Pull-up Resistors:
    SDA: 2.2k ohm to 3.3V (user validated)
    SCL: 2.2k ohm to 3.3V (user validated)
    Acceptable range: 1.5k to 4.7k ohm
  
  Safety Note:
    I2C is for diagnostics and status monitoring only.
    UART is the authoritative path for safety-critical fan control.
    The thermal policy in this example is a fallback, not a primary control.
  
  Created by: Damika3002
  GitHub: https://github.com/Damika3002
  Repository: https://github.com/Damika3002/ESP32-c3-i2c-slave-reference
  License: MIT
  ============================================================================
*/

#include <Arduino.h>
#include <Wire.h>

// ============================================================================
// I2C Configuration
// ============================================================================
static constexpr uint8_t I2C_ADDRESS = 0x08;
static constexpr int SDA_PIN = 4;
static constexpr int SCL_PIN = 5;
static constexpr int LED_PIN = 2;
static constexpr uint32_t I2C_FREQUENCY_HZ = 50000;

static constexpr uint8_t REQUEST_MARKER = 0x01;
static constexpr uint8_t RESPONSE_MARKER = 0xA5;
static constexpr uint8_t RESPONSE_SIZE = 6;

// ============================================================================
// Hardware Pin Definitions
// ============================================================================
static constexpr int FAN_PWM_PIN = 10;       // Fan PWM output (via NPN/NMOS)
static constexpr int TEMP_SENSOR_PIN = 0;     // ADC pin for temperature (GPIO0 = ADC1_CH0)
static constexpr int EFUSE_FAULT_PIN = 9;     // TPS25940 eFuse fault (active low)

// ============================================================================
// Command Definitions
// ============================================================================
static constexpr uint8_t CMD_INITIALIZE = 0x99;
static constexpr uint8_t CMD_STATUS = 0x2A;
static constexpr uint8_t CMD_READ_TEMP = 0x10;
static constexpr uint8_t CMD_READ_RPM = 0x11;
static constexpr uint8_t CMD_FAN_OVERRIDE = 0x20;
static constexpr uint8_t CMD_FAN_CLEAR = 0x21;
static constexpr uint8_t CMD_READ_EFUSE = 0x30;
static constexpr uint8_t CMD_READ_ERRORS = 0x40;

// ============================================================================
// Status Codes
// ============================================================================
static constexpr uint8_t STATUS_OK = 0x00;
static constexpr uint8_t STATUS_UNKNOWN_COMMAND = 0x01;
static constexpr uint8_t STATUS_STATUS_REPLY = 0x02;
static constexpr uint8_t STATUS_INVALID_FRAME = 0xEE;
static constexpr uint8_t STATUS_EFUSE_FAULT = 0x10;
static constexpr uint8_t STATUS_THERMAL_WARNING = 0x20;
static constexpr uint8_t STATUS_THERMAL_CRITICAL = 0x40;

// ============================================================================
// Thermal Policy Constants (temperatures in degrees Celsius)
// ============================================================================
static constexpr float TEMP_FAN_OFF = 35.0f;
static constexpr float TEMP_FAN_LOW = 45.0f;
static constexpr float TEMP_FAN_MEDIUM = 55.0f;
static constexpr float TEMP_FAN_HIGH = 65.0f;
static constexpr float TEMP_FAN_FULL = 75.0f;
static constexpr float TEMP_CRITICAL = 85.0f;

static constexpr uint8_t FAN_DUTY_OFF = 0;
static constexpr uint8_t FAN_DUTY_LOW = 80;       // ~31% of 255
static constexpr uint8_t FAN_DUTY_MEDIUM = 140;    // ~55% of 255
static constexpr uint8_t FAN_DUTY_HIGH = 200;      // ~78% of 255
static constexpr uint8_t FAN_DUTY_FULL = 255;     // 100%

// ============================================================================
// Statistics (volatile for ISR safety)
// ============================================================================
volatile uint32_t rx_count = 0;
volatile uint32_t request_count = 0;
volatile uint32_t tx_count = 0;
volatile uint32_t invalid_count = 0;
volatile uint32_t efuse_fault_count = 0;
volatile uint32_t thermal_warning_count = 0;

volatile uint8_t last_rx[8] = {};
volatile uint8_t last_rx_len = 0;

// ============================================================================
// Fan Controller State
// ============================================================================
static constexpr uint8_t RESPONSE_BUFFER_SIZE = 6;
uint8_t response[RESPONSE_BUFFER_SIZE] = {
  RESPONSE_MARKER,
  STATUS_INVALID_FRAME,
  0x00,
  0xE1,
  0x00,
  0x00
};

// Fan state
static uint8_t fan_pwm_duty = FAN_DUTY_OFF;
static bool fan_override = false;
static uint8_t fan_override_duty = 0;
static uint16_t fan_rpm = 0;

// Temperature
static float temperature_c = 0.0f;
static uint8_t temp_error_count = 0;

// System status bitmask
// Bit 0: fan running
// Bit 1: fan override active
// Bit 2: thermal warning
// Bit 3: thermal critical
// Bit 4: eFuse fault
// Bit 5: I2C error
static uint8_t system_status = 0;

// Control loop timing
static uint32_t last_control_update = 0;
static constexpr uint32_t CONTROL_INTERVAL_MS = 1000;  // 1 second

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
  response[4]++;
  response[5] = makeChecksum(
    response[0], response[1], response[2], response[3], response[4]
  );
}

static uint8_t floatToByte(float value) {
  if (value < 0.0f) return 0;
  if (value > 255.0f) return 255;
  return static_cast<uint8_t>(value);
}

// ============================================================================
// Fan Control Functions
// ============================================================================
static void setFanSpeed(uint8_t duty) {
  fan_pwm_duty = duty;
  analogWrite(FAN_PWM_PIN, duty);
}

static void updateSystemStatus() {
  system_status = 0;

  if (fan_pwm_duty > 0) {
    system_status |= (1 << 0);  // Fan running
  }
  if (fan_override) {
    system_status |= (1 << 1);  // Override active
  }
  if (temperature_c >= TEMP_FAN_HIGH) {
    system_status |= (1 << 2);  // Thermal warning
  }
  if (temperature_c >= TEMP_CRITICAL) {
    system_status |= (1 << 3);  // Thermal critical
  }
  if (digitalRead(EFUSE_FAULT_PIN) == LOW) {
    system_status |= (1 << 4);  // eFuse fault
  }
}

static void readTemperature() {
  int adc_value = analogRead(TEMP_SENSOR_PIN);

  // Example conversion: LM35-style sensor
  // LM35: 10mV per degree C, ADC at 3.3V = 4095
  // temp_c = (adc_value * 3.3 / 4095) * 100
  // For ESP32-C3 12-bit ADC:
  temperature_c = (static_cast<float>(adc_value) * 3.3f / 4095.0f) * 100.0f;

  // Sanity check
  if (temperature_c < -10.0f || temperature_c > 150.0f) {
    temp_error_count++;
    temperature_c = 25.0f;  // Fallback to ambient
  }
}

static void applyThermalPolicy() {
  // If override is active, use override duty
  if (fan_override) {
    setFanSpeed(fan_override_duty);
    return;
  }

  // Check eFuse fault first
  if (digitalRead(EFUSE_FAULT_PIN) == LOW) {
    efuse_fault_count++;
    // Run fan at full speed on eFuse fault
    setFanSpeed(FAN_DUTY_FULL);
    return;
  }

  // Apply thermal policy
  if (temperature_c >= TEMP_CRITICAL) {
    setFanSpeed(FAN_DUTY_FULL);
    thermal_warning_count++;
  } else if (temperature_c >= TEMP_FAN_FULL) {
    setFanSpeed(FAN_DUTY_FULL);
  } else if (temperature_c >= TEMP_FAN_HIGH) {
    setFanSpeed(FAN_DUTY_HIGH);
  } else if (temperature_c >= TEMP_FAN_MEDIUM) {
    setFanSpeed(FAN_DUTY_MEDIUM);
  } else if (temperature_c >= TEMP_FAN_LOW) {
    setFanSpeed(FAN_DUTY_LOW);
  } else {
    setFanSpeed(FAN_DUTY_OFF);
  }
}

static void updateFanRpm() {
  // Simulated RPM based on duty cycle
  // Replace with actual tachometer reading
  if (fan_pwm_duty == 0) {
    fan_rpm = 0;
  } else {
    fan_rpm = static_cast<uint16_t>(
      (static_cast<uint32_t>(fan_pwm_duty) * 5000) / 255
    );
  }
}

static uint8_t getStatusByte() {
  updateSystemStatus();
  return system_status;
}

static uint8_t getErrorByte() {
  uint8_t errors = 0;

  if (temp_error_count > 0) errors |= (1 << 0);
  if (efuse_fault_count > 0) errors |= (1 << 1);
  if (invalid_count > 0) errors |= (1 << 2);
  if (thermal_warning_count > 0) errors |= (1 << 3);

  return errors;
}

// ============================================================================
// I2C Receive Callback
// ============================================================================
void receiveEvent(int count) {
  (void)count;
  rx_count++;

  uint8_t local[8] = {};
  uint8_t length = 0;

  while (Wire.available() && length < sizeof(local)) {
    local[length++] = static_cast<uint8_t>(Wire.read());
  }

  last_rx_len = length;
  for (uint8_t i = 0; i < length; ++i) {
    last_rx[i] = local[i];
  }

  Serial.print("RX: ");
  for (uint8_t i = 0; i < length; i++) {
    Serial.printf("%02X ", local[i]);
  }
  Serial.println();

  // Validate frame
  if (length != 2 || local[0] != REQUEST_MARKER) {
    invalid_count++;
    Serial.printf("INVALID: len=%u, b0=%02X\n", length, local[0]);
    setResponse(STATUS_INVALID_FRAME, (length >= 2) ? local[1] : 0x00, 0xE1);
    return;
  }

  const uint8_t command = local[1];

  switch (command) {
    case CMD_INITIALIZE:
      // Return current fan duty as the value
      setResponse(STATUS_OK, command, fan_pwm_duty);
      Serial.println("CMD: 0x99 (Initialize)");
      break;

    case CMD_STATUS:
      // Return system status bitmask
      setResponse(STATUS_STATUS_REPLY, command, getStatusByte());
      Serial.println("CMD: 0x2A (Status)");
      break;

    case CMD_READ_TEMP:
      // Return temperature as integer
      setResponse(STATUS_OK, command, floatToByte(temperature_c));
      Serial.printf("CMD: 0x10 (Read Temp: %.1f C)\n", temperature_c);
      break;

    case CMD_READ_RPM:
      // Return RPM divided by 100 (fits in one byte)
      setResponse(STATUS_OK, command, static_cast<uint8_t>(fan_rpm / 100));
      Serial.printf("CMD: 0x11 (Read RPM: %u)\n", fan_rpm);
      break;

    case CMD_FAN_OVERRIDE:
      // Enable fan override at full speed
      fan_override = true;
      fan_override_duty = FAN_DUTY_FULL;
      setResponse(STATUS_OK, command, 0x01);
      Serial.println("CMD: 0x20 (Fan Override ON)");
      break;

    case CMD_FAN_CLEAR:
      // Clear fan override
      fan_override = false;
      setResponse(STATUS_OK, command, 0x01);
      Serial.println("CMD: 0x21 (Fan Override OFF)");
      break;

    case CMD_READ_EFUSE:
      // Return eFuse status (0 = OK, 1 = fault)
      setResponse(
        STATUS_OK,
        command,
        (digitalRead(EFUSE_FAULT_PIN) == LOW) ? 0x01 : 0x00
      );
      Serial.println("CMD: 0x30 (Read eFuse)");
      break;

    case CMD_READ_ERRORS:
      // Return error byte
      setResponse(STATUS_OK, command, getErrorByte());
      Serial.println("CMD: 0x40 (Read Errors)");
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
void requestEvent() {
  request_count++;
  tx_count++;

  digitalWrite(LED_PIN, !digitalRead(LED_PIN));

  // CRITICAL: Use Wire.write() not Wire.slaveWrite()
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
  // Configure GPIO
  pinMode(LED_PIN, OUTPUT);
  pinMode(FAN_PWM_PIN, OUTPUT);
  pinMode(EFUSE_FAULT_PIN, INPUT_PULLUP);

  digitalWrite(LED_PIN, LOW);
  analogWrite(FAN_PWM_PIN, 0);

  // Initialize Serial
  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("========================================");
  Serial.println("ESP32-C3 I2C Slave - Fan Controller");
  Serial.println("========================================");
  Serial.printf("I2C Address: 0x%02X\n", I2C_ADDRESS);
  Serial.printf("SDA: GPIO%d | SCL: GPIO%d | LED: GPIO%d\n",
    SDA_PIN, SCL_PIN, LED_PIN);
  Serial.printf("Fan PWM: GPIO%d | eFuse: GPIO%d\n",
    FAN_PWM_PIN, EFUSE_FAULT_PIN);
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

  Wire.onReceive(receiveEvent);
  Wire.onRequest(requestEvent);

  // Initial temperature reading
  readTemperature();

  Serial.println("Fan controller ready.");
  Serial.println("I2C for diagnostics. UART for safety-critical control.");
  Serial.println("========================================");
  Serial.println();
}

// ============================================================================
// Main Loop
// ============================================================================
void loop() {
  uint32_t now = millis();

  // Run control loop at 1 Hz
  if (now - last_control_update >= CONTROL_INTERVAL_MS) {
    last_control_update = now;

    // Read temperature
    readTemperature();

    // Apply thermal policy
    applyThermalPolicy();

    // Update fan RPM (simulated)
    updateFanRpm();

    // Update system status
    updateSystemStatus();

    // Print status
    noInterrupts();
    uint32_t rx = rx_count;
    uint32_t req = request_count;
    uint32_t tx = tx_count;
    uint32_t inv = invalid_count;
    uint32_t efuse = efuse_fault_count;
    uint32_t therm = thermal_warning_count;
    interrupts();

    Serial.println("--- FAN CONTROLLER STATUS ---");
    Serial.printf("Temp: %.1f C | Fan: %u/255 | RPM: %u\n",
      temperature_c, fan_pwm_duty, fan_rpm);
    Serial.printf("Status: 0x%02X | Override: %s\n",
      system_status, fan_override ? "ON" : "OFF");
    Serial.printf("I2C: rx=%lu req=%lu tx=%lu inv=%lu\n",
      static_cast<unsigned long>(rx),
      static_cast<unsigned long>(req),
      static_cast<unsigned long>(tx),
      static_cast<unsigned long>(inv)
    );
    Serial.printf("Faults: efuse=%lu thermal=%lu temp_err=%u\n",
      static_cast<unsigned long>(efuse),
      static_cast<unsigned long>(therm),
      temp_error_count
    );
    Serial.println("------------------------------");
  }

  delay(10);
}
