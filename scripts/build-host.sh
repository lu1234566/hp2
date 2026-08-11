#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT/.local/build/host"
mkdir -p "$BUILD_DIR"

g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic \
    -I"$ROOT/src/portcore/include" \
    "$ROOT/src/portcore/runtime.cpp" \
    "$ROOT/src/portcore/ue_package.cpp" \
    "$ROOT/tools/hp2_probe.cpp" \
    -o "$BUILD_DIR/hp2_probe"

g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic \
    -I"$ROOT/src/portcore/include" \
    "$ROOT/src/portcore/runtime.cpp" \
    "$ROOT/src/portcore/ue_package.cpp" \
    "$ROOT/tests/portcore_tests.cpp" \
    -o "$BUILD_DIR/portcore_tests"

"$BUILD_DIR/portcore_tests"
echo "Host tools: $BUILD_DIR"
