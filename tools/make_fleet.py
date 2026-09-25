#!/usr/bin/env python3
"""ARMOR-RADAR - makes the fleet secret and the files that use it (once).

Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.

    python tools/make_fleet.py

Writes, in the git-ignored secrets/ directory:
  fleet.secret     the fleet secret (16+ characters), read by tools/adopt_node.py
  generic.conf     CONFIG_ARMOR_FLEET_SECRET="...", read by `tools/build_node.sh generic`, so the universal image derives each board's set-up code
                   from this secret and the board's MAC
  fleet.json       the settings every node shares, from fleet.example.json (edit it: the Wi-Fi password, the broker address)
Nothing is overwritten and no secret is printed. Losing fleet.secret is harmless for nodes already adopted, but new boards flashed with the same
image can then no longer be adopted from the network (the code is still shown on the board's USB console).
"""
import secrets
import shutil
import sys
from pathlib import Path

SECRETS = Path(__file__).resolve().parents[1] / "secrets"


def main() -> int:
    SECRETS.mkdir(exist_ok=True)
    secret_file, conf_file, fleet_file = SECRETS / "fleet.secret", SECRETS / "generic.conf", SECRETS / "fleet.json"
    if secret_file.exists():
        secret = secret_file.read_text(encoding="utf-8").strip()
        print("secrets/fleet.secret already exists: kept")
    else:
        secret = secrets.token_urlsafe(24)
        secret_file.write_text(secret + "\n", encoding="utf-8")
        print("secrets/fleet.secret written")
    if conf_file.exists():
        print("secrets/generic.conf already exists: kept")
    else:
        conf_file.write_text(f'# The universal image (tools/build_node.sh generic). Git-ignored.\nCONFIG_ARMOR_FLEET_SECRET="{secret}"\n', encoding="utf-8")
        print("secrets/generic.conf written")
    if fleet_file.exists():
        print("secrets/fleet.json already exists: kept")
    else:
        shutil.copyfile(SECRETS / "fleet.example.json", fleet_file)
        print("secrets/fleet.json written from the example: edit the Wi-Fi password and the broker address")
    return 0


if __name__ == "__main__":
    sys.exit(main())
