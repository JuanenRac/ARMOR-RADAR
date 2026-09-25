#!/usr/bin/env python3
"""ARMOR-RADAR - adopts a freshly flashed node over the network: administrator, name, broker identity and the settings every node shares.

Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.

With the universal image (tools/build_node.sh generic) every board is the same until it is adopted. For a handful of nodes the panel is
enough; for many, this tool does what the panel would, the same way for each:

    tools/adopt_node.py 192.168.0.181 --id perimetro-3 --name "North fence 3" --setup-code K7M2QX9PTR \\
        --fleet secrets/fleet.json --broker-ssh-host 192.168.0.180 --broker-ssh-user hydra-umc --broker-ssh-key ~/.ssh/id_key

1. it creates the administrator with the set-up code (shown on the node's USB console, or computed from its MAC and the fleet secret; a random password, written to secrets/<id>.admin, git-ignored, never printed);
2. it asks the broker for the node's own identity (tools/provision_node.sh) unless you give --mqtt-username and --mqtt-password;
3. it applies the fleet's shared settings (the Wi-Fi name and password, the broker address, the language...) and the node's own (identifier,
   name, and a fixed address if you ask for one), and restarts the node.

The set-up code is the one the node shows on its USB console, or the fixed one of a universal image built with secrets/generic.conf (see
docs/NODE_PANEL.md). Only the Python standard library is used. Passwords never appear on the screen.
"""
from __future__ import annotations

import argparse
import hashlib
import hmac
import http.cookiejar
import json
import os
import re
import secrets
import subprocess
import sys
import time
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
NODE_ID = re.compile(r"^[a-z0-9][a-z0-9_-]{0,63}$")


class AdoptError(RuntimeError):
    pass


class Node:
    """A node's panel API, with its session cookie."""

    def __init__(self, address: str, timeout: float = 15.0) -> None:
        self.base = address if address.startswith("http") else f"http://{address}"
        self.timeout = timeout
        self.jar = http.cookiejar.CookieJar()
        self.opener = urllib.request.build_opener(urllib.request.HTTPCookieProcessor(self.jar))

    def call(self, method: str, path: str, body: dict | None = None) -> tuple[int, dict]:
        data = json.dumps(body).encode("utf-8") if body is not None else None
        request = urllib.request.Request(f"{self.base}/api/v1/{path}", data=data, method=method,
                                         headers={"X-Requested-With": "armor", **({"Content-Type": "application/json"} if data else {})})
        try:
            with self.opener.open(request, timeout=self.timeout) as response:
                return response.status, json.loads(response.read() or b"{}")
        except urllib.error.HTTPError as error:
            try:
                return error.code, json.loads(error.read() or b"{}")
            except ValueError:
                return error.code, {}
        except (urllib.error.URLError, TimeoutError, ConnectionError, OSError) as error:
            raise AdoptError(f"the node does not answer at {self.base}: {error}") from error


SETUP_ALPHABET = "ABCDEFGHJKMNPQRSTUVWXYZ23456789"   # capital letters and digits without look-alikes: 31 symbols, as in the firmware


def setup_code_for(secret: str, mac: str) -> str:
    """The set-up code of a board: HMAC-SHA256 of its MAC (twelve lowercase hexadecimal digits) with the fleet secret, ten symbols of it.
    The firmware (main/node_store.cpp) makes the same code from the same two things."""
    digits = re.sub(r"[^0-9a-f]", "", mac.lower())
    if len(digits) != 12:
        raise AdoptError(f"not a MAC address: {mac}")
    digest = hmac.new(secret.encode("utf-8"), digits.encode("ascii"), hashlib.sha256).digest()
    return "".join(SETUP_ALPHABET[byte % 31] for byte in digest[:10])


def deep_merge(base: dict, extra: dict) -> dict:
    """`extra` on top of `base`, section by section."""
    merged = dict(base)
    for key, value in extra.items():
        merged[key] = deep_merge(merged[key], value) if isinstance(value, dict) and isinstance(merged.get(key), dict) else value
    return merged


def read_seed(path: Path, name: str) -> str:
    """One CONFIG_ line of a secrets file made by provision_node.sh (our own file)."""
    match = re.search(rf'^{name}="([^"]*)"$', path.read_text(encoding="utf-8"), re.MULTILINE)
    if not match:
        raise AdoptError(f"{name} is not in {path}")
    return match.group(1)


def provision_identity(args: argparse.Namespace) -> tuple[str, str]:
    """The broker identity of the node: given, or asked for through provision_node.sh over SSH."""
    if args.mqtt_username and args.mqtt_password:
        return args.mqtt_username, args.mqtt_password
    if not (args.broker_ssh_host and args.broker_ssh_user):
        raise AdoptError("give --mqtt-username and --mqtt-password, or --broker-ssh-host and --broker-ssh-user so the broker can be asked")
    conf = ROOT / "secrets" / f"{args.id}.conf"
    if not conf.exists():
        command = ["bash", str(ROOT / "tools" / "provision_node.sh"), args.id, "--host", args.broker_ssh_host, "--user", args.broker_ssh_user]
        if args.broker_ssh_key:
            command += ["--key", args.broker_ssh_key]
        result = subprocess.run(command, capture_output=True, text=True)
        if result.returncode != 0:
            raise AdoptError("the broker identity could not be made: " + (result.stderr or result.stdout).strip().splitlines()[-1])
    return read_seed(conf, "CONFIG_ARMOR_MQTT_USERNAME"), read_seed(conf, "CONFIG_ARMOR_MQTT_PASSWORD")


def wait_until(check, seconds: float, what: str) -> None:
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        try:
            if check():
                return
        except AdoptError:
            pass
        time.sleep(2)
    raise AdoptError(f"timed out waiting for {what}")


def adopt(args: argparse.Namespace) -> int:
    if not NODE_ID.match(args.id):
        raise AdoptError("the identifier must be lowercase letters, digits, - and _ (1 to 64, not starting with - or _)")
    fleet = json.loads(Path(args.fleet).read_text(encoding="utf-8")) if args.fleet else {}
    if (fleet.get("ap") or {}).get("password") == "change-this-wifi-password":
        raise AdoptError("secrets/fleet.json still has the example Wi-Fi password: edit it first")
    node = Node(args.address)

    status, session = node.call("GET", "session")
    if status != 200:
        raise AdoptError("the node did not give a session")
    if not session.get("setup"):
        raise AdoptError("this node already has users: it is not new (factory-reset it, or hold BOOT for 8 seconds after power-up)")

    admin_file = ROOT / "secrets" / f"{args.id}.admin"
    if admin_file.exists() and not args.force:
        raise AdoptError(f"{admin_file} already exists: this identifier was adopted before (use --force to replace it)")
    password = secrets.token_urlsafe(18)
    admin_file.parent.mkdir(exist_ok=True)
    # The password is written BEFORE the node is asked for it, so it can never be lost.
    fd = os.open(admin_file, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
    with os.fdopen(fd, "w", encoding="utf-8") as handle:
        handle.write(f'# Administrator of node {args.id}. Written by tools/adopt_node.py; git-ignored.\nuser="admin"\npassword="{password}"\n')

    username, mqtt_password = provision_identity(args)

    language = (fleet.get("ui") or {}).get("language", "en")
    code = args.setup_code
    if not code:
        secret_file = Path(args.fleet_secret_file)
        if not secret_file.exists() or not session.get("mac"):
            raise AdoptError("give --setup-code (shown on the node's USB console), or build the universal image with a fleet secret (python tools/make_fleet.py)")
        code = setup_code_for(secret_file.read_text(encoding="utf-8").strip(), session["mac"])
    status, answer = node.call("POST", "setup", {"code": code, "user": "admin", "password": password, "language": language})
    if status != 200:
        raise AdoptError(f"the node refused the set-up ({answer.get('error', status)}); the code is the one shown on its USB console, or the fleet's")
    print(f"{args.id}: administrator created; the node restarts")

    def signed_in() -> bool:
        answered, data = node.call("GET", "session")
        return answered == 200 and not data.get("setup")

    time.sleep(args.restart_wait)
    wait_until(signed_in, 120, "the node to come back")
    status, answer = node.call("POST", "login", {"user": "admin", "password": password})
    if status != 200:
        raise AdoptError(f"could not sign in as the administrator ({answer.get('error', status)})")

    settings = deep_merge(fleet, {"node": {"id": args.id, "name": args.name or args.id}})
    settings = deep_merge(settings, {"mqtt": {"enabled": True, "username": username, "password": mqtt_password}})
    if args.broker_uri:
        settings = deep_merge(settings, {"mqtt": {"uri": args.broker_uri}})
    if args.ip:
        settings = deep_merge(settings, {"ip": {"dhcp": False, "address": args.ip, "netmask": args.netmask, "gateway": args.gateway, "dns1": args.dns or args.gateway}})
    status, answer = node.call("PUT", "config", settings)
    if status != 200:
        problems = ", ".join(f"{p['path']}: {p['code']}" for p in answer.get("problems", [])) or answer.get("error", status)
        raise AdoptError(f"the node refused the settings ({problems})")
    node.call("POST", "reboot", {})
    print(f"{args.id}: settings applied ({'fixed address ' + args.ip if args.ip else 'address by DHCP'}); restarting. Administrator password: {admin_file}")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("address", help="the node's address (or http://host:port) while it is new: on the wire, or 192.168.4.1 on its set-up Wi-Fi")
    parser.add_argument("--id", required=True, help="the node's identifier (also its identity on the broker)")
    parser.add_argument("--name", help="the name shown in the panel and in Studio (default: the identifier)")
    parser.add_argument("--setup-code", help="the code the node shows on its USB console (not needed when the image was built with a fleet secret)")
    parser.add_argument("--fleet-secret-file", default=str(ROOT / "secrets" / "fleet.secret"), help="the fleet secret, from which the code of each board is computed (default secrets/fleet.secret)")
    parser.add_argument("--fleet", help="a JSON file with the settings every node shares (see secrets/fleet.example.json)")
    parser.add_argument("--ip", help="a fixed address for this node (otherwise DHCP)")
    parser.add_argument("--netmask", default="255.255.255.0")
    parser.add_argument("--gateway")
    parser.add_argument("--dns")
    parser.add_argument("--broker-uri", help="overrides the broker address of the fleet file")
    parser.add_argument("--mqtt-username")
    parser.add_argument("--mqtt-password")
    parser.add_argument("--broker-ssh-host")
    parser.add_argument("--broker-ssh-user")
    parser.add_argument("--broker-ssh-key")
    parser.add_argument("--restart-wait", type=float, default=8.0, help="seconds to wait before looking for the node again (default 8)")
    parser.add_argument("--force", action="store_true", help="replace secrets/<id>.admin")
    args = parser.parse_args()
    if args.ip and not args.gateway:
        parser.error("--ip needs --gateway")
    try:
        return adopt(args)
    except AdoptError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
