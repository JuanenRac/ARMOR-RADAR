#!/usr/bin/env python3
"""Validate the payloads the firmware serialiser produces against ARMOR-COMMON.

Usage:  emit_samples | python tests/check_contract.py
        python tests/check_contract.py --binary build/host/emit_samples

The firmware's JSON writer is hand-written C++; this is the proof that what it
prints is what the published contract accepts (field names, integer types, the
15-track limit, the lux range and the node-id pattern).
"""
import argparse
import json
import subprocess
import sys
from pathlib import Path

COMMON = Path(__file__).resolve().parents[2] / "ARMOR-COMMON" / "src"
sys.path.insert(0, str(COMMON))
from armor_common.contracts import ContractError, validate_payload  # noqa: E402


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", help="run this emit_samples binary instead of reading stdin")
    args = parser.parse_args()
    text = subprocess.run([args.binary], capture_output=True, text=True, check=True).stdout if args.binary else sys.stdin.read()
    checked = 0
    for line in text.splitlines():
        kind, _, body = line.partition(" ")
        if kind == "ERROR":
            print(f"the serialiser refused a sample it should have produced: {line}", file=sys.stderr)
            return 1
        payload = json.loads(body)
        try:
            validate_payload(kind, payload)
        except ContractError as error:
            print(f"{kind} payload violates the contract: {error}\n  {body}", file=sys.stderr)
            return 1
        checked += 1
    if checked < 7:
        print(f"expected at least 7 samples, got {checked}", file=sys.stderr)
        return 1
    print(f"CONTRACT=PASS {checked} firmware payloads accepted by ARMOR-COMMON")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
