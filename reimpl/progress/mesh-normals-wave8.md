# Wave-8 — STATIC per-vertex NORMAL generation + instance source-normal binding (W8-NORMALS)

Owner: W8-NORMALS. Closes the wave-7 named gap: the wave-7 sun-NdotL kernel
(`object_light_shade`, gap @0x5c7129) and the env-map reflection walk
(`env_map_walk`, @0x5c9054) both consume a per-vertex OBJECT-SPACE normal reached
through a POINTER the engine stores in the 80-byte instance/cache vertex at
**+0x48 (== +72)** — but "the .bgf instance Vertex carries no object-space normals".
This wave reconstructs the engine's STATIC (rest-pose) per-vertex normal GENERATION
and the per-instance +0x48 source-normal WIRING that feeds it.

Files owned (NEW): `src/render/mesh_normals.{h,cpp}`,
`tests/unit/mesh_normals_test.cpp`, this doc. Read-only:
`mesh_postprocess.*`, `mesh_load.*`, `bgf_loader.*`, `mesh_transform*.cpp`,
`object_light_shade.*`, `env_map_walk.*`, `anim_normals.*`, `util/math.*`.
NO bind-site file edited (city_view3d.* / universe_render.cpp / scene_view.* are the
orchestrator's).

---

## The reconstructed function (the real call tree, rule 7)

```
VIBE_Mesh_ComputeVertexNormals        0x5D1A6C   STATIC per-vertex normals
  caller: VIBE_Mesh_LoadBgfFile @0x5d2348 (call site @0x5d2f97) — run once at .BGF load
  ├─ pass 1: per poly  faceNormal = VIBE_Math_TriangleNormal(v0,v1,v2) -> poly+44
  │           (TriangleNormal @0x5cb824 -> VectorNormalize @0x5cb148: face normals are UNIT)
  └─ pass 2: per vertex i  acc = SUM of poly+44 over every poly with i in {vtx0,vtx1,vtx2}
              VIBE_Math_VectorNormalize(&acc)  -> vertex +12/+16/+20
```

Disasm-exact field map (verified):
- vertex stride **24 bytes** (`v10 += 6` floats): pos @+0/+4/+8, normal @+12/+16/+20.
- poly stride **56 bytes** (`j += 56`): vertex indices @+24/+28/+32, face normal @+44/+48/+52.
- `result+16` = vertex base, `result+17` = vertex count, `result+18` = poly base,
  `result+19` = poly count.

**The normal scheme (engine-exact):** an UNWEIGHTED average of adjacent UNIT face
normals, then normalized. NOT area-weighted (the face normal is normalized to unit
length before the sum) and NOT angle-weighted. A vertex referenced by N triangles
gets the sum of N unit face normals; the per-TRIANGLE count matters (a triangulated
quad face contributes 1 or 2 normals at a corner depending on the shared diagonal —
this is exactly what the engine does, and the cube golden test pins it).

The ANIMATED counterpart `VIBE_Anim_CalculateAnimNormals @0x5d0020` (already in
`anim_normals.*`) is structurally identical (TriangleNormal -> per-vertex sum ->
VectorNormalize) but per morph keyframe, and additionally byte-quantizes the result
to keyframe+184 (bias `dword_5CBA30`={1,1,1}, scale `dbl_628F04`=0.5 * `dbl_628F0C`).
This wave is the STATIC/rest path only (the float normals at vertex+12).

---

## The +0x48 instance source-normal wiring (the named gap)

The 80-byte instance/cache vertex carries at **+0x48 (= +72, the 18th dword)** a
POINTER into the shared 24-byte source mesh vertex (pos @+0, normal @+12 — the record
`VIBE_Mesh_ComputeVertexNormals` fills). Confirmed at FOUR reader sites (disasm-exact):

| reader | site | access | what it reads |
|---|---|---|---|
| sun NdotL arm | 0x5c717c | `edx=*(inst+0x48); n=edx[0..2]` | normal at **+0** |
| env-map reflect (non-skinned) | 0x5c92a4 | `p=*(inst+72); n=*(p+12..+20)` | normal at **+12** |
| `VIBE_Mesh_TransformVertexNormals` | 0x5c9c58 | `p=*(inst+72); n=*(p+12..+20)` | normal at **+12** |
| `VIBE_Mesh_InterpolateMorphVertices` (pos) | 0x5c953c | `p=*(inst+72); pos=*(p+0..+8)` | position at +0 |

So +0x48 is the source-vertex pointer. The **draw/env-map/normal-transform** frame
binds +0x48 to the source vertex BASE (+0) and reads the normal at +12. The **sun
light-cache** frame (built by `VIBE_Light_BuildObjectCache @0x5c8218` over the LOD
frame `VIBE_Mesh_SelectLodFrame @0x5adb6c`) dereferences +0x48 at +0 — i.e. its
+0x48 binds directly to the source NORMAL (+12 of the vertex). Both conventions are
modelled as `MeshNormalSource`:
- `kBindToVertexBase`   — +0x48 -> `&source[i]`        (readers deref +12)  draw/env-map
- `kBindToVertexNormal` — +0x48 -> `&source[i].normal` (sun cache derefs +0) sun cache

`BindInstanceNormals` produces, per instance vertex (1:1 by index with the source
verts), the raw pointer to store at +0x48 PLUS the resolved 3-float object-space
normal (always the +12 normal — what every reader ultimately uses).
`FlattenInstanceNormals` packs those into the 3-floats-per-vertex array the wave-7
entries take.

---

## Clean entries exposed (the brief's ask)

```cpp
// gilde.exe 0x5D1A6C — static per-vertex normal generation
void render::GenerateVertexNormals(SourceMeshVertex* verts, int vertexCount,
                                   const NormalTriangle* tris, int triCount,
                                   float* faceNormalsOut /*opt, 3/tri*/);
// (+ std::vector overload)

// the instVert+0x48 source-normal binding
enum class render::MeshNormalSource { kBindToVertexBase, kBindToVertexNormal };
struct render::InstanceVertexNormalBinding { const void* sourcePtr; float normal[3]; };
void render::BindInstanceNormals(const SourceMeshVertex*, int, MeshNormalSource,
                                 InstanceVertexNormalBinding* out);
void render::FlattenInstanceNormals(const InstanceVertexNormalBinding*, int, float* out);
```

`SourceMeshVertex` (24 bytes, pos@+0/normal@+12) is layout-identical to
`render::MeshVertex` (mesh_load.h) and `BgfVertex` (bgf_loader.h) — the record the
.BGF loader emits and `mesh_postprocess::ComputeVertexNormals(Mesh&)` fills. This
module's portable form lets the wave-7 lighting + env-map paths be driven and
golden-tested without the heavyweight engine `Mesh`/object records.

Reuse, no duplication: `mesh_postprocess::ComputeVertexNormals(Mesh&)` (@0x5D1A6C
over the full Mesh struct) and `anim_normals::CalculateAnimNormals` (@0x5d0020) are
NOT re-implemented; this module is the portable static generator + the +0x48 binding
that those bind to. The math reuses `util::TriangleNormal` / `util::VectorNormalize`
(the existing 1:1 of 0x5cb824 / 0x5cb148).

---

## THE BIND-SITE HANDOFF (CityView3D / universe object-build) — for the orchestrator

At .BGF load the source mesh's vertex normals are generated once
(`GenerateVertexNormals`, or `mesh_postprocess::ComputeVertexNormals(Mesh&)` over the
full Mesh). When the per-object instance/light-cache geometry is materialised from
the shared source mesh:

1. **Bind +0x48.** For each instance vertex i (1:1 with source vertex i) set the
   instance's +0x48 slot = `BindInstanceNormals(...).sourcePtr`:
   - the **draw/env-map** frame uses `kBindToVertexBase` (the readers
     `VIBE_Mesh_TransformVertexNormals @0x5c9c58` and the env-map non-skinned arm
     @0x5c92a4 deref +12);
   - the **sun light-cache** frame (`VIBE_Light_BuildObjectCache @0x5c8218`) uses
     `kBindToVertexNormal` (the sun arm @0x5c717c derefs +0).

   This is the single step that "makes the .bgf instance Vertex carry object-space
   normals" — after it, `*(vertex+0x48)` resolves to a real normal.

2. **Feed wave-7.** Pass the flattened normals to the wave-7 entries:
   - sun diffuse: `object_light_shade::LightMeshVertices(..., boneMatrix, boneY,
     boneScale, ...)` reads `MeshLightVertex.vnormal` = the object-space normal
     (`FlattenInstanceNormals` output, 3 floats/vertex). Its bone-matrix overload
     does the Y-rebias + bone-world rotate + renormalize before NdotL.
   - env-map reflection (reflective material only, `texRec+104 & 1`):
     `env_map_walk::ComputeEnvMapVertexUvs(verts, n, {m3x3, /*skin*/nullptr,
     vertexNormals=FlattenInstanceNormals output, reflectiveFlags})` — the
     non-skinned arm consumes the same per-vertex normals.

With +0x48 bound, the bone-matrix `LightMeshVertices` (sun NdotL) and
`ComputeEnvMapVertexUvs` (spherical reflection UV) both fire on real object-space
normals — the wave-6/7 reconstructions finally have their input.

No bind-site file is edited here (rule: city_view3d.* / universe_render.cpp /
scene_view.* owned by the orchestrator); this is the documented handoff.

---

## Constants

The static normal scheme needs NO magic constants — it is pure geometry
(`TriangleNormal` cross-product + sum + `VectorNormalize`). `VectorNormalize`
zero-length behaviour (-> 0,0,0) is the only edge case, reproduced 1:1.
(The animated path's byte-quantize constants `dword_5CBA30`={1,1,1,1},
`dbl_628F04`=0.5, `dbl_628F0C` belong to `anim_normals` @0x5d0020, get_bytes-verified
this wave but not used by the static float path.)

---

## Tests

`tests/unit/mesh_normals_test.cpp` — 8 tests, 109 checks, all pass:
- (a) FlatQuadAllPlusZ — CCW quad -> face & vertex normals = +Z (sign convention +
  averaging).
- (b) ReversedWindingFlips — CW -> -Z; unreferenced vertex collapses to 0.
- (c) UnitCubeCornerDiagonals — triangulated cube: corners 0/7 = ±1/sqrt(3) clean
  diagonals; every corner unit-length with correct outward sign (pins the
  engine-exact per-TRIANGLE averaging, not an idealized symmetric guess).
- (d) DegenerateCollapsesToZero — collinear triangle -> zero normal -> (0,0,0)
  (VectorNormalize @0x5cb148).
- (e) EmptyAndNullGuards — no topology zeroes all normals; null/zero inputs safe.
- (f) BindToVertexBase — +0x48 -> source base, deref +12 == normal; resolved
  normal[] == +12.
- (g) BindToVertexNormal — +0x48 -> source normal, deref +0 == normal (sun arm).
- (h) FlattenForWave7 — 3 floats/vertex for LightMeshVertices / EnvMapWalkInputs.

Build: `cmake --build build` — `guild` lib + `mesh_normals_test` compile/link clean;
`./build/mesh_normals_test` => "109 checks, 0 failures".

---

## Status

COMPLETE & 1:1. The STATIC per-vertex normal generator
(`VIBE_Mesh_ComputeVertexNormals @0x5D1A6C`, portable form), the engine's exact
unweighted-face-normal-average scheme, and the per-instance +0x48 source-normal
binding (both engine conventions, all four reader sites mapped) are reconstructed
and golden-tested. The wave-7 sun-NdotL + env-map reflection passes now have their
object-space normal input. Only the bind-site +0x48 store + the per-object frame
selection (sun cache vs draw/env-map) are the orchestrator's, documented above.

## Addresses (reference)
- `VIBE_Mesh_ComputeVertexNormals` @0x5D1A6C (static gen; caller `VIBE_Mesh_LoadBgfFile`
  @0x5d2348, call @0x5d2f97)
- `VIBE_Anim_CalculateAnimNormals` @0x5d0020 (animated counterpart; anim_normals.*)
- readers of +0x48: sun arm @0x5c717c (in `VIBE_Light_ApplyToCachedVertices` @0x5c6f90),
  env-map non-skinned @0x5c92a4 (in `VIBE_Mesh_ComputeVertexLighting` @0x5c9054),
  `VIBE_Mesh_TransformVertexNormals` @0x5c9c58, `VIBE_Mesh_InterpolateMorphVertices`
  @0x5c953c (position source)
- light-cache builder `VIBE_Light_BuildObjectCache` @0x5c8218 (LOD frame
  `VIBE_Mesh_SelectLodFrame` @0x5adb6c)
- math: `VIBE_Math_TriangleNormal` @0x5cb824, `VIBE_Math_VectorNormalize` @0x5cb148
