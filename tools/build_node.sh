#!/usr/bin/env bash
# ARMOR-RADAR - builds the firmware of one node in the official ESP-IDF container.
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#
#   tools/build_node.sh generic                # the UNIVERSAL image for the default board (s3-eth): no node's data inside, writes dist/generic-s3-eth.bin
#   tools/build_node.sh generic s3-wifi        # the same firmware for an ESP32-S3-WROOM-1 N16R8 board with no Ethernet, writes dist/generic-s3-wifi.bin
#   tools/build_node.sh perimetro-1 [BOARD]    # reads secrets/perimetro-1.conf, writes dist/perimetro-1-BOARD.bin
#
# BOARD is s3-eth (Waveshare ESP32-S3-ETH, W5500 Ethernet) or s3-wifi (ESP32-S3-WROOM-1 N16R8, Wi-Fi only). The board profile decides the pin table, the
# default way in and whether the Ethernet driver is built: an image is for ONE board, flash it only to that board.
#
# secrets/<node>.conf holds that node's own settings (its identity, its broker password, and anything else that differs from
# the defaults) as CONFIG_ lines; tools/provision_node.sh creates it. It is git-ignored: a password never enters the repository.
# Run it from Linux, or from WSL on Windows (Docker needed). The result is ONE merged image, flashed at address 0.
set -euo pipefail
NODE="${1:-}"
BOARD="${2:-s3-eth}"
[[ "$NODE" =~ ^[a-z0-9][a-z0-9_-]{0,63}$ ]] || { echo "usage: build_node.sh NODE_ID [s3-eth|s3-wifi]   (lowercase letters, digits, - and _)" >&2; exit 2; }
[[ "$BOARD" == "s3-eth" || "$BOARD" == "s3-wifi" ]] || { echo "the board is s3-eth or s3-wifi, not '$BOARD'" >&2; exit 2; }
IMAGE="$NODE-$BOARD"
IDF_IMAGE="${ARMOR_IDF_IMAGE:-espressif/idf:v5.4.2}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CONF="secrets/$NODE.conf"
DEFAULTS="sdkconfig.defaults;sdkconfig.board.$BOARD;$CONF"
if [[ "$NODE" == "generic" ]]; then
  # The universal image: the same firmware for every board. Name, address, Wi-Fi, broker and users are set in each node's own panel (or
  # by tools/adopt_node.py). secrets/generic.conf, if it exists (tools/make_fleet.py makes it), holds the fleet secret from which each
  # board's set-up code is derived using its MAC.
  DEFAULTS="sdkconfig.defaults;sdkconfig.board.$BOARD"
  [[ -f "$ROOT/secrets/generic.conf" ]] && DEFAULTS="$DEFAULTS;secrets/generic.conf"
else
  [[ -f "$ROOT/$CONF" ]] || { echo "missing $CONF - run tools/provision_node.sh $NODE first (or copy secrets/node.conf.example), or build the universal image: tools/build_node.sh generic" >&2; exit 1; }
  grep -q "CONFIG_ARMOR_NODE_ID=\"$NODE\"" "$ROOT/$CONF" || { echo "$CONF must set CONFIG_ARMOR_NODE_ID=\"$NODE\"" >&2; exit 1; }
fi

DOCKER="docker"
docker info >/dev/null 2>&1 || DOCKER="sudo docker"
mkdir -p "$ROOT/dist" "$ROOT/build"
# The build runs as the caller so no file in the tree ends up owned by root; HOME is a scratch directory for idf.py.
# The machine keeps answering while it builds: the container sees only 4 cores (ARMOR_BUILD_CPUSET, default 0-3), so the build runs about 6 jobs instead of one per core of the host.
$DOCKER run --rm --cpuset-cpus="${ARMOR_BUILD_CPUSET:-0-3}" -u "$(id -u):$(id -g)" -e HOME=/tmp -v "$ROOT":/project -w /project "$IDF_IMAGE" bash -c "
  set -e
  BUILD=build/$IMAGE
  idf.py -B \$BUILD -D SDKCONFIG=\$BUILD/sdkconfig -D 'SDKCONFIG_DEFAULTS=$DEFAULTS' set-target esp32s3 >/dev/null
  idf.py -B \$BUILD -D SDKCONFIG=\$BUILD/sdkconfig -D 'SDKCONFIG_DEFAULTS=$DEFAULTS' build
  cd \$BUILD
  python -m esptool --chip esp32s3 merge_bin -o /project/dist/$IMAGE.bin @flash_args
"
ls -l "$ROOT/dist/$IMAGE.bin"
sha256sum "$ROOT/dist/$IMAGE.bin" | cut -d' ' -f1 | sed "s/^/sha256 /"
echo "flash it (to a $BOARD board only) with: python -m esptool --chip esp32s3 -p COMx write_flash 0x0 dist/$IMAGE.bin"
