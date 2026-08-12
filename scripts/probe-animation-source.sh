#!/usr/bin/env bash
set -euo pipefail

if [[ -z "${HP2_DRIVE_URL:-}" ]]; then
    echo "HP2_DRIVE_URL is not configured." >&2
    exit 64
fi

for command_name in gdown iat bsdtar 7z unshield g++; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Required command is missing: $command_name" >&2
        exit 69
    fi
done

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
    if [[ -n "${probe_root:-}" && -d "$probe_root" ]]; then
        find "$probe_root" -depth -mindepth 1 -delete || true
        rmdir "$probe_root" || true
    fi
}
trap cleanup EXIT

echo "Downloading the owner's original HP2 media..."
gdown --fuzzy "$HP2_DRIVE_URL" --output "$mdf_path"

echo "Converting MDF to ISO..."
iat "$mdf_path" "$iso_path" >"$report_root/iat.log" 2>&1

echo "Extracting the optical-disc filesystem with the proven fallback chain..."
extracted=false
if bsdtar -tf "$mdf_path" >"$report_root/mdf-files.txt" 2>"$report_root/extract.log" \
    && [[ -s "$report_root/mdf-files.txt" ]]; then
    bsdtar -xf "$mdf_path" -C "$disc_root" >>"$report_root/extract.log" 2>&1
    extracted=true
elif bsdtar -tf "$iso_path" >"$report_root/iso-files.txt" 2>>"$report_root/extract.log" \
    && [[ -s "$report_root/iso-files.txt" ]]; then
    bsdtar -xf "$iso_path" -C "$disc_root" >>"$report_root/extract.log" 2>&1
    extracted=true
fi

if [[ "$extracted" != true ]]; then
    7z x -y "$mdf_path" "-o$disc_root" >"$report_root/7z.log" 2>&1 || true
fi
if ! find "$disc_root" -type f -print -quit | grep -q .; then
    7z x -y "$iso_path" "-o$disc_root" >>"$report_root/7z.log" 2>&1 || true
fi
if ! find "$disc_root" -type f -print -quit | grep -q .; then
    echo "No files could be extracted from either MDF or converted ISO." >&2
    exit 65
fi

cab_count=0
while IFS= read -r -d '' cab_path; do
    cab_count=$((cab_count + 1))
    target="$installer_root/cab-$cab_count"
    mkdir -p "$target"
    {
        echo "### $cab_path"
        unshield l "$cab_path" || true
    } >>"$report_root/unshield.log" 2>&1
    unshield -d "$target" x "$cab_path" >>"$report_root/unshield.log" 2>&1 || true
done < <(find "$disc_root" -type f -iname 'data1.cab' -print0)

hpmodels_path="$(find "$probe_root" -type f -iname 'HPModels.u' -print -quit)"
if [[ -z "$hpmodels_path" ]]; then
    echo "HPModels.u was not found after the proven extraction path." >&2
    exit 65
fi

echo "Building bounded animation probes..."
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
common_flags=(-std=c++17 -O2 -Wall -Wextra -Wpedantic -Isrc/portcore/include)

g++ "${common_flags[@]}" "${common_sources[@]}" tools/hp2_animation_probe.cpp \
    -o "$build_dir/hp2_animation_probe" >"$report_root/build.log" 2>&1
g++ "${common_flags[@]}" "${common_sources[@]}" tools/hp2_animation_layout_probe.cpp \
    -o "$build_dir/hp2_animation_layout_probe" >>"$report_root/build.log" 2>&1
g++ "${common_flags[@]}" "${common_sources[@]}" tools/hp2_animation_lazy_probe.cpp \
    -o "$build_dir/hp2_animation_lazy_probe" >>"$report_root/build.log" 2>&1
g++ "${common_flags[@]}" "${common_sources[@]}" tools/hp2_animation_packed_probe.cpp \
    -o "$build_dir/hp2_animation_packed_probe" >>"$report_root/build.log" 2>&1

"$build_dir/hp2_animation_probe" "$probe_root" HPModels skGenMaleAnims \
    >"$report_root/animation-skGenMaleAnims.json"
"$build_dir/hp2_animation_layout_probe" "$probe_root" HPModels skGenMaleAnims \
    >"$report_root/animation-layout-candidates.json"
"$build_dir/hp2_animation_lazy_probe" "$probe_root" HPModels skGenMaleAnims \
    >"$report_root/animation-lazy-candidates.json"
"$build_dir/hp2_animation_packed_probe" "$probe_root" HPModels skGenMaleAnims \
    >"$report_root/animation-packed-candidates.json"

python3 - "$report_root/animation-skGenMaleAnims.json" \
    "$report_root/animation-layout-candidates.json" \
    "$report_root/animation-lazy-candidates.json" \
    "$report_root/animation-packed-candidates.json" \
    "$report_root/summary.json" "$cab_count" <<'PY'
import json
import pathlib
import sys
animation = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
layouts = json.loads(pathlib.Path(sys.argv[2]).read_text(encoding="utf-8"))
lazy = json.loads(pathlib.Path(sys.argv[3]).read_text(encoding="utf-8"))
packed = json.loads(pathlib.Path(sys.argv[4]).read_text(encoding="utf-8"))
summary = {
    "schema": "hp2-animation-source-summary-v5",
    "installshield_cab_sets": int(sys.argv[6]),
    "package": animation.get("package"),
    "object": animation.get("object"),
    "class": animation.get("class"),
    "version": animation.get("version"),
    "native_bytes": animation.get("native_bytes"),
    "bone_table_valid": animation.get("bone_table_valid"),
    "bone_channel_count": animation.get("bone_channel_count"),
    "mesh_name_matches": animation.get("mesh_name_matches"),
    "mesh_parent_matches": animation.get("mesh_parent_matches"),
    "motion_count": animation.get("motion_count"),
    "baseline_motion_valid": animation.get("motions_valid"),
    "baseline_motion_error_stage": animation.get("motion_error_stage"),
    "plain_layout_candidates": layouts.get("layouts", []),
    "lazy_layout_candidates": lazy.get("layouts", []),
    "packed_layout_candidates": packed.get("top_candidates", []),
}
pathlib.Path(sys.argv[5]).write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
PY

echo "Animation metadata probe complete. Original media remains only in the temporary runner directory."
