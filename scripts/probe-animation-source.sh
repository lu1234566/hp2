#!/usr/bin/env bash
set -euo pipefail

if [[ -z "${HP2_DRIVE_URL:-}" ]]; then
    echo "HP2_DRIVE_URL is not configured." >&2
    exit 64
fi

report_root="${1:-.local/hp2-animation-report}"
mkdir -p "$report_root"
report_root="$(cd "$report_root" && pwd)"
runner_temp="${RUNNER_TEMP:-/tmp}"
probe_root="$(mktemp -d "$runner_temp/hp2-animation-probe.XXXXXX")"
disc_root="$probe_root/disc"
installer_root="$probe_root/installer"
mdf_path="$probe_root/HPCOS.mdf"
iso_path="$probe_root/HPCOS.iso"
mkdir -p "$disc_root" "$installer_root"

cleanup() {
    if [[ -d "${probe_root:-}" ]]; then
        find "$probe_root" -depth -mindepth 1 -delete || true
        rmdir "$probe_root" || true
    fi
}
trap cleanup EXIT

echo "Downloading the owner's original HP2 media..."
gdown --fuzzy "$HP2_DRIVE_URL" --output "$mdf_path"

echo "Converting and extracting media..."
iat "$mdf_path" "$iso_path" >"$report_root/iat.log" 2>&1
if bsdtar -tf "$mdf_path" >/dev/null 2>"$report_root/extract.log"; then
    bsdtar -xf "$mdf_path" -C "$disc_root" >>"$report_root/extract.log" 2>&1
elif bsdtar -tf "$iso_path" >/dev/null 2>>"$report_root/extract.log"; then
    bsdtar -xf "$iso_path" -C "$disc_root" >>"$report_root/extract.log" 2>&1
else
    7z x -y "$iso_path" "-o$disc_root" >>"$report_root/extract.log" 2>&1
fi

cab_count=0
while IFS= read -r -d '' cab_path; do
    cab_count=$((cab_count + 1))
    target="$installer_root/cab-$cab_count"
    mkdir -p "$target"
    unshield -d "$target" x "$cab_path" >>"$report_root/unshield.log" 2>&1 || true
done < <(find "$disc_root" -type f -iname 'data1.cab' -print0)

if ! find "$installer_root" -type f -iname 'HPModels.u' -print -quit | grep -q .; then
    echo "HPModels.u was not found after extraction." >&2
    exit 65
fi

echo "Building bounded animation probe..."
build_dir="$probe_root/build"
mkdir -p "$build_dir"
common_sources=(
    src/portcore/runtime.cpp
    src/portcore/ue_actor.cpp
    src/portcore/ue_lightmap.cpp
    src/portcore/ue_mesh.cpp
    src/portcore/ue_model.cpp
    src/portcore/ue_package.cpp
    src/portcore/ue_skeletal.cpp
    src/portcore/ue_texture.cpp
)
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic -Isrc/portcore/include \
    "${common_sources[@]}" tools/hp2_animation_probe.cpp \
    -o "$build_dir/hp2_animation_probe" >"$report_root/build.log" 2>&1

"$build_dir/hp2_animation_probe" "$probe_root" HPModels skGenMaleAnims \
    >"$report_root/animation-skGenMaleAnims.json"

python3 - "$report_root/animation-skGenMaleAnims.json" "$report_root/summary.json" <<'PY'
import json
import pathlib
import sys
source = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
summary = {
    "schema": "hp2-animation-source-summary-v1",
    "package": source.get("package"),
    "object": source.get("object"),
    "class": source.get("class"),
    "version": source.get("version"),
    "serial_size": source.get("serial_size"),
    "native_offset": source.get("native_offset"),
    "native_bytes": source.get("native_bytes"),
    "property_count": source.get("property_count"),
    "first_compact_valid": source.get("first_compact_valid"),
    "first_compact_value": source.get("first_compact_value"),
    "first_compact_bytes": source.get("first_compact_bytes"),
    "properties": source.get("properties", []),
    "compact_candidates": source.get("compact_candidates", []),
    "relevant_names": source.get("relevant_names", []),
}
pathlib.Path(sys.argv[2]).write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
PY

echo "Animation metadata probe complete. Original media remains only in the temporary runner directory."
