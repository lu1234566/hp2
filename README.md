# HP2 Mobile PortCore

Clean-room Android runtime for a personal, legitimate copy of **Harry Potter
and the Chamber of Secrets (PC)**.

The project follows the useful part of the
[StS2 Launcher](https://github.com/Ekyso/StS2-Launcher) pattern: the APK
contains only open launcher/runtime code, starts without copyrighted game
data, validates files installed by the owner, and keeps the original files
outside Git. It deliberately omits StS2's Godot, .NET, Steam, Harmony, cloud
and gameplay-touch layers.

## Current gate: G5 in progress

G0 through G4 are complete. On 2026-08-11 the Galaxy A57 visibly confirmed the
recognizable `Duel10` room with all 33 decodable BSP materials and the static
visibility-lightmap atlas, closing G4. G5a now adds the separate object path:
it reads the `ULevel` actor array, bounded `StateFrame` and tagged transform/
mesh properties, decodes directly referenced UE1 `Mesh`, `LodMesh` and
`SkeletalMesh` reference-pose geometry, and places the first duelist in the
BSP world. No original asset is included in Git or the APK.

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
- bounded UE1 P8 texture/palette and BSP light-mask readers covered by
  synthetic tests;
- material-batched GLES rendering with a second UV set for UE1 static shadow
  masks;
- a bounded UE1 `ULevel` actor/`StateFrame` census for transforms and direct
  mesh references;
- bounded UE1 `Mesh`, `LodMesh` and `SkeletalMesh` reference-pose readers with
  actor transforms and original mesh-material resolution;
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

G2 additionally decodes the largest `Model` payload in `Duel10.unr` and emits
only aggregate metadata: `Model314` contains 764 points, 455 BSP nodes, 252
surfaces, 8,975 BSP vertices and 1,389 valid triangles. The image, map payload
and all extracted files are deleted at the end of the run.

The G3a metadata-only probe resolved the dominant visible BSP material to
`WizardDuel.DumbleWood_WD`, decoded its 128×128 P8 mip through `Palette42`, and
found 514 triangles using that material. Only these names, dimensions and
counts leave the private runner; its 65,536 decoded RGBA bytes do not.

G4a parses each `FLightMapIndex`, the packed `LightBits` shadow masks and the
null-terminated per-surface light list. The private workflow reports only
aggregate counts and atlas dimensions. Its 2026-08-11 run decoded 33/33 BSP
materials with no failures, textured all 1,389 triangles, parsed 245/245
referenced lightmaps and 1,379 shadow masks, and produced a 256×256 atlas that
lights 1,344 triangles across 245 surfaces. Decoded textures, mask pixels and
the atlas remain inside the ephemeral process and are deleted with the
original data.

The G4a APK was then confirmed on the Galaxy A57: the `Duel10` room rendered
with its original wall, floor, trim and beam textures plus visible static light
variation. That device evidence closes G4 and is intentionally recorded as
metadata rather than committing a screenshot of copyrighted game content.

The G5a private probe parsed all 274 non-null `Duel10` actors with zero actor
failures. Two actors carry direct mesh references. The visible duelist resolves
to `HPModels.skhp2_genmale1Mesh`, a `SkeletalMesh` whose reference pose has 291
vertices, 616 triangles and 135 bones. All 616 triangles resolved to two decoded
materials with no failures. The other direct reference is the camera's utility
`LodMesh`; it is decoded for format coverage but deliberately excluded from the
rendered world. These are metadata counts only—the vertices and pixels never
leave the ephemeral private runner.

- [G1 report](docs/G1_ORIGINAL_DISC_REPORT.md)
- [Metadata-only package catalog](docs/hp2-package-catalog.json)
- [G2 format notes](docs/G2_FORMAT_NOTES.md)
- [G3 format notes](docs/G3_FORMAT_NOTES.md)
- [G4 format notes](docs/G4_FORMAT_NOTES.md)
- [G5 format notes](docs/G5_FORMAT_NOTES.md)

## Build without Colab

Every push and pull request runs host tests and builds a debug APK in GitHub
Actions. Download `HP2-Mobile-G5a-dev-debug` from the workflow run's
**Artifacts** section.

Development APKs from G3a onward use the checked-in, non-production development
certificate so later test builds can update in place. It is intentionally not a
release credential and must never be reused to sign a production build. Builds
older than G3a used ephemeral CI certificates and may require one final
uninstall/reinstall before this stable development update chain begins.

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
./.local/build/host/hp2_map_probe "/path/to/installed/HP2/Maps/Duel10.unr" "/path/to/installed/HP2"
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
| G2 | First real HP2 map geometry | PASS — rendered on Galaxy A57 |
| G3 | UVs and real textures | PASS — visibly confirmed on Galaxy A57 |
| G4 | Lightmaps and recognizable room | PASS — visibly confirmed on Galaxy A57 |
| G5 | Actors, meshes and animation | IN PROGRESS — G5a reference-pose actor awaiting device validation |
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
