#!/usr/bin/env python3
"""Convert original binary .blm levels to the text .lvl format.

    tools/blm2lvl.py assets/v1/level                 # convert in place
    tools/blm2lvl.py assets/v1/level --verify        # also check round-trip
    tools/blm2lvl.py assets/v1/level --keep-binary   # leave the .blm files
"""

import argparse
import pathlib
import re
import struct
import sys

HEADER = pathlib.Path(__file__).resolve().parent.parent / "src/core/tilechar.hpp"
BLM_SIZE = 260
GRID = 16


def load_table(header: pathlib.Path) -> list[str]:
    """Read kTileChars[] out of the C++ header, in declaration order."""
    text = header.read_text(encoding="utf-8")
    body = re.search(r"kTileChars\[48\]\s*=\s*\{(.*?)\n\};", text, re.S)
    if not body:
        sys.exit(f"{header}: cannot find the kTileChars table")

    chars: list[str] = []
    for literal in re.finditer(r"'(\\?.)'\s*,", body.group(1)):
        raw = literal.group(1)
        chars.append({"\\'": "'", "\\\\": "\\"}.get(raw, raw))

    if len(chars) != 48:
        sys.exit(f"{header}: expected 48 characters, parsed {len(chars)}")
    if len(set(chars)) != 48:
        sys.exit(f"{header}: the table has duplicate characters")
    return chars


def read_blm(path: pathlib.Path) -> bytes:
    data = path.read_bytes()
    if len(data) != BLM_SIZE:
        sys.exit(f"{path}: expected {BLM_SIZE} bytes, got {len(data)}")
    (payload,) = struct.unpack(">I", data[:4])
    if payload != GRID * GRID:
        sys.exit(f"{path}: unexpected payload length {payload}")
    return data[4:]


def to_text(grid: bytes, chars: list[str], title: str) -> str:
    lines = [f"; {title}"]
    for y in range(GRID):
        lines.append("".join(chars[grid[y * GRID + x]] for x in range(GRID)))
    return "\n".join(lines) + "\n"


GROUND = 18


def to_grid(text: str, chars: list[str]) -> bytes:
    """Mirror of the C++ parser, used only to verify the conversion.

    Path is a space, so the two habits a text parser usually has -- skipping
    blank lines and trimming trailing whitespace -- would both destroy data
    here. Instead every non-comment line is a row and a short one is padded
    out with path.
    """
    lookup = {c: i for i, c in enumerate(chars)}
    rows = []
    for line in text.split("\n"):
        if line.startswith(";"):
            continue
        if len(rows) == GRID:
            break
        line = line.rstrip("\r")
        row = bytes(lookup[c] for c in line)
        rows.append(row + bytes([GROUND]) * (GRID - len(row)))
    return b"".join(rows)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("directory", type=pathlib.Path)
    ap.add_argument("--verify", action="store_true",
                    help="re-parse each .lvl and compare against the source bytes")
    ap.add_argument("--keep-binary", action="store_true",
                    help="do not delete the .blm files after converting")
    args = ap.parse_args()

    chars = load_table(HEADER)
    sources = sorted(args.directory.glob("*.blm"))
    if not sources:
        sys.exit(f"{args.directory}: no .blm files found")

    converted = 0
    for src in sources:
        grid = read_blm(src)
        dst = src.with_suffix(".lvl")
        dst.write_text(to_text(grid, chars, src.stem), encoding="utf-8")

        if args.verify:
            back = to_grid(dst.read_text(encoding="utf-8"), chars)
            if back != grid:
                sys.exit(f"{dst}: round-trip mismatch")

        if not args.keep_binary:
            src.unlink()
        converted += 1

    print(f"converted {converted} level(s)" + (", round-trip verified" if args.verify else ""))
    return 0


if __name__ == "__main__":
    sys.exit(main())
