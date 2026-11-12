#!/bin/sh
# Build SDL-free native tests using the shared CMake source list and solver flags.
# Usage: tools/build_hint_tests.sh && build-hint/hint_game
set -eu

cd "$(dirname "$0")/.."
cmake -S . -B build-hint -DBOBBY_BUILD_APP=OFF -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Release
cmake --build build-hint --target hint_game gen_bench --parallel "${JOBS:-4}"
