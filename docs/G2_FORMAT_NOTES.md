# G2 clean-room package-index notes

G2 starts with a bounded format-reading step before any renderer claims real
geometry. The first target is `Maps/Duel10.unr`: it is small enough for rapid
iteration but large enough to contain a useful level object graph.

The clean-room reader now parses:

- UE1/UE2 compact signed indices;
- version 64+ compact-length name strings and name flags;
- import records (`ClassPackage`, `ClassName`, outer reference, object name);
- export records (class/super/outer references, object name, flags, serialized
  size and offset);
- resolved `Model`, `Polys` and `Level` candidates.
- UE1 `Model` primitive bounds, vector/point arrays, BSP nodes, surfaces and
  vertex pool;
- bounded fan triangulation of valid BSP node polygons.

The private G2b probe validated `Duel10.unr` version 79 and `Model314` with 764
points, 455 BSP nodes, 252 surfaces, 8,975 BSP vertices and 1,389 valid
triangles. Its occupied bounds are `[-127, -1344.03, -1152]` through
`[3360, 127, 448]`. No map payload, texture, executable, vertex list or other
copyrighted game data is written to Git or to the workflow artifact. The
private workflow emits aggregate JSON metadata, then deletes the extracted
installer tree.

Format facts were cross-checked against the MIT-licensed UE Viewer
implementation, especially
[`UnCoreSerialize.cpp`](https://github.com/gildor2/UEViewer/blob/master/Unreal/UnCoreSerialize.cpp),
[`UnPackage.cpp`](https://github.com/gildor2/UEViewer/blob/master/Unreal/UnrealPackage/UnPackage.cpp),
and
[`UnPackage2.cpp`](https://github.com/gildor2/UEViewer/blob/master/Unreal/UnrealPackage/UnPackage2.cpp).
The PortCore parser is a small independent implementation scoped to HP2's
observed package versions 76 and 79.

The Android runtime now uploads these decoded triangles to an OpenGL ES 3
diagnostic renderer. This is G2b device-test status, not a G2 pass: the gate
passes only after the real-map frame is confirmed on the target Android
device.
