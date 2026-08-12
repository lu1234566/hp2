#!/usr/bin/env bash
set -euo pipefail

GRADLE_VERSION="8.7"
ANDROID_PLATFORM="34"
ANDROID_BUILD_TOOLS="34.0.0"
ANDROID_NDK="26.3.11579264"
ANDROID_CMAKE="3.22.1"

export DEBIAN_FRONTEND=noninteractive

if command -v apt-get >/dev/null 2>&1; then
    apt-get update -y
    apt-get install -y --no-install-recommends git curl unzip zip ca-certificates
    rm -rf /var/lib/apt/lists/*
fi

if ! command -v gradle >/dev/null 2>&1 || ! gradle --version 2>/dev/null | grep -q "Gradle ${GRADLE_VERSION}"; then
    echo "Installing Gradle ${GRADLE_VERSION}..."
    mkdir -p /opt/gradle
    curl -fsSL "https://services.gradle.org/distributions/gradle-${GRADLE_VERSION}-bin.zip" -o /tmp/gradle.zip
    unzip -q /tmp/gradle.zip -d /opt/gradle
    ln -sf "/opt/gradle/gradle-${GRADLE_VERSION}/bin/gradle" /usr/local/bin/gradle
    rm -f /tmp/gradle.zip
fi

SDKMANAGER="$(command -v sdkmanager || true)"
if [ -z "$SDKMANAGER" ] && [ -n "${ANDROID_SDK_ROOT:-}" ]; then
    SDKMANAGER="$(find "$ANDROID_SDK_ROOT/cmdline-tools" -type f -name sdkmanager 2>/dev/null | head -n 1 || true)"
fi
if [ -z "$SDKMANAGER" ] && [ -n "${ANDROID_HOME:-}" ]; then
    SDKMANAGER="$(find "$ANDROID_HOME/cmdline-tools" -type f -name sdkmanager 2>/dev/null | head -n 1 || true)"
fi

if [ -z "$SDKMANAGER" ]; then
    echo "sdkmanager was not found in the Codespaces image."
    exit 1
fi

echo "Accepting Android SDK licenses..."
yes | "$SDKMANAGER" --licenses >/dev/null 2>&1 || true

echo "Installing HP2 Android toolchain..."
"$SDKMANAGER" \
    "platform-tools" \
    "platforms;android-${ANDROID_PLATFORM}" \
    "build-tools;${ANDROID_BUILD_TOOLS}" \
    "ndk;${ANDROID_NDK}" \
    "cmake;${ANDROID_CMAKE}"

echo
echo "HP2 Codespaces environment ready."
java -version
gradle --version | head -n 8
"$SDKMANAGER" --version

echo
echo "Build with: bash scripts/build.sh debug"
