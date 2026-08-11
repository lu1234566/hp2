# G5 clean-room actor and mesh notes

## G5a actor census

The first G5 step deliberately separates level actors from mesh decoding. A
UE1 `Level` export begins with ordinary tagged object properties, followed by
its actor array. Each non-null positive object reference selects an exported
actor. Objects carrying `RF_HasStack` serialize a bounded `StateFrame` before
their tagged properties.

The current reader accepts package versions 64–99 and extracts only the data
needed to choose the next decoder safely:

- actor object and class names;
- `Location`, `Rotation` and `PrePivot`;
- `DrawScale` and `DrawScale3D`;
- `bHidden`;
- direct `Mesh` and `StaticMesh` object references.

Counts, class names and referenced object names may leave the private runner.
Actor payloads, mesh vertices, texture pixels and other original data may not.
All counts, compact indices, property sizes and name references are bounded
before allocation or access. Synthetic version-79 fixtures cover the actor
array, `StateFrame`, struct, float, bool and object-reference paths.

The 2026-08-11 private run found 309 actor-array references, of which 274 were
non-null and all 274 parsed successfully. It found 266 actors with a location,
153 with a rotation and two direct `Mesh` references. Most room decoration is
still inherited through imported class defaults and intentionally remains for a
later G5 increment.

## Direct mesh formats and placement

The two direct references establish the exact first formats needed by G5a:

- `HPModels.skhp2_genmale1Mesh` is a `SkeletalMesh` used by `Duellist0`;
- `UnrealShare.8Ball3rd` is a legacy `LodMesh` attached to a camera utility
  actor, so it is decoded but not drawn.

The bounded reader shares the serialized `UMesh` prefix, then handles legacy
triangle records, `LodMesh` faces/wedges/material slots and the `SkeletalMesh`
reference-point and bone tables. It does not yet evaluate animation sequences
or skin weights. For this checkpoint, skeletal points are rendered in their
stored reference pose.

The private probe decoded both assets with zero mesh failures. The duelist has
291 reference points, 616 triangles, three declared texture slots and 135 bone
records. Two texture assets cover all 616 rendered triangles, with zero
material failures. The camera mesh has 610 vertices and 90 triangles but is
excluded by actor class. Only these aggregate counts and object labels leave
the ephemeral runner.

Placement applies mesh origin, mesh scale and rotation origin, then actor
`PrePivot`, `DrawScale`, `DrawScale3D`, `Rotation` and `Location`. A synthetic
version-79 package verifies the external package reference, packed mesh
vertices, UVs, triangle and transform path without using any original asset.

G5 is not complete at G5a: class-default decoration meshes, skeletal animation,
collision and scripted behavior remain separate acceptance steps.
