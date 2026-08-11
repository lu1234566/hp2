# G1 original-disc report

G1 was completed against metadata from the owner's original `HPCOS.mdf`. The
disc image was processed temporarily in GitHub Actions and was deleted at the
end of the job. No game asset, executable, key or modified file is stored in
this repository or in the published report artifact.

## Result

| Check | Result |
|---|---:|
| MDF size | 686,921,728 bytes |
| MDF SHA-256 | `3ad7a4d5ac4f9ccae70f60e07a74b805f65b30c45d6d19d6abfebc274ecdb681` |
| InstallShield CAB sets | 2 |
| Package candidates | 107 |
| Structurally valid packages | 107 |
| Probe exit code | 0 |

Package types:

| Extension | Count |
|---|---:|
| `.unr` maps | 42 |
| `.utx` textures | 53 |
| `.u` code/content packages | 11 |
| `.uax` audio banks | 1 |

Package header versions were 61 (2 files), 68 (2), 69 (1), 76 (17) and 79
(85). Every package reported licensee version 0.

The complete metadata-only inventory is in
[`hp2-package-catalog.json`](hp2-package-catalog.json). Paths in that catalog
are normalized to the installed layout (`Maps`, `Textures`, `System` and
`Sounds`) instead of exposing temporary extraction paths.

## Gate decision

**G1: PASS.** The clean-room reader can identify the exact package set from
the supplied original media. G2 can now target real `.unr` model data, starting
with a small map, without copying proprietary bytes into Git.

## Reproducibility and boundaries

The opt-in workflow `.github/workflows/probe-original-disc.yml` reads the
private repository secret `HP2_DRIVE_URL`, downloads only the owner's original
disc image, extracts it in the ephemeral runner and publishes metadata. It
never reads or requires crack/key folders. The secret value is not printed.
