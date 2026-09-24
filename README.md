<p align="center">
  <img src="images/ARMOR_BANNER.svg" alt="ARMOR-RADAR banner" width="100%">
</p>

# 📡 ARMOR-RADAR

<p align="center">🇺🇸 <b>English</b> | <a href="README_spa.md">🇪🇸 Español</a></p>

### Field-node firmware and its host-tested core

<p align="center">
  <img src="https://img.shields.io/badge/License-GPL%203.0-blue.svg" alt="GPL 3.0">
  <img src="https://img.shields.io/badge/Language-C%2B%2B17-00599c.svg" alt="Language">
  <img src="https://img.shields.io/badge/Target-ESP32--S3-e7352c.svg" alt="Target">
  <img src="https://img.shields.io/badge/Maturity-scaffolding-FFB020.svg" alt="Maturity">
</p>

---

**Honesty check - what runs today:** **Maturity: scaffolding.** The hardware-independent core (77 checks) and the firmware's JSON (validated by ARMOR-COMMON) are real. `main/app_main.cpp` is **not compiled here** (it needs ESP-IDF 5.x and a board), and the LD2450/LD2461 frame layout is **deliberately not decoded**: it must come from the vendor protocol document and captured UART data, so no track is ever invented.

---

## 1. 🛠️ OVERVIEW

* **Resynchronising framer:** finds frames in a noisy UART stream by header, length and tail, drops one byte on a mismatch and carries on. Its protocol spec for the real radar is unconfigured until the vendor document is added.
* **Contract-exact messages:** the telemetry and health JSON follow the published schemas (15 tracks, 5 per sensor, lux range, node-id pattern) and refuse to write anything invalid.
* **Climate and light logic:** dew point (Magnus), an anti-fog PTC heater controller with hysteresis that switches off on any bad reading, and a local day/night decision.
* **Static-reflector map:** learned only during an operator's calibration and applied only to echoes that are both at a learned position and stationary, so a person standing still is never hidden.
* **Safe start:** the node refuses to start with an invalid or placeholder identity or duplicate radar pins, uses wall-clock time (SNTP) for timestamps, and announces `offline` through an MQTT last will.

---

## 2. 🔧 BUILD & RUN

```bash
cmake -S tests -B build/host && cmake --build build/host
build/host/test_core                                  # 77 checks, -Werror
build/host/emit_samples | python tests/check_contract.py
idf.py build                                          # firmware, needs ESP-IDF 5.x
```

The host tests need any C++17 compiler (Linux, WSL, MSYS2). See the [hardware boundary](docs/HARDWARE_BOUNDARY.md).

---

## 📂 DIRECTORY STRUCTURE

```text
ARMOR-RADAR/
├── main/
│   ├── app_main.cpp        firmware (needs ESP-IDF)
│   └── core/               framer, telemetry_json, climate, static_map, node_id
├── tests/                  test_core.cpp, emit_samples.cpp, check_contract.py
├── Kconfig.projbuild, sdkconfig.defaults
└── docs/HARDWARE_BOUNDARY.md
```

---

## 👤 AUTHOR

**JuanenRac (Electro Hobby 3D)** · electrohobby3d@gmail.com

## 📜 LICENSE

GPL-3.0-or-later - see [LICENSE](LICENSE).
