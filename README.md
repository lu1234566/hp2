# HP2 Mobile PortCore

Clean-room Android runtime for a personal, legitimate copy of **Harry Potter and the Chamber of Secrets (PC)**.

The project follows the useful part of the [StS2 Launcher](https://github.com/Ekyso/StS2-Launcher) pattern: the APK contains only open launcher/runtime code, starts without copyrighted game data, validates files installed by the owner, and keeps the original files outside Git. It deliberately omits StS2's Godot, .NET, Steam, Harmony, cloud and touch layers.

## Current gate: G1 passed; G2 next

G0 is the native foundation. G1 verified it against the owner's original disc
and catalogued all 107 detected packages successfully. This is not a playable
port yet; G2 is the first real map-geometry milestone.

The current foundation provides:

- C++17 PortCore shared by Android and host tools;
- Android `GameActivity` shell, OpenGL ES 3 renderer and 16 KB page alignment;
- controller-only input for buttons, sticks, triggers and D-pad;
- touch events filtered before they reach PortCore;
- recursive probe for UE1-style `.u`, `.utx`, `.unr`, `.uax` and `.umx` package summaries;
- a data-free APK that reads the owner's files from the app-specific `game/` directory;
- GitHub Actions builds, so Google Colab is not part of the workflow.

The bootstrap screen uses four bars, from left to right:

1. native runtime loaded;
2. external controller produced an input event;
3. package candidates were found;
4. at least one package summary passed structural validation.

The gold square follows the left stick. Press **Start** to rescan the game directory after copying files.

## Original media probe

The original MDF was processed in an ephemeral GitHub Actions runner without
Google Colab. The run found 42 maps, 53 texture packages, 11 code/content
packages and one audio bank; all 107 package summaries passed structural
validation.

- [G1 report](docs/G1_ORIGINAL_DISC_REPORT.md)
- [Metadata-only package catalog](docs/hp2-package-catalog.json)

The image and extracted game files were deleted at the end of the run. Only
package header metadata is committed.

## Build without Colab

Every push and pull request runs host tests and builds a debug APK in GitHub Actions. Download `HP2-Mobile-G0-debug` from the workflow run's **Artifacts** section.

For a local Android build, install JDK 17, Gradle 9.4.1 and the Android command-line tools, then run:

```bash
export ANDROID_SDK_ROOT=/path/to/Android/Sdk
./scripts/setup.sh
./scripts/build.sh
```

For the host package probe and tests, only a C++17 compiler is required:

```bash
./scripts/build-host.sh
./.local/build/host/hp2_probe "/path/to/installed/HP2"
```

## Install original game data

Do not commit, attach or publish the MDF, executables or extracted game assets. Install the PC game from your own disc image, then point the installer script at the **installed game directory** containing folders such as `Maps`, `System`, `Textures` and `Sounds`.

Windows PowerShell:

```powershell
adb install -r .\HP2-Mobile-0.1.0-g0-debug.apk
.\scripts\install-game-data.ps1 -GameDirectory "C:\Games\Harry Potter II"
```

Linux/macOS:

```bash
adb install -r HP2-Mobile-0.1.0-g0-debug.apk
./scripts/install-game-data.sh "/path/to/installed/HP2"
```

This project never reads or requires cracks, keys, modified executables or the Drive folders named `Таблетка` and `Мануал + Ключ`.

## Controller mapping target

| Control | Planned HP2 action |
|---|---|
| Left stick | Move |
| Right stick | Camera |
| A | Jump / confirm |
| B | Back / cancel |
| X | Use |
| Y | Cast spell |
| L1 / R1 | Change target |
| L2 / R2 | Block / action-run |
| Start | Menu; rescan during G0 |
| Select | Map |
| D-pad | Menu navigation |

Bindings remain provisional until original HP2 input actions are catalogued.

## Gate roadmap

| Gate | Acceptance criterion | Status |
|---|---|---|
| G0 | Native Android shell, gamepad path and package-summary probe | PASS |
| G1 | Exact HP2 package/version catalog from the owner's original media | PASS — 107/107 packages catalogued |
| G2 | First real HP2 map geometry | Next |
| G3 | UVs and real textures | Pending |
| G4 | Lightmaps and recognizable room | Pending |
| G5 | Actors, meshes and animation | Pending |
| G6 | Scripted gameplay, collision and camera | Pending |
| G7 | Audio, saves and level transitions | Pending |
| G8 | Android performance and full controller validation | Pending |

No gate passes on placeholder geometry, checkerboards presented as final assets, or synthetic game data.

## Repository boundaries

- `src/portcore/`: portable clean-room runtime and package probe.
- `android/`: thin Android shell; no game assets.
- `tools/`: host inspection utilities.
- `tests/`: synthetic structural tests only.
- `scripts/`: local build and ADB data installation.

Launcher and clean-room runtime code are MIT licensed. Harry Potter and all original game content belong to their respective rights holders and are not included.
