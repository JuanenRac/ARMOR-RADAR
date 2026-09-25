<p align="center">
  <img src="images/ARMOR_BANNER.svg" alt="ARMOR-RADAR banner" width="100%">
</p>

# 📡 ARMOR-RADAR

<p align="center">🇺🇸 <b>English</b> | <a href="README_spa.md">🇪🇸 Español</a></p>

### Field-node firmware (Waveshare ESP32-S3-ETH, three LD2450 radars, Ethernet) and its host-tested core

<p align="center">
  <img src="https://img.shields.io/badge/License-GPL%203.0-blue.svg" alt="GPL 3.0">
  <img src="https://img.shields.io/badge/Language-C%2B%2B17-00599c.svg" alt="Language">
  <img src="https://img.shields.io/badge/Target-ESP32--S3-e7352c.svg" alt="Target">
  <img src="https://img.shields.io/badge/Maturity-scaffolding-FFB020.svg" alt="Maturity">
</p>

---

**Honesty check - what runs today:** **Maturity: scaffolding.** The hardware-independent core (167 checks), the **HLK-LD2450 decoder** (written from the Hi-Link manual; its worked example decodes to the manual's values), the VEML7700 conversion and the firmware's JSON (validated by ARMOR-COMMON) are tested on a computer, and the **firmware image builds** in the ESP-IDF 5.4.2 container. **It has never run on a board**: no frame has been captured from a real module, the Ethernet and light-sensor drivers are untried, the LD2461 is not decoded (no document) and the radar modules are not configured by the node.

---

## 1. 🛠️ OVERVIEW

* **Two nodes of 270 degrees:** each Waveshare ESP32-S3-ETH reads up to three LD2450 radars on its three UARTs, mounted 75 degrees apart, over wired Ethernet (W5500) with DHCP or a fixed address. Studio's *Add a 270° node* creates the three radars already wired to the node.
* **Resynchronising framer and LD2450 decoder:** finds frames in a noisy UART stream by header, length and tail, drops one byte on a mismatch and carries on; each 30-byte frame gives up to three targets (x, y, speed, distance gate) that become contract tracks. A radar that stops reporting leaves no ghost targets.
* **Radar health on the console:** per radar, bytes, good and bad frames every ten seconds, and why a radar is not reporting. Telemetry is withheld while no radar reports, never an empty 'all clear'.
* **VEML7700 ambient light** with automatic range and the datasheet's correction. Without the sensor the node withholds telemetry, or sends an explicit bench value that it logs at every start.
* **Contract-exact messages:** the telemetry and health JSON follow the published schemas (15 tracks, 5 per sensor, lux range, node-id pattern) and refuse to write anything invalid; timestamps are wall-clock (SNTP) and an MQTT last will announces `offline`.
* **Bench tools:** one image per node from its own settings file, provisioning of the broker identity without showing the password, flashing over USB, and a converter from a log of raw frames to a test fixture ([bench bring-up](docs/BENCH_BRINGUP.md)).

---

## 2. 🔧 BUILD & RUN

```bash
cmake -S tests -B build/host && cmake --build build/host
build/host/test_core                                  # 167 checks, -Werror
build/host/emit_samples | python tests/check_contract.py
tools/provision_node.sh perimetro-1 --host <cm5> --user <user> --key <key>
tools/build_node.sh perimetro-1                       # one image in dist/, in the ESP-IDF container
```

```bat
tools\flash.bat perimetro-1 COM5 monitor
```

The host tests need any C++17 compiler (Linux, WSL, MSYS2). See the [bench bring-up](docs/BENCH_BRINGUP.md) and the [hardware boundary](docs/HARDWARE_BOUNDARY.md).

---

## 📂 DIRECTORY STRUCTURE

```text
ARMOR-RADAR/
├── main/
│   ├── app_main.cpp        tasks: radars, network, MQTT, statistics
│   ├── board_ethernet.cpp  W5500 over SPI
│   ├── light_sensor.cpp    VEML7700 over I2C
│   ├── Kconfig.projbuild   every pin and setting
│   └── core/               framer, ld2450, veml7700, radar_health, telemetry_json, climate, static_map, node_id
├── tests/                  test_core.cpp, emit_samples.cpp, check_contract.py, test_tools.py
├── tools/                  build_node.sh, provision_node.sh, flash.bat, frames_to_fixture.py
├── secrets/                node.conf.example (the real files are git-ignored)
├── sdkconfig.defaults
└── docs/                   BENCH_BRINGUP.md, HARDWARE_BOUNDARY.md
```

---

## 👤 AUTHOR

**JuanenRac (Electro Hobby 3D)** · electrohobby3d@gmail.com

## 📜 LICENSE

GPL-3.0-or-later - see [LICENSE](LICENSE).
