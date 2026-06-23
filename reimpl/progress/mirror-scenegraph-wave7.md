# Mirror reflection — scene-graph halves (wave-7, W7-MIRRORSG)

Status: **reconstructed & wired (1:1)** — closes the rule-8 gap left by wave-6
(`progress/mirror-render-wave6.md`): the node binder + clip-plane builder that
wave-6 reconstructed only as hooks are now full reconstructions. The complete
chain `ShouldRenderMirrorPass → PrepareReflectionNode → CreateClippingPlanes →
CreateOutline → BuildSilhouettePoints → (reflect/clip/append)` is now in the tree.

## What was reconstructed (addresses + completeness)

| addr      | name                                  | reimpl symbol (guild::render)              | status |
|-----------|---------------------------------------|--------------------------------------------|--------|
| 0x5F676C  | VIBE_Mirror_PrepareReflectionNode     | `PrepareReflectionNode`                     | **1:1 full** |
| 0x5F5D08  | VIBE_Mirror_CreateClippingPlanes      | `CreateClippingPlanes`                      | **1:1 full** |
| 0x5F58FC  | VIBE_Mirror_CreateOutline             | `CreateOutline` (in mirror_silhouette)      | **1:1 full** |
| 0x5F5740  | VIBE_Mirror_BuildSilhouettePoints     | `BuildSilhouettePoints` (wave-6, reused)    | **1:1 full** |

Owned files:
- `src/render/mirror_project.{h,cpp}`    — `CreateClippingPlanes`, `PrepareReflectionNode`,
  the allocator hook (`MirrorAlloc/Free`, 0x438f10/0x43923c) and the frustum-table
  hook (`SetMirrorFrustumTableHook`, dword_13DB398).
- `src/render/mirror_silhouette.{h,cpp}` — `CreateOutline` (alongside the wave-6
  `BuildSilhouettePoints`).
- `tests/unit/mirror_scenegraph_test.cpp` — 11 tests / 40 checks, all pass (also
  clean under `-fsanitize=address,undefined`).

NOT touched (other owners): `mirror.{h,cpp}` (wave-6 gate/entry),
`fxrecon_particle_mirror_shadow.*` (shadow agent), and every bind-site file
(`city_view3d.*` etc.) — the handoff is documented below.

## The call tree (rule 7)

```
render::ShouldRenderMirrorPass            0x5b3af0 (gate predicate, wave-6)
  ↑ reflectionPrepared (dword_649D6C) is set by:
VIBE_Mirror_PrepareReflectionNode         0x5F676C   ← per-reflective-node binder
  ├ finds the reflective child (child byte[104] & 0x20)
  ├ installs the project callback (VIBE_Mirror_ProjectReflectedVertices 0x5F6084)
  ├ computes the mirror plane (normal + d) for the matched surface poly
  ├ VIBE_Mirror_CreateClippingPlanes      0x5F5D08
  │   ├ collect mirror-surface polys (poly+20 == key), back-facing (poly+36 i8<0)
  │   ├ gather + dedup their vertex points (0.001 tolerance / VectorWithinTolerance)
  │   ├ VIBE_Mirror_CreateOutline         0x5F58FC
  │   │   ├ VIBE_Mirror_BuildSilhouettePoints 0x5F5740 (silhouette edges)
  │   │   └ chain edges into one ordered boundary loop (merge collinear, dedup)
  │   ├ per outline edge: plane n = TriangleNormal(origin,a,b); d = -(n·a)
  │   └ append the per-orientation frustum planes (dword_13DB398) verbatim
  └ publishes the node (dword_649D6C = node) when a clip-plane list results
```

## Byte-exact details

### CreateClippingPlanes (0x5F5D08)
- Mesh block at object+460: `[1]`=poly array (40-byte stride), `[3]`=poly count.
- Pass 1 counts polys with `poly+20 == surfaceKey`; 0 → return null.
- Pass 2 collects the **back-facing** ones (`(i8)poly+36 < 0`) into a poly-ptr list.
- Each kept poly contributes its 3 vertex pointers; they are deduplicated into the
  `unique` set by pointer-equality OR `VectorWithinTolerance(.,.,0.001)`.
- `< 3` unique points, or a `CreateOutline` that yields `< 6` outline pointers →
  return null (degenerate).
- Final buffer (the engine's return value): **8-byte header** (`+0` i32 plane
  count, `+4` byte = 1) followed by `count` × **16-byte** plane records
  `{nx,ny,nz,d}`. Per edge `(a,b)`: `n = normalize((a-o)×(b-o))` (TriangleNormal,
  origin = flt_5CA2E0 = {0,0,0}), `d = -(n·a)`. Frustum planes appended after.
- Verified golden: a triangle mirror surface → exactly 3 edge planes, each equal
  to `TriangleNormal(origin,a,b)` + `d=-(n·a)` (either orientation), flag byte = 1.

### CreateOutline (0x5F58FC)
- Allocates the silhouette-pair buffer + the connectivity matrix, runs
  BuildSilhouettePoints, then walks the edge set: starting at edge 0 it repeatedly
  picks the next unvisited edge that shares the current endpoint AND is collinear
  (within 0.001 per component, either orientation) with the running edge direction
  — merging collinear segments and following the boundary. Emitted `(a,b)` pairs
  are dedup'd (both orientations). When a chain closes it restarts at the first
  not-yet-visited pair until all are consumed.
- Output: `*outArray` = the ordered `(a0,b0,a1,b1,…)` pointer array, `*outCount` =
  the (always even) pointer count. `< 3` points → returns false, writes nothing.
- Verified golden: a triangle's 3 points → a closed 3-edge loop (6 pointers), each
  vertex appearing twice and every edge's head == the next edge's tail.

### PrepareReflectionNode (0x5F676C)
- Early-outs while `dword_649D6C` is already set (a node is published).
- Scans the mesh's child sub-objects (`mesh+460→parent+480` count, `+5` array) for
  the reflective one (`child byte[104] & 0x20`). None → returns the node unbound.
- Binds it into the draw context: installs the project callback (a2+20), the matched
  surface poly (a2+12/+16) and the current mesh (a2+4). The +528 active byte drives
  the rebuild bit (`2*((u8)(16*b)>>7)` == bit3<<1).
- On rebuild: re-locates the back-facing matched poly, computes the mirror plane
  (normal + `d = n·v0`), frees any old clip planes and rebuilds them via
  CreateClippingPlanes; publishes (`dword_649D6C = node`) iff a list results.

## Rule-8 hooks (named, not faked)

These are the genuinely coupled leaves; each carries its address + reason and a
faithful default. The pure math/control-flow above is 1:1.

1. **Memory-debug allocator** (`0x438f10` AllocDebug / `0x43923c` FreeDebug) —
   `MirrorAlloc/MirrorFree` (+ `SetMirrorAllocHook/SetMirrorFreeHook`). The default
   **zero-fills** (calloc) because the decompile of 0x438f10 shows it
   `memset(p,0,n)`s every allocation, and the dedup/outline code depends on the
   buffers being zeroed (first-null-slot scan; visited/emitted marks).
2. **Per-orientation frustum table** `dword_13DB398` — a RUNTIME-populated global
   (verified all-zero at static analysis via `get_bytes`; not a constant table).
   Indexed by the kept polys' OR'd clip outcode (& 0x3F), stride 26 dwords;
   entry[0] = extra plane count, entry[2..] = the 16-byte plane records appended
   verbatim after the outline planes. Supplied via `SetMirrorFrustumTableHook`
   (default = 0 extra planes — the outline-only path the engine takes when nothing
   is frustum-clipped). The portable vertex view carries no per-vertex outcode
   (a runtime view-clip field), so the orientation code is 0 and the engine's
   "no frustum clip" branch is reproduced exactly.

Pointer-buffer sizing: the original sizes its pointer scratch in **bytes assuming
4-byte (32-bit) pointers**. The recon stores real host pointers, so each pointer
buffer keeps the SAME slot count (`engine_bytes/4` slots) at host pointer width —
capacity-exact and byte-safe. The float plane records (16 bytes) are size-exact.
Provenance comments mark every such conversion.

## CityView3D / BeginUniverseFrame handoff refinement (rule 13)

The pass is already wired by wave-6 into `play::CityView3D::doMirrors`
(`@0x5b3af0`, after particles, before present) via `ShouldRenderMirrorPass`. Today
that bind site hard-codes:

```cpp
gate.reflectionPrepared = false;   // "no prepared reflection node in this view (gap)"
```

because `PrepareReflectionNode` was a hook. **Refinement now unblocked** (the
orchestrator owns `city_view3d.cpp`; this is the documented handoff — NOT edited
here):

- During the scene walk (the same walk that feeds the main pass), call
  `render::PrepareReflectionNode(node, &ctx, &reflectionPreparedGlobal, frame,
  basis)` for each scene node. `reflectionPreparedGlobal` is the view's mirror of
  `dword_649D6C`.
- Then set `gate.reflectionPrepared = (reflectionPreparedGlobal != nullptr)` (and
  `planeParamA/B`, `runtimeActive` from the bound mirror plane) instead of the
  hard-coded `false`. When a scene actually contains a reflective surface
  (`child byte[104] & 0x20`), the gate flips true and the existing reflected-poly
  append path (`AppendMirroredPolys`, wave-6) emits the reflection into the same
  draw list — no other bind-site change needed.
- Install the real backend allocator into `SetMirrorAllocHook/FreeHook` (the live
  `VIBE_Memory_AllocDebug/FreeDebug`) and, when the camera frustum-clip table is
  populated, install `SetMirrorFrustumTableHook` so the per-orientation frustum
  planes are appended. With the defaults the pass is identical to the standalone
  (outline-only) path.

Net: when the scene carries no reflective node the frame is byte-identical to the
non-mirror path (gate false). When it does, the full 1:1 reflection pass runs.

## 1:1 binary-diff audit + FIX (line-for-line re-verification)

A line-for-line re-diff of `PrepareReflectionNode` against the disassembly
(0x5f688a..0x5f68c8) found a **divergence** in the mirror-plane derivation and
**fixed it**:

- **Was (DIVERGED):** the binder fabricated the plane normal with
  `TriangleNormal(origin, v0, v1)` — a cheap analogue (rule-8 violation).
- **Engine (truth):** at 0x5f688a it rotates the bound poly's **stored** normal
  (source = `*(boundPoly+16)+44`) by the camera node's frame via
  `VIBE_Transform_RotateVectorWithFrame` (0x5c8ab4) into `ctx+24..32`, then
  `*(ctx+36) = n.x*v0.x + n.y*v0.y + n.z*v0.z`  (d = n · firstVertexPos).
- **Fix:** `MirrorPoly` now carries the poly's stored `normal[3]`; the binder
  rotates it through a NAMED hook `SetMirrorRotateNormalHook`
  (`VIBE_Transform_RotateVectorWithFrame` 0x5c8ab4, default = IDENTITY — the
  engine's behaviour when `dword_13FCD1C == 0`, the standalone path) and forms
  `d = n · v0` exactly. No more fabricated normal. Pinned by golden test
  `MirrorPrepareNode.PlaneFromStoredNormalAndFirstVertex` (n == stored normal,
  d == n·v0 == 5.0 for a {0,0,1}/v0={2,3,5} case).

Everything else re-verified **1:1** against the decompile/disasm:
- `CreateClippingPlanes` (0x5f5d08): alloc sizes (24*kept points / unique = +12*kept
  bytes), back-facing collect, dedup, outline, per-edge `TriangleNormal(origin,a,b)`
  + `d = -(n·a)`, frustum-table append. Confirmed `v16` starts 0 (`ebx ^= ecx`).
- `BuildSilhouettePoints` (0x5f5740): row/col connectivity stamping
  (`conn[j*n+i]=conn[i*n+j]=1`), `-0.01` tolerance (`dbl_62C2F0`), all-other-points
  test — byte-exact.
- `CreateOutline` (0x5f58fc): unchanged, covered by wave-7 golden tests.

## Build / test

```
g++ -std=c++17 -Isrc -Iinclude -I. -Itests -o mirror_sg_test \
  tests/unit/mirror_scenegraph_test.cpp tests/framework/test_main.cpp \
  src/render/mirror_project.cpp src/render/mirror_silhouette.cpp \
  src/util/math.cpp src/util/math_trig.cpp src/util/coord.cpp
# => 40 checks, 0 failures  (also clean under -fsanitize=address,undefined)
```

CMake globs `src/**` and `tests/unit/*.cpp`, so the new file is picked up
automatically. The wave-6 `mirror_render_wave6_test.cpp` is unaffected (no shared
symbols redefined; the new entries are all additive).
