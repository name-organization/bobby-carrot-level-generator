#!/bin/sh
# Builds the native hint test into build-hint/ without CMake or SDL:
#
#   build-hint/libcadical.a   CaDiCaL (src/third_party/cadical), built once
#   build-hint/hint_game      the hint through the real Game class
#
#   tools/build_hint_tests.sh && build-hint/hint_game
set -eu

cd "$(dirname "$0")/.."
CXX=${CXX:-c++}
CC=${CC:-cc}
OUT=build-hint
CAD=src/third_party/cadical
# Same configuration as the CMake build, see CMakeLists.txt.
CADFLAGS="-O2 -w -DNDEBUG -DNBUILD -DQUIET -DNTRACING -DNCONTRACTS -DNCLOSEFROM"
FLAGS="-std=c++20 -O2 -Wall -Wextra -Wno-unused-parameter"

mkdir -p "$OUT/cadical"
if [ ! -f "$OUT/libcadical.a" ] || [ -n "$(find "$CAD" -newer "$OUT/libcadical.a" | head -1)" ]; then
    echo "building CaDiCaL..."
    for f in "$CAD"/*.cpp; do
        $CXX -std=c++17 $CADFLAGS -c "$f" -o "$OUT/cadical/$(basename "$f" .cpp).o" &
    done
    for f in "$CAD"/*.c; do
        $CC $CADFLAGS -c "$f" -o "$OUT/cadical/$(basename "$f" .c).c.o" &
    done
    wait
    rm -f "$OUT/libcadical.a"
    ar rcs "$OUT/libcadical.a" "$OUT"/cadical/*.o 2>/dev/null
fi

$CXX $FLAGS tests/hint_game.cpp src/core/hint.cpp src/core/sat.cpp src/core/level.cpp \
    src/core/game.cpp src/core/player.cpp src/core/rules.cpp "$OUT/libcadical.a" \
    -o "$OUT/hint_game"
echo "built $OUT/hint_game"
