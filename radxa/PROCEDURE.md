# Radxa ROCK 5C I2C6 50 kHz Procedure

This document records the commands and procedure used to investigate I2C bus 6 on the Radxa ROCK 5C and configure its requested bus frequency to 50 kHz for ESP32-C3 slave testing.

This is a test configuration. It is not proof that the physical SCL waveform is exactly 50 kHz. A logic analyzer or oscilloscope is required to verify the actual SCL frequency.

---

## Table of Contents

- [Purpose](#purpose)
- [Hardware](#hardware)
- [Protocol Used](#protocol-used)
- [Important Transaction Rule](#important-transaction-rule)
- [Install Diagnostic Tools](#install-diagnostic-tools)
- [Initial Bus Scan](#initial-bus-scan)
- [Test the Write Transaction](#test-the-write-transaction)
- [Test the Read Transaction](#test-the-read-transaction)
- [Error Meanings](#error-meanings)
- [Find the Active Device-Tree Node](#find-the-active-device-tree-node)
- [Locate Clock-Frequency Properties](#locate-clock-frequency-properties)
- [Inspect the Live Device Tree](#inspect-the-live-device-tree)
- [Device-Tree Overlay Source](#device-tree-overlay-source)
- [Compile the Overlay](#compile-the-overlay)
- [Find Boot Configuration](#find-boot-configuration)
- [Back Up Boot Configuration](#back-up-boot-configuration)
- [Add an Overlay (Armbian)](#add-an-overlay-armbian)
- [Add an Overlay (extlinux)](#add-an-overlay-extlinux)
- [Apply the Overlay](#apply-the-overlay)
- [Validate Bus Operation](#validate-bus-operation)
- [Measure Actual SCL Frequency](#measure-actual-scl-frequency)
- [Restore Original I2C6 Configuration](#restore-original-i2c6-configuration)
- [Restore from Backup if Boot Fails](#restore-from-backup-if-boot-fails)
- [Useful Diagnostic Commands](#useful-diagnostic-commands)
- [ESP32-Side Information](#esp32-side-information)
- [Final Conclusion from Testing](#final-conclusion-from-testing)
- [Recommended Next Investigation](#recommended-next-investigation)

---

## Purpose

This document records the commands and procedure used to investigate I2C bus 6 on the Radxa ROCK 5C and configure its requested bus frequency to 50 kHz for ESP32-C3 slave testing.

This is a test configuration. It is not proof that the physical SCL waveform is exactly 50 kHz. A logic analyzer or oscilloscope is required to verify the actual SCL frequency.

---

## Hardware

- Master: Radxa ROCK 5C.
- Linux adapter: `/dev/i2c-6`.
- Slave: ESP32-C3FH4.
- Slave address: `0x08`.
- SDA: ESP32-C3 GPIO4.
- SCL: ESP32-C3 GPIO5.
- Pull-up resistors: 2.2k ohm on SDA and SCL to 3.3V.
- ESP32 heartbeat LED: GPIO2 (not GPIO8).

---

## Protocol Used

The Radxa writes exactly two bytes:

```text
01 command
```

Initialization command:

```text
01 99
```

Test command:

```text
01 2A
```

The ESP32 response is six bytes:

```text
A5 status command response sequence checksum
```

The checksum is:

```text
marker ^ status ^ command ^ response ^ sequence
```

Expected initialization response:

```text
A5 00 99 11 sequence checksum
```

Expected test-command response:

```text
A5 02 2A 2B sequence checksum
```

---

## Important Transaction Rule

Always use one complete write followed by one read:

```text
write exactly two bytes
wait for the slave
read exactly six bytes
```

Do not perform repeated reads without a new write. The ESP32 slave TX path is FIFO-based, so repeated reads can consume the queued frame and expose stale or empty-FIFO data.

---

## Install Diagnostic Tools

Check whether the I2C tools are installed:

```bash
command -v i2cdetect
command -v i2ctransfer
```

On Debian/Ubuntu-based systems, install them with:

```bash
sudo apt update
sudo apt install -y i2c-tools
```

List I2C adapters:

```bash
ls -l /dev/i2c-*
i2cdetect -l
```

Confirm adapter 6:

```bash
ls -l /dev/i2c-6
```

---

## Initial Bus Scan

Scan bus 6:

```bash
sudo i2cdetect -y 6
```

A valid ESP32 slave may appear at `0x08`:

```text
00: 08 -- -- -- ...
```

If the address does not appear, do not continue with transfer testing yet. Check ESP32 power, firmware, SDA/SCL wiring, pull-ups, pin assignment, and whether the slave firmware is running.

Repeated scans were used during debugging:

```bash
sudo i2cdetect -y 6
```

The observed result was intermittent detection of `0x08`. This indicates that the ESP32 slave was not maintaining a stable ACK response.

---

## Test the Write Transaction

Send initialization:

```bash
sudo i2ctransfer -y 6 w2@0x08 0x01 0x99
```

Send the test command:

```bash
sudo i2ctransfer -y 6 w2@0x08 0x01 0x2a
```

A successful write normally produces no terminal output.

---

## Test the Read Transaction

Wait before reading:

```bash
sleep 0.50
```

Read exactly six bytes:

```bash
sudo i2ctransfer -y 6 r6@0x08
```

Expected initialization format:

```text
0xa5 0x00 0x99 0x11 <sequence> <checksum>
```

Expected `0x2A` format:

```text
0xa5 0x02 0x2a 0x2b <sequence> <checksum>
```

Use the complete sequence as one shell block:

```bash
sudo i2ctransfer -y 6 w2@0x08 0x01 0x99
sleep 0.50
sudo i2ctransfer -y 6 r6@0x08
```

For the second command:

```bash
sudo i2ctransfer -y 6 w2@0x08 0x01 0x2a
sleep 0.50
sudo i2ctransfer -y 6 r6@0x08
```

Do not run this repeatedly without a new write:

```bash
sudo i2ctransfer -y 6 r6@0x08
```

---

## Error Meanings

This error:

```text
Error: Sending messages failed: No such device or address
```

means the master did not receive an ACK from address `0x08` during that transaction. Possible causes include an inactive ESP32 slave, native-driver state loss, bus electrical problems, or a slave TX/RX state that is not armed correctly.

Frames beginning with `0xA5` are structurally aligned. Frames beginning with values such as `0xEE`, `0x01`, `0xE1`, or `0x04` are stale, shifted, invalid, or otherwise not valid protocol frames.

Examples observed during debugging:

```text
0xa5 0xee 0x00 0xe1 0x01 0xab
0xee 0x00 0xe1 0x02 0xa8 0xa5
0x01 0xff 0xff 0xff 0xff 0xff
0xe1 0x03 0xa9 0xa5 0xee 0x00
0x04 0xae 0xdf 0xff 0xff 0xff
```

These results confirmed that lowering the bus frequency did not by itself fix the ESP32 slave FIFO/state problem.

---

## Find the Active Device-Tree Node

Resolve the device-tree node used by Linux adapter 6:

```bash
readlink -f /sys/class/i2c-adapter/i2c-6/device/of_node
```

Store it in a shell variable:

```bash
NODE=$(readlink -f /sys/class/i2c-adapter/i2c-6/device/of_node)
echo "$NODE"
```

Inspect the adapter information:

```bash
udevadm info -q property -p /sys/class/i2c-adapter/i2c-6
```

Inspect the device-tree node directory:

```bash
ls -la "$NODE"
```

---

## Locate Clock-Frequency Properties

Search the live device tree:

```bash
find /proc/device-tree -type f -name clock-frequency -print
```

Read all discovered frequency properties:

```bash
for f in $(find /proc/device-tree -type f -name clock-frequency); do
    echo "FILE: $f"
    od -An -tu4 "$f"
done
```

Read the frequency for the active bus-6 node:

```bash
NODE=$(readlink -f /sys/class/i2c-adapter/i2c-6/device/of_node)
echo "$NODE"
od -An -tu4 "$NODE/clock-frequency" 2>/dev/null || echo "No explicit clock-frequency property"
```

If the output is approximately:

```text
50000
```

the requested 50 kHz property is active for that node.

If the property does not exist, Linux is using the driver/board default rather than an explicit device-tree value.

---

## Inspect the Live Device Tree

If `dtc` is installed:

```bash
command -v dtc
```

Dump the running device tree:

```bash
sudo dtc -I fs -O dts /proc/device-tree > ~/live-device-tree.dts
```

Search for likely I2C6 nodes:

```bash
grep -n -i -E 'i2c6|fec80000|clock-frequency' ~/live-device-tree.dts | head -200
```

Search around the RK3588 I2C controller address that was investigated:

```bash
grep -n -A30 -B20 -i -E 'fec80000|i2c6' ~/live-device-tree.dts | head -250
```

The extracted live tree contained I2C6 pinctrl groups such as:

```text
i2c6
i2c6m4-xfer
i2c6m3-xfer
i2c6m1-xfer
i2c6m0-xfer
```

The live-tree dump also printed many unrelated warnings because it was generated from the running device tree. Those warnings do not automatically indicate an I2C6 failure.

---

## Device-Tree Overlay Source

The intended overlay change was:

```dts
/dts-v1/;
/plugin/;

&i2c6 {
    clock-frequency = <50000>;
};
```

If the board device tree does not expose the label `i2c6`, target the controller by its unit address instead. The exact unit address must be confirmed from the base device tree. Do not guess it.

A fragment-style overlay can be used when the target path/label is known:

```dts
/dts-v1/;
/plugin/;

/ {
    compatible = "rockchip,rk3588";
};

fragment@0 {
    target = <&i2c6>;
    __overlay__ {
        clock-frequency = <50000>;
    };
};
```

Save as:

```text
i2c6-50k-overlay.dts
```

---

## Compile the Overlay

Install the device-tree compiler if necessary:

```bash
sudo apt update
sudo apt install -y device-tree-compiler
```

Compile source to overlay blob:

```bash
dtc -@ -I dts -O dtb \
    -o i2c6-50k-overlay.dtbo \
    i2c6-50k-overlay.dts
```

Check the result:

```bash
ls -lh i2c6-50k-overlay.dts i2c6-50k-overlay.dtbo
file i2c6-50k-overlay.dtbo
```

Inspect strings in the blob:

```bash
strings i2c6-50k-overlay.dtbo | head -100
```

Important: a compiled `.dtbo` file existing on disk does not prove that the bootloader is loading it.

---

## Find Boot Configuration

Inspect boot directories:

```bash
ls -la /boot
ls -la /boot/firmware 2>/dev/null
ls -la /boot/extlinux 2>/dev/null
ls -la /boot/dtb 2>/dev/null
```

Search boot configuration for overlays and 50 kHz settings:

```bash
grep -RniE 'i2c6|clock-frequency|50000|50k|dtbo|overlay|FDTOVERLAYS' \
    /boot /boot/firmware /etc 2>/dev/null
```

Common files to inspect:

```text
/boot/armbianEnv.txt
/boot/extlinux/extlinux.conf
/boot/firmware/extlinux/extlinux.conf
/boot/uEnv.txt
/boot/boot.cmd
/boot/boot.scr
```

Do not edit or delete an unknown boot entry until it is backed up.

---

## Back Up Boot Configuration

Create a backup:

```bash
sudo cp -a /boot "/boot.backup-before-i2c6-change-$(date +%Y%m%d-%H%M%S)"
```

If `/boot/firmware` exists separately:

```bash
sudo cp -a /boot/firmware "/boot-firmware.backup-before-i2c6-change-$(date +%Y%m%d-%H%M%S)"
```

---

## Add an Overlay (Armbian)

If the system uses `/boot/armbianEnv.txt`, inspect it:

```bash
sudo sed -n '1,200p' /boot/armbianEnv.txt
```

Copy the overlay into the board overlay directory only if that is the directory used by the current boot configuration. Find candidate directories:

```bash
find /boot -type d -iname '*overlay*' -print
find /boot -type f -name '*.dtbo' -print
```

Example copy command after identifying the correct directory:

```bash
sudo cp i2c6-50k-overlay.dtbo /boot/dtb/rockchip/overlay/
```

Add the overlay name to the existing overlay list, preserving unrelated overlays. Example:

```text
user_overlays=i2c6-50k-overlay
```

If `user_overlays` already contains other overlays, append rather than replace them. The exact syntax depends on the installed boot environment, so inspect the existing file before editing.

Edit:

```bash
sudo nano /boot/armbianEnv.txt
```

---

## Add an Overlay (extlinux)

Inspect the configuration:

```bash
sudo sed -n '1,240p' /boot/extlinux/extlinux.conf
```

or:

```bash
sudo sed -n '1,240p' /boot/firmware/extlinux/extlinux.conf
```

Look for:

```text
FDT
FDTOVERLAYS
```

An overlay entry may look like:

```text
FDTOVERLAYS /boot/dtb/rockchip/overlay/i2c6-50k-overlay.dtbo
```

If an existing `FDTOVERLAYS` line contains other overlays, append the new overlay according to the syntax already in use. Do not replace the complete line blindly.

Edit:

```bash
sudo nano /boot/extlinux/extlinux.conf
```

or:

```bash
sudo nano /boot/firmware/extlinux/extlinux.conf
```

---

## Apply the Overlay

After adding the overlay to the active boot configuration:

```bash
sudo reboot
```

After reboot, check that adapter 6 still exists:

```bash
ls -l /dev/i2c-6
```

Check the active device-tree node:

```bash
NODE=$(readlink -f /sys/class/i2c-adapter/i2c-6/device/of_node)
echo "$NODE"
```

Check the requested frequency:

```bash
od -An -tu4 "$NODE/clock-frequency" 2>/dev/null || echo "No explicit clock-frequency property"
```

Scan the bus:

```bash
sudo i2cdetect -y 6
```

---

## Validate Bus Operation

First scan once:

```bash
sudo i2cdetect -y 6
```

Then run one write/read transaction:

```bash
sudo i2ctransfer -y 6 w2@0x08 0x01 0x99
sleep 0.50
sudo i2ctransfer -y 6 r6@0x08
```

Repeat only as a new complete transaction:

```bash
sudo i2ctransfer -y 6 w2@0x08 0x01 0x2a
sleep 0.50
sudo i2ctransfer -y 6 r6@0x08
```

A logic analyzer should be connected to SDA and SCL to verify:

- SCL frequency is approximately 50 kHz.
- SDA/SCL idle high.
- Correct address byte for `0x08`.
- ACK after the slave address.
- ACK after both write bytes.
- A STOP between write and read if using separate commands.
- Correct six-byte read sequence.

---

## Measure Actual SCL Frequency

Software validation:

```bash
NODE=$(readlink -f /sys/class/i2c-adapter/i2c-6/device/of_node)
od -An -tu4 "$NODE/clock-frequency" 2>/dev/null || true
```

Electrical validation requires a logic analyzer or oscilloscope. Measure the period of SCL. At 50 kHz:

```text
period = 20 microseconds
```

The property value and actual waveform can differ because of controller divider rounding, driver behavior, clock stretching, and bus timing.

---

## Restore Original I2C6 Configuration

Back up first:

```bash
sudo cp -a /boot "/boot.backup-before-i2c6-restore-$(date +%Y%m%d-%H%M%S)"
```

Find the custom reference:

```bash
grep -RniE 'i2c6|clock-frequency|50000|50k|i2c6-50k|dtbo|overlay' \
    /boot /boot/firmware /etc 2>/dev/null
```

If `/boot/armbianEnv.txt` contains a custom overlay, remove only the custom overlay name:

```bash
sudo nano /boot/armbianEnv.txt
```

If `extlinux.conf` contains a custom `FDTOVERLAYS` entry, remove only the I2C6 50 kHz `.dtbo` path:

```bash
sudo nano /boot/extlinux/extlinux.conf
```

or:

```bash
sudo nano /boot/firmware/extlinux/extlinux.conf
```

Do not remove unrelated overlays.

Reboot:

```bash
sudo reboot
```

Verify that bus 6 still exists:

```bash
ls -l /dev/i2c-6
```

Verify that the explicit 50 kHz property is gone:

```bash
NODE=$(readlink -f /sys/class/i2c-adapter/i2c-6/device/of_node)
echo "$NODE"
od -An -tu4 "$NODE/clock-frequency" 2>/dev/null || echo "No explicit clock-frequency property"
```

If the custom overlay is no longer referenced, it is safe to leave the `.dtbo` file unused. Delete it only after confirming that no boot file references it:

```bash
grep -Rni 'i2c6-50k-overlay' /boot /boot/firmware /etc 2>/dev/null || true
```

Then optionally remove it:

```bash
sudo rm -f /boot/dtb/rockchip/overlay/i2c6-50k-overlay.dtbo
```

Use the actual path found on the system.

---

## Restore from Backup if Boot Fails

If the system fails to boot normally, use the boot/recovery method available for the ROCK 5C and restore the saved boot directory/configuration. Do not overwrite the complete boot directory unless the backup corresponds to the same system state.

For a normal running system, restore a specific configuration file rather than all of `/boot`:

```bash
sudo cp /boot.backup-before-i2c6-restore-YYYYMMDD-HHMMSS/armbianEnv.txt /boot/armbianEnv.txt
```

Adjust the backup path and filename to the actual values.

---

## Useful Diagnostic Commands

Check kernel messages if permissions allow:

```bash
sudo dmesg | grep -i -E 'i2c|rockchip|dtb|overlay|device tree'
```

Check the adapter name:

```bash
cat /sys/class/i2c-adapter/i2c-6/name
```

Check adapter metadata:

```bash
udevadm info -q all -p /sys/class/i2c-adapter/i2c-6
```

Check GPIO line ownership if `gpiod` tools are installed:

```bash
gpioinfo
```

Check all I2C devices:

```bash
find /sys/bus/i2c/devices -maxdepth 1 -type l -print
```

---

## ESP32-Side Information

Arduino board package:

```text
Arduino-ESP32 3.3.8
```

Native header path:

```text
C:\Users\damik\AppData\Local\Arduino15\packages\esp32\tools\esp32c3-libs\3.3.8\include\esp_driver_i2c\include\driver\i2c_slave.h
```

The ESP32 sketches evolved through these API corrections:

- Removed `Wire.h` (when using native driver).
- Removed `callbacks.on_request` because it does not exist in the installed API.
- Replaced unsupported `i2c_slave_write()` with `i2c_slave_transmit()`.
- Used the installed three-argument `i2c_slave_receive()`.
- Removed unsupported `receive_buf_depth`.
- Removed unsupported `enable_internal_pullup`.
- Replaced `callbacks.on_receive` with `callbacks.on_recv_done`.
- Removed `eventData->length` because the installed event structure has no such member.

The final working sketch uses the Arduino `Wire` library with `Wire.write()` in `onRequest()`, fixed two-byte command validation, a six-byte response frame, and diagnostics for invalid frames, TX errors, receive-arm errors, and queue errors.

---

## Final Conclusion from Testing

The 50 kHz change was useful as a controlled experiment, but it did not make the ESP32-C3 native slave reliable on its own. The observed problems continued:

- Intermittent `0x08` detection.
- `No such device or address`.
- Stale/shifted response bytes.
- `0xFF` bytes when the ESP32 TX FIFO was empty or misaligned.

The root cause was identified as the use of `Wire.slaveWrite()` instead of `Wire.write()` in the `onRequest()` callback. After applying the fix, 20 consecutive transactions succeeded without any shifted bytes or errors.

The 50 kHz device tree overlay remains a useful configuration for reducing bus timing stress, but the primary fix was the software change.

Keep UART as the authoritative Radxa-to-fan-controller path while I2C remains a diagnostic and status monitoring interface.

---

## Recommended Next Investigation

1. Inspect the exact installed `i2c_slave.h` definitions.
2. Confirm the exact lifetime and semantics of `eventData->buffer`.
3. Build a minimal native ESP-IDF slave test with no fan-control code.
4. Test one write followed by one read only.
5. Capture SDA/SCL with a logic analyzer.
6. Confirm actual SCL frequency rather than relying only on the device-tree property.
7. Restore the original bus frequency after testing if 50 kHz is no longer needed.
