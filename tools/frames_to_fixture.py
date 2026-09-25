#!/usr/bin/env python3
"""ARMOR-RADAR - turns the serial log of a node into a test fixture of real LD2450 frames.

Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.

With CONFIG_ARMOR_RADAR_HEX_DUMP=y the node logs its first frames as "FRAME r1 aaff0300...55cc". Save that console log to a file and run

    python tools/frames_to_fixture.py console.log tests/fixtures/ld2450_real.hex

The fixture has one frame per line ("r1 <60 hex characters>"). tests/test_core.cpp decodes every line of tests/fixtures/ld2450_real.hex
when it exists and fails if any frame is not a well-formed LD2450 frame: the first check of the decoder against a real module.
Lines that are not complete 30-byte frames (a log line cut in the middle) are reported and skipped.
"""
import re
import sys

FRAME = re.compile(r"FRAME r([123]) ([0-9a-f]+)")
HEADER, TAIL, LENGTH = "aaff0300", "55cc", 30


def convert(lines):
    good, skipped = [], 0
    for line in lines:
        match = FRAME.search(line)
        if not match:
            continue
        radar, hexa = match.groups()
        if len(hexa) == LENGTH * 2 and hexa.startswith(HEADER) and hexa.endswith(TAIL):
            good.append(f"r{radar} {hexa}")
        else:
            skipped += 1
    return good, skipped


def main(argv):
    if len(argv) != 3:
        print(__doc__)
        return 2
    with open(argv[1], encoding="utf-8", errors="replace") as source:
        frames, skipped = convert(source)
    if not frames:
        print("no frame found: is CONFIG_ARMOR_RADAR_HEX_DUMP=y and is this the console log of the node?", file=sys.stderr)
        return 1
    with open(argv[2], "w", encoding="utf-8", newline="\n") as target:
        target.write("# Real LD2450 frames captured from a node, one per line: radar, then the 30 bytes in hexadecimal.\n")
        target.write("\n".join(frames) + "\n")
    counts = {r: sum(1 for f in frames if f.startswith(f"r{r} ")) for r in "123"}
    print(f"{len(frames)} frames written to {argv[2]} (radar 1: {counts['1']}, radar 2: {counts['2']}, radar 3: {counts['3']}); {skipped} skipped")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
