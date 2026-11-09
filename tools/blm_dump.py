#!/usr/bin/env python3
"""Dump a .blm level as an annotated ASCII grid.

The original format is a 4-byte big-endian payload length (always 256)
followed by a 16x16 row-major grid of tile bytes. One byte is simultaneously
the tile index in tileset.png and the gameplay meaning of the cell.

    tools/blm_dump.py assets/v1/level/normal01.blm
    tools/blm_dump.py assets/v1/level/*.blm --summary
"""

import argparse
import collections
import pathlib
import struct
import sys

LEGEND = {
    18: ("..", "ground"),
    19: ("CC", "carrot"),
    20: ("c.", "carrot taken"),
    21: ("ST", "start"),
    22: ("R+", "red switch on"),
    23: ("R-", "red switch off"),
    24: ("/1", "corner east-south"),
    25: ("/2", "corner west-south"),
    26: ("/3", "corner west-north"),
    27: ("/4", "corner north-east"),
    28: ("--", "rail horizontal"),
    29: ("||", "rail vertical"),
    30: ("cr", "cracked"),
    31: ("##", "hole"),
    32: ("k1", "grey key"),
    33: ("L1", "grey lock"),
    34: ("k2", "yellow key"),
    35: ("L2", "yellow lock"),
    36: ("k3", "red key"),
    37: ("L3", "red lock"),
    38: ("Y+", "yellow switch on"),
    39: ("Y-", "yellow switch off"),
    40: ("<<", "conveyor left"),
    41: (">>", "conveyor right"),
    42: ("^^", "conveyor up"),
    43: ("vv", "conveyor down"),
    44: ("EX", "exit"),
    45: ("gg", "grass"),
    46: ("g.", "grass trampled"),
}


def read_grid(path: pathlib.Path) -> bytes:
    data = path.read_bytes()
    if len(data) != 260:
        raise ValueError(f"{path}: expected 260 bytes, got {len(data)}")
    (payload,) = struct.unpack(">I", data[:4])
    if payload != 256:
        raise ValueError(f"{path}: unexpected payload length {payload}")
    return data[4:]


def render(grid: bytes) -> str:
    lines = []
    for y in range(16):
        row = []
        for x in range(16):
            tile = grid[y * 16 + x]
            if tile < 18:
                row.append("  " if tile == 0 else "%2d" % tile)
            else:
                row.append(LEGEND.get(tile, ("?%d" % tile, "unknown"))[0])
        lines.append(" ".join(row))
    return "\n".join(lines)


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("files", nargs="+", type=pathlib.Path)
    ap.add_argument("--summary", action="store_true",
                    help="only print tile counts, one line per level")
    args = ap.parse_args()

    for path in args.files:
        grid = read_grid(path)
        counts = collections.Counter(grid)

        if args.summary:
            interesting = {t: n for t, n in sorted(counts.items()) if t >= 18}
            print(f"{path.name:16} carrots={counts[19]:3d} grass={counts[45]:3d} "
                  f"tiles={interesting}")
            continue

        print(f"== {path} ==")
        print(render(grid))
        print()
        used = sorted(t for t in counts if t >= 18 and t in LEGEND)
        for tile in used:
            glyph, name = LEGEND[tile]
            print(f"  {glyph}  {tile:3d}  {name} x{counts[tile]}")
        unknown = sorted(t for t in counts if t >= 18 and t not in LEGEND)
        if unknown:
            print(f"  unknown tile ids: {unknown}")
        print()
    return 0


if __name__ == "__main__":
    sys.exit(main())
