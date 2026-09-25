# Bench bring-up: two ESP32-S3-ETH nodes, three LD2450 radars each

The firmware of this repository targets the **Waveshare ESP32-S3-ETH** (W5500 Ethernet over SPI) with up to three
**HLK-LD2450** radars and an optional **VEML7700** light sensor. Two such boards, each with three radars, make two nodes of
270 degrees. **None of it has run on a board yet**: this page is the order to do things in, and what to look at, so the first
day on the bench finds the faults quickly. The C++ core is tested on a computer; the firmware image builds in a container;
everything that needs the board is unverified until you have done this page.

## 1. Wiring

Pins are menuconfig values (`Kconfig.projbuild`); these are the defaults and what the manufacturer's pin table shows as free.
The W5500 is already wired on the board (MOSI 11, MISO 12, SCLK 13, CS 14, INT 10, RST 9), and GPIO 4 to 7 belong to the SD
card socket, so they are not used.

| Signal | Board pin | Goes to |
|---|---|---|
| Radar 1 receive | GPIO16 | radar 1 **TX** |
| Radar 2 receive | GPIO17 | radar 2 **TX** |
| Radar 3 receive | GPIO18 | radar 3 **TX** |
| 5 V | VBUS | 5 V of each radar |
| Ground | GND | GND of each radar |
| I2C SDA / SCL (light sensor) | GPIO1 / GPIO2 | VEML7700 SDA / SCL, and its 3V3 and GND |

* The radars' **RX** pin is left unconnected: this firmware does not configure the modules.
* The LD2450 wants 5 V and speaks 3.3 V logic, so its TX goes straight to the ESP32-S3.
* Power the board from the USB-C port for the bench, or from a PoE module on its PoE header for the installation. Three radars plus
  the board stay well under what a USB port gives; measure it, do not assume it.
* Common ground for everything, and short wires: 256000 baud is fast for a jumper harness.

## 2. Two identities on the broker

Each node has its own identity (it may only write its own telemetry and health, and read its own commands):

    tools/provision_node.sh perimetro-1 --host 192.168.0.180 --user hydra-umc --key ~/.ssh/id_ed25519_hydra_umc
    tools/provision_node.sh perimetro-2 --host 192.168.0.180 --user hydra-umc --key ~/.ssh/id_ed25519_hydra_umc

This asks the bench broker for the identity and writes `secrets/<node>.conf` (git-ignored, the password is never printed). Use
your own names if you prefer: lowercase letters, digits, `-` and `_`.

## 3. Build and flash

From WSL (Docker with the `espressif/idf:v5.4.2` image, about 11 GB the first time):

    tools/build_node.sh perimetro-1
    tools/build_node.sh perimetro-2

From Windows, with the board plugged into the USB-C port:

    tools\flash.bat perimetro-1 COM5 monitor

`tools\flash.bat` creates its own Python environment with `esptool` the first time. If the board does not answer, hold BOOT,
press and release RESET, release BOOT, and run it again. Each image contains its own node identity and broker password: never
flash the image of one node onto the board of the other.

## 4. What the console should say, in order

1. `node perimetro-1 starting: 3 radar(s) on RX GPIO 16/17/18`
2. `light: ...` : the VEML7700 answers, or an error naming the pins. **Without the sensor** the node withholds telemetry because
   the message contract needs a lux value: for a bench test without it set `CONFIG_ARMOR_LIGHT_VEML7700=n` and
   `CONFIG_ARMOR_LUX_FALLBACK=300` in `secrets/<node>.conf` (the log says BENCH MODE at every start; never leave this in an installation).
3. `armor-eth: driver started`, then `link up, MAC ...`, then `address 192.168.x.y`. If it stops at *no W5500 answers on SPI*, the
   board or the pins are not what this page assumes: check the manufacturer's table again.
4. Every ten seconds, one line per radar:
   * `reporting, 10.0 frames/s`: this radar is wired right;
   * `no data`: its TX is not reaching the pin, or it has no power;
   * `bytes but no valid frame`: wrong baud rate, missing common ground, or a module in another mode.
5. `MQTT connected`, and the node appears in Studio's Radar menu as online within a few seconds.

## 5. Capture real frames (the first thing to do)

The decoder was written from the manual and checked against the manual's worked example, **never against a real module**. Set
`CONFIG_ARMOR_RADAR_HEX_DUMP=y`, flash, walk in front of each radar, save the console log and run:

    python tools/frames_to_fixture.py console.log tests/fixtures/ld2450_real.hex

Then run the host tests: they decode every captured frame and fail if one is not a well-formed LD2450 frame. Send the file, and
one line saying where you stood relative to the radar (to the left, to the right, how far), and the sideways axis of the radar
(the assumption Studio makes) can be confirmed or corrected with evidence.

## 6. The 270 degree node

Each LD2450 sees 120 degrees (plus or minus 60 from where it faces). Mount the three radars **75 degrees apart** and they cover
270 degrees, with 45 degrees of overlap between neighbours. In Studio, Radar menu, **Add a 270° node** creates the three radars
already wired to the node as radars 1, 2 and 3 (1 on the left of the middle one, 3 on the right, seen from the node looking
outwards); move them to where the node stands and set the facing of the middle one. If a real radar shows targets on the wrong
side, tick *Mirror sideways axis* on it.

A person standing in the overlap is seen by two radars and counted by both: the alert level does not change, but the target count
of the node can read two for one person.

## 7. What is not done

* The radar modules are not configured by the node (their command set is not in the manual available): they run with the settings
  they have.
* No LD2461 (no document).
* No TLS unless you put a CA certificate at `certs/ca.pem` and use an `mqtts://` URI. The bench broker is plain MQTT on the LAN.
* The AHT20 (temperature and humidity) is not read: the message contract has no field for it.
* Over-the-air updates: flash by cable.
