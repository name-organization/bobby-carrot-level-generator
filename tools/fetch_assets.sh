#!/usr/bin/env bash
# Fetch original assets into assets/v1 (or the supplied destination).
# Assets belong to FDG Entertainment and are not included in this repository.
# Usage: tools/fetch_assets.sh [destination]

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

cp "$WORK/zig/zig-jok/assets/v1/image/"*.png "$DEST/image/"

# Convert binary levels using the character table in src/core/tilechar.hpp.
cp "$WORK/zig/zig-jok/assets/v1/level/"*.blm "$DEST/level/"
"$(dirname "$0")/blm2lvl.py" "$DEST/level" --verify

cp "$WORK/jar/Extr/logo.png" "$WORK/jar/Extr/title.png" "$WORK/jar/Extr/end.png" "$DEST/image/"

# Use pre-rendered Ogg tracks; SMAF-only effects are synthesized by the mixer.
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
