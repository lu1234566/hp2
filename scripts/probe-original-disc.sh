#!/usr/bin/env bash
set -euo pipefail

if [[ -z "${HP2_DRIVE_URL:-}" ]]; then
    echo "HP2_DRIVE_URL is not configured." >&2
    exit 64
fi

for command_name in gdown iat bsdtar unshield cmake; do
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

echo "Extracting the ISO filesystem..."
bsdtar -tf "$iso_path" >"$report_root/iso-files.txt"
bsdtar -xf "$iso_path" -C "$disc_root"

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

echo "Building the clean-room package probe..."
./scripts/build-host.sh >"$report_root/build-host.log" 2>&1

probe_status=0
./.local/build/host/hp2_probe "$probe_root" >"$report_root/probe.json" || probe_status=$?

find "$disc_root" "$installer_root" -type f -printf '%s\t%p\n' \
    | sed "s#${probe_root}/##" \
    | LC_ALL=C sort >"$report_root/extracted-files.tsv"

python3 - "$report_root/probe.json" "$report_root/summary.json" \
    "$mdf_bytes" "$mdf_sha256" "$cab_count" "$probe_status" <<'PY'
import json
import pathlib
import sys

probe_path = pathlib.Path(sys.argv[1])
summary_path = pathlib.Path(sys.argv[2])
probe = json.loads(probe_path.read_text(encoding="utf-8"))
summary = {
    "schema": "hp2-original-disc-probe-v1",
    "mdf_bytes": int(sys.argv[3]),
    "mdf_sha256": sys.argv[4],
    "installshield_cab_sets": int(sys.argv[5]),
    "probe_exit_code": int(sys.argv[6]),
    "package_candidates": probe.get("candidate_count", 0),
    "valid_packages": probe.get("valid_count", 0),
}
summary_path.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
PY

echo "Original game data was processed only in the temporary runner directory."
echo "Reports: $report_root"
