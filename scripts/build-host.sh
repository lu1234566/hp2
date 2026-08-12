#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
BUILD_DIR="$ROOT/.local/build/host"
mkdir -p "$BUILD_DIR"

common_sources=(
    "$ROOT/src/portcore/runtime.cpp"
    "$ROOT/src/portcore/ue_actor.cpp"
    "$ROOT/src/portcore/ue_lightmap.cpp"
    "$ROOT/src/portcore/ue_mesh.cpp"
    "$ROOT/src/portcore/ue_model.cpp"
    "$ROOT/src/portcore/ue_package.cpp"
    "$ROOT/src/portcore/ue_skeletal.cpp"
    "$ROOT/src/portcore/ue_texture.cpp"
)
common_flags=(
    -std=c++17
    -O2
    -Wall
    -Wextra
    -Wpedantic
    -I"$ROOT/src/portcore/include"
)

g++ "${common_flags[@]}" "${common_sources[@]}" \
    "$ROOT/tools/hp2_probe.cpp" -o "$BUILD_DIR/hp2_probe"

g++ "${common_flags[@]}" "${common_sources[@]}" \
    "$ROOT/tools/hp2_map_probe.cpp" -o "$BUILD_DIR/hp2_map_probe"

g++ "${common_flags[@]}" "${common_sources[@]}" \
    "$ROOT/tools/hp2_skeletal_probe.cpp" -o "$BUILD_DIR/hp2_skeletal_probe"

g++ "${common_flags[@]}" "${common_sources[@]}" \
    "$ROOT/tests/portcore_tests.cpp" -o "$BUILD_DIR/portcore_tests"

g++ "${common_flags[@]}" "${common_sources[@]}" \
    "$ROOT/tests/skeletal_probe_tests.cpp" -o "$BUILD_DIR/skeletal_probe_tests"

"$BUILD_DIR/portcore_tests"
"$BUILD_DIR/skeletal_probe_tests"
echo "Host tools: $BUILD_DIR"
