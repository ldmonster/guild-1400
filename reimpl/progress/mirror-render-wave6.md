# Mirror / Reflection render pipeline — wave-6 (W6-MIRROR)

Status: **reconstructed & wired (math complete; coupled alloc/scene-graph leaves
hooked per rule 8)**. All owned math is 1:1 with the Hex-Rays decompile and
covered by golden + render unit tests.

Module files (owned):
- `src/render/mirror.{h,cpp}`            — reflect, box clip-cull, reflected-poly append, pass gate
- `src/render/mirror_project.{h,cpp}`    — reflect + perspective project a vertex stream
- `src/render/mirror_silhouette.{h,cpp}` — silhouette-edge extractor
- `src/render/fxrecon_particle_mirror_shadow.{h,cpp}` — mirror clip-plane kernel + reflection-node binder (shared cluster; only the mirror parts are mine)

Tests: `tests/unit/mirror_render_wave6_test.cpp` (17 tests / 58 checks, all pass)
plus pre-existing `tests/unit/fxrecon_particle_mirror_shadow_test.cpp` covering
`Mirror_BuildClipPlane`.

---

## The real reflection call tree (rule 7)

```
render::BeginUniverseFrame                 0x5b3900   (the live 3D frame spine)
  └─[0x5b3af0 gate]→ SceneGraph_WalkAndInvoke(cb = BuildMirroredGeometry)
        VIBE_Mirror_BuildMirroredGeometry  0x5F637C   ← per-node reflection cb
          └ VIBE_Mirror_ClipPolygonToPlanes 0x5F6148  (reflect 8 corners + clip-cull)
          └ LABEL_22: append reflected polys to the global PolyList

(node preparation, reached earlier from the main scene walk:)
  VIBE_Mirror_PrepareReflectionNode        0x5F676C
    └ VIBE_Mirror_ProjectReflectedVertices 0x5F6084   (installed as a2+20 callback)
    └ VIBE_Mirror_CreateClippingPlanes     0x5F5D08
        └ VIBE_Mirror_CreateOutline        0x5F58FC
            └ VIBE_Mirror_BuildSilhouettePoints 0x5F5740
        └ VIBE_Math_TriangleNormal         0x5cb824  (n = (v1-v0)x(v2-v0); d = -n.v0)
```

## Reconstructed functions (addresses + completeness)

| addr      | name                                | reimpl symbol                         | status |
|-----------|-------------------------------------|---------------------------------------|--------|
| 0x5F6148  | VIBE_Mirror_ClipPolygonToPlanes     | `ReflectPointAcrossPlane`,`ClipReflectedBox` | **1:1 full** |
| 0x5F6084  | VIBE_Mirror_ProjectReflectedVertices| `ReflectAndProjectVertices`           | **1:1 full** |
| 0x5F637C  | VIBE_Mirror_BuildMirroredGeometry   | `AppendMirroredPolys` (LABEL_22 core) | **1:1 core** |
| 0x5F5740  | VIBE_Mirror_BuildSilhouettePoints   | `BuildSilhouettePoints`               | **1:1 full** |
| 0x5F5D08  | VIBE_Mirror_CreateClippingPlanes    | `fxrecon::Mirror_BuildClipPlane` (plane-eq kernel) | **1:1 kernel** |
| 0x5F676C  | VIBE_Mirror_PrepareReflectionNode   | (binder; coupled to scene-graph node) | hooked, see rule-8 note |
| 0x5B3AF0  | BeginUniverseFrame mirror gate      | `ShouldRenderMirrorPass`/`MirrorPassGate` | **1:1 predicate** |

### The reflection math (byte-for-byte)
A point P is mirrored across the plane (unit normal n, distance d):
```
t  = -(P·n - d) * 2.0          // flt_62C39C == flt_62C3A0 == 0x40000000 == 2.0f
P' = P + t·n
```
Verified constants via `get_bytes`:
- `0x62C39C` / `0x62C3A0` = `00 00 00 40` = 2.0f (reflection scale)
- `0x62C2F0` = `7B 14 AE 47 E1 7A 84 BF` = -0.01 (dbl, silhouette tolerance)
- `0x5CA2E0` = all-zero vec3 (origin reference for TriangleNormal)
- near gate `0x33D6E555` = 869711765 (poly depth-acceptance threshold, +28 field)

### Clip-cull (0x5F6148)
The 8 source corners (source stride **20 floats**, `v5 += 20`) are reflected into
a 32-float scratch buffer (corner stride **4 floats**, `v18[i]`, `i += 4`). For
each clip plane: a corner is INSIDE when `n·P >= plane.d`; the engine breaks on
the first inside corner. If all 8 are outside any one plane (`v11 >= 8`) the
mirror is culled (`return 0`). Otherwise it expands the running near/far depth
bounds (`flt_13FD168` min, `flt_13FCF3C` max) over the reflected corners' z.
`ClipReflectedBox` reproduces this with a caller-supplied `cornerStrideFloats`
(the engine buffer uses 4).

### Reflected-poly append (0x5F637C LABEL_22)
Per mirrored poly (40-byte stride), append to the PolyList when:
- texture id `v39 != *(a2+12)` (skip the mirror surface itself), AND
- at least one of the 3 verts has `*(v+28) < 869711765` (near gate), AND
- the no-cull bit `(*(j+38) & 4)` is set, OR the **reflected** signed screen
  area is positive: `(x0-x2)(y0-y1) > (x0-x1)(y0-y2)` (winding flipped by the
  reflection, so the test is reversed vs the normal forward path).
Sort key = `((tex - dword_1406A84) >> 7) + 1` when textured, else 0. Bounded by
remaining capacity `min(dword_13ECE80 - dword_13FC770, polyCount)`.

### Silhouette extractor (0x5F5740)
For every ordered pair (j,i), j≠i, with `conn[j*n+i]==0`: the plane normal is
`TriangleNormal(origin, pt[j], pt[i])`; the edge is a silhouette when **no other**
point pt[k] satisfies `normal·pt[k] < -0.01`. Emits `(pt[j], pt[i])` pointer pairs
and stamps the symmetric connectivity matrix (`conn[j*n+i] = conn[i*n+j] = 1`).

---

## EXACT CityView3D / BeginUniverseFrame handoff (rule 13)

The mirror reflection pass is the final geometry stage of `BeginUniverseFrame`
(@0x5b3900), emitted **after** the main scene walk + `ProcessSceneNode`, **after**
the particle systems (`VIBE_Particle_RenderSystem` loop), **after** the sky flares
(`UpdateSkyFlares`), and **before** the depth-bound fixups / `ResetEngineState`.

The bind-site call to add to `play::CityView3D`'s frame (orchestrator-owned file,
NOT edited here):

```cpp
// AFTER the object draw list + particles + sky flares, BEFORE present:
render::MirrorPassGate gate{
    /*featureEnabled    */ (mirrorFeatureByte & 0x40) != 0,  // byte_14080EC[0]
    /*reflectionPrepared*/ reflectionNodePrepared,           // dword_649D6C != 0
    /*planeParamA       */ mirrorPlaneA != 0,                // dword_1408A74
    /*planeParamB       */ mirrorPlaneB != 0,                // dword_1408A78
    /*runtimeActive     */ (mirrorRuntimeByte & 1) != 0,     // byte_1408A98
};
if (render::ShouldRenderMirrorPass(gate)) {
    // Re-walk the scene graph; for each node invoke the reflection callback that
    // reflects+clips the node's box (ClipReflectedBox) and, when kept, appends the
    // node's reflected polys (AppendMirroredPolys) into the SAME DrawList the main
    // pass filled. The reflection is then rasterized with the scene.
    for (auto* node : sceneNodes)
        appendNodeReflectedPolys(node, mirrorPlane, clipPlanes, &drawList);
}
```

- WHERE in frame order: last geometry stage, just before `flt_13FD168` near-bound
  fixup and `dword_649DA4 = dword_13FC770`.
- INPUTS: the prepared reflection node (`dword_649D6C`), the mirror plane
  (node+24/28/32 normal, +36 d), the clip-plane list built by CreateClippingPlanes,
  and the shared `DrawList` (PolyList cursor `dword_13FC570` / count `dword_13FC770`
  / capacity `dword_13ECE80`).
- GATE: the 5-term predicate above (`ShouldRenderMirrorPass`). When the mirror
  feature is off or no reflection node was prepared, the pass is skipped and the
  frame is identical to the non-mirror path.

---

## Rule-8 note (what is hooked, not faked)

`PrepareReflectionNode` (0x5F676C) and the alloc/copy/traversal halves of
`CreateClippingPlanes` (0x5F5D08) are coupled to: the scene-graph node records
(object +460/+492 mesh blocks, the 40-byte poly stride array), the memory-debug
allocator (0x438f10 / 0x43923c), the per-node draw callback slot (a2+20), and the
transform helpers (`RotateVectorWithFrame` 0x5c8ab4). The **pure arithmetic** —
the reflection, the clip-cull, the projection, the silhouette/outline math and the
plane-equation kernel — is reconstructed 1:1; the coupled leaves are reached
through the fxrecon hooks (each NAMED with its address + reason), so the module
stays portable and link-clean. Nothing is approximated.

## Build/test note

Owned sources compile clean (`-fsyntax-only` + standalone link). The full
`mirror_render_wave6_test` CMake target was transiently blocked by concurrent
edits in non-owned files (`render/water_render.h` missing `DrawList`,
`render/sky.cpp` missing `size_t`, `render/sprite_scale.h` static_asserts) — other
wave-6 agents' in-flight work. Verified independently by linking the three owned
mirror sources + `util/{math,coord,math_trig}.cpp`:

```
g++ -std=c++17 -Isrc -Iinclude -I. -Itests -o mirror_test \
  tests/unit/mirror_render_wave6_test.cpp tests/framework/test_main.cpp \
  src/render/mirror.cpp src/render/mirror_project.cpp src/render/mirror_silhouette.cpp \
  src/util/math.cpp src/util/coord.cpp src/util/math_trig.cpp
# => 58 checks, 0 failures
```
