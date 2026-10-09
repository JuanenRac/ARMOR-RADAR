<p align="center">
  <img src="images/ARMOR_BANNER.svg" alt="ARMOR-RADAR banner" width="100%">
</p>

# 📡 ARMOR-RADAR

<p align="center">
  <a href="README.md">🇺🇸 English</a> |
  <a href="README_spa.md">🇪🇸 Español</a> |
  <a href="README_fra.md">🇫🇷 Français</a> |
  <a href="README_ita.md">🇮🇹 Italiano</a> |
  🇩🇪 <b>Deutsch</b> |
  <a href="README_zho.md">🇨🇳 简体中文</a> |
  <a href="README_jpn.md">🇯🇵 日本語</a>
</p>

### Feldknoten-Firmware (Waveshare ESP32-S3-ETH, drei Radare oder Präsenzsensoren, Ethernet und WLAN) mit eigenem Web-Panel und ihrem am Rechner getesteten Kern

<p align="center">
  <img src="https://img.shields.io/badge/License-GPL%203.0-blue.svg" alt="GPL 3.0">
  <img src="https://img.shields.io/badge/Language-C%2B%2B17-00599c.svg" alt="Language">
  <img src="https://img.shields.io/badge/Target-ESP32--S3-e7352c.svg" alt="Target">
  <img src="https://img.shields.io/badge/Maturity-scaffolding-FFB020.svg" alt="Maturity">
</p>

---

**Ehrlichkeitsprüfung - was heute läuft:** **Reifegrad: Gerüst.** Der hardwareunabhängige Kern (die Decoder des LD2450, des LD2461 und von vier Präsenzsensoren, der Befehlskanal des LD2450, die Einstellungen und ihre Prüfungen, die Pin-Tabelle, Benutzer und Sitzungen, die Logik abgebildeter Pins, der Netzplan und der Nachrichten-Serialisierer, dessen Ausgabe ARMOR-COMMON akzeptiert) ist auf einem Computer getestet (925 Prüfungen), das Web-Panel wurde in einem echten Browser gegen einen Ersatzknoten erprobt und das Firmware-Image baut im ESP-IDF-Container. **Die Firmware läuft auf zwei echten Knoten** (Waveshare ESP32-S3-ETH): das Panel mit seinem ersten Administrator und den Anmeldungen, die im Flash gehaltenen Einstellungen, die Ethernet-Verbindung, die Broker-Verbindung mit einem Konto je Knoten, die Telemetrie, die Server und Studio erreicht, die Live-Karte und das Over-the-air-Update (vom Panel, aus einer Datei, vom GitHub-Release und von Studio) wurden darauf benutzt, und dabei wurden echte Fehler gefunden und behoben. **Auf einem echten Knoten noch nicht festgehalten:** der Befehlskanal des Radars gegen das Dokument des Herstellers, die Decoder des LD2461 und der Präsenzsensoren (sie stimmen nur mit den Beispielen ihrer Handbücher überein), der an das Kabel gebrückte WLAN-Zugangspunkt, der Lichtsensor, der Strom, den das PoE-Modul liefert, die Bluetooth-Einrichtung vom Telefon und wie jeder Browser das vom Panel selbst erzeugte HTTPS-Zertifikat behandelt.

---

## 🎯 Überblick

* **Zwei Platinen, eine Firmware:** die Waveshare ESP32-S3-ETH (Ethernet, Standard) und ein ESP32-S3-WROOM-1 N16R8 ohne Ethernet (nur WLAN: die Einrichtung fragt nach dem WLAN zum Beitreten, und der Knoten behält immer sein eigenes Netz). Das Image wird beim Bauen gewählt (`tools/build_node.sh generic s3-eth` oder `generic s3-wifi`); Pin-Tabelle und Zugangsweg folgen der Platine, das Panel blendet aus, was die Platine nicht hat, und ein Image gilt nur für seine Platine. Beide bauen und sind am Rechner getestet; keine lief je auf einer Platine.
* **Zwei Knoten mit 270 Grad:** jedes Waveshare ESP32-S3-ETH liest bis zu drei LD2450-Radare an seinen drei UARTs, im Abstand von 75 Grad montiert, über kabelgebundenes Ethernet (W5500) mit DHCP oder fester Adresse, gespeist per PoE oder USB. Studios *Add a 270° node* legt die drei Radare bereits dem Knoten zugeordnet an.
* **Ein Web-Panel auf jedem Knoten,** im Aussehen von Studio und in sieben Sprachen, in die Firmware eingebettet: Übersicht, Netzwerk, WLAN, Broker, Radare, Pins, Benutzer, Firmware-Update und Protokoll. Ein Knoten ohne Benutzer öffnet das WLAN `ARMOR-SETUP-xxxxxx` und legt mit einem Einrichtungscode seinen ersten Administrator an; Passwörter sind gesalzenes PBKDF2, Sitzungen zufällige Token, und jede Einstellung liegt im Flash des Knotens, sodass ein Image alle Knoten bedient und kein Passwort einkompiliert ist ([das Panel](docs/NODE_PANEL.md)).
* **Ein WLAN aus vielen Knoten:** jeder Knoten kann einen an seinen Ethernet-Port angebundenen Access Point anbieten; mit gleichem Namen und Passwort und dem Kanal auf automatisch (1, 6 oder 11 nach MAC) sehen Telefone und WLAN-Sensoren ein Netz mit einem DHCP-Server. Es ist kein Funk-Mesh: jeder Knoten behält sein Kabel. Ein Knoten kann stattdessen dem WLAN eines Routers beitreten (mit Netzwerksuche), und einer ohne Kabel lässt sich per **Bluetooth** über die Android-App konfigurieren.
* **Pins für den Server:** jeder freie Pin wird zu einem Eingang, einem Ausgang (mit sicherem Zustand bei Verlust des Brokers), PWM oder einer analogen Messung und erscheint als Gerät des Servers, sodass ein Relais oder ein Kontakt keine neue Firmware braucht. Die reservierten Pins der Platine werden nie angeboten.
* **Over-the-Air-Updates mit Rollback** aus dem Panel (zwei Slots im 16-MB-Flash) und ein **Link aus Studio** zum Panel jedes Knotens, über die Adresse, die der Knoten veröffentlicht.
* **Das Panel über HTTPS:** der Knoten erzeugt sein eigenes Zertifikat (selbstsigniert, im Flash gehalten) und bedient das Panel auf Port 443 zusätzlich zu 80 oder nur auf 443; das Sitzungs-Cookie ist als Secure markiert und der Fingerabdruck des Zertifikats wird zum Vergleich mit der Browserwarnung angezeigt. **Stabile Spur-Identitäten:** die Ziele jedes Rahmens werden den zuvor verfolgten zugeordnet, sodass eine Person dieselbe Spur-ID behält, ein verlorener Rahmen nicht flackert und die Positionen leicht geglättet werden.
* **Sechs Sensormodelle, eines je Port:** ein Port trägt einen LD2450 oder LD2461 (Tracker, die den Perimeter speisen, mit dem eigenen Feld des Modells in Studio) oder einen Präsenzsensor (LD2410, LD2412, LD2410S, MR24HPC1), der zum Gerät des Servers wird und Anwesenheit und Entfernung veröffentlicht. Im Panel gewählt; was jeder ist, sein Protokoll und was nicht geprüft wurde, steht in `docs/SENSORS.md`.
* **LD2450-Dekoder, Zustand und Konfiguration:** ein neu synchronisierender Rahmenfinder findet Rahmen in einem verrauschten Strom; jeder 30-Byte-Rahmen liefert bis zu drei Ziele, die zu Vertragsspuren werden; Konsole und Panel sagen je Radar, ob es meldet, schweigt oder Unsinn sendet. Das Panel kann auch die Version des Moduls lesen, ein oder drei Ziele wählen und Erkennungszonen setzen (ein an einem Modul ungeprüftes Protokoll).
* **Umgebungslicht und vertragsgenaue Nachrichten:** der VEML7700 mit automatischem Bereich; Telemetrie-, Zustands- und Informations-JSON folgt den veröffentlichten Schemas und weigert sich, Ungültiges zu schreiben, mit Wanduhr-Zeitstempeln (SNTP) und einem MQTT-Last-Will. Telemetrie wird zurückgehalten, solange kein Radar meldet, nie eine leere „Entwarnung“.
* **Ein Image für jede Platine, wie ein Netzwerkprodukt:** die Firmware ist dieselbe und die MAC unterscheidet die Platinen (`armor-` und sechs Ziffern bis zur Benennung); `adopt_node.py` gibt einem frisch geflashten Knoten über das Netzwerk seinen Administrator, seine Broker-Identität und die gemeinsamen Einstellungen der Flotte, mit einem aus seiner MAC berechneten Einrichtungscode: 5 Knoten oder 27 sind dieselbe Arbeit. **Werkzeuge am Prüfstand:** Flashen per USB-C, ein Stellvertreter-Knoten zum Arbeiten am Panel und ein Konverter von einem Protokoll roher Rahmen zu einem Testfixture ([Inbetriebnahme am Prüfstand](docs/BENCH_BRINGUP.md)).
* **Update von GitHub oder aus einer Datei, mit Fortschrittsbalken:** das Panel folgt den Weiterleitungen von GitHub, prüft den neben der Firmware veröffentlichten SHA-256 (`armor_radar.bin.sha256`) und zeigt Download und Schreiben als Balken; der Knoten startet mit dem neuen Image neu und kehrt von selbst zurück, wenn es nicht startet. Eine Live-Karte dessen, was die Radare sehen, wird per Websocket vom Knoten geliefert.

## 📂 Struktur des Repositorys

```text
ARMOR-RADAR/
├── main/
│   ├── app_main.cpp        start-up: settings, radars, pins, network, panel, broker, Bluetooth
│   ├── node_store.cpp      settings and users in flash
│   ├── network.cpp         Ethernet, Wi-Fi access point and bridge, station
│   ├── web_server.cpp      the panel and its JSON API, login, update
│   ├── api_shared.cpp      the operations the panel and Bluetooth share (status, settings, Wi-Fi scan)
│   ├── ble_provision.cpp   configuration from a phone over Bluetooth (NimBLE)
│   ├── radar_manager.cpp   UARTs, frames, health, command channel (armor_radar.cpp, radar_tracks.hpp)
│   ├── gpio_manager.cpp    the pins mapped for the server
│   ├── mqtt_link.cpp       clock, health, telemetry, information, pin topics
│   ├── board_ethernet.cpp, light_sensor.cpp, tls_cert.cpp, log_buffer.cpp, entropy.cpp
│   ├── Kconfig.projbuild   the first settings of a build
│   └── core/               framer, ld2450, ld2450_command, ld2461, presence, sensor_model, node_config, board_pins, network_plan, auth, ble_frame, ble_dispatch, gpio_logic, json, veml7700, telemetry_json...
├── panel/                  index.html, app.js, text.js (7 languages), style.css
├── tests/                  test_core.cpp, test_node.cpp, test_sensors.cpp, test_board_wifi.cpp, emit_samples.cpp, check_contract.py, test_tools.py
├── tools/                  build_node.sh, make_fleet.py, adopt_node.py, provision_node.sh, flash.bat, pack_panel.py, panel_mock.mjs, frames_to_fixture.py
├── secrets/                node.conf.example, fleet.example.json (the real files are git-ignored)
├── partitions.csv, sdkconfig.defaults, sdkconfig.board.*
└── docs/                   BENCH_BRINGUP.md, NODE_PANEL.md, BLE_PROVISIONING.md, SENSORS.md, HARDWARE_BOUNDARY.md
```

## 🛠️ Entwicklungsumgebung

```bash
cmake -S tests -B build/host && cmake --build build/host
build/host/test_core && build/host/test_node && build/host/test_sensors && build/host/test_board_wifi   # 925 checks, -Werror
build/host/emit_samples | python tests/check_contract.py
node tools/panel_mock.mjs --user admin:adminpass123   # the panel without a board
python tools/make_fleet.py                             # once: the fleet secret and the shared settings
tools/build_node.sh generic                           # ONE image for every Waveshare board (dist/generic-s3-eth.bin), in the ESP-IDF container
tools/build_node.sh generic s3-wifi                   # the same firmware for an ESP32-S3-WROOM-1 N16R8 with no Ethernet
```

```bat
tools\flash.bat generic COM5 monitor                  # each Waveshare board, once, by USB-C (add s3-wifi for the other board)
```

```bash
tools/adopt_node.py 192.168.0.181 --id perimetro-3 --fleet secrets/fleet.json --broker-ssh-host <cm5> --broker-ssh-user <user>
```

Die Rechnertests brauchen einen C++17-Compiler (Linux, WSL, MSYS2). Siehe die [Inbetriebnahme am Prüfstand](docs/BENCH_BRINGUP.md), das [Panel](docs/NODE_PANEL.md) und die [Hardwaregrenze](docs/HARDWARE_BOUNDARY.md).

## 🔗 Verwandte Projekte

**A.R.M.O.R.** (Autonomous Radar & Multimodal Observation Range) ist ein Perimeter-Sicherheitssystem aus unabhängigen Repositorys. Jedes hat eine eigene Version, eigene Tests und ein eigenes README; hier ist die Familie:

* **[ARMOR-COMMON](https://github.com/JuanenRac/ARMOR-COMMON)** - Nachrichtenverträge, Validierer, Konformitätsvektoren und generierte Typen
* **ARMOR-RADAR** (dieses Repository) - Feldknoten-Firmware für ESP32-S3 mit drei Radaren und eigenem Web-Panel
* **[ARMOR-SOLAR](https://github.com/JuanenRac/ARMOR-SOLAR)** - Protokolle für Solar-Wechselrichter und -Batterien und die Nachrichten eines Gateway-Knotens
* **[ARMOR-ELECTRICAL](https://github.com/JuanenRac/ARMOR-ELECTRICAL)** - Elektroknoten: Zähler, die Nachricht der Netzmesswerte und die Regeln fürs Schalten
* **[ARMOR-HMI](https://github.com/JuanenRac/ARMOR-HMI)** - Touch-Panel: der Systemzustand auf einem Wandbildschirm, Scharf- und Quittieren sowie das Zuhause des Sprachassistenten
* **[ARMOR-NETWORK](https://github.com/JuanenRac/ARMOR-NETWORK)** - Das lokale Netzwerk: seine Geräte, das Internet und was sich ändert
* **[ARMOR-SERVER](https://github.com/JuanenRac/ARMOR-SERVER)** - Zentraler Koordinator: Telemetrie, Alarme, Geräte, Solarmesswerte und Kameras
* **[ARMOR-STUDIO](https://github.com/JuanenRac/ARMOR-STUDIO)** - Web-Konsole: Kameras, Radar, Alarme, Solarenergie und 2D/3D-Standortdesigner
* **[ARMOR-ANDROID-CONTROL](https://github.com/JuanenRac/ARMOR-ANDROID-CONTROL)** - Android-Bedienclient mit Live-Radar in 2D/3D
* **[ARMOR-SERVER-AI](https://github.com/JuanenRac/ARMOR-SERVER-AI)** - Visuelle Inferenzrichtlinie, die ihre Entscheidungen erklärt und nie handelt
* **[ARMOR-VOICE-AI](https://github.com/JuanenRac/ARMOR-VOICE-AI)** - Offline-Sprachabsichten mit einer nicht fälschbaren Bestätigung
* **[ARMOR-HARDWARE](https://github.com/JuanenRac/ARMOR-HARDWARE)** - Gehäuse, Elektronik und die Abnahmematrix am Prüfstand
* **[ARMOR-DEVOPS](https://github.com/JuanenRac/ARMOR-DEVOPS)** - Bereitstellung, CM5-Prüfstand, Backup und TLS
* **[ARMOR-SIMULATOR](https://github.com/JuanenRac/ARMOR-SIMULATOR)** - Offline-Telemetriesimulator mit wiederholbaren Fehlern
* **[ARMOR-UPDATER](https://github.com/JuanenRac/ARMOR-UPDATER)** - Erkennt, installiert und aktualisiert die eigenen Repositories des Ökosystems
* **[ARMOR-DOCS](https://github.com/JuanenRac/ARMOR-DOCS)** - Architektur, Sicherheitsgrundlage und die Fähigkeitsmatrix

## 📚 Dokumentation und Community

Hier gibt es mehr zu lesen:

* [Fähigkeitsmatrix: was belegt ist und was nicht](https://github.com/JuanenRac/ARMOR-DOCS/blob/main/docs/CAPABILITY_MATRIX.md)
* [Projektkatalog: Versionen und wie die Repositorys voneinander abhängen](https://github.com/JuanenRac/ARMOR-DOCS/blob/main/docs/PROJECT_CATALOG.md)
* [Änderungsverlauf dieses Repositorys](CHANGELOG.md)
* [Lizenz (GPL-3.0-or-later)](LICENSE)
* Fragen, Ideen und Meldungen: electrohobby3d@gmail.com

## 👤 AUTOR

**JuanenRac (Electro Hobby 3D)** · electrohobby3d@gmail.com

## 📜 LIZENZ

GPL-3.0-or-later - siehe [LICENSE](LICENSE).
