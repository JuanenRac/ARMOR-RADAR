# The node's web panel

Every node serves its own panel on port 80, from the address it has (or from its own set-up Wi-Fi). It looks like ARMOR-STUDIO
(same colours, the menu on the left), speaks the seven languages of the project, and is embedded in the firmware, so it needs no
Internet and a firmware update updates it too. The panel runs on two real nodes (login, settings, firmware update, live map); it was also exercised against a
stand-in node (`tools/panel_mock.mjs`) in a real browser, and the node side is tested on a computer for its logic.

## First start: set-up

A node that has no user is in **set-up**: it answers only the set-up screen, and it opens a Wi-Fi network of its own,
`ARMOR-SETUP-xxxxxx` (WPA2, never bridged), so a node with no cable can be reached too (join it and open `http://192.168.4.1/`). A node that is set up but has no address 90 seconds after it starts (no cable, no Wi-Fi it can join, no access point of its own) opens the same set-up Wi-Fi again, with the set-up code as its password, so it can always be reached. A fixed address (address, mask, gateway, DNS) can be given for the cable or for the Wi-Fi station.

The first administrator is created with a **set-up code**. The code is either

* the one of the build (`CONFIG_ARMOR_SETUP_CODE`; `tools/provision_node.sh` writes one into the node's secrets file), or
* a random one, made once and kept in flash so that it is the same at every start, shown on the USB console every 15 seconds.

The code is also the password of the set-up Wi-Fi. It opens the panel only while the node has no user; it stays the password of the node's rescue Wi-Fi afterwards. After the administrator is created the node
restarts, which closes the set-up network. The set-up screen also takes a Wi-Fi network for the node to join (optional on a board with a cable: it is how a node is set up on a bench with no cable). To start again, hold the **BOOT** button for 8 seconds in the first 30 seconds after power-up
(it erases the settings and the users), or use *Factory reset* in the panel.

## Many nodes: one image, adopted over the network

The firmware is the same for every board and the **MAC** tells the boards apart: a new node is `armor-` and the last six digits of its MAC. Build the
universal image once (`tools/build_node.sh generic`), flash it to every board by USB-C, and adopt each node from the computer:
`tools/adopt_node.py <address> --id <name> --fleet secrets/fleet.json ...` creates its administrator, gets its identity from the broker and applies the
settings every node shares (Wi-Fi, broker, language). With a fleet secret (`python tools/make_fleet.py`) the set-up code of a board is
HMAC-SHA256(secret, MAC), so no cable is needed; the secret is inside the image, so it protects a fleet against the network and not against someone
holding a board.

## Pages

| Page | What it does |
|---|---|
| Overview | node, hardware, date and time (with where the time comes from), flash memory (total, reserved by the partitions, what the two firmware slots hold and which one runs), network, broker and the state of the three radars, refreshed every three seconds |
| Network | Ethernet or Wi-Fi as the connection; DHCP or a fixed address, mask, gateway, two DNS servers, host name; the time zone and the time server (or, with the server off, the browser's clock); Bluetooth; automatic restart; the configuration file (download and load) |
| Wi-Fi | the node's own access point (name, security, password, channel, hidden, clients, power, width, country, joined to the wire or not) and, for a node without a cable, the Wi-Fi station |
| Broker | the MQTT broker and its backups, user and password, the health and telemetry periods; the light sensor and its pins |
| Radars | per radar: connected, label, RX and TX pin, position and heading, live state; the LD2450 commands and detection zones (kept in the settings and told to the sensor again at every start) |
| Map | the three radars and what they see on one live plan, with buttons to spread them over 360° or 270° |
| Pins | pins mapped as inputs, outputs, PWM or analogue readings, with their live state and a button to switch them |
| Users | up to four users (administrator or read-only viewer), passwords, and "my password" |
| Firmware and log | update from a file, restart, factory reset, and the node's log |

Pausing the pointer over a field, a button or a menu entry shows a short hint in the panel's language. Choosing a language in the panel also stores it in the node's settings.

Settings are edited in a working copy and saved with one bar at the bottom; the node checks every value and answers with the fields
that are wrong. Almost everything applies after a restart (the bar says so and offers it).

## One Wi-Fi network from many nodes

With the access point *joined to the wired network* (the default), the wire and the Wi-Fi of a node are two ports of one bridge, so a
phone or a Wi-Fi sensor gets its address from the same DHCP server as everything else and keeps it when it moves to another node. To make
several nodes look like one network:

1. give them the **same** network name, security and password;
2. leave the channel on *automatic*: each node picks 1, 6 or 11 from its own MAC, so neighbours rarely share a channel;
3. connect all of them to the same switch.

This is not a radio mesh: every node needs its cable, and a node with no cable cannot relay another. The phone decides when to change node,
so a phone may hold on to a distant node longer than a professional system would let it. Each node serves up to ten clients, and the
throughput is that of a small microcontroller (a few megabits): meant for sensors, a phone and configuration, not for a household's
Internet. The clients of the access point are on the same network as the broker, so use WPA2 or WPA3 with a real password.

## From Studio

The node tells the server where its panel is: when it connects to the broker, and every minute, it publishes `armor/node/<id>/info` (its name, firmware, IPv4 address and port; the contract is in ARMOR-COMMON). Studio's Radar menu, *Radar nodes*, then shows a **Panel** link beside each node that has said it. The link opens the node's address, so it works from a computer on the same network as the node (Studio reached from the Internet cannot open a private address). The node's broker identity needs `armor/node/<id>/info` (`add node` grants it; `upgrade-node` adds it to older ones).

## Mapped pins

A pin given a name and a job in the panel becomes a **device of the server** (`armor/device/<node>/<pin>/…`), with no new firmware:

| Job | Reports | Commands |
|---|---|---|
| input | `{"triggered":true}`, or `open`, `on`, `tamper`, debounced; a pull-up or pull-down; inverted if wired to ground | none |
| output | `{"on":true}` (or `locked`) | `ON`, `OFF`, `TOGGLE`, `pulse` or `pulse:1500` (ms), or JSON `{"on":true}`; an initial state; **a safe state** to fall back to when the broker has been unreachable for a chosen time |
| PWM | `{"brightness":40}` | a number 0 to 100, `ON`, `OFF`, `TOGGLE`, or `{"level":40}`; up to eight pins and four different frequencies |
| analogue | a canonical field (`battery`, `lux`, `temperature`…) or the raw millivolts (`mv`), `value = mV × scale + offset` | none |

The panel shows the two topics under each pin. In Studio add a device of protocol *wifi* or *wired* with those topics; the state topic
carries a canonical field, so no mapping is needed. The node's broker identity may use `armor/device/<its id>/#`
(`mqtt_identity.sh add node` grants it; `mqtt_identity.sh upgrade-node <id>` adds it to a node created earlier).

The pin table of the board is in `main/core/board_pins.hpp`: GPIO 26 to 32 (flash), 33 to 37 (octal PSRAM), 19 and 20 (USB), 9 to 14
(Ethernet) and 8 (camera) are never offered; 0, 3, 45 and 46 (read while the chip starts) are offered with a warning; 4 to 7 are the
microSD socket. The camera connector shares pins with the header, so a wiring that uses a camera must be checked against the schematic.

## The radars' command channel

The node's TX pins let the panel talk to the LD2450: read its firmware and tracking mode, choose one target or three, detection zones
(report only what is inside, or ignore what is inside), Bluetooth on or off, restart, factory reset. The **report frame** is written from
the manufacturer's manual; the **command channel is not in that manual**. It follows the manufacturer's serial protocol as it is publicly
documented and implemented by community drivers, and it has not been checked against the manufacturer's document nor against a module.
The panel shows the module's last raw answer next to every command, and offers no change of the serial speed. The first thing to do on a
bench is *Read information*: an answer with a version such as `1.02.22062416` confirms the framing.

## Updating the firmware

*Firmware and log → Update* takes the `.bin` of this project (`build/<node>-<board>/armor_radar.bin`, for example `build/generic-s3-eth/armor_radar.bin`; not the merged image in `dist/`). The node
checks the image (its own hash and its project name) before it changes the boot partition, restarts into the new version, and the boot loader
goes back to the previous version by itself if the new one does not bring its panel up and stay up for thirty seconds. The flash holds
two application slots (`partitions.csv`). The *Firmware slots* card of the same page lists both slots with their versions and boots the other one at the next restart (to go back to the previous version, or forward to the one just installed; an administrator, with a confirmation, and never an empty slot or a firmware of another project). The first flash of a board is still USB (`tools\flash.bat`).

*Firmware and log → Search GitHub* installs the newest release of the repository by itself. A release is installed only if it carries two files: `armor_radar.bin`
(the application image, not the merged one) and `armor_radar.bin.sha256` (its SHA-256 in hex, the line `sha256sum armor_radar.bin` writes). The node downloads the
image, hashes it while it comes in and, when the hash is not the one published, throws it away without touching the boot partition; a release without that file is not
offered. The upload by hand does not need it.

## Security notes

* Passwords are stored as PBKDF2-HMAC-SHA256 with a random salt (10 000 rounds); sessions are random tokens in an `HttpOnly`,
  `SameSite=Strict` cookie that ends after 30 minutes without use, and every request that changes something must carry a header a web
  page from another site cannot add. Five wrong passwords from one address lock it for 30 seconds, doubling up to five minutes.
* The panel speaks **plain HTTP**: on a network you do not trust the password crosses it in clear. HTTPS on the node (a certificate made
  on the board) is not done. The broker connection is plain MQTT unless a CA certificate is embedded (`certs/ca.pem`, `mqtts://`).
* The set-up code appears in the node's log (and so in the panel's log) until the node restarts after set-up; it is useless once a user exists.
* A node with an access point is a way into the network it is joined to. Use a strong Wi-Fi password.
* The random numbers come from the chip's generator with the converter's noise source enabled for the moment they are made.

## Working on the panel without a board

```bash
node tools/panel_mock.mjs --user admin:adminpass123      # http://127.0.0.1:8090/
```

The stand-in serves the panel from `panel/` and answers `/api/v1` from memory. `tools/pack_panel.py` compresses the panel for the firmware
(`text.js`, the seven languages, is joined in front of `app.js`).
