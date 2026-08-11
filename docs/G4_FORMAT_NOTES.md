# G4 clean-room lighting notes

G4a extends the G3 BSP renderer in two independent directions: it loads every
decodable material referenced by the selected `Duel10` model, and it consumes
the UE1 static-lighting data serialized after the model's `Polys` reference.
For package version 79 that suffix contains:

1. an array of `FLightMapIndex` records;
2. the packed `LightBits` byte array;
3. collision bounds, leaf hulls and convex leaves;
4. a model light-actor reference array whose per-lightmap lists end at a null
   object reference.

Each light-map record contains a byte offset, three-component pan, clamped
width and height, U/V scale and the first light reference. One packed bit per
texel records whether a static light reaches that texel. G4a expands each mask,
applies a bounded 3×3 blur, combines the visibility masks and packs only
lightmaps referenced by real BSP triangles into an RGBA atlas. The atlas has a
one-texel duplicated border around every region to prevent bilinear sampling
from leaking between surfaces.

For BSP point `P`, surface base `B`, texture vectors `U` and `V`, light-map pan
`L`, scale `S`, source light-map dimensions `W × H`, and atlas region origin
`R`, the normalized atlas coordinate is derived from:

```text
source_u = (dot(P,U) - (dot(B,U) + L.x - 0.5*S.u)) / S.u
source_v = (dot(P,V) - (dot(B,V) + L.y - 0.5*S.v)) / S.v
atlas_uv = (R + clamp(source, 0.5, size - 0.5)) / atlas_size
```

The current G4a layer uses the original baked visibility masks with a neutral
grayscale response. Reconstructing the exact colored contribution of every
light actor is a later G4 refinement; the APK therefore does not claim
pixel-identical UE1 lighting yet.

Synthetic version-79 fixtures verify serialization order, bounds, mask pitch,
null-terminated light lists, atlas packing and UV mapping. The private workflow
may publish aggregate counts and atlas dimensions only. Original texture
pixels, shadow masks, map payloads and generated atlas pixels never leave the
ephemeral runner and are never added to Git or the APK.
