#!/usr/bin/env bash
#
# Fetches the original Bobby Carrot assets into assets/v1.
#
# The graphics, levels and music belong to FDG Entertainment. They are NOT
# redistributed with this project: this script pulls them from public
# reference repositories into your working copy, which is fine for private
# use. Do not publish a build that bundles them. See README.md.
#
# Usage: tools/fetch_assets.sh [destination]   (default: assets/v1)

set -euo pipefail

DEST="${1:-assets/v1}"
WORK="$(mktemp -d)"
trap 'rm -rf "$WORK"' EXIT

ZIG_REPO="https://github.com/TheWaWaR/bobby-carrot.git"
JAR_REPO="https://github.com/W95Psp/BobbyCarrot.git"

echo "==> cloning reference repositories"
git clone --depth 1 --quiet "$ZIG_REPO" "$WORK/zig"
git clone --depth 1 --quiet "$JAR_REPO" "$WORK/jar"

echo "==> installing into $DEST"
mkdir -p "$DEST/image" "$DEST/level" "$DEST/audio"

# Sprites, tilesets and the 50 original levels.
cp "$WORK/zig/zig-jok/assets/v1/image/"*.png "$DEST/image/"

# The reference repositories ship the original binary .blm levels; this project
# reads the text format, so convert them on the way in. The character table is
# taken from src/core/tilechars.hpp, so the two cannot drift apart.
cp "$WORK/zig/zig-jok/assets/v1/level/"*.blm "$DEST/level/"
"$(dirname "$0")/blm2lvl.py" "$DEST/level" --verify

# Title, logo and ending artwork only exist in the unpacked mid-let.
cp "$WORK/jar/Extr/logo.png" "$WORK/jar/Extr/title.png" "$WORK/jar/Extr/end.png" "$DEST/image/"

# The soundtrack ships as MIDI and Yamaha SMAF; browsers play neither, so use
# the Ogg renderings. carrot/lock effects have no Ogg equivalent and are
# synthesised at runtime instead (see src/audio/audio.cpp).
cp "$WORK/jar/musiques/"*.ogg "$DEST/audio/"

echo "==> done"
printf '    images: %s\n    levels: %s\n    audio:  %s\n' \
    "$(ls "$DEST/image" | wc -l)" \
    "$(ls "$DEST/level"/*.lvl | wc -l)" \
    "$(ls "$DEST/audio" | wc -l)"

if [ "$(ls "$DEST/level"/*.lvl | wc -l)" -ne 50 ]; then
    echo "WARNING: expected 50 level files" >&2
    exit 1
fi
