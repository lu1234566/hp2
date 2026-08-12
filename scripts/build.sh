#!/usr/bin/env bash
set -euo pipefail

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$ROOT_DIR"

BUILD_TYPE="${1:-debug}"

if command -v ./gradlew >/dev/null 2>&1; then
    GRADLE_CMD="./gradlew"
elif command -v gradle >/dev/null 2>&1; then
    GRADLE_CMD="gradle"
else
    echo "Gradle not found. In Codespaces run: bash scripts/setup-codespace.sh"
    exit 1
fi

case "$BUILD_TYPE" in
    debug)
        TASK=":app:assembleDebug"
        APK="android/app/build/outputs/apk/debug/app-debug.apk"
        ;;
    release)
        TASK=":app:assembleRelease"
        APK="android/app/build/outputs/apk/release/app-release.apk"
        ;;
    *)
        echo "Usage: bash scripts/build.sh [debug|release]"
        exit 2
        ;;
esac

echo "========================================"
echo "  HP2 Mobile - ${BUILD_TYPE} build"
echo "========================================"
"$GRADLE_CMD" --no-daemon "$TASK"

echo
echo "APK: $APK"
