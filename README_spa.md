<p align="center">
  <img src="images/ARMOR_BANNER.svg" alt="ARMOR-RADAR banner" width="100%">
</p>

# 📡 ARMOR-RADAR

<p align="center"><a href="README.md">🇺🇸 English</a> | 🇪🇸 <b>Español</b></p>

### Firmware del nodo de campo y su núcleo probado en el ordenador

<p align="center">
  <img src="https://img.shields.io/badge/License-GPL%203.0-blue.svg" alt="GPL 3.0">
  <img src="https://img.shields.io/badge/Language-C%2B%2B17-00599c.svg" alt="Language">
  <img src="https://img.shields.io/badge/Target-ESP32--S3-e7352c.svg" alt="Target">
  <img src="https://img.shields.io/badge/Maturity-scaffolding-FFB020.svg" alt="Maturity">
</p>

---

**Comprobación de honestidad - qué funciona hoy:** **Madurez: scaffolding.** El núcleo independiente del hardware (124 comprobaciones), el **decodificador del HLK-LD2450** (escrito a partir del manual de Hi-Link; su ejemplo resuelto da los valores del manual) y el JSON del firmware (validado por ARMOR-COMMON) son reales. `main/app_main.cpp` **no se compila aquí** (necesita ESP-IDF 5.x y una placa), **no se ha capturado ninguna trama de un módulo real**, el LD2461 no se decodifica (no hay documento) y la telemetría se retiene hasta que exista un driver del sensor de luz.

---

## 1. 🛠️ DESCRIPCIÓN

* **Delimitador que se resincroniza y decodificador LD2450:** encuentra tramas en un flujo UART ruidoso por cabecera, longitud y cola, descarta un byte si no coincide y sigue; cada trama de 30 bytes da hasta tres objetivos (x, y, velocidad, puerta de distancia) que pasan a ser pistas del contrato. Un radar que deja de informar no deja objetivos fantasma.
* **Mensajes exactos al contrato:** el JSON de telemetría y salud sigue los esquemas publicados (15 pistas, 5 por sensor, rango de lux, patrón de identificador) y se niega a escribir algo inválido.
* **Lógica de clima y luz:** punto de rocío (Magnus), un control del calefactor PTC antivaho con histéresis que se apaga ante cualquier lectura errónea, y una decisión día/noche local.
* **Mapa de reflectores estáticos:** se aprende solo durante una calibración del operador y se aplica solo a ecos que están en una posición aprendida y quietos, de modo que una persona parada nunca se oculta.
* **Arranque seguro:** el nodo se niega a arrancar con una identidad inválida o de ejemplo o con pines de radar duplicados, usa hora real (SNTP) en las marcas de tiempo y anuncia `offline` mediante un last will de MQTT.

---

## 2. 🔧 COMPILAR Y EJECUTAR

```bash
cmake -S tests -B build/host && cmake --build build/host
build/host/test_core                                  # 124 comprobaciones, -Werror
build/host/emit_samples | python tests/check_contract.py
idf.py build                                          # firmware, necesita ESP-IDF 5.x
```

Los tests en el ordenador necesitan cualquier compilador C++17 (Linux, WSL, MSYS2). Véase el [límite de hardware](docs/HARDWARE_BOUNDARY.md).

---

## 📂 ESTRUCTURA DE DIRECTORIOS

```text
ARMOR-RADAR/
├── main/
│   ├── app_main.cpp        firmware (necesita ESP-IDF)
│   └── core/               framer, ld2450, telemetry_json, climate, static_map, node_id
├── tests/                  test_core.cpp, emit_samples.cpp, check_contract.py
├── Kconfig.projbuild, sdkconfig.defaults
└── docs/HARDWARE_BOUNDARY.md
```

---

## 👤 AUTOR

**JuanenRac (Electro Hobby 3D)** · electrohobby3d@gmail.com

## 📜 LICENCIA

GPL-3.0-or-later - véase [LICENSE](LICENSE).
