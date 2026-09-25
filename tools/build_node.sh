#!/usr/bin/env bash
# ARMOR-RADAR - builds the firmware of one node in the official ESP-IDF container.
# Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.
#
#   tools/build_node.sh perimetro-1            # reads secrets/perimetro-1.conf, writes dist/perimetro-1.bin
#
# secrets/<node>.conf holds that node's own settings (its identity, its broker password, and anything else that differs from
# the defaults) as CONFIG_ lines; tools/provision_node.sh creates it. It is git-ignored: a password never enters the repository.
# Run it from Linux, or from WSL on Windows (Docker needed). The result is ONE merged image, flashed at address 0.
set -euo pipefail
NODE="${1:-}"
[[ "$NODE" =~ ^[a-z0-9][a-z0-9_-]{0,63}$ ]] || { echo "usage: build_node.sh NODE_ID   (lowercase letters, digits, - and _)" >&2; exit 2; }
IDF_IMAGE="${ARMOR_IDF_IMAGE:-espressif/idf:v5.4.2}"
ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
CONF="secrets/$NODE.conf"
[[ -f "$ROOT/$CONF" ]] || { echo "missing $CONF - run tools/provision_node.sh $NODE first (or copy secrets/node.conf.example)" >&2; exit 1; }
grep -q "CONFIG_ARMOR_NODE_ID=\"$NODE\"" "$ROOT/$CONF" || { echo "$CONF must set CONFIG_ARMOR_NODE_ID=\"$NODE\"" >&2; exit 1; }

DOCKER="docker"
docker info >/dev/null 2>&1 || DOCKER="sudo docker"
mkdir -p "$ROOT/dist" "$ROOT/build"
# The build runs as the caller so no file in the tree ends up owned by root; HOME is a scratch directory for idf.py.
$DOCKER run --rm -u "$(id -u):$(id -g)" -e HOME=/tmp -v "$ROOT":/project -w /project "$IDF_IMAGE" bash -c "
  set -e
  BUILD=build/$NODE
  idf.py -B \$BUILD -D SDKCONFIG=\$BUILD/sdkconfig -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;$CONF' set-target esp32s3 >/dev/null
  idf.py -B \$BUILD -D SDKCONFIG=\$BUILD/sdkconfig -D 'SDKCONFIG_DEFAULTS=sdkconfig.defaults;$CONF' build
  cd \$BUILD
  python -m esptool --chip esp32s3 merge_bin -o /project/dist/$NODE.bin @flash_args
"
ls -l "$ROOT/dist/$NODE.bin"
sha256sum "$ROOT/dist/$NODE.bin" | cut -d' ' -f1 | sed "s/^/sha256 /"
echo "flash it with: python -m esptool --chip esp32s3 -p COMx write_flash 0x0 dist/$NODE.bin"
