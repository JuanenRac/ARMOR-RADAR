# Hardware boundary

## What is tested, and where

| Part | Where it runs | Evidence |
|---|---|---|
| The core in `main/core` (framer, message serialiser, dew point and heater, day/night, static map, node id) | Any computer with CMake and a C++17 compiler | `tests/test_core.cpp` (77 checks, `-Werror`) |
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

## The frame decoder is deliberately absent

`main/core/frame_framer.hpp` recovers frames from a noisy byte stream (header, length,
tail, resynchronisation) but knows nothing about what is inside them, and its
`ProtocolSpec` for the real radar is **unconfigured**, so it emits nothing. A real LD2450 /
LD2461 decoder shall be added only from the vendor protocol document and captured UART
fixtures; it must never infer a binary layout from an unverified source. Until then the
node never invents a track: it reports health only.

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

Ethernet PHY pins and PoE power path, the TLS trust anchor, the sensor drivers, the real
radar frame layout, and everything that needs the physical board. See
[ARMOR-HARDWARE](../../ARMOR-HARDWARE) for the enclosure and its validation boundary.
