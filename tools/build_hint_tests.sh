#!/bin/sh
# Builds the native tests into build-hint/ without CMake or SDL:
#
#   build-hint/libcadical.a   CaDiCaL (src/third_party/cadical), built once
#   build-hint/hint_game      the hint through the real Game class
#   build-hint/gen_bench      the level generator bench, see tests/gen_bench.cpp
#
#   tools/build_hint_tests.sh && build-hint/hint_game
#   tools/build_hint_tests.sh && build-hint/gen_bench --count 5
set -eu

cd "$(dirname "$0")/.."
CXX=${CXX:-c++}
CC=${CC:-cc}
OUT=build-hint
CAD=src/third_party/cadical
# Same configuration as the CMake build, see CMakeLists.txt.
CADFLAGS="-O2 -w -DNDEBUG -DNBUILD -DQUIET -DNTRACING -DNCONTRACTS -DNCLOSEFROM"
FLAGS="-std=c++20 -O2 -Wall -Wextra -Wno-unused-parameter"
# Same list as CORE_SOURCES in CMakeLists.txt.
CORE="src/core/level.cpp src/core/rules.cpp src/core/player.cpp src/core/game.cpp
      src/core/generator.cpp src/core/roomgen.cpp src/core/autotile.cpp src/core/wfc.cpp
      src/core/sat.cpp src/core/hint.cpp"

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

for test in hint_game gen_bench; do
    # shellcheck disable=SC2086 # CORE is a list of files
    $CXX $FLAGS "tests/$test.cpp" $CORE "$OUT/libcadical.a" -o "$OUT/$test"
done
echo "built $OUT/hint_game $OUT/gen_bench"
