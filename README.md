# HP2 Mobile Port

Unofficial Android port of **Harry Potter and the Chamber of Secrets (PC)**.

## Architecture

- **Native C++** engine (no emulation)
- **Controller-only** (Bluetooth/USB gamepad required)
- **OpenGL ES 3.0** renderer
- **AAudio** low-latency audio

## Quick Start

```bash
# 1. Setup Android SDK
export ANDROID_SDK_ROOT=/path/to/Android/Sdk
./scripts/setup.sh

# 2. Add HP2 engine source to `engine/` folder

# 3. Build
./scripts/build.sh
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
