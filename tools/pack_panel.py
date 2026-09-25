#!/usr/bin/env python3
"""ARMOR-RADAR - compresses the web panel for embedding in the firmware.
Copyright (C) 2026 JuanenRac (Electro Hobby 3D). GPL-3.0-or-later.

    pack_panel.py PANEL_DIR OUT_DIR

Writes index.html.gz, app.js.gz (text.js and app.js joined) and style.css.gz. The compression is reproducible (no timestamp), so the same panel always gives the same bytes.
"""
import gzip
import pathlib
import sys

ASSETS = ("index.html", "app.js", "style.css")


def pack(sources: list, target: pathlib.Path) -> None:
    data = b"\n".join(source.read_bytes() for source in sources)
    with target.open("wb") as raw:
        with gzip.GzipFile(filename="", mode="wb", fileobj=raw, compresslevel=9, mtime=0) as packed:
            packed.write(data)


def main() -> int:
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    panel, out = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2])
    out.mkdir(parents=True, exist_ok=True)
    for name in ASSETS:
        # the texts (text.js) are joined in front of the pages (app.js): one script for the browser
        sources = [panel / "text.js", panel / name] if name == "app.js" else [panel / name]
        for source in sources:
            if not source.is_file():
                print(f"missing panel file: {source}", file=sys.stderr)
                return 1
        pack(sources, out / (name + ".gz"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
