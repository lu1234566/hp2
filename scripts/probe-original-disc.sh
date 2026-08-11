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
    ./.local/build/host/hp2_map_probe "$duel10_path" \
        >"$report_root/g2-duel10-index.json" || g2_probe_status=$?
else
    echo "Duel10.unr was not found in the extracted installer payload." \
        >"$report_root/g2-duel10-index.error.txt"
fi

find "$disc_root" "$installer_root" -type f -printf '%s\t%p\n' \
    | sed "s#${probe_root}/##" \
    | LC_ALL=C sort >"$report_root/extracted-files.tsv"

python3 - "$report_root/probe.json" "$report_root/summary.json" \
    "$report_root/g2-duel10-index.json" "$mdf_bytes" "$mdf_sha256" \
    "$cab_count" "$probe_status" "$g2_probe_status" <<'PY'
import json
import pathlib
import sys

probe_path = pathlib.Path(sys.argv[1])
summary_path = pathlib.Path(sys.argv[2])
g2_path = pathlib.Path(sys.argv[3])
probe = json.loads(probe_path.read_text(encoding="utf-8"))
g2 = json.loads(g2_path.read_text(encoding="utf-8")) if g2_path.is_file() else {}
model = g2.get("model_geometry", {})
summary = {
    "schema": "hp2-original-disc-probe-v3",
    "mdf_bytes": int(sys.argv[4]),
    "mdf_sha256": sys.argv[5],
    "installshield_cab_sets": int(sys.argv[6]),
    "probe_exit_code": int(sys.argv[7]),
    "package_candidates": probe.get("candidate_count", 0),
    "valid_packages": probe.get("valid_count", 0),
    "g2_map_probe_exit_code": int(sys.argv[8]),
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
}
summary_path.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
PY

echo "Original game data was processed only in the temporary runner directory."
echo "Reports: $report_root"

if [[ -n "${HP2_BUNDLE_OUTPUT:-}" ]]; then
    if [[ -z "${HP2_IMPORT_PUBLIC_KEY_B64:-}" ]]; then
        echo "HP2_IMPORT_PUBLIC_KEY_B64 is required for an encrypted import bundle." >&2
        exit 64
    fi
    for command_name in zip openssl base64 cmp; do
        if ! command -v "$command_name" >/dev/null 2>&1; then
            echo "Required bundle command is missing: $command_name" >&2
            exit 69
        fi
    done

    bundle_output="$HP2_BUNDLE_OUTPUT"
    mkdir -p "$bundle_output"
    bundle_output="$(cd "$bundle_output" && pwd)"
    bundle_root="$probe_root/HP2-Mobile-Data-G2b"
    mkdir -p "$bundle_root/Maps" "$bundle_root/Textures" \
        "$bundle_root/System" "$bundle_root/Sounds" "$bundle_root/Music"

    copy_flat_unique() {
        local source_root="$1"
        local pattern="$2"
        local destination="$3"
        local source_path target_path
        while IFS= read -r -d '' source_path; do
            target_path="$destination/$(basename "$source_path")"
            if [[ -f "$target_path" ]]; then
                if ! cmp -s "$source_path" "$target_path"; then
                    echo "Import bundle filename collision: $(basename "$source_path")" >&2
                    exit 65
                fi
                continue
            fi
            cp -- "$source_path" "$target_path"
        done < <(find "$source_root" -type f -iname "$pattern" -print0)
    }

    copy_flat_unique "$installer_root/cab-2/Component_2" '*.unr' "$bundle_root/Maps"
    copy_flat_unique "$installer_root/cab-2/Component_4" '*.utx' "$bundle_root/Textures"
    copy_flat_unique "$installer_root/cab-2/Component_3" '*.ogg' "$bundle_root/Music"
    copy_flat_unique "$installer_root/cab-2/Component_13" '*.uax' "$bundle_root/Sounds"

    system_source="$installer_root/cab-2/Component_5"
    while IFS= read -r -d '' source_path; do
        relative_path="${source_path#"$system_source"/}"
        target_path="$bundle_root/System/$relative_path"
        mkdir -p "$(dirname "$target_path")"
        cp -- "$source_path" "$target_path"
    done < <(find "$system_source" -type f \
        \( -iname '*.u' -o -iname '*.int' -o -iname '*.ini' \) -print0)

    hgame_path="$(find "$disc_root" -type f -ipath '*/setup/hgame.u' -print -quit)"
    if [[ -z "$hgame_path" ]]; then
        echo "The original HGame.u package was not found on the disc." >&2
        exit 65
    fi
    cp -- "$hgame_path" "$bundle_root/System/HGame.u"

    map_count="$(find "$bundle_root/Maps" -type f -iname '*.unr' | wc -l)"
    texture_count="$(find "$bundle_root/Textures" -type f -iname '*.utx' | wc -l)"
    code_count="$(find "$bundle_root/System" -type f -iname '*.u' | wc -l)"
    sound_bank_count="$(find "$bundle_root/Sounds" -type f -iname '*.uax' | wc -l)"
    music_count="$(find "$bundle_root/Music" -type f -iname '*.ogg' | wc -l)"
    package_count=$((map_count + texture_count + code_count + sound_bank_count))
    if [[ "$map_count" -ne 42 || "$texture_count" -ne 53 \
        || "$code_count" -ne 11 || "$sound_bank_count" -ne 1 \
        || "$package_count" -ne 107 || "$music_count" -ne 141 ]]; then
        echo "Import bundle counts do not match the validated original media." >&2
        exit 65
    fi
    if find "$bundle_root" -type f \
        \( -iname '*.exe' -o -iname '*.dll' -o -iname '*.com' -o -iname '*.bat' \
        -o -iname '*.cmd' -o -iname '*.msi' -o -iname '*.scr' -o -iname '*.sys' \) \
        -print -quit | grep -q .; then
        echo "An executable file reached the import bundle boundary." >&2
        exit 65
    fi

    plain_zip="$probe_root/HP2-Mobile-Data-G2b.zip"
    (
        cd "$bundle_root"
        zip -q -6 -r "$plain_zip" Maps Textures System Sounds Music
    )

    public_key="$probe_root/import-public.pem"
    secret_path="$probe_root/import-secret.bin"
    encrypted_zip="$bundle_output/HP2-Mobile-Data-G2b.zip.enc"
    encrypted_secret="$bundle_output/HP2-Mobile-Data-G2b.secret.enc"
    hmac_path="$bundle_output/HP2-Mobile-Data-G2b.zip.hmac"
    printf '%s' "$HP2_IMPORT_PUBLIC_KEY_B64" | base64 -d >"$public_key"
    openssl rand 80 >"$secret_path"
    aes_key_hex="$(dd if="$secret_path" bs=1 count=32 2>/dev/null | od -An -tx1 | tr -d ' \n')"
    hmac_key_hex="$(dd if="$secret_path" bs=1 skip=32 count=32 2>/dev/null | od -An -tx1 | tr -d ' \n')"
    iv_hex="$(dd if="$secret_path" bs=1 skip=64 count=16 2>/dev/null | od -An -tx1 | tr -d ' \n')"
    openssl enc -aes-256-cbc -K "$aes_key_hex" -iv "$iv_hex" \
        -in "$plain_zip" -out "$encrypted_zip"
    openssl dgst -sha256 -mac HMAC -macopt "hexkey:$hmac_key_hex" \
        -binary "$encrypted_zip" >"$hmac_path"
    openssl pkeyutl -encrypt -pubin -inkey "$public_key" \
        -pkeyopt rsa_padding_mode:oaep \
        -pkeyopt rsa_oaep_md:sha256 \
        -pkeyopt rsa_mgf1_md:sha256 \
        -in "$secret_path" -out "$encrypted_secret"

    python3 - "$bundle_output/manifest.json" "$plain_zip" "$encrypted_zip" \
        "$map_count" "$texture_count" "$code_count" "$sound_bank_count" \
        "$music_count" <<'PY'
import hashlib
import json
import pathlib
import sys

manifest_path = pathlib.Path(sys.argv[1])
plain_path = pathlib.Path(sys.argv[2])
encrypted_path = pathlib.Path(sys.argv[3])

def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as source:
        for chunk in iter(lambda: source.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()

manifest = {
    "schema": "hp2-mobile-owner-import-v1",
    "archive": "HP2-Mobile-Data-G2b.zip",
    "plain_bytes": plain_path.stat().st_size,
    "plain_sha256": sha256(plain_path),
    "encrypted_bytes": encrypted_path.stat().st_size,
    "encrypted_sha256": sha256(encrypted_path),
    "maps": int(sys.argv[4]),
    "textures": int(sys.argv[5]),
    "code_packages": int(sys.argv[6]),
    "sound_banks": int(sys.argv[7]),
    "music_tracks": int(sys.argv[8]),
    "package_total": int(sys.argv[4]) + int(sys.argv[5]) + int(sys.argv[6]) + int(sys.argv[7]),
    "contains_executables": False,
}
manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
PY

    rm -f "$plain_zip" "$secret_path"
    echo "Encrypted owner import bundle: $bundle_output"
fi
