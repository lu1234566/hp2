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

if ! find "$probe_root" -type f -iname 'HPModels.u' -print -quit | grep -q .; then
    echo "HPModels.u was not found after extraction." >&2
    exit 65
fi

echo "Building semantic HP2 animation probe..."
build_dir="$probe_root/build"
mkdir -p "$build_dir"
common_sources=(
    src/portcore/runtime.cpp
    src/portcore/ue_actor.cpp
    src/portcore/ue_animation.cpp
    src/portcore/ue_animation_hp2.cpp
    src/portcore/ue_animation_hp2_codec.cpp
    src/portcore/ue_lightmap.cpp
    src/portcore/ue_mesh.cpp
    src/portcore/ue_model.cpp
    src/portcore/ue_package.cpp
    src/portcore/ue_skeletal.cpp
    src/portcore/ue_texture.cpp
)
common_flags=(-std=c++17 -O2 -Wall -Wextra -Wpedantic -Isrc/portcore/include)

g++ "${common_flags[@]}" "${common_sources[@]}" tools/hp2_animation_semantic_probe.cpp \
    -o "$build_dir/hp2_animation_semantic_probe" >"$report_root/build.log" 2>&1

"$build_dir/hp2_animation_semantic_probe" "$probe_root" HPModels skGenMaleAnims \
    >"$report_root/animation-semantic.json"

python3 - "$report_root/animation-semantic.json" "$report_root/summary.json" \
    "$cab_count" <<'PY'
import json
import pathlib
import sys
probe = json.loads(pathlib.Path(sys.argv[1]).read_text(encoding="utf-8"))
summary = {
    "schema": "hp2-animation-source-summary-v13",
    "installshield_cab_sets": int(sys.argv[3]),
    "runtime_valid": probe.get("valid", False),
    "runtime_error": probe.get("error"),
    "file_version": probe.get("file_version"),
    "package": probe.get("package"),
    "object": probe.get("object"),
    "bones": probe.get("bones", 0),
    "moves": probe.get("moves"),
    "sequences": probe.get("sequences", 0),
    "tracks": probe.get("tracks", 0),
    "master_quaternions": probe.get("master_quaternions", 0),
    "master_positions": probe.get("master_positions", 0),
    "master_deltas": probe.get("master_deltas", 0),
    "remaining_bytes": probe.get("remaining_bytes"),
    "mesh_points": probe.get("mesh_points", 0),
    "mesh_bones": probe.get("mesh_bones", 0),
    "animation_reference_object": probe.get("animation_reference_object"),
    "influence_slots": probe.get("influence_slots", 0),
    "skeleton_matches": probe.get("skeleton_matches", False),
    "bind_valid": probe.get("bind_valid", False),
    "bind_rms": probe.get("bind_rms"),
    "bind_max": probe.get("bind_max"),
    "bone_map_entries": probe.get("bone_map_entries", 0),
    "bone_map_identity_entries": probe.get("bone_map_identity_entries", 0),
    "bone_map_negative_entries": probe.get("bone_map_negative_entries", 0),
    "bone_map_invalid_entries": probe.get("bone_map_invalid_entries", 0),
    "convention_confirmed": probe.get("convention_confirmed", False),
    "best_quaternion_mapping": probe.get("best_quaternion_mapping"),
    "best_quaternion_components": probe.get("best_quaternion_components"),
    "best_quaternion_sign_mask": probe.get("best_quaternion_sign_mask"),
    "best_quaternion_w_sign": probe.get("best_quaternion_w_sign"),
    "best_quaternion_mean_error": probe.get("best_quaternion_mean_error"),
    "best_position_mapping": probe.get("best_position_mapping"),
    "best_position_sign_mask": probe.get("best_position_sign_mask"),
    "best_position_relative_rms": probe.get("best_position_relative_rms"),
    "stable_moves": probe.get("stable_moves", 0),
    "max_deformation": probe.get("max_deformation"),
    "first_stable_sequence": probe.get("first_stable_sequence"),
}
pathlib.Path(sys.argv[2]).write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
PY

echo "Semantic HP2 animation probe complete. Original media remains only in the temporary runner directory."
