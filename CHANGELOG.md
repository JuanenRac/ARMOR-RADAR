# Changelog

All notable changes to this project are documented here.

## [0.2.2] - Firmware for two Ethernet nodes with three radars each

- **The firmware builds.** It had never been compiled: doing it in the ESP-IDF 5.4.2 container found real faults (the menuconfig file was not where ESP-IDF reads it, the W5500 driver was not enabled, a missing include) and they are fixed. `tools/build_node.sh NODE` produces one merged image per node (about 700 KB) from `secrets/NODE.conf`.
- **Ethernet of the Waveshare ESP32-S3-ETH** (W5500 over SPI, pins from the manufacturer's table), DHCP or a fixed address, the chip's own MAC, link and address logged, and the network started after the radars so the bench log is useful with no cable.
- **VEML7700 driver** (I2C) with automatic range selection and the datasheet's non-linearity correction; the conversion and the range ladder are host-tested against the datasheet's figures. Without the sensor the node still withholds telemetry, or sends an explicit bench value (`CONFIG_ARMOR_LUX_FALLBACK`) that is logged at every start.
- **Radar health:** per radar, bytes, good and bad frames every ten seconds, with the reason a radar is not reporting (no data, garbled, silent). Telemetry is withheld while no radar reports, instead of publishing an empty 'all clear'. Up to three radars per node (`CONFIG_ARMOR_RADAR_COUNT`); pins are checked against the Ethernet and I2C pins.
- **Bench tools:** `tools/provision_node.sh` asks the broker for the node's identity and writes its settings without showing the password, `tools/flash.bat` writes the image over USB, `tools/frames_to_fixture.py` turns a log of raw frames into a test fixture that the host tests decode.
- `docs/BENCH_BRINGUP.md`: the wiring, the order of the first day and what the console must say. 167 host checks (was 124).

## [0.2.1] - The LD2450 frame decoder

- `main/core/ld2450.hpp` decodes the HLK-LD2450 report frame from the Hi-Link manual V1.00: 30 bytes, header `AA FF 03 00`, three 8-byte targets (x, y, speed, distance resolution, bit 15 set means positive), tail `55 CC`. The manual's worked example is a test and decodes to the manual's values.
- `TrackSet` keeps the newest tracks of the three radars and forgets a radar that stops reporting, so a dead radar leaves no ghost targets.
- The firmware wires the decoder to the framers and publishes telemetry at most five times a second, but **withholds it while there is no ambient-light reading** (no light-sensor driver yet) instead of inventing a lux value. It is not compiled here (no ESP-IDF).
- The slot number (1 to 3) is used as the track id because the manual gives no better identity; an LD2461 is not decoded (no document). 124 host checks (was 77).

## [0.2.0]

- Hardware-independent core with 77 host checks: resynchronising framer, contract-exact JSON, dew point, PTC controller, static-reflector map, node-id validation.
- Firmware refuses invalid identity and duplicate pins, uses SNTP time and an MQTT last will.
- The LD2450/LD2461 frame layout is intentionally not decoded until the vendor document is available.
