# Hardware Guide

Complete hardware reference for the ESP32-C3FH4 I2C slave and Radxa ROCK 5C I2C master configuration.

---

## Table of Contents

- [Overview](#overview)
- [ESP32-C3FH4 Specifications](#esp32-c3fh4-specifications)
- [Radxa ROCK 5C Specifications](#radxa-rock-5c-specifications)
- [Pin Assignments](#pin-assignments)
- [Wiring Diagram](#wiring-diagram)
- [Pull-up Resistors](#pull-up-resistors)
- [Fan PWM Driver Circuit](#fan-pwm-driver-circuit)
- [Temperature Sensor Interface](#temperature-sensor-interface)
- [eFuse (TPS25940) Interface](#efuse-tps25940-interface)
- [Status LED](#status-led)
- [Power Supply](#power-supply)
- [GPIO Strapping Pins](#gpio-strapping-pins)
- [Board Layout Considerations](#board-layout-considerations)
- [Signal Integrity](#signal-integrity)
- [Bill of Materials](#bill-of-materials)

---

## Overview

This document describes the hardware configuration for the I2C diagnostic link between the Radxa ROCK 5C (master) and the ESP32-C3FH4 (slave). It includes pin assignments, wiring, pull-up resistor selection, and peripheral interface circuits.

---

## ESP32-C3FH4 Specifications

| Parameter | Value |
|-----------|-------|
| Chip | ESP32-C3FH4 |
| Package | QFN-32 (5x5 mm) |
| CPU | 32-bit RISC-V single-core |
| Clock | 160 MHz |
| SRAM | 400 KB |
| Flash | 4 MB internal (embedded) |
| WiFi | 802.11b/g/n (2.4 GHz) |
| Bluetooth | Bluetooth 5 (LE) |
| GPIO | 22 (GPIO0-GPIO21) |
| I2C Controllers | 1 |
| SPI | 2 |
| UART | 2 |
| ADC | 5 channels (12-bit) |
| PWM | 6 channels |
| Operating Voltage | 3.0V to 3.6V |
| Operating Temperature | -40 C to +85 C |

### ESP32-C3FH4 vs ESP32-C3FN4

| Variant | Flash | Package |
|---------|-------|---------|
| ESP32-C3FH4 | 4 MB internal | QFN-32 |
| ESP32-C3FN4 | 4 MB internal | QFN-32 |
| ESP32-C3FH4Z | 4 MB internal | QFN-32 (lead-free) |

The FH4 variant has embedded flash die in the same package. No external flash chip is required.

---

## Radxa ROCK 5C Specifications

| Parameter | Value |
|-----------|-------|
| SoC | RK3588S |
| CPU | 4x Cortex-A76 (2.4 GHz) + 4x Cortex-A55 (1.8 GHz) |
| RAM | 4 GB / 8 GB / 16 GB LPDDR4X |
| I2C Controllers | 8 (I2C0 through I2C7) |
| I2C6 Base Address | 0xfec80000 |
| GPIO | 40-pin header |
| Operating System | Debian / Ubuntu / Armbian |
| Kernel | Linux (mainline or vendor) |

### I2C6 on the 40-Pin Header

The I2C6 bus is available on the 40-pin GPIO header. Consult the Radxa ROCK 5C pinout documentation for the exact SDA and SCL pin numbers, as they may vary by board revision.

---

## Pin Assignments

### ESP32-C3FH4 Pin Assignment

| GPIO | Function | Direction | Notes |
|------|----------|-----------|-------|
| GPIO0 | ADC1_CH0 / Temperature sensor | Input | Analog input for temperature |
| GPIO2 | Status LED | Output | Non-strapping pin, safe for LED |
| GPIO3 | ADC1_CH3 / Boost enable (optional) | Output | Alternative use in fan controller |
| GPIO4 | I2C SDA | Bidirectional | I2C data line |
| GPIO5 | I2C SCL | Bidirectional | I2C clock line |
| GPIO8 | Do NOT use | - | Strapping pin (must be HIGH at boot) |
| GPIO9 | eFuse fault input | Input | TPS25940 FAULT pin (active low) |
| GPIO10 | Fan PWM output | Output | PWM signal to fan driver |
| GPIO20 | UART RX (Radxa link) | Input | Safety-critical UART |
| GPIO21 | UART TX (Radxa link) | Output | Safety-critical UART |

### Radxa ROCK 5C Pin Assignment

| Pin | Function | Notes |
|-----|----------|-------|
| I2C6 SDA | I2C data | Connect to ESP32-C3 GPIO4 |
| I2C6 SCL | I2C clock | Connect to ESP32-C3 GPIO5 |
| GND | Common ground | Connect to ESP32-C3 GND |
| 3.3V | Power (optional) | Can power ESP32-C3 if USB not used |

---

## Wiring Diagram

### Minimal I2C Connection

```
Radxa ROCK 5C              ESP32-C3FH4
---------------             ------------
I2C6 SDA          ------>  GPIO4 (SDA)
I2C6 SCL          ------>  GPIO5 (SCL)
GND               ------>  GND
```

### Full System Connection

```
Radxa ROCK 5C              ESP32-C3FH4
---------------             ------------
I2C6 SDA          ------>  GPIO4 (SDA)        + 2.2k pull-up to 3.3V
I2C6 SCL          ------>  GPIO5 (SCL)        + 2.2k pull-up to 3.3V
GND               ------>  GND
UART TX           ------>  GPIO20 (UART RX)   (safety-critical link)
UART RX           ------>  GPIO21 (UART TX)   (safety-critical link)

                           ESP32-C3FH4          External Components
                           ------------          -------------------
                           GPIO2  ------>  LED (330 ohm) ------> GND
                           GPIO10 ------>  Fan PWM driver (NPN/NMOS)
                           GPIO0  ------>  Temperature sensor (analog)
                           GPIO9  ------>  TPS25940 FAULT pin
```

---

## Pull-up Resistors

### Why Pull-ups Are Required

The I2C bus uses open-drain drivers on both SDA and SCL lines. The lines idle HIGH when no device is pulling them LOW. Pull-up resistors are required to return the lines to HIGH after a device releases them.

The ESP32-C3 has internal pull-ups, but they are approximately 45k ohm, which is too weak for reliable I2C communication at any practical bus speed.

### Recommended Values

| Resistor Value | Bus Capacitance | Wire Length | Current Draw | Suitability |
|----------------|----------------|-------------|--------------|-------------|
| 1.5k ohm | High (100+ pF) | Long (50+ cm) | 2.2 mA | Best for long bus |
| 2.2k ohm | Medium (30-100 pF) | Medium (10-30 cm) | 1.5 mA | Recommended (validated) |
| 4.7k ohm | Low (under 30 pF) | Short (under 10 cm) | 0.7 mA | Standard value |

### Configuration Used in This Project

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

### Calculation

The pull-up resistor value depends on the bus capacitance and the desired rise time:

```
R_pull = t_rise / (0.8473 * C_bus)
```

Where:
- `t_rise` is the desired rise time (300 ns for 100 kHz, 120 ns for 400 kHz)
- `C_bus` is the total bus capacitance (typically 10-400 pF)

For 50 kHz with 100 pF bus capacitance:

```
R_pull = 5000 ns / (0.8473 * 100 pF) = 59k ohm (theoretical maximum)
```

In practice, use much lower values (2.2k to 4.7k) for reliable operation.

---

## Fan PWM Driver Circuit

The ESP32-C3 GPIO10 cannot drive a fan directly. A transistor or MOSFET driver is required.

### NPN Transistor Driver (Low-Side Switch)

```
3.3V or 5V (fan power)
   |
  Fan (+) terminal
   |
  Fan (-) terminal
   |
  Collector of NPN (e.g., 2N2222, BC547)
   |
  Emitter of NPN -----> GND
   |
  Base of NPN <---- 330 ohm <---- GPIO10
```

### N-Channel MOSFET Driver (Low-Side Switch)

```
3.3V or 5V (fan power)
   |
  Fan (+) terminal
   |
  Fan (-) terminal
   |
  Drain of NMOS (e.g., 2N7000, IRLZ44N)
   |
  Source of NMOS -----> GND
   |
  Gate of NMOS <---- 100 ohm <---- GPIO10
   |
  10k ohm pulldown from Gate to GND (ensures fan off at boot)
```

### PWM Frequency

The ESP32 `analogWrite()` function produces a PWM signal. The default frequency is 1000 Hz, which is suitable for most 4-wire PWM fans. For 2-wire fans, a lower frequency (25 kHz) may be needed.

---

## Temperature Sensor Interface

### Analog Temperature Sensor (e.g., LM35)

```
3.3V
 |
 LM35 VCC pin
 |
 LM35 VOUT pin ------> GPIO0 (ADC1_CH0)
 |
 LM35 GND pin -----> GND
```

The LM35 produces 10 mV per degree Celsius. The ESP32-C3 ADC is 12-bit with a 3.3V reference:

```
temp_c = (adc_value * 3.3 / 4095) * 100
```

### Alternative: NTC Thermistor

```
3.3V
 |
 10k NTC thermistor
 |
 +-----> GPIO0 (ADC1_CH0)
 |
 10k resistor
 |
 GND
```

The NTC requires a voltage divider and Steinhart-Hart calculation in firmware.

---

## eFuse (TPS25940) Interface

The TPS25940 eFuse provides overcurrent and overvoltage protection. Its FAULT pin goes LOW when a fault is detected.

### Connection

```
TPS25940 FAULT pin ------> GPIO9 (ESP32-C3)

GPIO9 configured as INPUT_PULLUP (internal pull-up is sufficient
because the TPS25940 FAULT pin is open-drain)
```

### Behavior

When GPIO9 reads LOW:

1. The fan controller immediately sets fan to full speed
2. The efuse_fault_count counter increments
3. The system status bitmask sets bit 4 (0x10)
4. The I2C command 0x30 returns 0x01 (fault active)

---

## Status LED

### Connection

```
GPIO2 ------> 330 ohm ------> LED Anode
LED Cathode ----------------> GND
```

### Why GPIO2 and Not GPIO8

GPIO8 is a strapping pin on the ESP32-C3. It must be HIGH during reset. Using it for an LED can:

- Prevent the ESP32-C3 from booting if the LED pulls it LOW
- Interfere with the I2C peripheral (GPIO8 is the default I2C SDA on some board variants)
- Cause unpredictable behavior during power-on reset

GPIO2 is safe for general-purpose I/O and does not affect the boot process.

---

## Power Supply

### USB Power (Recommended for Development)

Connect the ESP32-C3FH4 via USB to the development computer. This provides:

- 5V power (regulated to 3.3V on-board)
- Serial communication (USB CDC)
- Programming interface

### External 3.3V Power (Production)

For production use, power the ESP32-C3FH4 from a regulated 3.3V supply:

```
3.3V regulator ------> 3V3 pin on ESP32-C3 board
GND                  ------> GND pin on ESP32-C3 board
```

Add a 100uF electrolytic capacitor and a 0.1uF ceramic capacitor near the ESP32-C3 VCC/GND pins for decoupling.

### Common Ground

Common ground between the Radxa ROCK 5C and the ESP32-C3 is required for I2C communication. Without a common ground reference, the I2C signals are undefined.

---

## GPIO Strapping Pins

The ESP32-C3 has strapping pins that configure the boot mode. Their state during reset determines how the chip boots.

| Pin | Required State at Boot | Effect if Wrong |
|-----|----------------------|-----------------|
| GPIO2 | LOW | Affects VDD_SPI voltage |
| GPIO8 | HIGH | Boot fails or enters wrong mode |
| GPIO9 | HIGH | Enters download mode instead of normal boot |

### Safe GPIO Usage

| GPIO | Safe for I/O | Notes |
|------|-------------|-------|
| GPIO0 | Yes | ADC input, boot determines flash voltage |
| GPIO1 | Yes | TX pin (UART0) |
| GPIO2 | Yes (with caution) | Must be LOW at boot |
| GPIO3 | Yes | ADC input |
| GPIO4 | Yes | I2C SDA (this project) |
| GPIO5 | Yes | I2C SCL (this project) |
| GPIO6 | Yes | General purpose |
| GPIO7 | Yes | General purpose |
| GPIO8 | NO | Strapping pin (must be HIGH at boot) |
| GPIO9 | NO | Strapping pin (must be HIGH at boot) |
| GPIO10 | Yes | Fan PWM (this project) |
| GPIO18 | Yes | USB D- |
| GPIO19 | Yes | USB D+ |
| GPIO20 | Yes | UART RX (this project) |
| GPIO21 | Yes | UART TX (this project) |

---

## Board Layout Considerations

### I2C Signal Routing

1. Keep SDA and SCL traces short (under 30 cm)
2. Route SDA and SCL close together for matched impedance
3. Avoid crossing other high-speed signals
4. Place pull-up resistors close to the master end of the bus
5. Add a ground plane under the I2C traces for noise immunity

### Decoupling

1. Place 0.1uF ceramic capacitor within 5 mm of each I2C device VCC pin
2. Place 10uF electrolytic capacitor near the bus power entry point
3. Use a solid ground plane for return currents

### Thermal Considerations

1. Keep the temperature sensor away from heat-generating components
2. Place the ESP32-C3 away from the fan airflow path if measuring ambient temperature
3. If measuring heatsink temperature, place the sensor in contact with the heatsink

---

## Signal Integrity

### I2C Bus Capacitance

The total bus capacitance should be under 400 pF for standard-mode I2C (100 kHz). At 50 kHz, higher capacitance is tolerable, but rise time degrades.

Sources of bus capacitance:

| Source | Typical Capacitance |
|--------|-------------------|
| ESP32-C3 SDA/SCL pins | 10 pF each |
| Radxa I2C6 SDA/SCL pins | 10 pF each |
| Wire (10 cm) | 5 pF per cm |
| PCB trace (10 cm) | 1 pF per cm |
| Total (typical short bus) | 50-100 pF |

### Rise Time

The rise time depends on the pull-up resistor and bus capacitance:

```
t_rise = 0.8473 * R_pull * C_bus
```

For 2.2k ohm and 100 pF:

```
t_rise = 0.8473 * 2200 * 100e-12 = 186 ns
```

This is well within the 1000 ns limit for 100 kHz I2C and the 2000 ns limit for 50 kHz.

### Noise Immunity

1. Use twisted pair for SDA and SCL if wire length exceeds 20 cm
2. Add a 100 pF capacitor from SCL to GND near the slave if noise is present
3. Keep I2C traces away from switching regulators and PWM signals
4. Use a ground plane under I2C traces

---

## Bill of Materials

### Essential Components

| Component | Value | Quantity | Notes |
|-----------|-------|----------|-------|
| ESP32-C3FH4 | - | 1 | 4 MB internal flash, QFN-32 |
| Radxa ROCK 5C | - | 1 | I2C master |
| Pull-up resistor | 2.2k ohm | 2 | SDA and SCL to 3.3V |
| LED | 3 mm or 5 mm | 1 | Status indicator |
| LED resistor | 330 ohm | 1 | LED current limiting |

### Fan Controller Components (Example 03)

| Component | Value | Quantity | Notes |
|-----------|-------|----------|-------|
| NPN transistor | 2N2222 or BC547 | 1 | Fan PWM driver |
| Base resistor | 330 ohm | 1 | Transistor base |
| Temperature sensor | LM35 | 1 | Analog temperature |
| TPS25940 eFuse | - | 1 | Overcurrent protection |

### Optional Components

| Component | Value | Quantity | Notes |
|-----------|-------|----------|-------|
| Decoupling capacitor | 0.1uF ceramic | 2 | Near ESP32 VCC |
| Bulk capacitor | 10uF electrolytic | 1 | Power stability |
| Twisted pair wire | - | 1 m | I2C SDA/SCL (long runs) |

### Pull-up Resistor Alternatives

| Value | Quantity | Use Case |
|-------|----------|---------|
| 1.5k ohm | 2 | Long bus (50+ cm) or high capacitance |
| 2.2k ohm | 2 | Recommended (validated in this project) |
| 4.7k ohm | 2 | Short bus (under 10 cm) or low power |
