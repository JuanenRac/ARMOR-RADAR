<p align="center">
  <img src="images/ARMOR_BANNER.svg" alt="ARMOR-RADAR banner" width="100%">
</p>

# 📡 ARMOR-RADAR

<p align="center">🇺🇸 <b>English</b> | <a href="README_spa.md">🇪🇸 Español</a></p>

### Field-node firmware (Waveshare ESP32-S3-ETH, three radars or presence sensors, Ethernet and Wi-Fi) with its own web panel, and its host-tested core

<p align="center">
  <img src="https://img.shields.io/badge/License-GPL%203.0-blue.svg" alt="GPL 3.0">
  <img src="https://img.shields.io/badge/Language-C%2B%2B17-00599c.svg" alt="Language">
  <img src="https://img.shields.io/badge/Target-ESP32--S3-e7352c.svg" alt="Target">
  <img src="https://img.shields.io/badge/Maturity-scaffolding-FFB020.svg" alt="Maturity">
</p>

---

**Honesty check - what runs today:** **Maturity: scaffolding.** The hardware-independent core (748 checks: the decoders of the LD2450, the LD2461 and four presence sensors, the LD2450 command channel, the settings and their checks, the pin table, users and sessions, the mapped-pin logic, the network plan and the message serialiser, whose output ARMOR-COMMON accepts) is tested on a computer, the **web panel** was exercised in a real browser against a stand-in node, and the **firmware image builds** in the ESP-IDF 5.4.2 container. **It has never run on a board**: no frame has been captured from a real module, the Ethernet, Wi-Fi bridge, light-sensor and update code are untried, the radar command channel is unchecked against the manufacturer's document and against a module, the panel is plain HTTP, and the LD2461 and presence-sensor decoders match only the worked examples of their manuals, with no module behind them.

---

## 1. 🛠️ OVERVIEW

* **Two nodes of 270 degrees:** each Waveshare ESP32-S3-ETH reads up to three LD2450 radars on its three UARTs, mounted 75 degrees apart, over wired Ethernet (W5500) with DHCP or a fixed address, powered by PoE or USB. Studio's *Add a 270° node* creates the three radars already wired to the node.
* **A web panel on every node,** in the look of Studio and its seven languages, embedded in the firmware: overview, network, Wi-Fi, broker, radars, pins, users, firmware update and log. A node with no user opens the Wi-Fi `ARMOR-SETUP-xxxxxx` and creates its first administrator with a set-up code; passwords are salted PBKDF2, sessions are random tokens, and every setting lives in the node's flash, so one image serves every node and no password is compiled in ([the panel](docs/NODE_PANEL.md)).
* **One Wi-Fi from many nodes:** each node can offer an access point joined to its Ethernet port; give the nodes the same name and password and the channel on automatic (1, 6 or 11 by MAC) and phones and Wi-Fi sensors see one network with one DHCP server. It is not a radio mesh: every node keeps its cable. A node can instead join a router's Wi-Fi (with a search for networks), and one with no cable can be configured over **Bluetooth** from the Android app.
* **Pins for the server:** any free pin becomes an input, an output (with a safe state when the broker is lost), PWM or an analogue reading, and appears as a device of the server, so a relay or a contact needs no new firmware. The board's reserved pins are never offered.
* **Over-the-air updates with rollback** from the panel (two slots on the 16 MB flash), and a **link from Studio** to each node's panel, from the address the node publishes.
* **Six sensor models, one per port:** a port carries an LD2450 or an LD2461 (trackers that feed the perimeter, with the model's own field in Studio) or a presence sensor (LD2410, LD2412, LD2410S, MR24HPC1) that becomes a device of the server and publishes presence and distance. Chosen in the panel; what each one is, its protocol and what was not verified are in `docs/SENSORS.md`.
* **LD2450 decoder, health and configuration:** a resynchronising framer finds frames in a noisy stream; each 30-byte frame gives up to three targets that become contract tracks; the console and the panel say per radar whether it reports, is silent or garbled. The panel can also read the module's version, choose one or three targets and set detection zones (a protocol that is unchecked against a module).
* **Ambient light and contract-exact messages:** the VEML7700 with automatic range; telemetry, health and information JSON that follow the published schemas and refuse to write anything invalid, with wall-clock timestamps (SNTP) and an MQTT last will. Telemetry is withheld while no radar reports, never an empty 'all clear'.
* **One image for every board, like a network product:** the firmware is the same and the MAC tells the boards apart (`armor-` and six digits until named); `adopt_node.py` gives a freshly flashed node its administrator, its broker identity and the fleet's shared settings over the network, with a set-up code computed from its MAC, so 5 nodes or 27 are the same work. **Bench tools:** flashing over USB-C, a stand-in node for working on the panel, and a converter from a log of raw frames to a test fixture ([bench bring-up](docs/BENCH_BRINGUP.md)).

---

## 2. 🔧 BUILD & RUN

```bash
cmake -S tests -B build/host && cmake --build build/host
build/host/test_core && build/host/test_node && build/host/test_sensors   # 748 checks, -Werror
build/host/emit_samples | python tests/check_contract.py
node tools/panel_mock.mjs --user admin:adminpass123   # the panel without a board
python tools/make_fleet.py                             # once: the fleet secret and the shared settings
tools/build_node.sh generic                           # ONE image for every board, in the ESP-IDF container
```

```bat
tools\flash.bat generic COM5 monitor                  # each board, once, by USB-C
```

```bash
tools/adopt_node.py 192.168.0.181 --id perimetro-3 --fleet secrets/fleet.json --broker-ssh-host <cm5> --broker-ssh-user <user>
```

The host tests need any C++17 compiler (Linux, WSL, MSYS2). See the [bench bring-up](docs/BENCH_BRINGUP.md), the [panel](docs/NODE_PANEL.md) and the [hardware boundary](docs/HARDWARE_BOUNDARY.md).

---

## 📂 DIRECTORY STRUCTURE

```text
ARMOR-RADAR/
├── main/
│   ├── app_main.cpp        start-up: settings, radars, pins, network, panel, broker
│   ├── node_store.cpp      settings and users in flash
│   ├── network.cpp         Ethernet, Wi-Fi access point and bridge, station
│   ├── web_server.cpp      the panel and its JSON API, login, update
│   ├── radar_manager.cpp   UARTs, frames, health, command channel
│   ├── gpio_manager.cpp    the pins mapped for the server
│   ├── mqtt_link.cpp       clock, health, telemetry, information, pin topics
│   ├── board_ethernet.cpp, light_sensor.cpp, log_buffer.cpp, entropy.cpp
│   ├── Kconfig.projbuild   the first settings of a build
│   └── core/               framer, ld2450, ld2450_command, node_config, board_pins, network_plan, auth, gpio_logic, json, veml7700, telemetry_json...
├── panel/                  index.html, app.js, text.js (7 languages), style.css
├── tests/                  test_core.cpp, test_node.cpp, emit_samples.cpp, check_contract.py, test_tools.py
├── tools/                  build_node.sh, make_fleet.py, adopt_node.py, provision_node.sh, flash.bat, pack_panel.py, panel_mock.mjs, frames_to_fixture.py
├── secrets/                node.conf.example, fleet.example.json (the real files are git-ignored)
├── partitions.csv, sdkconfig.defaults
└── docs/                   BENCH_BRINGUP.md, NODE_PANEL.md, HARDWARE_BOUNDARY.md
```

---

## 👤 AUTHOR

**JuanenRac (Electro Hobby 3D)** · electrohobby3d@gmail.com

## 📜 LICENSE

GPL-3.0-or-later - see [LICENSE](LICENSE).
