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
if COMMON.is_dir():
    sys.path.insert(0, str(COMMON))
    from armor_common.contracts import ContractError, validate_payload  # noqa: E402
else:
    # A real, honest degradation - not a fallback that quietly re-implements
    # or vendors a copy of the contract: ARMOR-COMMON only exists as a
    # sibling checkout (the normal local layout every cross-repo script in
    # this ecosystem assumes; see ARMOR-COMMON/scripts/armor-project.sh).
    # A single-repo CI checkout (GitHub Actions) never has that sibling, so
    # this one cross-repo check is skipped there - the firmware's own host
    # tests above it in build-test still run and still gate the build.
    ContractError = None
    validate_payload = None


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--binary", help="run this emit_samples binary instead of reading stdin")
    args = parser.parse_args()
    if validate_payload is None:
        print(f"CONTRACT=SKIPPED no sibling {COMMON} checkout (this check only runs where ARMOR-COMMON is a sibling repo)")
        return 0
    text = subprocess.run([args.binary], capture_output=True, text=True, check=True).stdout if args.binary else sys.stdin.read()
    checked = 0
    devices = 0
    for line in text.splitlines():
        kind, _, body = line.partition(" ")
        if kind == "ERROR":
            print(f"the serialiser refused a sample it should have produced: {line}", file=sys.stderr)
            return 1
        payload = json.loads(body)
        if kind == "device_state":   # the server's own vocabulary, not part of ARMOR-COMMON: only its shape is checked here
            if not isinstance(payload, dict) or not payload:
                print(f"device_state payload is not a non-empty object: {body}", file=sys.stderr)
                return 1
            devices += 1
            continue
        try:
            validate_payload(kind, payload)
        except ContractError as error:
            print(f"{kind} payload violates the contract: {error}\n  {body}", file=sys.stderr)
            return 1
        checked += 1
    if checked < 9:
        print(f"expected at least 9 samples, got {checked}", file=sys.stderr)
        return 1
    if devices < 8:
        print(f"expected at least 8 device states, got {devices}", file=sys.stderr)
        return 1
    print(f"CONTRACT=PASS {checked} firmware payloads accepted by ARMOR-COMMON, {devices} device states well formed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
