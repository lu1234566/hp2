#!/usr/bin/env bash
set -euo pipefail

if [[ -z "${ANDROID_SDK_ROOT:-}" ]]; then
    echo "ANDROID_SDK_ROOT is not set." >&2
    exit 1
fi

SDK_MANAGER="$ANDROID_SDK_ROOT/cmdline-tools/latest/bin/sdkmanager"
if [[ ! -x "$SDK_MANAGER" ]]; then
    echo "sdkmanager was not found at $SDK_MANAGER" >&2
    exit 1
fi

yes | "$SDK_MANAGER" --licenses >/dev/null || true
"$SDK_MANAGER" \
    "platforms;android-37" \
    "build-tools;36.0.0" \
    "platform-tools" \
    "ndk;28.2.13676358" \
    "cmake;3.22.1"

echo "Android SDK components are ready."
