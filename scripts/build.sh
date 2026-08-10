#!/bin/bash
set -e
echo "========================================"
echo "  HP2 Mobile - Build"
echo "========================================"
cd android
./gradlew assembleRelease
echo ""
echo "APK: android/app/build/outputs/apk/release/app-release.apk"
