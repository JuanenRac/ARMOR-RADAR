# Changelog

All notable changes to this project are documented here.

## [0.2.3] - A node with its own web panel, over-the-air updates, a shared Wi-Fi and pins for the server

- **Settings in flash, one image for every node.** Identity, address, Wi-Fi, broker, radars, pins and users are one checked JSON document in the node's flash, edited from its own panel. The build's values (`Kconfig`) are only the *first* settings, so a build with none of them is a generic image and no password has to be compiled in. The pin, static-address and I2C options moved from `Kconfig` to the settings.
- **Web panel** on port 80, embedded in the firmware, in the look of ARMOR-STUDIO and the seven languages: Overview, Network (DHCP or a fixed address, mask, gateway, two DNS, host name), Wi-Fi, Broker, Radars, Pins, Users and Firmware and log. Settings are edited in a working copy, checked by the node (the wrong fields come back marked) and saved from one bar; most apply after a restart, which the bar offers.
- **Login and set-up.** No default password: a node with no user answers only the set-up screen and opens the Wi-Fi `ARMOR-SETUP-xxxxxx`; the first administrator is created with a set-up code (fixed by the build, or random and shown on the USB console). Passwords are PBKDF2-HMAC-SHA256 with a random salt; sessions are random tokens in an `HttpOnly`, `SameSite=Strict` cookie; five wrong passwords lock an address for 30 seconds, doubling up to five minutes; up to four users, administrators or read-only viewers, and the last administrator cannot be removed. Holding BOOT for 8 seconds after power-up erases settings and users.
- **Over-the-air update with rollback.** Two 3 MB application slots on the 16 MB flash; the upload is checked (magic byte, the image's own hash, its project name) before the boot partition changes, and the boot loader goes back to the previous image if the new one does not bring its panel up and keep it up for 30 seconds.
- **Wi-Fi access point, bridged or on its own.** Name, security (open, WPA2, WPA3, both), password, channel, hidden, clients, power, width and country are settings. Joined to the wire (an lwIP bridge of the Ethernet port and the access point, as in ESP-IDF's bridge example), several nodes with the same name, security and password look like one Wi-Fi network with one DHCP server, and the channel on *automatic* picks 1, 6 or 11 from each node's MAC. A Wi-Fi station mode is there for nodes without a cable. **Nothing of it has run on a board.**
- **Pins mapped over the network.** Any free pin becomes an input (debounced, pull-up or down, inverted), an output (initial state, pulses, and a safe state to fall back to when the broker has been unreachable for a chosen time), PWM (up to eight pins, four frequencies) or an analogue reading (scaled), reported as a device of the server (`armor/device/<node>/<pin>/state` and `/set`) in the canonical fields, so Studio needs no mapping. The board's pin table (`board_pins.hpp`) never offers the flash, PSRAM, USB, Ethernet and camera pins and warns about the strapping pins.
- **The radars' command channel.** Each radar now has its TX pin (defaults 15, 21, 38; RX 16, 17, 18), and the panel can read the module's firmware and tracking mode, choose one target or three, set detection zones, switch Bluetooth, restart it and restore its factory settings. The protocol is the manufacturer's public serial protocol, **not** the manual the decoder was written from, and it is unchecked against the manufacturer's document and against a module; the panel says so and shows the raw answer. The serial speed is never changed.
- **Board.** The image now targets the 16 MB flash and enables the 8 MB octal PSRAM (optional: the node starts without it). The 5 V of the radars is meant to come from VSYS.
- **Studio link.** The node publishes `armor/node/<id>/info` (name, firmware, address, port) when it connects and every minute, validated by the shared contract, so Studio can link to its panel.
- **Tools.** `provision_node.sh` also makes the set-up code; `flash.bat` can erase the flash first; `panel_mock.mjs` stands in for a node to work on the panel; `pack_panel.py` embeds the panel. The bench guide and the hardware boundary were rewritten, and `docs/NODE_PANEL.md` describes the panel.
- **Tests.** 167 checks in `test_core` and 397 in the new `test_node` (JSON, network text, pin table, settings and their refusals, network plan, users, sessions and the throttle, mapped-pin logic, the LD2450 command channel, the information message), `-Werror`; the firmware's payloads are accepted by ARMOR-COMMON; the panel passed a scripted browser session in the seven languages at desktop and phone width; the firmware builds clean in ESP-IDF 5.4.2.

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
