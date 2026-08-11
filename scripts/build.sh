#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
if ! command -v gradle >/dev/null 2>&1; then
    echo "Gradle 9.4.1 is required locally. GitHub Actions can build the APK without a local setup." >&2
    exit 1
fi

cd "$ROOT"
gradle --no-daemon :app:assembleDebug
echo "APK directory: $ROOT/android/app/build/outputs/apk/debug"
