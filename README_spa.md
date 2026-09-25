<p align="center">
  <img src="images/ARMOR_BANNER.svg" alt="ARMOR-RADAR banner" width="100%">
</p>

# 📡 ARMOR-RADAR

<p align="center"><a href="README.md">🇺🇸 English</a> | 🇪🇸 <b>Español</b></p>

### Firmware del nodo de campo (Waveshare ESP32-S3-ETH, tres radares o sensores de presencia, Ethernet y Wi-Fi) con su propio panel web, y su núcleo probado en el ordenador

<p align="center">
  <img src="https://img.shields.io/badge/License-GPL%203.0-blue.svg" alt="GPL 3.0">
  <img src="https://img.shields.io/badge/Language-C%2B%2B17-00599c.svg" alt="Language">
  <img src="https://img.shields.io/badge/Target-ESP32--S3-e7352c.svg" alt="Target">
  <img src="https://img.shields.io/badge/Maturity-scaffolding-FFB020.svg" alt="Maturity">
</p>

---

**Comprobación de honestidad - qué funciona hoy:** **Madurez: scaffolding.** El núcleo independiente del hardware (748 comprobaciones: los decodificadores del LD2450, del LD2461 y de cuatro sensores de presencia, el canal de comandos del LD2450, los ajustes y sus comprobaciones, la tabla de pines, usuarios y sesiones, la lógica de los pines asignados, el plan de red y el serializador de mensajes, cuya salida acepta ARMOR-COMMON) se prueba en el ordenador, el **panel web** se ejercitó en un navegador real contra un nodo simulado, y la **imagen del firmware compila** en el contenedor de ESP-IDF 5.4.2. **Nunca se ha ejecutado en una placa**: no se ha capturado ninguna trama de un módulo real, el código de Ethernet, del puente Wi-Fi, del sensor de luz y de actualización no se ha probado, el canal de comandos del radar no está comprobado con el documento del fabricante ni con un módulo, el panel va por HTTP sin cifrar y los decodificadores del LD2461 y de los sensores de presencia solo coinciden con los ejemplos resueltos de sus manuales, sin ningún módulo detrás.

---

## 1. 🛠️ DESCRIPCIÓN

* **Dos nodos de 270 grados:** cada Waveshare ESP32-S3-ETH lee hasta tres radares LD2450 en sus tres UART, montados con 75 grados de separación, por Ethernet cableada (W5500) con DHCP o dirección fija, alimentada por PoE o USB. *Añadir un nodo de 270°* en Studio crea los tres radares ya asignados al nodo.
* **Un panel web en cada nodo,** con el aspecto de Studio y sus siete idiomas, integrado en el firmware: resumen, red, Wi-Fi, broker, radares, pines, usuarios, actualización del firmware y registro. Un nodo sin usuarios abre el Wi-Fi `ARMOR-SETUP-xxxxxx` y crea su primer administrador con un código de configuración; las contraseñas usan PBKDF2 con sal, las sesiones son tokens aleatorios y todos los ajustes viven en la flash del nodo, así que una imagen sirve para todos los nodos y no se compila ninguna contraseña ([el panel](docs/NODE_PANEL.md)).
* **Un solo Wi-Fi desde muchos nodos:** cada nodo puede ofrecer un punto de acceso unido a su puerto Ethernet; dales el mismo nombre y contraseña y el canal en automático (1, 6 u 11 según la MAC) y los móviles y sensores Wi-Fi ven una red con un servidor DHCP. No es una malla por radio: cada nodo conserva su cable. Un nodo puede en cambio unirse al Wi-Fi de un router (con búsqueda de redes), y uno sin cable se puede configurar por **Bluetooth** desde la app de Android.
* **Pines para el servidor:** cualquier pin libre pasa a ser una entrada, una salida (con estado seguro si se pierde el broker), PWM o una lectura analógica, y aparece como dispositivo del servidor, así que un relé o un contacto no necesita firmware nuevo. Los pines reservados de la placa nunca se ofrecen.
* **Actualizaciones por el aire con vuelta atrás** desde el panel (dos ranuras en la flash de 16 MB), y un **enlace desde Studio** al panel de cada nodo, a partir de la dirección que el nodo publica.
* **Seis modelos de sensor, uno por puerto:** un puerto lleva un LD2450 o un LD2461 (rastreadores que alimentan el perímetro, con el campo propio del modelo en Studio) o un sensor de presencia (LD2410, LD2412, LD2410S, MR24HPC1) que pasa a ser un dispositivo del servidor y publica presencia y distancia. Se elige en el panel; qué es cada uno, su protocolo y lo que no se verificó está en `docs/SENSORS.md`.
* **Decodificador LD2450, salud y configuración:** un delimitador que se resincroniza encuentra tramas en un flujo ruidoso; cada trama de 30 bytes da hasta tres objetivos que pasan a ser pistas del contrato; la consola y el panel dicen por radar si informa, está en silencio o llega ininteligible. El panel también puede leer la versión del módulo, elegir uno o tres objetivos y fijar zonas de detección (un protocolo sin comprobar con un módulo).
* **Luz ambiente y mensajes exactos al contrato:** el VEML7700 con rango automático; JSON de telemetría, salud e información que sigue los esquemas publicados y se niega a escribir algo inválido, con marcas de tiempo reales (SNTP) y un last will de MQTT. La telemetría se retiene mientras ningún radar informa, nunca un «todo despejado» vacío.
* **Una imagen para todas las placas, como un producto de red:** el firmware es el mismo y la MAC distingue las placas (`armor-` y seis dígitos hasta que se le da nombre); `adopt_node.py` da a un nodo recién grabado su administrador, su identidad en el broker y los ajustes comunes de la flota por la red, con un código de configuración calculado desde su MAC, así que 5 nodos o 27 dan el mismo trabajo. **Herramientas de banco:** grabación por USB-C, un nodo simulado para trabajar en el panel y un conversor de un registro de tramas en bruto a un fixture de pruebas ([puesta en marcha](docs/BENCH_BRINGUP.md)).

---

## 2. 🔧 COMPILAR Y EJECUTAR

```bash
cmake -S tests -B build/host && cmake --build build/host
build/host/test_core && build/host/test_node && build/host/test_sensors   # 748 comprobaciones, -Werror
build/host/emit_samples | python tests/check_contract.py
node tools/panel_mock.mjs --user admin:adminpass123   # el panel sin placa
python tools/make_fleet.py                             # una vez: el secreto de la flota y los ajustes comunes
tools/build_node.sh generic                           # UNA imagen para todas las placas, en el contenedor de ESP-IDF
```

```bat
tools\flash.bat generic COM5 monitor                  # cada placa, una vez, por USB-C
```

```bash
tools/adopt_node.py 192.168.0.181 --id perimetro-3 --fleet secrets/fleet.json --broker-ssh-host <cm5> --broker-ssh-user <usuario>
```

Los tests en el ordenador necesitan cualquier compilador C++17 (Linux, WSL, MSYS2). Véanse la [puesta en marcha](docs/BENCH_BRINGUP.md), el [panel](docs/NODE_PANEL.md) y el [límite del hardware](docs/HARDWARE_BOUNDARY.md).

---

## 📂 ESTRUCTURA DE DIRECTORIOS

```text
ARMOR-RADAR/
├── main/
│   ├── app_main.cpp        arranque: ajustes, radares, pines, red, panel, broker
│   ├── node_store.cpp      ajustes y usuarios en la flash
│   ├── network.cpp         Ethernet, punto de acceso Wi-Fi y puente, estación
│   ├── web_server.cpp      el panel y su API JSON, acceso, actualización
│   ├── radar_manager.cpp   UART, tramas, salud, canal de comandos
│   ├── gpio_manager.cpp    los pines asignados para el servidor
│   ├── mqtt_link.cpp       hora, salud, telemetría, información, temas de pines
│   ├── board_ethernet.cpp, light_sensor.cpp, log_buffer.cpp, entropy.cpp
│   ├── Kconfig.projbuild   los primeros ajustes de una compilación
│   └── core/               framer, ld2450, ld2450_command, node_config, board_pins, network_plan, auth, gpio_logic, json, veml7700, telemetry_json...
├── panel/                  index.html, app.js, text.js (7 idiomas), style.css
├── tests/                  test_core.cpp, test_node.cpp, emit_samples.cpp, check_contract.py, test_tools.py
├── tools/                  build_node.sh, make_fleet.py, adopt_node.py, provision_node.sh, flash.bat, pack_panel.py, panel_mock.mjs, frames_to_fixture.py
├── secrets/                node.conf.example, fleet.example.json (los reales están fuera de git)
├── partitions.csv, sdkconfig.defaults
└── docs/                   BENCH_BRINGUP.md, NODE_PANEL.md, HARDWARE_BOUNDARY.md
```

---

## 👤 AUTOR

**JuanenRac (Electro Hobby 3D)** · electrohobby3d@gmail.com

## 📜 LICENCIA

GPL-3.0-or-later - véase [LICENSE](LICENSE).
