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

The metadata result will determine whether `Duel10` primarily uses legacy
`Mesh`/`LodMesh` objects, class-default mesh references or a later static-mesh
container. G5a will implement only the format demonstrated by that private
result before producing the next device APK.
