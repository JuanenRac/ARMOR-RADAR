<p align="center">
  <img src="images/ARMOR_BANNER.svg" alt="ARMOR-RADAR banner" width="100%">
</p>

# 📡 ARMOR-RADAR

<p align="center">
  <a href="README.md">🇺🇸 English</a> |
  <a href="README_spa.md">🇪🇸 Español</a> |
  🇫🇷 <b>Français</b> |
  <a href="README_ita.md">🇮🇹 Italiano</a> |
  <a href="README_deu.md">🇩🇪 Deutsch</a> |
  <a href="README_zho.md">🇨🇳 简体中文</a> |
  <a href="README_jpn.md">🇯🇵 日本語</a>
</p>

### Firmware de nœud de terrain (Waveshare ESP32-S3-ETH, trois radars ou capteurs de présence, Ethernet et Wi-Fi) avec son propre panneau web, et son cœur testé sur ordinateur

<p align="center">
  <img src="https://img.shields.io/badge/License-GPL%203.0-blue.svg" alt="GPL 3.0">
  <img src="https://img.shields.io/badge/Language-C%2B%2B17-00599c.svg" alt="Language">
  <img src="https://img.shields.io/badge/Target-ESP32--S3-e7352c.svg" alt="Target">
  <img src="https://img.shields.io/badge/Maturity-scaffolding-FFB020.svg" alt="Maturity">
</p>

---

**Vérification d'honnêteté - ce qui fonctionne aujourd'hui:** **Maturité : scaffolding.** Le cœur indépendant du matériel (797 contrôles : les décodeurs du LD2450, du LD2461 et de quatre capteurs de présence, le canal de commande du LD2450, les réglages et leurs vérifications, la table des broches, les utilisateurs et sessions, la logique des broches associées, le plan réseau et le sérialiseur de messages, dont la sortie est acceptée par ARMOR-COMMON) est testé sur ordinateur, le **panneau web** a été essayé dans un vrai navigateur face à un nœud simulé, et l'**image du firmware se compile** dans le conteneur ESP-IDF 5.4.2. **Il n'a jamais tourné sur une carte** : aucune trame n'a été capturée depuis un vrai module, le code Ethernet, pont Wi-Fi, capteur de lumière et mise à jour est inessayé, le canal de commande des radars n'est vérifié ni face au document du fabricant ni face à un module, le certificat HTTPS du panneau est fabriqué par le nœud lui-même et inessayé dans un vrai navigateur, et les décodeurs du LD2461 et des capteurs de présence ne correspondent qu'aux exemples de leurs manuels, sans module réel.

---

## 🎯 Présentation

* **Deux nœuds de 270 degrés :** chaque Waveshare ESP32-S3-ETH lit jusqu'à trois radars LD2450 sur ses trois UART, montés à 75 degrés l'un de l'autre, par Ethernet filaire (W5500) en DHCP ou adresse fixe, alimenté en PoE ou USB. *Add a 270° node* de Studio crée les trois radars déjà associés au nœud.
* **Un panneau web sur chaque nœud,** à l'apparence de Studio et en sept langues, intégré au firmware : vue d'ensemble, réseau, Wi-Fi, broker, radars, broches, utilisateurs, mise à jour du firmware et journal. Un nœud sans utilisateur ouvre le Wi-Fi `ARMOR-SETUP-xxxxxx` et crée son premier administrateur avec un code d'installation ; les mots de passe sont en PBKDF2 salé, les sessions sont des jetons aléatoires et tous les réglages sont dans la flash du nœud, si bien qu'une image sert tous les nœuds et qu'aucun mot de passe n'est compilé ([le panneau](docs/NODE_PANEL.md)).
* **Un seul Wi-Fi à partir de plusieurs nœuds :** chaque nœud peut offrir un point d'accès relié à son port Ethernet ; avec le même nom et mot de passe et le canal en automatique (1, 6 ou 11 selon la MAC), téléphones et capteurs Wi-Fi voient un seul réseau avec un seul serveur DHCP. Ce n'est pas un maillage radio : chaque nœud garde son câble. Un nœud peut aussi rejoindre le Wi-Fi d'un routeur (avec recherche de réseaux), et un nœud sans câble peut être configuré en **Bluetooth** depuis l'application Android.
* **Broches pour le serveur :** toute broche libre devient une entrée, une sortie (avec un état sûr si le broker est perdu), du PWM ou une lecture analogique, et apparaît comme un appareil du serveur : un relais ou un contact n'exige aucun nouveau firmware. Les broches réservées de la carte ne sont jamais proposées.
* **Mises à jour par radio (OTA) avec retour arrière** depuis le panneau (deux emplacements sur la flash de 16 Mo), et un **lien depuis Studio** vers le panneau de chaque nœud, à partir de l'adresse que le nœud publie.
* **Le panneau en HTTPS :** le nœud fabrique son propre certificat (auto-signé, conservé en flash) et sert le panneau sur le port 443 en plus du 80, ou seulement sur le 443 ; le cookie de session est marqué Secure et l'empreinte du certificat s'affiche pour la comparer à l'avertissement du navigateur. **Identités de pistes stables :** les cibles de chaque trame sont associées à celles suivies auparavant ; une personne garde donc le même identifiant, une trame perdue ne fait pas clignoter et les positions sont un peu lissées.
* **Six modèles de capteurs, un par port :** un port porte un LD2450 ou un LD2461 (traqueurs qui alimentent le périmètre, avec le champ propre du modèle dans Studio) ou un capteur de présence (LD2410, LD2412, LD2410S, MR24HPC1) qui devient un appareil du serveur et publie présence et distance. Choisi dans le panneau ; ce qu'est chacun, son protocole et ce qui n'a pas été vérifié sont dans `docs/SENSORS.md`.
* **Décodeur, santé et configuration du LD2450 :** un découpeur qui se resynchronise trouve les trames dans un flux bruité ; chaque trame de 30 octets donne jusqu'à trois cibles qui deviennent des pistes du contrat ; la console et le panneau indiquent par radar s'il émet, se tait ou envoie des données illisibles. Le panneau peut aussi lire la version du module, choisir une ou trois cibles et définir des zones de détection (un protocole non vérifié face à un module).
* **Lumière ambiante et messages conformes au contrat :** le VEML7700 avec plage automatique ; JSON de télémétrie, de santé et d'information qui suit les schémas publiés et refuse d'écrire quoi que ce soit d'invalide, avec des horodatages d'horloge murale (SNTP) et un testament MQTT. La télémétrie est retenue tant qu'aucun radar n'émet, jamais un « tout est calme » vide.
* **Une image pour toutes les cartes, comme un produit réseau :** le firmware est le même et l'adresse MAC distingue les cartes (`armor-` et six chiffres jusqu'au nommage) ; `adopt_node.py` donne à un nœud fraîchement flashé son administrateur, son identité de broker et les réglages communs de la flotte par le réseau, avec un code d'installation calculé depuis sa MAC : 5 nœuds ou 27, c'est le même travail. **Outils de banc :** flashage par USB-C, un nœud simulé pour travailler sur le panneau et un convertisseur d'un journal de trames brutes en jeu de test ([mise en service sur banc](docs/BENCH_BRINGUP.md)).

## 📂 Structure du dépôt

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

## 🛠️ Environnement de développement

```bash
cmake -S tests -B build/host && cmake --build build/host
build/host/test_core && build/host/test_node && build/host/test_sensors   # 797 checks, -Werror
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

Les tests sur ordinateur demandent un compilateur C++17 (Linux, WSL, MSYS2). Voir la [mise en service sur banc](docs/BENCH_BRINGUP.md), le [panneau](docs/NODE_PANEL.md) et la [frontière matérielle](docs/HARDWARE_BOUNDARY.md).

## 🔗 Projets liés

**A.R.M.O.R.** (Autonomous Radar & Multimodal Observation Range) est un système de sécurité périmétrique composé de dépôts indépendants. Chacun a sa propre version, ses propres tests et son propre README ; voici la famille :

* **[ARMOR-COMMON](../ARMOR-COMMON)** - Contrats de messages, validateurs, vecteurs de conformité et types générés
* **ARMOR-RADAR** (ce dépôt) - Firmware du nœud de terrain pour ESP32-S3 avec trois radars et son propre panneau web
* **[ARMOR-SOLAR](../ARMOR-SOLAR)** - Protocoles des onduleurs et batteries solaires et messages d'un nœud passerelle
* **[ARMOR-SERVER](../ARMOR-SERVER)** - Coordinateur central : télémétrie, alarmes, appareils, relevés solaires et caméras
* **[ARMOR-STUDIO](../ARMOR-STUDIO)** - Console web : caméras, radar, alarmes, énergie solaire et concepteur de site 2D/3D
* **[ARMOR-ANDROID-CONTROL](../ARMOR-ANDROID-CONTROL)** - Client Android de l'opérateur avec radar 2D/3D en direct
* **[ARMOR-SERVER-AI](../ARMOR-SERVER-AI)** - Politique d'inférence visuelle qui explique ses décisions et n'agit jamais
* **[ARMOR-VOICE-AI](../ARMOR-VOICE-AI)** - Intentions vocales hors ligne avec une confirmation impossible à falsifier
* **[ARMOR-HARDWARE](../ARMOR-HARDWARE)** - Boîtiers, électronique et matrice d'acceptation sur banc
* **[ARMOR-DEVOPS](../ARMOR-DEVOPS)** - Déploiement, banc d'essai CM5, sauvegarde et TLS
* **[ARMOR-SIMULATOR](../ARMOR-SIMULATOR)** - Simulateur de télémétrie hors ligne avec des pannes reproductibles
* **[ARMOR-DOCS](../ARMOR-DOCS)** - Architecture, base de sécurité et matrice des capacités

## 📚 Documentation et communauté

Pour en savoir plus :

* [Matrice des capacités : ce qui est prouvé et ce qui ne l'est pas](../ARMOR-DOCS/docs/CAPABILITY_MATRIX.md)
* [Catalogue des projets : versions et dépendances entre les dépôts](../ARMOR-DOCS/docs/PROJECT_CATALOG.md)
* [Historique des modifications de ce dépôt](CHANGELOG.md)
* [Licence (GPL-3.0-or-later)](LICENSE)
* Questions, idées et rapports : electrohobby3d@gmail.com

## 👤 AUTEUR

**JuanenRac (Electro Hobby 3D)** · electrohobby3d@gmail.com

## 📜 LICENCE

GPL-3.0-or-later - voir [LICENSE](LICENSE).
