# Radxa ROCK 5C Configuration

Configuration guide for setting up the Radxa ROCK 5C as an I2C master for ESP32-C3 slave communication.

---

## Table of Contents

- [Overview](#overview)
- [Hardware Setup](#hardware-setup)
- [I2C Tools Installation](#i2c-tools-installation)
- [Bus Discovery](#bus-discovery)
- [Device Tree Overlay (50 kHz)](#device-tree-overlay-50-khz)
- [Verification](#verification)
- [Quick Test](#quick-test)
- [Restoring Original Configuration](#restoring-original-configuration)
- [Files in This Directory](#files-in-this-directory)

---

## Overview

The Radxa ROCK 5C (RK3588S) serves as the I2C master. It communicates with the ESP32-C3FH4 slave over I2C bus 6 (`/dev/i2c-6`) at address `0x08`.

The default I2C bus frequency may be 100 kHz or higher. For ESP32-C3 slave reliability, a device tree overlay can reduce the frequency to 50 kHz.

---

## Hardware Setup

| Parameter | Value |
|-----------|-------|
| Master | Radxa ROCK 5C |
| I2C Adapter | /dev/i2c-6 |
| Slave | ESP32-C3FH4 |
| Slave Address | 0x08 (7-bit) |
| SDA | Radxa I2C6 SDA pin to ESP32-C3 GPIO4 |
| SCL | Radxa I2C6 SCL pin to ESP32-C3 GPIO5 |
| GND | Common ground (required) |
| Pull-up SDA | 2.2k ohm to 3.3V |
| Pull-up SCL | 2.2k ohm to 3.3V |

### Wiring

```
Radxa ROCK 5C              ESP32-C3FH4
---------------             ------------
I2C6 SDA          ------>  GPIO4 (SDA)
I2C6 SCL          ------>  GPIO5 (SCL)
GND               ------>  GND
```

External pull-up resistors of 2.2k ohm are required on both SDA and SCL. Acceptable range is 1.5k to 4.7k ohm.

---

## I2C Tools Installation

Check if i2c-tools are installed:

```bash
command -v i2cdetect
command -v i2ctransfer
```

If not installed:

```bash
sudo apt update
sudo apt install -y i2c-tools
```

---

## Bus Discovery

List all I2C adapters:

```bash
ls -l /dev/i2c-*
i2cdetect -l
```

Confirm adapter 6 exists:

```bash
ls -l /dev/i2c-6
```

---

## Device Tree Overlay (50 kHz)

### Why 50 kHz

The ESP32-C3 I2C slave does not support hardware clock stretching. At higher bus speeds, the slave may miss address phases or fail to prepare responses in time. Reducing to 50 kHz improves reliability.

### Overlay File

The overlay source is in `i2c6-50k-overlay.dts`. It sets the `clock-frequency` property on the I2C6 node.

### Compile

```bash
sudo apt install -y device-tree-compiler
dtc -@ -I dts -O dtbo -o i2c6-50k-overlay.dtbo i2c6-50k-overlay.dts
```

### Install

Find the overlay directory:

```bash
find /boot -type d -iname '*overlay*' -print
find /boot -type f -name '*.dtbo' -print
```

Copy the compiled overlay (adjust the path based on your system):

```bash
sudo cp i2c6-50k-overlay.dtbo /boot/dtb/rockchip/overlay/
```

### Enable

Add the overlay to the boot configuration. The exact file depends on your distribution:

**For Armbian-based systems:**

```bash
sudo nano /boot/armbianEnv.txt
# Add or append to: user_overlays=i2c6-50k-overlay
```

**For extlinux-based systems:**

```bash
sudo nano /boot/extlinux/extlinux.conf
# Add or append to FDTOVERLAYS line:
# FDTOVERLAYS /boot/dtb/rockchip/overlay/i2c6-50k-overlay.dtbo
```

If `user_overlays` or `FDTOVERLAYS` already contains other overlays, append the new overlay name rather than replacing the entire line.

### Reboot

```bash
sudo reboot
```

---

## Verification

After reboot, verify the configuration:

### Step 1: Confirm adapter exists

```bash
ls -l /dev/i2c-6
```

### Step 2: Find the device tree node

```bash
NODE=$(readlink -f /sys/class/i2c-adapter/i2c-6/device/of_node)
echo "$NODE"
```

### Step 3: Check the frequency

```bash
od -An -tu4 "$NODE/clock-frequency" 2>/dev/null || echo "No explicit clock-frequency property"
```

If the output is `50000`, the overlay is active. If the property does not exist, the driver is using its default frequency.

### Step 4: Scan the bus

```bash
sudo i2cdetect -y 6
```

You should see `08` in the grid:

```
     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f
00:          -- -- -- -- -- 08 -- -- -- -- -- -- --
```

### Step 5: Test a transaction

```bash
sudo i2ctransfer -y 6 w2@0x08 0x01 0x99
sleep 0.50
sudo i2ctransfer -y 6 r6@0x08
```

Expected:

```diff
+ 0xa5 0x00 0x99 0x11 0x01 0x2c
```

---

## Quick Test

Use the test script for automated testing:

```bash
chmod +x test_i2c.sh

# Single transaction test
./test_i2c.sh single

# 10-cycle test (default)
./test_i2c.sh cycle

# 100-cycle stress test
./test_i2c.sh stress

# Full command set (requires Fan Controller firmware)
./test_i2c.sh full
```

---

## Restoring Original Configuration

### Back Up First

```bash
sudo cp -a /boot "/boot.backup-before-i2c6-restore-$(date +%Y%m%d-%H%M%S)"
```

### Find the Overlay Reference

```bash
grep -RniE 'i2c6|clock-frequency|50000|50k|i2c6-50k|dtbo|overlay' \
    /boot /boot/firmware /etc 2>/dev/null
```

### Remove the Overlay

**For Armbian-based systems:**

Edit `/boot/armbianEnv.txt` and remove `i2c6-50k-overlay` from the `user_overlays` line. Keep all other overlays intact.

```bash
sudo nano /boot/armbianEnv.txt
```

**For extlinux-based systems:**

Edit `/boot/extlinux/extlinux.conf` and remove the I2C6 overlay path from the `FDTOVERLAYS` line.

```bash
sudo nano /boot/extlinux/extlinux.conf
```

### Reboot and Verify

```bash
sudo reboot
```

After reboot:

```bash
ls -l /dev/i2c-6
NODE=$(readlink -f /sys/class/i2c-adapter/i2c-6/device/of_node)
od -An -tu4 "$NODE/clock-frequency" 2>/dev/null || echo "No explicit clock-frequency property"
```

If the property no longer exists or shows a different value, the overlay has been successfully removed. The bus will use the driver default frequency.

---

## Files in This Directory

| File | Description |
|------|-------------|
| `README.md` | This file - configuration overview |
| `PROCEDURE.md` | Detailed step-by-step procedure including diagnostics, backup, restore, and error interpretation |
| `test_i2c.sh` | Automated test script with single, cycle, stress, and full modes |
| `i2c6-50k-overlay.dts` | Device tree overlay source for 50 kHz I2C6 frequency |

---

## Important Notes

- The `clock-frequency` device tree property is a **requested** value. The actual SCL frequency may differ due to clock divider rounding, bus capacitance, and pull-up resistor values. Use a logic analyzer or oscilloscope to verify the physical SCL waveform.
- `i2cdetect -y 6` scans the bus. It does not set the bus frequency.
- `i2ctransfer -y 6 ...` performs a transaction. It does not set the persistent bus frequency.
- The 50 kHz setting is applied through the device tree at boot time, not through i2c-tools commands.
- Always back up `/boot` before modifying boot configuration. See `PROCEDURE.md` for the full backup and restore procedure.
