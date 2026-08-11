#!/usr/bin/env bash
set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "Usage: $0 <extracted-installed-HP2-directory>" >&2
    exit 64
fi

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SOURCE="$(cd "$1" && pwd)"
PROBE="$ROOT/.local/build/host/hp2_probe"
REPORT_DIR="$ROOT/.local/reports"
REMOTE_ROOT="/sdcard/Android/data/com.hp2.mobile/files/game"

if [[ ! -x "$PROBE" ]]; then
    "$ROOT/scripts/build-host.sh"
fi

mkdir -p "$REPORT_DIR"
if ! "$PROBE" "$SOURCE" > "$REPORT_DIR/hp2-package-probe.json"; then
    echo "No structurally valid Unreal packages were found in $SOURCE" >&2
    exit 2
fi

if ! command -v adb >/dev/null 2>&1; then
    echo "adb is required. Install Android platform-tools first." >&2
    exit 1
fi

adb get-state >/dev/null
adb shell mkdir -p "$REMOTE_ROOT"

copied=0
for directory in Maps Music Sounds System Textures; do
    if [[ -d "$SOURCE/$directory" ]]; then
        adb push "$SOURCE/$directory" "$REMOTE_ROOT/"
        copied=$((copied + 1))
    fi
done

if [[ $copied -eq 0 ]]; then
    echo "Expected HP2 directories were not found. Point this script at the installed PC game, not the MDF root." >&2
    exit 2
fi

echo "Game data installed in $REMOTE_ROOT"
echo "Open HP2 Mobile and press Start to rescan packages."
echo "Probe report: $REPORT_DIR/hp2-package-probe.json"
