# Wave-7 — Env-map reflection vertex mapping (W7-ENVMAP)

Owner: W7-ENVMAP. Closes the rule-8 gap named in `progress/vertex-lighting-wave6.md`
line 54: `VIBE_Mesh_ComputeVertexLighting @0x5c9054` is "env-map reflection, not
diffuse" — the spherical/reflective per-vertex mapping that gives shiny meshes (metal,
glass, polished surfaces) their env-mapped look.

Files owned/touched this wave:
- `src/render/env_map_walk.{h,cpp}` — the object walk (verified + completeness pass)
- `src/render/vertex_lighting.{h,cpp}` — the per-vertex kernels (READ-only verify; the
  reflect kernel `ComputeEnvMapReflectionUv` + `UnpackSkinNormal` already 1:1)
- `tests/unit/env_map_walk_test.cpp` — extended (4 -> 6 tests, 25 checks)
- this doc

## The reconstructed function (the real call tree, rule 7)

```
VIBE_Mesh_ComputeVertexLighting        0x5c9054   ENV-MAP reflection UV (NOT diffuse)
  ├─ early-outs: object(result) != 0 ; mesh container(a2) != 0 ;
  │     a2[4] (+16 sub-record) != 0 ; *(a2[4]+480) (material/light count) > 0
  ├─ REFLECTIVE-MATERIAL GATE (0x5c90aa loop over a2[5], the +20 array): find the
  │     first entry e where  *e != 0 && (*(e+104) & 1) != 0.  Bit0 of the texture
  │     record's +104 flag == "reflective / env-mapped material". No reflective
  │     material -> the walk does not run (engine never env-maps a matte object).
  ├─ VIBE_Transform_ComputeBoneWorldMatrix  0x5c8fac  -> builds the bone world 3x3
  │     (v20..v28 stack block, kernel order {m0,m1,m2, m4,m5,m6, m8,m9,m10}).
  │     The +528-sign test selects dword_13FCD1C vs null as its a2 (world vs local).
  ├─ VIBE_Anim_FindHighestPriorityLayer     0x5d0e84  -> active morph/skin layer;
  │     v6 = 192 * *layer + *(layer[26]+348)  (the active keyframe ptr) or 0.
  ├─ if v6 != 0  -> SKINNED branch (0x5c9165): per vertex (stride 80, count a2[2]),
  │     gated on byte *(vert+77):
  │       normal bytes = *(v6+184) + 3*i  (byte triple) -> UnpackSkinNormal
  │       n[k] = ((i16)byte + flt_628CC8(-128)) * flt_628CC4(2/255)
  │       Nt = m3x3 * n ;  R = pos + flt_628CBC(-2.0)*(Nt.pos)*Nt ;  normalize(R)
  │       vert.u(+32) = R.x*flt_628CC0(0.5) + 0.5 ;  vert.v(+36) = 0.5 + R.y*0.5
  └─ else        -> NON-SKINNED branch (0x5c92a4): same loop/gate/math, normal read
        from *(vert+72) -> +12/+16/+20 (a per-vertex source-normal record). The
        matrix multiply (v32/v33/v34 vs v20..v28) is byte-identical to the skinned
        branch — both index the same 3x3; only the normal SOURCE differs.
```

## Exact constants (get_bytes @0x628CBC.., little-endian float)

| symbol | bytes | value | role |
|---|---|---|---|
| flt_628CBC | `00 00 00 c0` | -2.0 | reflection coeff: R = P - 2(Nt.P)Nt |
| flt_628CC0 | `00 00 00 3f` | 0.5  | env-map UV scale & bias: uv = 0.5*R + 0.5 |
| flt_628CC4 | `81 80 00 3c` | 0.0078431373 (2/255) | skin-normal byte -> unit |
| flt_628CC8 | `00 00 00 c3` | -128.0 | skin-normal byte bias |

## What is reconstructed 1:1 here

- **Per-vertex reflect kernel** `render::ComputeEnvMapReflectionUv`
  (`vertex_lighting.cpp`): Nt = m3x3 * normal (kernel order {m0,m1,m2,m4,m5,m6,m8,m9,m10},
  i.e. nt0 = n0*m[0]+n1*m[3]+n2*m[6], etc — exactly v32/v33/v34 of 0x5c9054);
  dot = (Nt.pos)*(-2.0); R = dot*Nt + pos; `VectorNormalize(R)`
  (zero-length -> 0, per 0x5cb148); uv = (0.5*R.x+0.5, 0.5+0.5*R.y). Verified
  byte-for-byte against the decompile this wave (constants re-pulled via get_bytes).
- **Skin-normal unpack** `render::UnpackSkinNormal`: n[k] = ((i16)byte - 128)*(2/255).
  Promoting to i16 before the bias matches the original's `(__int16)v36` cast.
- **The object walk** `render::ComputeEnvMapVertexUvs` (`env_map_walk.cpp`): the
  vertex loop, the per-vertex +77 reflective gate, and the skinned (keyframe +184
  byte triples, stride 3) vs non-skinned (per-vertex source normal) source selection,
  driving the kernel. Returns the count of vertices whose UV was written. Early-outs
  (null matrix / no normal source / null verts / count<=0) mirror the engine's
  "no reflective material / no active layer / empty mesh" exits.

## Clean entry exposed (the brief's ask)

```cpp
// gilde.exe 0x5c9054
int render::ComputeEnvMapVertexUvs(Vertex* verts, int count, const EnvMapWalkInputs& in);
struct render::EnvMapWalkInputs {
    const float* m3x3;            // bone world 3x3 {m0,m1,m2,m4,m5,m6,m8,m9,m10}
    const u8*    skinNormalBytes; // skinned: keyframe +184 byte triples (3/vertex)
    const float* vertexNormals;   // non-skinned: per-vertex source normals (3/vertex)
    const u8*    reflectiveFlags; // optional per-vertex +77 gate (null => all)
};
```

## Boundaries (rule 8, NAMED — not faked)

The OBJECT-RECORD navigation that feeds the walk is the caller's responsibility (the
engine resolves it from the live object/mesh/anim records; reconstructing those
pointer chases here would be speculative out-of-tree code):
- The **reflective-material gate** (`*(texRec+104) & 1` over the a2[5] array @0x5c90aa)
  — the caller decides whether the object is env-mapped at all; passing
  `EnvMapWalkInputs` to this walk *is* that decision.
- The **bone world 3x3** (`VIBE_Transform_ComputeBoneWorldMatrix` @0x5c8fac) — supplied
  as `m3x3`. The +528-sign world/local select and the bone accumulate live in the
  transform module.
- The **active morph/skin keyframe** (`VIBE_Anim_FindHighestPriorityLayer` @0x5d0e84,
  keyframe = 192*layer + *(layer[26]+348)) — its presence chooses skinned vs
  non-skinned; the caller passes `skinNormalBytes` (skinned) or `vertexNormals`.
- The **non-skinned source-normal record** (`*(vert+72)+12/16/20`) — flattened into
  `vertexNormals` (3 floats/vertex); the walk never serialises engine records.

## THE BIND-SITE HANDOFF (CityView3D / object draw) — for the orchestrator

`VIBE_Mesh_ComputeVertexLighting` runs in the per-object mesh stage, AFTER the morph
/ vertex transform (`VIBE_Mesh_InterpolateMorphVertices` @0x5c953c) and BEFORE the
projection + raster — it writes the per-vertex env-map UV (+32/+36) that the textured
rasterizer then samples from the object's reflective (env-map) texture. In the live
CityView3D frame the object arm is:

```
render::BeginUniverseFrame (0x5b3900)
  -> for each drawable object (type 1/4, hasMesh):
       morph/transform vertices  (VIBE_Mesh_InterpolateMorphVertices 0x5c953c)
       *** if object has a reflective material (texRec +104 & 1):
             render::ComputeEnvMapVertexUvs(verts, n, {bone3x3, skinBytes|normals,
                                                       reflFlags})  <-- THE HANDOFF
             (overwrites vert.u/.v with the spherical env-map reflection UV) ***
       LightMeshVertices (diffuse shade, W6) -> ProjectVerticesToScreen
  -> RasterizeMeshList (0x5aec88) -> RasterizeTexturedTriangleRgbz (0x5F6C30)
       (samples the env texture at the reflected UV -> the shiny look)
  -> PresentToDevice
```

Inputs at the bind site:
- `m3x3` : `render::ComputeBoneWorldMatrix` for the object (transform module).
- skinned vs non-skinned: from `VIBE_Anim_FindHighestPriorityLayer` — if an active
  keyframe exists, pass `skinNormalBytes = keyframe+184`; else pass the per-vertex
  source normals as `vertexNormals`.
- `reflectiveFlags` : the per-vertex +77 byte (or null to env-map every vertex).
- GATE: only call when the object's material is reflective (the +104&1 test). The
  portable build can gate this on the same reflective-material flag the asset loader
  records. NO bind-site file edited this wave (city_view3d.*/universe_render.cpp are
  owned by the orchestrator) — this is the documented handoff.

## Tests

`tests/unit/env_map_walk_test.cpp` — 6 tests, 25 checks, all pass:
- (a) NonSkinnedMatchesKernel — non-skinned walk == kernel oracle.
- (b) ReflectiveGate — per-vertex +77 gate writes only flagged vertices.
- (c) SkinnedNormalSource — skin byte triple -> UnpackSkinNormal -> kernel.
- (d) EarlyOuts — null m3x3 / no normal source / null verts / count<=0 -> 0.
- (e) GoldenNonIdentityReflection — bone 3x3 {0,1,0,-1,0,0,0,0,1}, pos(1,2,3),
      n(1,0,0): Nt=(0,1,0), dot=-4, R=(1,-2,3)->normalize, u=0.6336306, v=0.2327388
      (pins both the reflect math and the walk's matrix-multiply ordering).
- (f) ConstantsGolden — R=(0,0,-1) -> uv (0.5,0.5) (flt_628CC0); skin byte 128 -> 0
      exactly (flt_628CC8), 255 -> (255-128)*2/255 (flt_628CC4).

Build: full-tree `cmake --build build -j` succeeds; `./build/env_map_walk_test` =>
"25 checks, 0 failures".

## Status

COMPLETE & 1:1. `VIBE_Mesh_ComputeVertexLighting @0x5c9054` env-map reflection-UV pass
(both skinned and non-skinned branches), the reflect kernel, the skin-normal unpack,
and all four constants (get_bytes-verified) are reconstructed and golden-tested. The
only deferred pieces are the NAMED object-record navigations above (bone matrix /
active layer / material gate / source-normal record), which are the caller's by design
and documented as the CityView3D handoff.
