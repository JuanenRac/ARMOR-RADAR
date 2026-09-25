# Bench bring-up: two ESP32-S3-ETH nodes, three LD2450 radars each

The firmware of this repository targets the **Waveshare ESP32-S3-ETH** (ESP32-S3R8 with 8 MB of octal PSRAM, 16 MB of flash, W5500
Ethernet over SPI, PoE through the module of the board) with up to three **HLK-LD2450** radars and an optional **VEML7700** light
sensor. Two such boards, each with three radars, make two nodes of 270 degrees. **None of it has run on a board yet**: this page is
the order to do things in, and what to look at, so the first day on the bench finds the faults quickly. The C++ core is tested on a
computer; the firmware image builds in a container; the panel was exercised in a browser against a stand-in node; everything that
needs the board is unverified until you have done this page.

## 1. Wiring

The pins are settings of the node (the **Radars** and **Broker** pages of its panel); these are the defaults. All of them are free
header pins of the board: the W5500 is already wired on it (MOSI 11, MISO 12, SCLK 13, CS 14, INT 10, RST 9), GPIO 4 to 7 belong to the
microSD socket, 19 and 20 to the USB port, and GPIO 33 to 37 to the octal PSRAM. The pin table with the reasons is
`main/core/board_pins.hpp`.

| Signal | Board pin | Goes to |
|---|---|---|
| Radar 1 receive | GPIO16 | radar 1 **TX** |
| Radar 2 receive | GPIO17 | radar 2 **TX** |
| Radar 3 receive | GPIO18 | radar 3 **TX** |
| Radar 1 transmit | GPIO15 | radar 1 **RX** |
| Radar 2 transmit | GPIO21 | radar 2 **RX** |
| Radar 3 transmit | GPIO38 | radar 3 **RX** |
| 5 V | VSYS (not VBUS) | 5 V of each radar |
| Ground | GND | GND of each radar |
| I2C SDA / SCL (light sensor) | GPIO1 / GPIO2 | VEML7700 SDA / SCL, and its 3V3 and GND |

* The radars' **RX** wire is what lets the panel configure them (zones, one or three targets, Bluetooth, restart). Without it the radars
  still report; the panel says the radar cannot be configured.
* The LD2450 wants 5 V and speaks 3.3 V logic, so its TX goes straight to the ESP32-S3.
* 5 V comes from **VSYS**, the board's own 5 V rail (VBUS is the USB connector's). Check in the schematic before wiring it, and measure the
  current: three radars plus the board must stay inside what the PoE module of the board can give, and inside what a USB port gives on the bench.
  Do not connect USB and PoE at the same time without reading the schematic.
* Common ground for everything, and short wires: 256000 baud is fast for a jumper harness.

## 2. One image for every board

Like a network product, **every board runs the same firmware**, and only its MAC address tells them apart: a new board is called `armor-` and the
last six digits of its MAC until you name it, and everything that makes it *your* node (its name, address, Wi-Fi, broker identity, users) lives in
its own flash and is set in its panel or with `tools/adopt_node.py`. There is nothing to rebuild per node, so 5 boards or 27 are the same work.

Once, on the computer that builds:

    python tools/make_fleet.py           # secrets/fleet.secret, secrets/generic.conf, secrets/fleet.json (git-ignored; edit fleet.json)
    tools/build_node.sh generic          # dist/generic.bin, the same file for every board (from WSL, in the ESP-IDF container)

`fleet.json` holds what every node shares (the Wi-Fi name and password, the broker address, the language). `fleet.secret` is what turns a board's
MAC into its **set-up code** (HMAC-SHA256, ten symbols), so a board can be adopted over the network with no cable. Without the secret the code is
random at every start and shown on the board's USB console.

## 3. Program a board (USB, once) and adopt it (network)

**Programming.** The board has an Ethernet port (RJ45, with PoE if the PoE module is on) and a **USB-C** port. The first program goes in by USB-C:
the ESP32-S3 has its USB built in, so the same cable gives the flashing and the log; nothing else is needed and no driver on Windows 10 and 11. Ethernet
cannot be used for the first program, because a blank chip has no network code yet; once a node runs this firmware, every later update goes in by
Ethernet from its panel (*Firmware and log*).

1. Plug the board to the PC with USB-C. A COM port appears (see it in the Device Manager).
2. `tools\flash.bat generic COM5 monitor` (use your port). It creates its own Python environment with `esptool` the first time. If the board does
   not answer, hold **BOOT**, press and release **RESET**, release BOOT, and run it again. Add `erase` to wipe the flash first.
3. Press RESET. The console prints `A.R.M.O.R. node armor-xxxxxx ... NOT SET UP YET`, and its MAC.
4. Unplug USB, connect the Ethernet cable (PoE from the switch powers it) and put the board where it will work. Do not connect USB and PoE at the same
   time without reading the schematic.

**Adopting.** Find the node's address (your router's list of DHCP clients, by the MAC the console showed, or `armor-xxxxxx`), then:

    tools/adopt_node.py 192.168.0.181 --id perimetro-3 --name "North fence 3" --fleet secrets/fleet.json \
        --broker-ssh-host 192.168.0.180 --broker-ssh-user hydra-umc --broker-ssh-key ~/.ssh/id_ed25519_hydra_umc

It asks the broker for the node's identity (`provision_node.sh`), creates the administrator (a random password kept in `secrets/<id>.admin`), applies
the fleet's settings and the node's own, and restarts it. `--ip 192.168.0.60 --gateway 192.168.0.1` gives it a fixed address instead of DHCP.
By hand, the panel does the same (step 4 below): open the address, enter the set-up code, choose the administrator, fill the pages. Nodes created
before the pins and the panel link existed need `sudo mqtt_identity.sh upgrade-node <id>` on the broker.

The alternative, an image per node with its identity already inside (`tools/provision_node.sh <id> ...` then `tools/build_node.sh <id>`), still works and
is what the first two boards were built with; it is not needed any more.

## 4. First start: the panel

1. The console says `A.R.M.O.R. node ..., NOT SET UP YET`, and every 15 seconds where to go and the setup code (unless the image has a fleet secret: then
   the code is the one `adopt_node.py` computes from the MAC, and the panel's set-up screen shows the MAC).
2. Open the address the console shows (DHCP), or join the Wi-Fi `ARMOR-SETUP-xxxxxx` (its password is the setup code) and open `http://192.168.4.1/`.
3. Enter the setup code, choose the administrator and a password. The node restarts.
4. Sign in. **Wi-Fi**: for the shared network give every node the same name, security and password, channel on automatic
   ([details](NODE_PANEL.md)). **Network**: a fixed address if you want one. **Broker**: the identity of this node.

## 5. What the console (and the Overview page) should say, in order

1. `node armor-... , firmware ...`, the radars' pins, and `pin "..."` lines for every mapped pin.
2. `light: ...`: the VEML7700 answers, or an error naming the pins. **Without the sensor** the node withholds telemetry because the message
   contract needs a lux value: for a bench test without it, untick *A VEML7700 is connected* and give a fixed lux in the **Broker** page (the
   log says BENCH MODE at every start; never leave this in an installation).
3. `link up`, then `address 192.168.x.y`. If it says *no W5500 answers on SPI*, the board or the pins are not what this page assumes: check
   the manufacturer's table again. If the PSRAM does not answer the node still starts (without it).
4. Every ten seconds, one line per radar (and the live state in the panel):
   * `reporting, 10.0 frames/s`: this radar is wired right;
   * `no data`: its TX is not reaching the pin, or it has no power;
   * `bytes but no valid frame`: wrong baud rate, missing common ground, or a module in another mode.
5. `MQTT connected`, and the node appears in Studio's Radar menu as online within a few seconds.
6. In **Radars**, *Read information*: a version such as `1.02.22062416` confirms the command channel. If the module answers nothing, check the
   TX wire; if it answers something else, the panel shows the raw bytes: that is the evidence to fix the protocol.

## 6. Capture real frames (the first thing to do)

The decoder was written from the manual and checked against the manual's worked example, **never against a real module**. Set
`CONFIG_ARMOR_RADAR_HEX_DUMP=y` in `secrets/<node>.conf`, build, flash, walk in front of each radar, save the console log and run:

    python tools/frames_to_fixture.py console.log tests/fixtures/ld2450_real.hex

Then run the host tests: they decode every captured frame and fail if one is not a well-formed LD2450 frame. Send the file, and one line
saying where you stood relative to the radar (to the left, to the right, how far), and the sideways axis of the radar (the assumption Studio
makes) can be confirmed or corrected with evidence.

## 7. The 270 degree node

Each LD2450 sees 120 degrees (plus or minus 60 from where it faces). Mount the three radars **75 degrees apart** and they cover 270 degrees,
with 45 degrees of overlap between neighbours. In Studio, Radar menu, **Add a 270° node** creates the three radars already wired to the node
as radars 1, 2 and 3 (1 on the left of the middle one, 3 on the right, seen from the node looking outwards); move them to where the node stands
and set the facing of the middle one. If a real radar shows targets on the wrong side, tick *Mirror sideways axis* on it.

A person standing in the overlap is seen by two radars and counted by both: the alert level does not change, but the target count of the node
can read two for one person.

## 8. What is not done

* The radar **command channel** follows the manufacturer's public serial protocol and is unverified against the document and against a module
  (the panel says so). The serial speed is never changed by the panel.
* No LD2461 (no document).
* The panel and the API are plain HTTP; the broker link is plain MQTT unless a CA certificate is embedded (`certs/ca.pem`, `mqtts://`).
* The AHT20 (temperature and humidity) is not read: the message contract has no field for it.
* The access point's throughput, the roaming of phones between nodes, and the current the PoE module gives are all unmeasured.
* Studio links to a node's panel by the address the node publishes (a private address: it opens from the same network, not from the Internet); the panel is not proxied through the server.
