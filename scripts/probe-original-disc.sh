#!/usr/bin/env bash
set -euo pipefail

if [[ -z "${HP2_DRIVE_URL:-}" ]]; then
    echo "HP2_DRIVE_URL is not configured." >&2
    exit 64
fi

for command_name in gdown iat bsdtar 7z isoinfo unshield cmake; do
    if ! command -v "$command_name" >/dev/null 2>&1; then
        echo "Required command is missing: $command_name" >&2
        exit 69
    fi
done

report_root="${1:-.local/hp2-disc-report}"
mkdir -p "$report_root"
report_root="$(cd "$report_root" && pwd)"

runner_temp="${RUNNER_TEMP:-/tmp}"
probe_root="$(mktemp -d "$runner_temp/hp2-disc-probe.XXXXXX")"
disc_root="$probe_root/disc"
installer_root="$probe_root/installer"
mdf_path="$probe_root/HPCOS.mdf"
iso_path="$probe_root/HPCOS.iso"

cleanup() {
    if [[ -n "${probe_root:-}" && -d "$probe_root" ]]; then
        find "$probe_root" -depth -mindepth 1 -delete
        rmdir "$probe_root"
    fi
}
trap cleanup EXIT

mkdir -p "$disc_root" "$installer_root"

echo "Downloading the owner's original disc image..."
gdown --fuzzy "$HP2_DRIVE_URL" --output "$mdf_path"

mdf_bytes="$(stat -c '%s' "$mdf_path")"
mdf_sha256="$(sha256sum "$mdf_path" | cut -d' ' -f1)"

echo "Converting MDF to ISO..."
iat "$mdf_path" "$iso_path" >"$report_root/iat.log" 2>&1

{
    echo "MDF bytes: $mdf_bytes"
    file "$mdf_path"
    echo "ISO bytes: $(stat -c '%s' "$iso_path")"
    file "$iso_path"
    isoinfo -d -i "$mdf_path" || true
    isoinfo -d -i "$iso_path" || true
} >"$report_root/image-diagnostics.txt" 2>&1

echo "Extracting the optical-disc filesystem..."
extracted=false
if bsdtar -tf "$mdf_path" >"$report_root/mdf-files.txt" 2>"$report_root/bsdtar.log" \
    && [[ -s "$report_root/mdf-files.txt" ]]; then
    bsdtar -xf "$mdf_path" -C "$disc_root" >>"$report_root/bsdtar.log" 2>&1
    extracted=true
elif bsdtar -tf "$iso_path" >"$report_root/iso-files.txt" 2>>"$report_root/bsdtar.log" \
    && [[ -s "$report_root/iso-files.txt" ]]; then
    bsdtar -xf "$iso_path" -C "$disc_root" >>"$report_root/bsdtar.log" 2>&1
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
    cab_target="$installer_root/cab-$cab_count"
    mkdir -p "$cab_target"
    {
        echo "### $cab_path"
        unshield l "$cab_path" || true
    } >>"$report_root/unshield.log" 2>&1
    unshield -d "$cab_target" x "$cab_path" >>"$report_root/unshield.log" 2>&1 || true
done < <(find "$disc_root" -type f -iname 'data1.cab' -print0)

echo "Building the clean-room package probes..."
bash ./scripts/build-host.sh >"$report_root/build-host.log" 2>&1

probe_status=0
./.local/build/host/hp2_probe "$probe_root" >"$report_root/probe.json" || probe_status=$?

g2_probe_status=66
duel10_path="$(find "$probe_root" -type f -iname 'Duel10.unr' -print -quit)"
if [[ -n "$duel10_path" ]]; then
    g2_probe_status=0
    ./.local/build/host/hp2_map_probe "$duel10_path" "$probe_root" \
        >"$report_root/g2-duel10-index.json" || g2_probe_status=$?
else
    echo "Duel10.unr was not found in the extracted installer payload." \
        >"$report_root/g2-duel10-index.error.txt"
fi

g5d_probe_status=0
./.local/build/host/hp2_skeletal_probe \
    "$probe_root" HPModels skhp2_genmale1Mesh \
    >"$report_root/g5d-skeletal.json" || g5d_probe_status=$?
if [[ "$g5d_probe_status" -ne 0 ]]; then
    echo "G5d skeletal probe exited with status $g5d_probe_status." \
        >"$report_root/g5d-skeletal.error.txt"
fi

find "$disc_root" "$installer_root" -type f -printf '%s\t%p\n' \
    | sed "s#${probe_root}/##" \
    | LC_ALL=C sort >"$report_root/extracted-files.tsv"

python3 - "$report_root/probe.json" "$report_root/summary.json" \
    "$report_root/g2-duel10-index.json" "$report_root/g5d-skeletal.json" \
    "$mdf_bytes" "$mdf_sha256" "$cab_count" "$probe_status" \
    "$g2_probe_status" "$g5d_probe_status" <<'PY'
import json
import pathlib
import sys

probe_path = pathlib.Path(sys.argv[1])
summary_path = pathlib.Path(sys.argv[2])
g2_path = pathlib.Path(sys.argv[3])
g5d_path = pathlib.Path(sys.argv[4])
probe = json.loads(probe_path.read_text(encoding="utf-8"))
g2 = json.loads(g2_path.read_text(encoding="utf-8")) if g2_path.is_file() else {}
g5d = json.loads(g5d_path.read_text(encoding="utf-8")) if g5d_path.is_file() else {}
model = g2.get("model_geometry", {})
texture = g2.get("g3_texture", {})
scene = g2.get("g4_scene", {})
actors = g2.get("g5_actor_census", {})
mesh_scene = g2.get("g5_mesh_scene", g2.get("g5_direct_mesh_scene", {}))
summary = {
    "schema": "hp2-original-disc-probe-v12",
    "mdf_bytes": int(sys.argv[5]),
    "mdf_sha256": sys.argv[6],
    "installshield_cab_sets": int(sys.argv[7]),
    "probe_exit_code": int(sys.argv[8]),
    "package_candidates": probe.get("candidate_count", 0),
    "valid_packages": probe.get("valid_count", 0),
    "g2_map_probe_exit_code": int(sys.argv[9]),
    "g2_map": "Duel10.unr" if g2 else None,
    "g2_names": g2.get("name_count", 0),
    "g2_imports": g2.get("import_count", 0),
    "g2_exports": g2.get("export_count", 0),
    "g2_geometry_candidates": g2.get("geometry_candidate_count", 0),
    "g2_model_valid": model.get("valid", False),
    "g2_model_export": model.get("object_name"),
    "g2_model_points": model.get("points", 0),
    "g2_model_nodes": model.get("nodes", 0),
    "g2_model_surfaces": model.get("surfaces", 0),
    "g2_model_vertices": model.get("vertices", 0),
    "g2_model_triangles": model.get("triangles", 0),
    "g2_model_bounds_min": model.get("bounds_min"),
    "g2_model_bounds_max": model.get("bounds_max"),
    "g2_model_error": model.get("error"),
    "g3_texture_valid": texture.get("valid", False),
    "g3_texture_material_index": texture.get("material_index"),
    "g3_texture_triangles": texture.get("triangle_count", 0),
    "g3_texture_package": texture.get("package_name"),
    "g3_texture_object": texture.get("object_name"),
    "g3_texture_palette": texture.get("palette_name"),
    "g3_texture_width": texture.get("width", 0),
    "g3_texture_height": texture.get("height", 0),
    "g3_texture_rgba_bytes": texture.get("rgba_bytes", 0),
    "g3_texture_error": texture.get("error"),
    "g4_texture_set_valid": scene.get("texture_set_valid", False),
    "g4_material_candidates": scene.get("material_candidates", 0),
    "g4_decoded_textures": scene.get("decoded_textures", 0),
    "g4_failed_materials": scene.get("failed_materials", 0),
    "g4_textured_triangles": scene.get("textured_triangles", 0),
    "g4_texture_rgba_bytes": scene.get("texture_rgba_bytes", 0),
    "g4_lightmap_valid": scene.get("lightmap_valid", False),
    "g4_model_lightmaps": scene.get("model_lightmaps", 0),
    "g4_light_bits_bytes": scene.get("light_bits_bytes", 0),
    "g4_referenced_lightmaps": scene.get("referenced_lightmaps", 0),
    "g4_shadow_masks": scene.get("shadow_masks", 0),
    "g4_lit_surfaces": scene.get("lit_surfaces", 0),
    "g4_lit_triangles": scene.get("lit_triangles", 0),
    "g4_atlas_width": scene.get("atlas_width", 0),
    "g4_atlas_height": scene.get("atlas_height", 0),
    "g4_texture_error": scene.get("texture_error"),
    "g4_lightmap_error": scene.get("lightmap_error"),
    "g5_actor_census_valid": actors.get("valid", False),
    "g5_level_object": actors.get("level_object_name"),
    "g5_actor_references": actors.get("actor_references", 0),
    "g5_non_null_actor_references": actors.get("non_null_actor_references", 0),
    "g5_parsed_actors": actors.get("parsed_actors", 0),
    "g5_actor_parse_failures": actors.get("parse_failures", 0),
    "g5_actors_with_location": actors.get("actors_with_location", 0),
    "g5_actors_with_rotation": actors.get("actors_with_rotation", 0),
    "g5_direct_mesh_references": actors.get("direct_mesh_references", 0),
    "g5_direct_static_mesh_references": actors.get("direct_static_mesh_references", 0),
    "g5_actor_classes": actors.get("class_inventory", []),
    "g5_direct_mesh_targets": actors.get("mesh_references", []),
    "g5_actor_error": actors.get("error"),
    "g5_mesh_valid": mesh_scene.get("valid", False),
    "g5_mesh_candidates": mesh_scene.get("candidate_instances", 0),
    "g5_direct_mesh_candidates": mesh_scene.get("direct_mesh_candidates", 0),
    "g5_inherited_mesh_candidates": mesh_scene.get("inherited_mesh_candidates", 0),
    "g5_class_exports_scanned": mesh_scene.get("class_exports_scanned", 0),
    "g5_class_default_streams_found": mesh_scene.get("class_default_streams_found", 0),
    "g5_class_default_scan_misses": mesh_scene.get("class_default_scan_misses", 0),
    "g5_class_resolution_failures": mesh_scene.get("class_resolution_failures", 0),
    "g5_decoded_mesh_assets": mesh_scene.get("decoded_mesh_assets", 0),
    "g5_decoded_mesh_instances": mesh_scene.get("decoded_mesh_instances", 0),
    "g5_decoded_inherited_mesh_instances": mesh_scene.get(
        "decoded_inherited_mesh_instances", 0
    ),
    "g5_rejected_inherited_draw_scales": mesh_scene.get(
        "rejected_inherited_draw_scales", 0
    ),
    "g5_failed_mesh_instances": mesh_scene.get("failed_mesh_instances", 0),
    "g5_actor_mesh_triangles": mesh_scene.get("source_triangles", 0),
    "g5_actor_textured_triangles": mesh_scene.get("textured_triangles", 0),
    "g5_actor_materials": mesh_scene.get("decoded_materials", 0),
    "g5_actor_failed_materials": mesh_scene.get("failed_materials", 0),
    "g5_actor_bounds_valid": mesh_scene.get("bounds_valid", False),
    "g5_actor_bounds_min": mesh_scene.get("bounds_min"),
    "g5_actor_bounds_max": mesh_scene.get("bounds_max"),
    "g5_mesh_assets": mesh_scene.get("assets", []),
    "g5_mesh_instances": mesh_scene.get("instances", []),
    "g5_mesh_error": mesh_scene.get("error"),
    "g5d_skeletal_probe_exit_code": int(sys.argv[10]),
    "g5d_skeletal_valid": g5d.get("valid", False),
    "g5d_skeletal_package": g5d.get("package_name"),
    "g5d_skeletal_object": g5d.get("object_name"),
    "g5d_skeletal_version": g5d.get("version", 0),
    "g5d_frame_vertices": g5d.get("frame_vertices", 0),
    "g5d_animation_frames": g5d.get("animation_frames", 0),
    "g5d_sequence_count": g5d.get("sequence_count", 0),
    "g5d_reference_points": g5d.get("reference_points", 0),
    "g5d_bones": g5d.get("bones", 0),
    "g5d_root_like_bones": g5d.get("root_like_bones", 0),
    "g5d_invalid_parent_bones": g5d.get("invalid_parent_bones", 0),
    "g5d_weight_index_records": g5d.get("weight_index_records", 0),
    "g5d_weight_index_first_max": g5d.get("weight_index_first_max", 0),
    "g5d_weight_index_second_max": g5d.get("weight_index_second_max", 0),
    "g5d_weight_words": g5d.get("weight_words", 0),
    "g5d_finite_weight_words": g5d.get("finite_weight_words", 0),
    "g5d_unit_interval_weight_words": g5d.get("unit_interval_weight_words", 0),
    "g5d_nonfinite_weight_words": g5d.get("nonfinite_weight_words", 0),
    "g5d_finite_weight_min": g5d.get("finite_weight_min"),
    "g5d_finite_weight_max": g5d.get("finite_weight_max"),
    "g5d_local_points": g5d.get("local_points", 0),
    "g5d_remaining_bytes": g5d.get("remaining_bytes", 0),
    "g5d_sequences": g5d.get("sequences", []),
    "g5d_error": g5d.get("error"),
}
summary_path.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
PY

echo "Original game data was processed only in the temporary runner directory."
echo "Reports: $report_root"
