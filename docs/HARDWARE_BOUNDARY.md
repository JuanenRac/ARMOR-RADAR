# Hardware boundary

## What is tested, and where

| Part | Where it runs | Evidence |
|---|---|---|
| The core in `main/core` (framer, LD2450 decoder and command channel, track set, message serialiser, dew point and heater, day/night, static map, node id, JSON reader and writer, settings and their checks, pin table, network plan, panel users and sessions, mapped-pin logic) | Any computer with CMake and a C++17 compiler | `tests/test_core.cpp` and `tests/test_node.cpp` (`-Werror`) |
| The firmware's JSON against the published contract | A computer | `tests/emit_samples.cpp` piped into `tests/check_contract.py`, validated by ARMOR-COMMON |
| The web panel (pages, seven languages, login, set-up, saving, field problems, pins, radars, phone width) | A real browser (headless Edge) against `tools/panel_mock.mjs`, a stand-in for a node | a scripted session; no text key left untranslated, no console error |
| `main/*.cpp` outside `core` (UARTs, W5500, Wi-Fi and the bridge, HTTP server, OTA, NVS, PWM, ADC, I2C, MQTT, SNTP, tasks) | ESP-IDF 5.4 | **builds** in the ESP-IDF container (`tools/build_node.sh`); **runs on two real ESP32-S3-ETH nodes**: the HTTP and HTTPS panel, users, settings in flash, Ethernet, MQTT with an account per node, telemetry, the live map and the over-the-air update. Not yet recorded on a node: the Wi-Fi access point bridged to the cable, the light sensor, mapped pins and the PoE current |

Run `build-test` from ARMOR-COMMON's launcher (or the commands in the README) for the host part. Without ESP-IDF the run states that the
firmware image was **not** built; `tools/build_node.sh` builds it in the container. The first day on a board is
[BENCH_BRINGUP.md](BENCH_BRINGUP.md); the panel is described in [NODE_PANEL.md](NODE_PANEL.md).

## The two boards

The firmware is built for two boards, chosen when the image is built (`tools/build_node.sh NODE BOARD`, `dist/NODE-BOARD.bin`; the host tests build both profiles). **An image is for ONE board.**

| Profile | Board | Way in | Pins |
| --- | --- | --- | --- |
| `s3-eth` (the default) | Waveshare ESP32-S3-ETH | The cable (DHCP or fixed), the Wi-Fi access point (bridged or on its own), or Wi-Fi | GPIO 9 to 14 (W5500) and 8 (camera) reserved, 4 to 7 the microSD socket |
| `s3-wifi` | ESP32-S3-WROOM-1 N16R8 (16 MB flash, 8 MB octal PSRAM, two USB-C sockets), no Ethernet | Wi-Fi only: a station, plus the node's own network | GPIO 4 to 14 free; 43, 44 (the USB-serial socket) and 48 (the RGB LED) with a warning |

The `s3-wifi` image has no W5500 driver and no bridge, and the panel hides what the board does not have. The three radars, the light sensor and the mapped pins use the same default pins on both boards. **Nothing about the `s3-wifi` board has been tried.**

## The Waveshare board

The target is the **Waveshare ESP32-S3-ETH**: ESP32-S3R8 (dual core, 240 MHz, **8 MB of octal PSRAM**), a **16 MB** flash, the W5500 Ethernet
(RJ45, 10/100), a USB-C port (native USB: the log and the flashing), a microSD socket, a camera connector, a ceramic antenna (an IPEX
connector is available by moving a resistor), and PoE through a separate module on the board's PoE header. Consequences in the firmware:

* the flash is split into two application slots for over-the-air updates with rollback (`partitions.csv`), and the settings store;
* the PSRAM is enabled but **optional**: if it does not answer, the node starts without it (a wrong guess must not brick a board);
* GPIO 33 to 37 (PSRAM), 26 to 32 (flash), 19 and 20 (USB), 9 to 14 (Ethernet) and 8 (camera) are never offered as pins; 0, 3, 45 and 46
  (read while the chip starts) come with a warning; 4 to 7 are the microSD socket. This table is `main/core/board_pins.hpp`, written from the
  manufacturer's pin map and the chip's datasheet. GPIO 33 and 34 are kept out although the datasheet is only certain about 35 to 37;
* the camera connector shares its pins with the header: which ones is not in the documents at hand, so a wiring that uses a camera must be
  checked against the schematic;
* the radars get 5 V from VSYS, not VBUS, and the PoE module's current has not been measured.

## The three radars

The three LD2450 modules need independent hardware UARTs, so all three ESP32-S3 UARTs are used (the chip has three, so no software UART is
needed). UART0 is the default console, therefore `sdkconfig.defaults` moves the console to the native USB Serial/JTAG peripheral. Confirm
on the real board that its USB connector is wired to the ESP32-S3 native USB before relying on this. The pins are settings of the node: RX
16, 17, 18 and TX 15, 21, 38 by default; the node refuses settings where a pin repeats or clashes with a reserved one.

The VEML7700 is read on I2C (GPIO 1 and 2 by default), its interrupt pin is not used. The AHT20 is not read: the message contract has no
field for it. The node fails closed: without a light value it withholds telemetry, unless the settings hold an explicit bench value.

## The LD2450: report frame and command channel

`main/core/frame_framer.hpp` recovers frames from a noisy byte stream (header, length, tail, resynchronisation) and knows nothing about
their content. `main/core/ld2450.hpp` is the decoder for the **HLK-LD2450** report frame, written from the Hi-Link *Instruction manual
V1.00*, section 6, and nothing else: UART 256000 8N1, a 30-byte frame with the header `AA FF 03 00`, three 8-byte targets (x, y, speed,
distance resolution; bit 15 set means positive) and the tail `55 CC`, ten times a second. The manual's own worked example is a test and
decodes to exactly the values the manual gives (x = -782 mm, y = 1713 mm, speed = -16 cm/s, gate 320 mm), and the JSON built from it is
accepted by ARMOR-COMMON.

`main/core/ld2450_command.hpp` builds the **configuration commands** and reads their acknowledgements (enter and leave configuration, one or
three targets, zones, Bluetooth, restart, factory reset, firmware version, the serial speed). That channel is **not in the manual above**: it
follows the manufacturer's separate serial protocol as it is publicly documented and as community drivers implement it. It has not been
checked against the manufacturer's document nor against a module, the panel says so, shows the module's raw answer, and offers no change
of the serial speed (a wrong index would leave the module unreachable).

What the manual does **not** say, and the code therefore does not claim:

* whether a target keeps its slot (1 to 3) from frame to frame: the slot number is used as the track id only because nothing better exists,
  so a track's identity across frames is **unproven**;
* the orientation of the axes beyond the example (the manual shows a wall-mounted radar with a detection range of 6 m and an azimuth of plus
  or minus 60 degrees);
* anything about the **LD2461**: the documents available are for the LD2450 only.

No frame has been captured from a real module yet. The first thing to do on the bench is to record real UART bytes into a test fixture and
check them against this decoder, and to read the module's firmware version from the panel.

The firmware withholds telemetry until an ambient-light reading exists, because the contract requires `lux` and a made-up value would mislead
the server's day/night decisions. It also withholds it while no radar is reporting, so silent radars never look like a quiet perimeter.

## Identity, time and offline detection

* The identity of a node is a setting (`armor-` and six digits of its MAC until it is changed); every node needs its own identity and broker ACL.
* Message timestamps are wall-clock milliseconds from SNTP, not uptime: the server ignores a message older than the last one it accepted from a
  node, so an uptime clock would silence a rebooted node for as long as it had run before. The node publishes nothing until its clock is set.
* The MQTT last will announces `online: false` on the node's health topic if it disappears. Its timestamp cannot be newer than the node's last
  message, so the server always applies an offline message and never lets it move the stored time backwards.
* An output pin whose safe state is set falls back to it when the broker has been unreachable for the time chosen; it stays there until the server
  sends a command.

## Not decided or not proven

The PoE power path and its current, the TLS trust anchor (a CA certificate at `certs/ca.pem` is embedded when it exists) and TLS for the panel,
real captured radar frames, the command channel against a real module, the LD2461, the Wi-Fi access point's throughput and roaming, the bridge
between the wire and the access point on a real board, and everything that needs the physical board. See
[ARMOR-HARDWARE](https://github.com/JuanenRac/ARMOR-HARDWARE) for the enclosure and its validation boundary.
