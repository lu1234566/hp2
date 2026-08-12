# HP2 Mobile Port

Unofficial Android port of **Harry Potter and the Chamber of Secrets (PC)**.

## Architecture

- **Native C++** engine (no emulation)
- **Controller-only** (Bluetooth/USB gamepad required)
- **OpenGL ES 3.0** renderer
- **AAudio** low-latency audio
- **arm64-v8a** Android target

## Chromebook / GitHub Codespaces

This repository is prepared to work entirely from a browser.

1. Open the repository on GitHub.
2. Select **Code → Codespaces → Create codespace on main**.
3. Wait for `.devcontainer/devcontainer.json` to finish provisioning.
4. The container installs the Android SDK/NDK, CMake and Gradle versions used by the project.
5. Build a debug APK with:

```bash
bash scripts/build.sh debug
```

The local output is:

```text
android/app/build/outputs/apk/debug/app-debug.apk
```

For a release build:

```bash
bash scripts/build.sh release
```

If the Codespace needs its toolchain repaired or refreshed, run:

```bash
bash scripts/setup-codespace.sh
```

## Automatic APK build

GitHub Actions runs `.github/workflows/android-build.yml` on pushes and pull requests. A successful run uploads an artifact named:

```text
hp2-mobile-debug
```

This allows development from the Chromebook while GitHub performs the Android/NDK compilation remotely.

## Local Linux setup

If Android command-line tools are already installed:

```bash
export ANDROID_SDK_ROOT=/path/to/Android/Sdk
bash scripts/setup.sh
bash scripts/build.sh debug
```

## Controller Mapping

| Button | Action |
|--------|--------|
| A | Jump / Confirm |
| B | Back / Cancel |
| X | Use |
| Y | Cast Spell |
| L1/R1 | Target |
| L2 | Block |
| R2 | Action / Run |
| Start | Menu |
| Select | Map |
| L3 | Crouch |
| R3 | Look Mode |
| D-Pad | Navigation |
| Left Stick | Move |
| Right Stick | Camera |

## License

Launcher code: MIT. Engine source: academic preservation only.
