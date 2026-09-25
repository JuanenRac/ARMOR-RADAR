# Hardware boundary

## What is tested, and where

| Part | Where it runs | Evidence |
|---|---|---|
| The core in `main/core` (framer, LD2450 decoder, track set, message serialiser, dew point and heater, day/night, static map, node id) | Any computer with CMake and a C++17 compiler | `tests/test_core.cpp` (167 checks, `-Werror`) |
| The firmware's JSON against the published contract | A computer | `tests/emit_samples.cpp` piped into `tests/check_contract.py`, validated by ARMOR-COMMON |
| `main/app_main.cpp`, `board_ethernet.cpp`, `light_sensor.cpp` (UARTs, W5500, I2C, MQTT, SNTP, tasks) | ESP-IDF 5.4 | **builds** in the `espressif/idf:v5.4.2` container (`tools/build_node.sh`); **never run on a board** |

Run `build-test` from ARMOR-COMMON's launcher (or the commands in the README) for
the host part. Without ESP-IDF the run states that the firmware image was **not**
built; `tools/build_node.sh` builds it in the container. The first day on a board is
[BENCH_BRINGUP.md](BENCH_BRINGUP.md).

## The three radars

The three LD2450/LD2461 modules need independent hardware UARTs, so all three ESP32-S3
UARTs are used. UART0 is the default console, therefore `sdkconfig.defaults` moves the
console to the native USB Serial/JTAG peripheral. Confirm on the real board that its USB
connector is wired to the ESP32-S3 native USB before relying on this. Duplicate RX pins
are refused at start.

The target board is the **Waveshare ESP32-S3-ETH**: Ethernet is a W5500 on SPI (MOSI 11, MISO 12, SCLK 13, CS 14, INT 10, RST 9, from the manufacturer's pin table), GPIO 4 to 7 belong to the SD-card socket, and the default radar receive pins are GPIO 16, 17 and 18. All of them are menuconfig values, and the node refuses to start if a radar pin repeats or clashes with the Ethernet or I2C pins.

The VEML7700 is read on I2C (GPIO 1 and 2 by default), its interrupt pin is not used. The AHT20 is not read: the message contract has no field for it. Firmware must fail closed if a configured UART or I2C device cannot be initialised: without a light value the node withholds telemetry, unless `CONFIG_ARMOR_LUX_FALLBACK` sets an explicit bench value.

## The LD2450 frame decoder

`main/core/frame_framer.hpp` recovers frames from a noisy byte stream (header, length,
tail, resynchronisation) and knows nothing about their content. `main/core/ld2450.hpp` is
the decoder for the **HLK-LD2450**, written from the Hi-Link *Instruction manual V1.00*
(2023-05-10), section 6, and nothing else: UART 256000 8N1, a 30-byte frame with the header
`AA FF 03 00`, three 8-byte targets (x, y, speed, distance resolution; bit 15 set means
positive) and the tail `55 CC`, ten times a second. The manual's own worked example is a
test and decodes to exactly the values the manual gives (x = -782 mm, y = 1713 mm,
speed = -16 cm/s, gate 320 mm), and the JSON built from it is accepted by ARMOR-COMMON.

What the manual does **not** say, and the code therefore does not claim:

* whether a target keeps its slot (1 to 3) from frame to frame: the slot number is used as
  the track id only because nothing better exists, so a track's identity across frames is
  **unproven**;
* the orientation of the axes beyond the example (the manual shows a wall-mounted radar
  with a detection range of 6 m and an azimuth of plus or minus 60 degrees);
* the module's configuration commands, which are not in this manual and are not implemented;
* anything about the **LD2461**: the documents available are for the LD2450 only.

No frame has been captured from a real module yet. The first thing to do on the bench is to
record real UART bytes into a test fixture and check them against this decoder.

The firmware withholds telemetry until an ambient-light reading exists, because the contract
requires `lux` and a made-up value would mislead the server's day/night decisions. It also
withholds it while no radar is reporting, so silent radars never look like a quiet perimeter.

## Identity, time and offline detection

* The node refuses to start with an invalid or placeholder identity
  (`CONFIG_ARMOR_NODE_ID`), because every node needs its own identity and broker ACL.
* Message timestamps are wall-clock milliseconds from SNTP, not uptime: the server ignores a
  message older than the last one it accepted from a node, so an uptime clock would silence a
  rebooted node for as long as it had run before. The node publishes nothing until its clock
  is set.
* The MQTT last will announces `online: false` on the node's health topic if it disappears.
  Its timestamp cannot be newer than the node's last message, so the server always applies an
  offline message and never lets it move the stored time backwards.

## Not decided or not proven

The PoE power path, the TLS trust anchor (a CA certificate at `certs/ca.pem` is embedded when it exists), real captured radar frames, the radar modules' own configuration, the LD2461, over-the-air updates, and everything that needs the physical board. See
[ARMOR-HARDWARE](../../ARMOR-HARDWARE) for the enclosure and its validation boundary.
