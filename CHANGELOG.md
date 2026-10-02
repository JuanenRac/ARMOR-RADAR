# Changelog

All notable changes to this project are documented here.

## [0.3.7] - A proper goodbye to the access point before restarting

- **The node never told the access point it was leaving before a restart:** `esp_restart()` just cuts the radio, with no deauthentication frame sent; some access points get stuck holding the old association and need restarting themselves before the node can rejoin. It now calls `esp_wifi_disconnect()` and gives it a moment before restarting, on every restart path (the panel, a firmware update, the factory reset held on BOOT).
- **The "Restarting..." screen never appeared after a firmware update:** `S.rebooting = true` was set without calling `render()` on that one path (the other two call it correctly) - the panel just sat on the old screen until the auto-reload kicked in on its own six seconds later. Now it shows immediately, same as the other two paths.
- **The default HTTP header limit (512 bytes) was too small for a real browser:** a session cookie plus a modern browser's own request headers (Sec-CH-UA client hints, Accept-*) can exceed it, which the panel refused outright - a blank page saying "Header fields are too long", clearable only by wiping cookies for that address. Raised to 2048 bytes.
- Built with ESP-IDF 5.5.5 (now also built natively on Windows, not only in the Docker container: `tools/build_node.sh` or `idf.py` directly against `C:\Espressif`'s install both produce the same image).

## [0.3.6] - The pin picker no longer offers a pin already in use

- **The panel's "Pines" (and the radar RX/TX fields) listed GPIOs the node was already using:** the dropdown only hid pins reserved by the board itself, not the ones the node's own three radars or its light sensor had already claimed - choosing one still failed at save time with a conflict, but the panel should not have offered it. It now hides whichever GPIO another radar, the light sensor or another mapped pin already holds.

## [0.3.5] - The station notices on its own when the gateway stops answering

- **A link watchdog for the Wi-Fi station:** found for real on a bench, more than once, with different routers: the radio stays "connected" (no disconnect event, a good signal, nothing in the log) but the gateway stops answering the node - and it never came back by itself until it was reset by hand. Every 20 seconds the node now pings its gateway a few times; after about a minute with not even one reply, it forces a fresh Wi-Fi association. The exact reason the gateway stops answering it is still open - this does not claim to have found it, only to recover from it without a hand reset.
- Built with ESP-IDF 5.5.5.

## [0.3.4] - A login that actually leaves you signed in

- **The session cookie was garbage:** it was built in a function's own local variable, and the HTTP server only keeps a pointer to a header's text, not a copy of it; by the time the response was really sent, that memory had already been reused for something else. The login or the first-time set-up answered "ok", but no browser ever kept a real session - re-entering the panel always looked like a fresh sign-in. The cookie is now kept alive until the response goes out.
- Built with ESP-IDF 5.5.5.

## [0.3.3] - Saving the settings over Bluetooth no longer restarts the node

- **Bluetooth task stack:** the task that serves the Bluetooth channel (`ble-worker`) ran out of stack while saving a Wi-Fi network with a fixed address, and the node restarted in the middle of the set-up (the app said the connection was lost). Its stack is now 16 KB instead of 8 KB.
- **Bluetooth is on at every start while there is no address:** a node set up with a cable that is not plugged in, or whose Wi-Fi was not joined, did not advertise at all and could not be found by the app; it now advertises at every start and stops a couple of minutes after it has an address.
- Built with ESP-IDF 5.5.5.

## [0.3.2] - A node that has just been set up can always be reached

- **A rescue access point.** A node that is set up (it has a user) but has no address 90 seconds after it starts - no cable, no Wi-Fi network it can join, no access point of its own - opens the set-up Wi-Fi `ARMOR-SETUP-xxxxxx` again, protected with the set-up code. Before, such a node closed its set-up Wi-Fi when the first administrator was created and vanished: no network at all (a real case on the bench: Wi-Fi given over Bluetooth, the node restarted and could not be found).
- **Why it did not join the Wi-Fi.** The `hello` of the Bluetooth channel and the panel's status carry `sta_error` (`network_not_found`, `wrong_password` or `failed`), read from the reason the radio gives when a connection fails.
- **Bluetooth stays on while there is no address.** A node set up to join a Wi-Fi network keeps advertising until it has an address, and stops two minutes after it has one, so the app can come back, read the outcome and correct the Wi-Fi.
- **A fixed address on the Wi-Fi too.** DHCP or a fixed address (address, mask, gateway, DNS) is now valid whichever connection the node uses; it was only honoured on the cable. The panel offers it for both, and the settings check refuses a bad address on the Wi-Fi like on the cable (2 new checks).
- Built with ESP-IDF 5.5.5.

## [0.3.1] - A board that boots, and a log that stays readable

- **Boot loop fixed (shared firmware base):** generating the TLS certificate overflowed the main task's stack on the first boot of a board (every start ended in "A stack overflow in task main"); the buffer is on the heap and the stack is 16 KB. Verified on a real board with the Ethernet image.
- **A light sensor that is not there is asked twice a minute, not every two seconds,** and the I2C driver's own error lines are silenced after the first report, so a board without a VEML7700 no longer fills the log with bus errors.

## [0.3.0]

- A GitHub Actions CI baseline (`.github/workflows/ci.yml`): validates the manifest, the version, CHANGELOG.md's heading, the seven README translations' structure and its own local Markdown links, then runs this project's real build/test through `tools/armor_project_tool.py build-test .` (vendored from ARMOR-COMMON, alongside `tools/armor_ci_validate.py` and `tools/_armor_readme_parity.py`, which do the manifest/docs checking).

## [0.2.9] - The Bluetooth `hello` says what kind of node it is

- **The panel's rows** no longer stretch to the tallest field of a row, and the JSON reader's header comment is the same as in the other node projects: the files this project shares with ARMOR-SOLAR and ARMOR-ELECTRICAL are now kept once in ARMOR-COMMON's `firmware_base` (no behaviour changes).
- The answer to `hello` on the Bluetooth configuration channel now carries `kind` (`radar`; the solar and electrical nodes say `solar` and `electrical`), so the app can tell the three kinds of node apart: the identifier of a radar node (`armor-` and the MAC) says nothing about it. Nothing else of the protocol changes.

## [0.2.8] - One firmware for two boards: with Ethernet and without

- **A version for the boards with no Ethernet.** The firmware is one code base with two **board profiles** chosen when the image is built: `tools/build_node.sh generic` (or `generic s3-eth`) writes `dist/generic-s3-eth.bin` for the Waveshare ESP32-S3-ETH, as before; `tools/build_node.sh generic s3-wifi` writes `dist/generic-s3-wifi.bin` for an **ESP32-S3-WROOM-1 N16R8** (16 MB flash, 8 MB octal PSRAM, two USB-C sockets) that has no Ethernet. An image is for ONE board: flash it only to that one (`tools\flash.bat NODE COMx [s3-eth|s3-wifi]` takes the board too).
- **The `s3-wifi` profile** builds without the W5500 driver and without the bridge, starts on Wi-Fi (the way in is always a station, plus the node's own network), refuses an Ethernet setting (`not_available`), and has its own pin table: GPIO 4 to 14 are ordinary header pins (no W5500, no camera connector, no microSD socket), 43 and 44 (the USB-serial socket) and 48 (the RGB LED) come with a warning. The pins of the three radars, the light sensor and the mapped pins by default are free on both boards.
- **Set-up without a cable:** on the `s3-wifi` board the set-up asks for the Wi-Fi network the node is to join (it is not created without it) and keeps the node's own network `ARMOR-xxxxxx`, with the setup code as its password, so the node can always be reached; the Android app's Bluetooth set-up does the same job from a phone. The panel hides the Ethernet choices and the bridge option there, and warns about the right pins.
- **Tests:** 859 host checks (797 on the `s3-eth` profile, as before, and 62 new ones on the `s3-wifi` profile: its pin table, its defaults, the refusal of the cable, and the layouts).
- **Not done:** a run on either board. The `s3-wifi` profile has never been on an N16R8 board; nothing in the Ethernet path changed.
- The host tests' `CMakeLists.txt` now also builds and runs `test_sensors` (136 checks): it existed and passed when compiled by hand, but it was missing from the build, so `build-test` could not find it.

## [0.2.7] - Placeholders in the usage lines

- The usage lines of `adopt_node.py`, `provision_node.sh` and the bench guide show `<user>` and `~/.ssh/id_key` where they showed the names of a real account and key. No code changed.

## [0.2.6] - HTTPS for the panel, and stable identities for the tracks

- **The panel over HTTPS.** *Network > Panel security*: HTTP and HTTPS (the default), HTTPS only (port 80 sends the browser to HTTPS), or HTTP only. The node makes its own certificate the first time (an ECDSA P-256 key and a self-signed certificate for its id, kept in flash and erased by a factory reset), so the password and the session cookie no longer travel in clear on the network; the cookie is marked Secure over HTTPS. The panel shows the certificate's SHA-256 fingerprint to compare with the browser's warning. If the certificate cannot be made, the node stays on plain HTTP rather than lose its panel.
- **A track keeps its identity.** A radar's slot number is not an identity, and the server counts and times targets by track id, so a lost frame could hand one person's id to another. The node now matches each frame's targets to the ones it was following (nearest first, inside a gate that grows while a target is missed), smooths the position a little, keeps a lost target for a few frames and still reports it for the first two, and gives a new one the next id (1 to 255, never 0). It applies to the LD2450 and the LD2461 and can be turned off in the build configuration for bench comparisons.
- **The states the firmware publishes for devices are now checked against the server**: `tests/emit_samples` writes them (presence and distance, pins), the shared fixture holds them, and ARMOR-SERVER's tests run them through its device layer.
- Tests: 167 + 494 + 136 checks; the panel exercised in a browser with the new card. Nothing has run on a board: the TLS handshake, its memory use next to Bluetooth and the certificate warning in real browsers are untried.

## [0.2.5] - Six sensor models on the three ports

- **A port carries one of six sensors**, chosen in the panel (or `radars[].model` in the settings), each at its own serial speed (or one set): the **HLK-LD2450** as before, the **HLK-LD2461** (a tracker with five tracks, decoded from its protocol document, whose report format the node sets to coordinates when the TX wire is connected because the factory default sends zones only), and four **presence sensors**, the **LD2410B/C**, **LD2412**, **LD2410S** and **Seeed MR24HPC1**. `docs/SENSORS.md` says what each one is, what was decoded, and what the documents leave open.
- **Presence sensors become devices of the server**: each publishes `armor/device/<node>/<name>/state` as `{"triggered":…,"distance_cm":…}` when it changes, every 30 seconds, and whenever the broker reconnects. The panel shows presence and distance live, and the numbers of each model (zones of an LD2461, energies of an LD2410, flags of an MR24).
- **The panel offers each model only its commands**: the LD2450 all of them; the LD2461 read information, zones (mapped onto its three "detect only inside / ignore inside" zones) and factory reset; the LD2410 and LD2412 read information, Bluetooth, restart and factory reset; the LD2410S and MR24HPC1 none. The node answers `unsupported` to the rest.
- **Fixed:** the panel showed the raw code (`no_answer`) instead of a sentence when a radar command failed, because the texts were stored under a different name from the one the panel looks up.
- **Fixed:** the two GATT tables of the Bluetooth service no longer give compiler warnings.
- **Tests:** 748 checks (167 + 478 + 103 new for the sensors: every worked example of the manufacturers' documents, checksums, resynchronisation after garbage and settings of the models); the panel was exercised in a real browser (a presence sensor with its distance, the per-model buttons, the error text) in the seven languages and at phone width; the universal image builds. **No sensor has been connected to a node.**

## [0.2.4] - Wi-Fi station with a network search, and configuration over Bluetooth

- **Join a router's Wi-Fi.** A node can connect to the Wi-Fi of a router or an access point instead of (or besides) offering its own: *Connection: Wi-Fi* and the station in the panel. **Search for networks** lists what the node hears (name, signal, channel, security, strongest first, one line per name) and one click fills the name in. It works in every layout: an access point alone is briefly used as a station too, and a node with Wi-Fi off starts it just for the search (the node's own clients may notice a moment). `GET /api/v1/wifi/scan`.
- **Bluetooth Low Energy configuration** for a node with no Ethernet cable: a NimBLE peripheral called `ARMOR-xxxxxx` with one GATT service (a write characteristic for requests and a notify one for answers) that carries the panel's operations as framed JSON: `hello`, `setup`, `login`, `config.get`, `config.put`, `wifi.scan`, `status`, `reboot`. The same set-up code and users as the panel, an administrator for any change, a throttle on wrong passwords, an encrypted link (LE Secure Connections, "just works", so it stops a passive listener and not someone present while the phone pairs). The setting *Bluetooth* is `setup` (only while the node has no user, the default), `always` or `off`; with `off` the stack is not even started. The protocol is documented in `docs/BLE_PROVISIONING.md`.
- The panel and the Bluetooth channel share one implementation of the status, the settings and the search (`api_shared`). The framing, the operations and their access rules are host-tested with a fake backend (466 checks in `test_node`); the radio side builds and **has never run on a board nor against a phone**.

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
- **One image for every board.** `tools/build_node.sh generic` builds the universal image: the same firmware for every board, named after its MAC (`armor-` and six digits) until it is adopted. With a fleet secret (`tools/make_fleet.py`) each board's set-up code is HMAC-SHA256 of its MAC, so `tools/adopt_node.py` can give a new node its administrator (a random password kept in `secrets/<id>.admin`), its broker identity and the fleet's shared settings over the network, with no USB cable; the set-up screen shows the MAC. The result is checked against a stand-in node, and the code derivation against openssl and the firmware's mapping.
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
