# G2 clean-room package-index notes

G2 starts with a bounded format-reading step before any renderer claims real
geometry. The first target is `Maps/Duel10.unr`: it is small enough for rapid
iteration but large enough to contain a useful level object graph.

The new `hp2_map_probe` reads only metadata:

- UE1/UE2 compact signed indices;
- version 64+ compact-length name strings and name flags;
- import records (`ClassPackage`, `ClassName`, outer reference, object name);
- export records (class/super/outer references, object name, flags, serialized
  size and offset);
- resolved `Model`, `Polys` and `Level` candidates.

No map payload, texture, executable or other copyrighted game data is written
to Git or to the workflow artifact. The private workflow emits a JSON object
index, then deletes the extracted installer tree.

Format facts were cross-checked against the MIT-licensed UE Viewer
implementation, especially
[`UnCoreSerialize.cpp`](https://github.com/gildor2/UEViewer/blob/master/Unreal/UnCoreSerialize.cpp),
[`UnPackage.cpp`](https://github.com/gildor2/UEViewer/blob/master/Unreal/UnrealPackage/UnPackage.cpp),
and
[`UnPackage2.cpp`](https://github.com/gildor2/UEViewer/blob/master/Unreal/UnrealPackage/UnPackage2.cpp).
The PortCore parser is a small independent implementation scoped to HP2's
observed package versions 76 and 79.

This is G2a, not a G2 pass. G2 passes only when decoded vertices/surfaces from
an original HP2 map are rendered by the Android runtime.
