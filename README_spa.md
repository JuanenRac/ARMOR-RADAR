<p align="center">
  <img src="images/ARMOR_BANNER.svg" alt="ARMOR-RADAR banner" width="100%">
</p>

# 📡 ARMOR-RADAR

<p align="center"><a href="README.md">🇺🇸 English</a> | 🇪🇸 <b>Español</b></p>

### Firmware del nodo de campo (Waveshare ESP32-S3-ETH, tres radares LD2450, Ethernet) y su núcleo probado en el ordenador

<p align="center">
  <img src="https://img.shields.io/badge/License-GPL%203.0-blue.svg" alt="GPL 3.0">
  <img src="https://img.shields.io/badge/Language-C%2B%2B17-00599c.svg" alt="Language">
  <img src="https://img.shields.io/badge/Target-ESP32--S3-e7352c.svg" alt="Target">
  <img src="https://img.shields.io/badge/Maturity-scaffolding-FFB020.svg" alt="Maturity">
</p>

---

**Comprobación de honestidad - qué funciona hoy:** **Madurez: scaffolding.** El núcleo independiente del hardware (167 comprobaciones), el **decodificador del HLK-LD2450** (escrito a partir del manual de Hi-Link; su ejemplo resuelto da los valores del manual), la conversión del VEML7700 y el JSON del firmware (validado por ARMOR-COMMON) se prueban en el ordenador, y la **imagen del firmware compila** en el contenedor de ESP-IDF 5.4.2. **Nunca se ha ejecutado en una placa**: no se ha capturado ninguna trama de un módulo real, los controladores de Ethernet y del sensor de luz no se han probado, el LD2461 no se decodifica (sin documento) y el nodo no configura los módulos de radar.

---

## 1. 🛠️ DESCRIPCIÓN

* **Dos nodos de 270 grados:** cada Waveshare ESP32-S3-ETH lee hasta tres radares LD2450 en sus tres UART, montados con 75 grados de separación, por Ethernet cableada (W5500) con DHCP o dirección fija. *Añadir un nodo de 270°* en Studio crea los tres radares ya asignados al nodo.
* **Delimitador que se resincroniza y decodificador LD2450:** encuentra tramas en un flujo UART ruidoso por cabecera, longitud y cola, descarta un byte si no coincide y sigue; cada trama de 30 bytes da hasta tres objetivos (x, y, velocidad, puerta de distancia) que pasan a ser pistas del contrato. Un radar que deja de informar no deja objetivos fantasma.
* **Salud de los radares en la consola:** por radar, bytes, tramas buenas y malas cada diez segundos, y por qué un radar no informa. La telemetría se retiene mientras ningún radar informa, nunca un «todo despejado» vacío.
* **Luz ambiente VEML7700** con rango automático y la corrección de la hoja de datos. Sin el sensor el nodo retiene la telemetría, o envía un valor de banco explícito que registra en cada arranque.
* **Mensajes exactos al contrato:** el JSON de telemetría y salud sigue los esquemas publicados (15 pistas, 5 por sensor, rango de lux, patrón de identificador) y se niega a escribir algo inválido; las marcas de tiempo son hora real (SNTP) y un last will de MQTT anuncia `offline`.
* **Herramientas de banco:** una imagen por nodo desde su propio fichero de ajustes, alta de la identidad en el broker sin mostrar la contraseña, grabación por USB y un conversor de un registro de tramas en bruto a un fixture de pruebas ([puesta en marcha](docs/BENCH_BRINGUP.md)).

---

## 2. 🔧 COMPILAR Y EJECUTAR

```bash
cmake -S tests -B build/host && cmake --build build/host
build/host/test_core                                  # 167 comprobaciones, -Werror
build/host/emit_samples | python tests/check_contract.py
tools/provision_node.sh perimetro-1 --host <cm5> --user <usuario> --key <clave>
tools/build_node.sh perimetro-1                       # una imagen en dist/, en el contenedor de ESP-IDF
```

```bat
tools\flash.bat perimetro-1 COM5 monitor
```

Los tests en el ordenador necesitan cualquier compilador C++17 (Linux, WSL, MSYS2). Véanse la [puesta en marcha](docs/BENCH_BRINGUP.md) y el [límite del hardware](docs/HARDWARE_BOUNDARY.md).

---

## 📂 ESTRUCTURA DE DIRECTORIOS

```text
ARMOR-RADAR/
├── main/
│   ├── app_main.cpp        tareas: radares, red, MQTT, estadísticas
│   ├── board_ethernet.cpp  W5500 por SPI
│   ├── light_sensor.cpp    VEML7700 por I2C
│   ├── Kconfig.projbuild   cada pin y ajuste
│   └── core/               framer, ld2450, veml7700, radar_health, telemetry_json, climate, static_map, node_id
├── tests/                  test_core.cpp, emit_samples.cpp, check_contract.py, test_tools.py
├── tools/                  build_node.sh, provision_node.sh, flash.bat, frames_to_fixture.py
├── secrets/                node.conf.example (los reales están fuera de git)
├── sdkconfig.defaults
└── docs/                   BENCH_BRINGUP.md, HARDWARE_BOUNDARY.md
```

---

## 👤 AUTOR

**JuanenRac (Electro Hobby 3D)** · electrohobby3d@gmail.com

## 📜 LICENCIA

GPL-3.0-or-later - véase [LICENSE](LICENSE).
