#!/bin/bash
set -e
echo "========================================"
echo "  HP2 Mobile - Setup"
echo "========================================"
if [ -z "$ANDROID_SDK_ROOT" ]; then
    echo "Set ANDROID_SDK_ROOT first!"
    echo "Example: export ANDROID_SDK_ROOT=/home/user/Android/Sdk"
    exit 1
fi
yes | $ANDROID_SDK_ROOT/cmdline-tools/latest/bin/sdkmanager --licenses 2>/dev/null || true
$ANDROID_SDK_ROOT/cmdline-tools/latest/bin/sdkmanager \
    "platforms;android-34" "build-tools;34.0.0" "ndk;26.3.11579264" "cmake;3.22.1"
echo "Setup complete!"
