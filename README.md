# HP2 Mobile PortCore

Clean-room Android runtime for a personal, legitimate copy of **Harry Potter
and the Chamber of Secrets (PC)**.

The project follows the useful part of the
[StS2 Launcher](https://github.com/Ekyso/StS2-Launcher) pattern: the APK
contains only open launcher/runtime code, starts without copyrighted game
data, validates files installed by the owner, and keeps the original files
outside Git. It deliberately omits StS2's Godot, .NET, Steam, Harmony, cloud
and gameplay-touch layers.

## Current gate: G2 in progress

G0 and G1 are complete. The native shell was confirmed on a Samsung Galaxy
A57, and the owner's original disc yielded 107/107 structurally valid Unreal
packages. G2b now decodes the primary `Duel10.unr` BSP model into 1,389 valid
triangles and submits that mesh to OpenGL ES 3. Device confirmation of the
real-map frame is the remaining acceptance criterion.

The current foundation provides:

- a simplified Android launcher with no Steam, Workshop, account or cloud UI;
- Storage Access Framework import for an installed game folder or ZIP, with no
  ADB requirement;
- safe MDF/ISO recognition with an explicit notice that native InstallShield
  extraction is the next installer module;
- C++17 PortCore shared by Android and host tools;
- Android `GameActivity`, OpenGL ES 3 and 16 KB page alignment;
- controller-only gameplay input for buttons, sticks, triggers and D-pad;
- touch events filtered before they reach PortCore gameplay;
- recursive UE package-summary validation;
- a UE1/UE2 package-index reader for compact names, imports and exports;
- a clean-room `hp2_map_probe` that decodes `Duel10.unr` while emitting only
  metadata and aggregate geometry counts;
- GitHub Actions builds, so Google Colab is not part of the workflow.

The runtime diagnostic screen uses four bars, from left to right:

1. native runtime loaded;
2. external controller produced an input event;
3. package candidates were found;
4. at least one package summary passed structural validation.

When a valid `Duel10.unr` is installed, the runtime draws its normalized BSP
mesh behind the diagnostic bars; the right stick changes its viewing angle.
The gold square remains only as the no-map fallback. Press **Start** to rescan
after an import. Touch is used only by the launcher, never as gameplay input.

## Install original game data

Install the APK, open **HP2 Mobile**, then choose one of these sources:

- **Import installed folder:** select a directory containing folders such as
  `Maps`, `Textures`, `System`, `Sounds` and `Music`;
- **Import ZIP:** select a ZIP containing those folders, even under one wrapper
  directory;
- **MDF/ISO:** the launcher currently recognizes the owner's original image,
  but does not yet unpack its InstallShield CAB sets on Android.

Imported files are copied to the app-specific `game/` directory. Executables,
DLLs and installer programs are deliberately excluded. Do not commit, attach
or publish the MDF, executables or extracted game assets.

The optional ADB scripts remain useful for developers, but they are no longer
the normal installation path.

This project never reads or requires cracks, keys, modified executables or the
Drive folders named `Таблетка` and `Мануал + Ключ`.

## Original media probe

The original MDF is processed in an ephemeral GitHub Actions runner. G1 found
42 maps, 53 texture packages, 11 code/content packages and one audio bank; all
107 package summaries passed structural validation.

G2b additionally decodes the largest `Model` payload in `Duel10.unr` and emits
only aggregate metadata: `Model314` contains 764 points, 455 BSP nodes, 252
surfaces, 8,975 BSP vertices and 1,389 valid triangles. The image, map payload
and all extracted files are deleted at the end of the run.

- [G1 report](docs/G1_ORIGINAL_DISC_REPORT.md)
- [Metadata-only package catalog](docs/hp2-package-catalog.json)
- [G2 format notes](docs/G2_FORMAT_NOTES.md)

## Build without Colab

Every push and pull request runs host tests and builds a debug APK in GitHub
Actions. Download `HP2-Mobile-G2b-dev-debug` from the workflow run's
**Artifacts** section.

For a local Android build, install JDK 17, Gradle 9.4.1 and the Android
command-line tools, then run:

```bash
export ANDROID_SDK_ROOT=/path/to/Android/Sdk
./scripts/setup.sh
./scripts/build.sh
```

For the host probes and tests, only a C++17 compiler is required:

```bash
./scripts/build-host.sh
./.local/build/host/hp2_probe "/path/to/installed/HP2"
./.local/build/host/hp2_map_probe "/path/to/installed/HP2/Maps/Duel10.unr"
```

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
| Start | Menu; rescan during bootstrap |
| Select | Map |
| D-pad | Menu navigation |

Bindings remain provisional until original HP2 input actions are catalogued.

## Gate roadmap

| Gate | Acceptance criterion | Status |
|---|---|---|
| G0 | Native Android shell, gamepad path and package-summary probe | PASS |
| G1 | Exact HP2 package/version catalog from the owner's original media | PASS — 107/107 packages |
| G2 | First real HP2 map geometry | DEVICE TEST — 1,389 real BSP triangles decoded |
| G3 | UVs and real textures | Pending |
| G4 | Lightmaps and recognizable room | Pending |
| G5 | Actors, meshes and animation | Pending |
| G6 | Scripted gameplay, collision and camera | Pending |
| G7 | Audio, saves and level transitions | Pending |
| G8 | Android performance and full controller validation | Pending |

No gate passes on placeholder geometry, checkerboards presented as final
assets, or synthetic game data.

## Repository boundaries

- `src/portcore/`: portable clean-room runtime and package readers.
- `android/`: launcher and thin native Android shell; no game assets.
- `tools/`: host inspection utilities.
- `tests/`: synthetic structural tests only.
- `scripts/`: local build, optional ADB installation and private media probe.

Launcher and clean-room runtime code are MIT licensed. Harry Potter and all
original game content belong to their respective rights holders and are not
included.
