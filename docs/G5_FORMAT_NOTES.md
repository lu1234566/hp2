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

The 2026-08-11 G5a private run found 309 actor-array references, of which 274 were
non-null and all 274 parsed successfully. It found 266 actors with a location,
153 with a rotation and two direct `Mesh` references. Most room decoration is
inherited through imported class defaults; resolving that separate path is the
G5c increment described below.

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

## G5b focused device validation

The G5a Galaxy A57 capture did not visually distinguish the duelist from the
completed G4 room. The metadata-only follow-up measured the placed actor at
`[2269.37, -863.14, -336.03]` through `[2292.28, -806.68, -248.52]`. Those
bounds are inside the BSP bounds, so the actor was neither missing nor clipped
outside the map. Its largest extent is about 88 world units, while the BSP's
largest extent is 3,487 units; the full-room diagnostic necessarily reduces it
to only a few dozen screen pixels.

G5b duplicates the already transformed actor triangles only inside the local
render buffer and normalizes that duplicate around the measured actor bounds.
Both views use the same 616 triangles, UVs and two decoded materials; no asset
data is added to the APK. A 2026-08-11 Galaxy A57 capture clearly showed the
textured student model in a coherent reference pose, approving G5b visually.
The phone had no controller connected, so the gold controller bar represented
only the absent optional input event, not a rendering failure.

## G5c inherited class defaults

UE1 level actors usually serialize only values that differ from their class
defaults. Furniture can therefore have a valid `Location` in `Duel10.unr` but
no direct `Mesh` tag: its imported `UClass` supplies `Mesh`, `PrePivot`,
`DrawScale`, `DrawScale3D` and sometimes `bHidden`.

G5c follows the actor's class reference into the external package and walks the
bounded superclass chain with cycle detection and caching. A `UClass` stores
its default-property block at the end of its export. The reader examines at
most the final 64 KiB, considers only candidate offsets named by relevant
render properties, and accepts a tagged-property stream only when its `None`
terminator lands exactly at the export end. It never needs to execute or fully
interpret UnrealScript bytecode. Object references remain paired with the
package that declared the default so a mesh import is resolved in the correct
namespace.

A synthetic version-79 fixture verifies an actor whose imported class lives in
one package and whose inherited mesh lives in another. It also verifies that
an actor override wins over the class default. The device diagnostic begins in
the complete room, switches automatically to an object-only view, then returns
to the room. Controller **A** is retained only as an optional manual override;
no touch gameplay was added.

The 2026-08-11 G5c private run scanned 79 class exports, found 45 relevant
default-property tails and resolved every class chain without error. It found
76 inherited mesh candidates and decoded 73 visible inherited instances,
bringing the scene to 27 distinct mesh assets and 32,595 triangles. The set
includes the room's throne, rectangular tables, footstools, candles, hanging
lamps, fireplace logs and jar, plus multiple character classes. Of the actor
triangles, 21,219 use 41 decoded materials; there were zero mesh failures and
zero material failures. These are metadata-only counts and object labels.

Some cutscene actors are intentionally positioned far outside the BSP room.
They remain harmlessly outside the normal world camera, but their extreme
bounds would make the object-only diagnostic too small. The focus copy therefore
includes only triangles whose centroid lies within the BSP bounds plus an 8%
margin; this does not alter any world placement or source geometry.

## G5c device failure and G5c2 scale correction

The first G5c Galaxy A57 capture was a visual **FAIL** even though decoding and
all four diagnostic bars completed. It showed torn, overlapping character
fragments and one giant close-up instead of the inherited objects. A new
metadata-only placement audit measured 24 human meshes at 16,000–21,000 world
units tall. Every affected instance received an inherited `DrawScale` of
`200.0`; no direct actor override had that value. The same directly referenced
duelist had already been validated at roughly 88 units in G5b. All plausible
instances in this map use scales from `0.75` through `2.5`.

G5c2 therefore leaves direct actor overrides untouched and rejects only an
extreme scale recovered from the provisional inherited-class tail scanner.
Rejected instances fall back to the UE actor default of `1.0`, and the report
records both the source value and rejection count. A synthetic external-class
fixture covers both a valid inherited scale and the rejected `200.0` regression.
The automatic room/object cycle remains enabled so the correction can be
verified without a controller.

The corrected Galaxy A57 capture on 2026-08-12 shows the object-only diagnostic
without giant inherited characters and then the complete textured/lightmapped
room with furniture and actors at plausible relative scale. That closes G5c2
as a visual **PASS**. The view is still intentionally a normalized diagnostic,
not the final gameplay camera.

## G5d skeletal stream mapping

G5d begins with the `SkeletalMesh` data that G5a deliberately skipped. The
existing reader already reaches the animation-sequence table, reference points,
`RefSkeleton`, `BoneWeightIndices`, `BoneWeights` and `LocalPoints`, but until
now it only counted or skipped those records. Assigning gameplay semantics to
the two 32-bit fields in each weight-index record without evidence would risk a
plausible-looking but incorrect skinning implementation.

A separate bounded clean-room decoder therefore replays the already validated
`Mesh`/`LodMesh` prefix and records the following in memory:

- animation sequence name/group, start frame, frame count, notify count and rate;
- reference points;
- reference-bone names, flags, orientation, position, length, size, child count
  and parent index;
- the two raw 32-bit words of each `BoneWeightIndices` record;
- each raw 32-bit `BoneWeights` word plus a diagnostic float interpretation;
- `LocalPoints` and the number of bytes left after the known skeletal tail.

The public/private report boundary remains metadata-only. The probe may emit
sequence labels, counts, parent-validity counts, raw-field maxima, float-range
statistics and remaining-byte counts. It does not emit point coordinates, bone
transforms, individual index records or individual weights. A synthetic
version-79 fixture validates complete consumption of a controlled skeletal
stream before the original HP2 mesh is inspected.

The first real G5d acceptance step is to reproduce the already established 291
reference points and 135 bones for `HPModels.skhp2_genmale1Mesh`, consume its
known skeletal stream coherently and use the aggregate weight statistics to
identify the serialized influence layout. CPU/GLES skinning and the first
moving character follow only after that layout is demonstrated rather than
guessed.

G5 remains open until a real character is skinned and animated. Camera,
collision and scripted gameplay remain G6 work and are intentionally not mixed
into this checkpoint.
