# G3 clean-room texture notes

G3 connects the already decoded BSP surfaces to original UE1 texture objects
without placing any game asset in Git or in the APK. For package versions
64–99, the bounded reader handles:

- signed surface panning values and UE1 world-to-texture coordinates;
- material import outer chains from a map into an external texture package;
- `Texture` exports with tagged UObject properties;
- UE1 lazy byte arrays and the first uncompressed P8 mipmap;
- `Palette` exports containing serialized RGBA colors;
- palette-index expansion to RGBA for OpenGL ES upload.

For a BSP point `P`, base point `B`, texture vectors `U` and `V`, texture size
`W × H`, and signed pans `PanU`/`PanV`, PortCore calculates:

```text
s = (dot(P - B, U) + PanU) / W
t = (dot(P - B, V) + PanV) / H
```

The host tests build a tiny synthetic version-79 map, texture package, mipmap
and 256-color palette. They verify material resolution, signed pan, UV values,
lazy-array bounds and exact P8-to-RGBA expansion. Synthetic pixels are used
only as parser tests and never presented as a completed gate.

The private original-media workflow receives the game root only inside its
ephemeral runner. The G3a probe passed against the original data and selected
`WizardDuel.DumbleWood_WD`: a 128×128 P8 texture using `Palette42`. It expands
to 65,536 RGBA bytes and is referenced by 514 valid `Duel10` triangles. The
workflow publishes only those names, dimensions and counts; it never publishes
pixel data. G3 passes only after this original texture is visibly confirmed on
the target Galaxy A57.
