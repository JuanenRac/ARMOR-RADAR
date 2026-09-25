# Hardware boundary

## What is tested, and where

| Part | Where it runs | Evidence |
|---|---|---|
| The core in `main/core` (framer, LD2450 decoder, track set, message serialiser, dew point and heater, day/night, static map, node id) | Any computer with CMake and a C++17 compiler | `tests/test_core.cpp` (124 checks, `-Werror`) |
| The firmware's JSON against the published contract | A computer | `tests/emit_samples.cpp` piped into `tests/check_contract.py`, validated by ARMOR-COMMON |
| `main/app_main.cpp` (UARTs, MQTT, SNTP, tasks) | ESP-IDF 5.x on a board | **not compiled here**; build it with `idf.py build` before use |

Run `build-test` from ARMOR-COMMON's launcher (or the commands in the README) for
the host part. Without ESP-IDF the run states that the firmware image was **not**
built.

## The three radars

The three LD2450/LD2461 modules need independent hardware UARTs, so all three ESP32-S3
UARTs are used. UART0 is the default console, therefore `sdkconfig.defaults` moves the
console to the native USB Serial/JTAG peripheral. Confirm on the real board that its USB
connector is wired to the ESP32-S3 native USB before relying on this. Duplicate RX pins
are refused at start.

The AHT20 and VEML7700 share I2C. The VEML7700 interrupt pin is intentionally not assumed
until the final ESP32-S3-ETH-PoE board pinout is selected. Firmware must fail closed if a
configured UART or I2C device cannot be initialised.

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

The firmware still withholds telemetry until an ambient-light reading exists (no light-sensor
driver yet), because the contract requires `lux` and a made-up value would mislead the
server's day/night decisions.

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

Ethernet PHY pins and PoE power path, the TLS trust anchor, the light-sensor driver, real captured radar frames, the LD2461, and everything that needs the physical board. See
[ARMOR-HARDWARE](../../ARMOR-HARDWARE) for the enclosure and its validation boundary.
