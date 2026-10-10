# Changelog

All notable changes to this project are documented here.

## [0.5.6] - No silence without a clock, firmware slots and a login over HTTP after one over HTTPS

- **A node without a clock no longer falls silent.** It used to drop everything it read until a time server answered, so a node on a network without internet, or one that restarted after a power cut with the provider down, said nothing at all. It now publishes with the time since it started, and ARMOR-SERVER stamps such a message with the moment it receives it; as soon as the clock is set it sends real dates again. (The radar used to wait for the clock before even connecting to the broker.)
- **The broker link no longer races with itself** when the node moves to another saved broker: the client is replaced under a lock and the old one is stopped outside it, so a message being published at that moment can no longer use a client that is being destroyed.
- **The panel's help** (*Firmware and log*) describes the firmware slots and their switch, the copy of the log and the check of the hash and the progress bar of the GitHub update, in the seven languages.
- **The node says what kind it is.** The public session answer (`/api/v1/session`) now carries `kind` (`radar`, `solar`, `electrical` or `hmi`) next to the node's id, version and board, so Studio's search for nodes in the network can tell a radar node from a solar one before it is added, even when its panel title cannot be read.
- **Copy the log** (*Firmware and log -> Log*): a button copies everything the log shows to the clipboard, in the seven languages. It also works when the panel is opened over plain HTTP, where the browser's own clipboard is not available, through a fallback.
- **A login over HTTP works after one over HTTPS.** The session cookie of the page served over HTTPS now has its own name (`armor_session_tls`); with one shared name, the browser kept the "Secure" cookie of the HTTPS login and refused to let a plain-HTTP page replace it, so the login over HTTP looked as if it did nothing (it worked in a private window).
- **The GitHub check gives back its memory before it asks for the hash.** The release's JSON (15 KB with the images of several boards) and its parsed tree are freed before the second TLS connection that reads the `.sha256`; with the bigger release the node said "the release brings no checksum" because it could not open that connection. The reason it could not is now written in the node's log.

## [0.5.5] - Switch between the two firmware slots from the panel

- **Firmware slots in the panel** (*Firmware and log -> Update*): a new card shows the two application slots (ota_0 and ota_1) with the version each one holds and which one runs, and a button boots the other one at the next restart - the way back to the version that ran before an update, or forward to the one just installed. It asks for confirmation, needs an administrator, refuses an empty slot or a firmware of another project, and the settings are kept. The same card exists in the radar, solar, electrical and touch-panel nodes, in the seven languages (`POST /api/v1/ota/switch`).

## [0.5.4] - Updating from GitHub works, and shows its progress

- **Real bug, found on the bench:** *Search GitHub → Install* stopped with `http_302`. GitHub answers a download with a redirect to another server (and a long, signed address in the `Location` header); the node did not follow it and its header buffer was too small for that address. Redirects are now followed (up to five) and the buffer holds the address. The update had never been able to download anything before this.
- **Progress:** the install runs in a task of its own and the panel shows a bar (*Downloading the firmware from GitHub… 42%*, then *Checking the image against the hash of the release…*), like the upload of a file does; when it is done the node restarts. A second install while one runs is refused (`ota_busy`).
- A node that runs 0.5.2 or 0.5.3 cannot update itself from GitHub (that is the bug above): flash this one by file once.


## [0.5.3] - The firmware installed from GitHub is checked against the hash of the release

- **Real hole closed:** the node computed the SHA-256 of the image it downloaded from GitHub but never compared it with anything. Each release now carries `armor_radar.bin.sha256` next to `armor_radar.bin`; the node reads it, installs only an image that hashes to exactly that, and otherwise refuses (`checksum_mismatch`) leaving the boot partition as it was. A newer release without that file is reported (`no_checksum`) and not installed. The manual upload is unchanged. The panel's messages for both cases are in the seven languages.


## [0.5.2] - Same fix as 0.5.1, with a number of its own

- A first 0.5.1 image went onto a bench node before the map fix was complete. This build carries the complete fix (see 0.5.1) and a new number, so a node that has the earlier 0.5.1 sees it as an update.

## [0.5.1] - The map of the panel never showed any people

- **Real bug, reported on the bench:** the live map of the panel never showed a person. The node put a browser on the list of those it sends the map to inside the websocket's handler, but ESP-IDF does not call that handler for the handshake (only for the frames that follow it, and a browser sends none), so the list stayed empty and nothing was ever sent. The handshake now has its own two steps: the session is checked before it is answered (without one the connection is closed), and the browser is put on the list right after. Each entry also remembers the server it belongs to (plain HTTP or HTTPS) and is answered through that one, places left taken by browsers that went away are freed (the oldest makes room when all four are busy), and the task that draws the map has more stack.


## [0.5.0] - Date and time, flash overview, hints everywhere and a node that can always be reached

- **Date and time:** a new *Date and time* card on the Overview page shows the node's local time and where it comes from (time server, set by hand, or not set yet). The Network page has a new clock card: the time zone (a list of common zones, with summer time handled by itself), the time server on or off, and, with the server off, a button that sets the node's clock from the browser. The clock now starts by itself as soon as the node has an address - before, it only started together with the broker connection, so a node with no broker never had a time. The time server moved out of the broker settings into a `time` section of the settings file; older files that still carry `mqtt.ntp` load as before.
- **Flash memory:** a new card on the Overview page with the flash size, how much the partitions reserve, what is left unassigned and, for each of the two firmware slots, how much of it the image uses, how much is free, which version it holds and which one is running or boots next.
- **A hint on everything:** pausing the pointer over a field, a button or a menu entry now shows a short explanation in the panel's language (all seven).
- **"Load a configuration file"** now lights up under the pointer like the other buttons.
- **Real bug, found on a bench:** a backup Wi-Fi network or broker lost its password the next time the settings were saved, because the form never holds a stored password and an entry that arrived without one replaced the stored one with an empty password. An entry sent without a password now keeps the one it had (same network name; same user on the same slot or address); a different name starts empty. A secured network tried with an empty password answers "no network with compatible security"; that reason is now shown as a wrong password instead of an anonymous failure.
- **Real bug, found on a bench:** the choice of language made in the panel was never written to the node's settings, so the exported file said English while the panel was in Spanish. Choosing a language now stores it at once, with no restart.
- **Real bug, found on a bench:** the node's rescue network disappeared right after signing in to the panel through it. While the node kept looking for the Wi-Fi network it could not find, every attempt scanned all channels, and the access point shares the radio, so the phone lost the network. While someone is connected to the rescue network the node now pauses those attempts and resumes them ten seconds after the last person leaves.
- **Real bug, found on a bench:** after the first set-up by Wi-Fi the node's own network was never seen again. The set-up code was erased the moment the administrator was created, and that code is the password of the node's own and rescue networks, which could then not start. The code is now always kept (the one derived from the fleet secret, the one built into the image, or a random one stored in flash so it is the same at every start), and the set-up screen accepts a Wi-Fi network for the node to join on every board, also on those with a cable.
- **Radars:** a *Label* field for every radar that is a tracker (it was only offered to the other sensors), shown in the Overview and on the map. The "Merge distance" field of the target fusion had lost its label and showed its internal name.
- **Map rewritten:** the three radars are drawn with the true six-metre reach and 120° field of view, turned the right way for their heading and no longer on top of one another; the plan keeps drawing on the canvas that is on screen (it had stopped after any redraw); a status line says whether the live feed is connected, waiting or showing targets; and two buttons spread the three radars over 360° (0° / 120° / -120°) or over 270° (75° apart).
- **Everything the panel tells a sensor is kept:** the detection zones (type and up to three rectangles) and the single/multi-target mode are now part of the settings, so the exported file holds them and the node tells the sensor again 6 seconds after every start - a replaced or reset module no longer silently loses them.

## [0.4.9] - Removing a backup network or broker now actually removes it

- **Real bug, reported by the user:** deleting both backup brokers in the panel and saving brought them straight back. The cause: saving only ever added the document's backup entries to the ones already stored, never replacing the list - so an empty list changed nothing, and a shorter one left the extra old entries behind. This is also the most likely real explanation for 0.4.8's "too many backup networks" problem: every save kept appending, until the stored list grew past the limit. Saving now replaces the list exactly as sent, same as it already did for mapped pins.

## [0.4.8] - A bad backup list no longer costs the whole configuration

- **Real bug, reported by the user:** after a plain restart (no save involved), the panel came up with every setting back to its defaults - broker, radars, everything. The cause: a stored `sta.backup` list with more entries than the panel allows (likely left over from early testing, before this limit existed) failed validation, and a single invalid field anywhere in the stored document discarded the entire thing.
- **Fixed where it is wrong:** more backup Wi-Fi networks or brokers than fit (3 and 2) now just lose the extras past the limit - the rest of the node's settings load normally. Boot now also logs every problem found, not only the first, for a case like this to be diagnosed without guessing.

## [0.4.7] - About and Help pages

- **About:** the node's own identity, firmware version, author and licence - what Studio's own About dialog already shows, adapted for a node's panel.
- **Help:** a tabbed page explaining each part of the menu (Overview, Network, Wi-Fi, Broker, Radars, Map, Pins, Users, Firmware and log) in plain language, in all seven languages - the same structure as Studio's own Help window.

## [0.4.6] - Updating from GitHub, and staying signed in

- **Check GitHub for a new version** (Update page): next to the existing manual upload, an admin can check the project's own GitHub releases and install the standalone app image in one step - never the "-complete" image, which has the bootloader too. The manual upload still works exactly as before, for a node with no reason to reach the Internet.
- **"Keep me signed in on this browser"** at login: unchecked, nothing changes (30 minutes idle still signs out); checked, the session survives closing the browser and lasts 30 days of actual use.
- Built and linked clean with the real toolchain (49% of the app partition free).

## [0.4.5] - Chip, flash, PSRAM and bootloader version on the Overview page

- **A new "Hardware" card:** the chip and its revision, how many cores, the flash and PSRAM sizes, and the IDF version of both the running firmware and the bootloader (not the same thing - a mismatch there usually means the wrong board file was built). Read straight from the chip and the bootloader's own descriptor (`esp_ota_get_bootloader_description`), not stored anywhere. Needed `spi_flash` added to the component's own dependencies; built clean with the real toolchain.

## [0.4.4] - A setup-code tool with no command line

- **`tools/setup_code.html`:** the fleet secret and a node's MAC in, the ten-character setup code out - the same HMAC-SHA256 the firmware and `adopt_node.py` already use, computed once by this page's own JavaScript (Web Crypto) with no server and no network request. Useful when setting up several boards from just their MAC, before any of them has network access yet.

## [0.4.3] - An eye on the login and set-up passwords

- **Sign-in, the admin password and the Wi-Fi password at set-up** now have an eye button that shows what was typed - the one place a mistyped password locks someone out with no other field to check it against.

## [0.4.2] - A live top-down map of the three radars

- **New "Map" page:** the node at the centre, each radar's own cone (coloured, from its calibration on the Radars page) and the targets it currently sees - the same shared-plane, already-merged positions telemetry publishes, pushed to the browser over a WebSocket (`/ws/radar-map`) a few times a second instead of being polled. Logged-in only, same session as the rest of the panel. Builds clean with the real ESP-IDF toolchain (52% of the app partition free); not yet confirmed on a physical board.

## [0.4.1] - One shared map for the three radars, and merging what both see

- **Spatial calibration, per radar:** position (offset X/Y from the node's own centre) and orientation (yaw, and a downward tilt - pitch) in the panel's Sensor cards. The targets a radar reports are rotated and moved onto one shared plane before publishing, instead of three separate local ones - set all three to 0/120/240 degrees of yaw for all-round coverage, or leave everything at zero for exactly the old behaviour (nothing changes until it is set).
- **Merging overlapping targets:** a new "merge threshold" (Radars page, millimetres, 0 by default) - when two different radars report a target within that distance of each other on the shared plane, they are published once, at their midpoint, instead of twice. 0 keeps every radar's targets exactly as reported.
- New settings only (`radars[].offset_x_mm/offset_y_mm/yaw_deg/pitch_deg`, `fusion.merge_mm`): a node with none of this set publishes identical telemetry to before.

## [0.4.0] - Download and load a whole configuration

- **An admin can now download this node's whole configuration** (Network page, secrets included) as a .json file - the same document the flash keeps - and load one back into the form before saving. Built for setting up a batch of identical boards from a single bench node instead of retyping Wi-Fi, broker, radars and pins by hand. The node's own identity (its node ID) is never overwritten by an import: every board keeps its own. `GET /api/v1/config/export` is a new, admin-only route; loading a file changes nothing until the existing Save button is pressed.

## [0.3.9] - A periodic restart the panel can turn on

- **A new System setting:** "do not apply" or every 1, 2, 3, 4, 6, 12, 24 or 48 hours. It is the same clean restart as every other one (Wi-Fi told it is leaving before the radio powers down), just timed instead of triggered by a save or a firmware update - for a board nobody is going to touch for weeks. Settings-schema change only (`system.auto_restart_hours`, 0 by default): a node left at "do not apply" behaves exactly as before.

## [0.3.8] - Backup Wi-Fi networks and backup brokers

- **Up to 3 backup Wi-Fi networks and 2 backup brokers**, in the panel's Wi-Fi and Broker pages: tried in order, the same way a phone or laptop remembers more than one network, only after the one above has failed to connect for about 10-15 seconds (Wi-Fi) or stayed unreachable for about 30 seconds (the broker) - never while it still works. Settings-schema change only: `sta.backup[]` and `mqtt.backup[]`, both empty by default, so a node with none configured behaves exactly as before (confirmed on real hardware: flashed, config preserved, boots clean, 0 errors, same single network as before).

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
