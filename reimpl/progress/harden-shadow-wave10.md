# Wave-10 hardening — SHADOW cluster (W10-SHADOW)

MCP-free memory-safety + edge-case pass over the waves 6–9 SHADOW reconstructions.
No new 1:1 reconstruction; every fix is a faithful guard that keeps the original's
in-bounds path byte-identical, and every golden value is unchanged.

Cluster: `src/render/shadow.{h,cpp}`, `shadow_render.{h,cpp}`, `shadow_project.{h,cpp}`,
`shadow_light_list.{h,cpp}`, `shadow_ground.{h,cpp}`, `fxrecon_particle_mirror_shadow.{h,cpp}`.

## Method

Built the cluster's unit/itest/e2e tests under ASAN+UBSAN:

```
cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DGUILD_BACKEND=OFF \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -g"
cmake --build build-asan --target shadow_cache_test shadow_ground_test \
  fxrecon_particle_mirror_shadow_test shadow_node_update_test \
  shadow_object_render_test shadow_project_test shadow_light_list_test \
  shadow_project_e2e_test shadow_project_itest -j$(nproc)
```

The pre-existing suite was already ASAN-clean for the in-bounds golden vectors, but
the integration/e2e drivers and the new degenerate vectors tripped genuine UB and a
latent vertical OOB. Both are now fixed and pinned.

## Bugs found + fixed (all faithful — no observable behavior change)

### 1. Signed-shift UB in the rasterizer edge setup — `src/render/shadow_render.cpp`
`EdgeSetup` (shared by `ComputeEdgeSlope` 0x603d00 / `InterpolateEdgeZ` 0x5f6a8c)
rendered two x86 idioms with C++ signed left shifts, which are UB for negative
operands. UBSAN tripped on `shadow_project_itest` and `shadow_object_render_test`:

```
shadow_render.cpp:22:38: runtime error: left shift of negative value -786432
```

- `(long long)dx << 16` (the `(dx<<16)/dy` 16.16 slope) → replaced with
  `(long long)dx * 65536`. Multiply is well-defined for signed and gives the
  **identical** value (`dx` scaled by 2^16) with no UB.
- `(py0 + 0xFFFF) >> 16 << 16` (the decompiler's rendering of the x86
  `(py0 + 0xFFFF) & 0FFFF0000h` ceil-to-16 idiom) → replaced with the mask form
  `(py0 + 0xFFFF) & ~0xFFFF`, which is exactly the original instruction and
  bit-identical for all inputs (including negatives), with no `<< 16` UB.
- Also widened the start-X interpolation product to `(long long)slope * frac`
  (was `int * long long` with an implicit promotion) for clarity; value unchanged.

These are pure UB fixes: the original FPU/integer result is preserved bit-for-bit.
The integration test's golden pixels (`px[10*32+8]==1`, `px[0]==0`) are unchanged.

### 2. Vertical/horizontal OOB write in `FillSpans` for non-square surfaces — `src/render/shadow_render.cpp`
The original shadow surface is **square** (`res x res`), so `RasterizeTriangle`'s
validation (which bounds the projected verts only against the surface *width*) is
sufficient and `FillSpans` never leaves the buffer. The reconstruction's
`ShadowSurface`, however, lets `height != width`; a triangle taller than a short
surface would walk `dstRow` past the buffer (heap OOB write), and the span ceil can
reach the surface width at the boundary.

Added a memory-safety window guard in both the 8bpp and 16bpp `FillSpans` loops:
the destination row must lie inside `[pixels, pixels + pitch*height)`, and the span
is clamped to `[0, width)`. **For the square in-bounds case (the original's real
use) the guard never fires and the output is byte-identical** — `s.spanLen` (the
observable scratch global) is still recorded unclamped, exactly as the original. The
guard is a no-op when `height`/`width` are 0 (legacy callers that leave them unset).
Pinned by `ShadowObjectRender.NonSquareSurfaceNoRowOverflow`.

### 3. Over-large light-count OOB read — `src/render/fxrecon_particle_mirror_shadow.cpp`
`Shadow_CastFromAllLights` iterates `s.lights[v2]` for `v2 < s.misc5C`
(dword_1408A5C). A corrupt/over-large count would read past the fixed
`ShadowModuleState::lights[64]` storage. Clamped the loop to the array capacity
(`sizeof(lights)/sizeof(lights[0])`). The valid path (count ≤ 64; the collector
caps the live list at 4) is byte-identical. Pinned by
`FxReconShadow.CastFromAllLightsClampsOverlargeCount`.

## Edge / degenerate tests added (per the brief)

NEW file `tests/unit/shadow_cache_test.cpp` (63 checks) — the previously-untested
list-bookkeeping half of `shadow.{h,cpp}`:
- `FreeCasterEntry` (0x5f4710) occupied + already-empty.
- `ClearAllCasters` (0x5f474c) **4-slot table FULL**, empty, and disabled-gate.
- `RemoveCasterByLight` (0x5f47dc) match / no-match / disabled-gate.
- `AcquireCacheSlot` (0x5f1e7c) surface-cache scan: **empty → bind first free**,
  **zero-capacity → -1** (size()==0 loop bound), **single slot reuse**,
  **full → evict by type** (phase-2 path), **full → no-evict -1**, and the
  **minSpread** reuse threshold. This is the "4-slot caster cache full/reuse/evict"
  coverage the brief calls out.

`tests/unit/shadow_object_render_test.cpp` (+13 checks → 31):
- `SingleTriangleInBounds` (single-triangle splat),
- `OverlargeTriangleRejectedNoWrite` (projected bbox larger than the surface → the
  validation over-clip rejects the whole tri, no write — the wave-7 over-clip),
- `NegativeCoordTriangleRejected`,
- `NonSquareSurfaceNoRowOverflow` (pins fix #2),
- `CasterAtAndBelowGround` (caster on / below the ground plane).

`tests/unit/shadow_project_test.cpp` (+10 checks → 32):
- `DegenerateDirectionalLightDir` / `DegeneratePointLightCoincident` — zero-vector /
  parallel-to-ground / coincident light directions (IEEE inf/nan; well-defined, not
  UB — same as the FPU),
- `ProjectMeshEmptyAndSingle` (n==0 / n==1 spans),
- `ComputeCasterHeightZeroSamples` (sampleCount==0 fold boundary).

`tests/unit/shadow_ground_test.cpp` (+20 checks → 60):
- `RasterizeSingleCell`, `RasterizeSingleColumnNoStitch`, `RasterizeSingleRowNoStitch`
  (v37==0 / v36==0 phase-2 guards), `RasterizeNullHeights` (the `heights ? : 0`
  guard), `BuildCapacityGateRejects` + `FlatQuadCapacityGateRejects` (the
  dword_64A7EC capacity gates at 0x5f31fa / 0x5f3385).

`tests/unit/shadow_node_update_test.cpp` (+13 checks → 54):
- `CastTableFullNoFreeSlotNoOp` (4-slot node caster table full, no match, no free
  slot → redraw block skipped, no OOB), `CastNullArgsNoOp` (null node/light gate).

`tests/unit/fxrecon_particle_mirror_shadow_test.cpp` (+1 check → 92):
- `CastFromAllLightsClampsOverlargeCount` (pins fix #3).

The ≤4-lights / 0-lights light-list boundary and the 0-lights `UpdateNodeShadows`
case were already covered (`ShadowLightList.PushFiltersAndCapsAtFour`,
`ShadowNode.UpdateZeroLightsNoCast`).

## Test totals (ASAN+UBSAN, all pass; identical in the normal build)

| target                               | checks |
|--------------------------------------|--------|
| shadow_cache_test (new)              | 63     |
| shadow_ground_test                   | 60     |
| fxrecon_particle_mirror_shadow_test  | 92     |
| shadow_node_update_test              | 54     |
| shadow_object_render_test            | 31     |
| shadow_project_test                  | 32     |
| shadow_light_list_test               | 22     |
| shadow_project_e2e_test              | 20     |
| shadow_project_itest                 | 25     |

`shadow_project_e2e_test` runs clean under ASAN with `GUILD_GAME_DIR` set and skips
cleanly without `GUILD_E2E_ASSETS`. Both `build-asan/` and the normal `build/` are
green for every target above.

## BEHAVIORAL ambiguities flagged for MCP (NOT changed — rule 8)

These are observable-behavior questions that need the decompile to resolve; left
exactly as the prior waves wrote them:

1. **`RasterizeHeightField` phase-2 vertex indexing base** (`shadow_ground.cpp`
   0x5f2a58). Phase 2 indexes `vertexA` with absolute indices starting at 0
   (`vA = rowBase + col`), but phase 1 emits starting at the entry `vertexCountA`.
   When the pools are *reused* (entry `vertexCountA != 0`) phase 2 references the
   wrong (but in-bounds) vertices. The tests always start at 0 so this never
   surfaces. NEEDS MCP: confirm whether the original references phase-1 slots
   relative to the phase-entry counter or absolutely. Not a memory-safety bug
   (reads stay in-bounds); a potential 1:1 correctness question only.

2. **`heightBase` (a4) unclamped in `RasterizeHeightField`.** The clip rect is
   clamped to `[0, dim)` so `row*stride + col` stays in `[0, dim²)`, but the engine
   base offset `a4` is added without a bound. With the faithful dim²-sized heightmap
   and `heightBase==0` this is in-bounds; a nonzero base is the engine's global
   heightmap offset (caller-owned). Left as-is — clamping it would change the
   address the original reads. NEEDS MCP only if a nonzero `heightBase` is ever
   passed with a smaller-than-global buffer.

Neither was touched; both keep the current observable output.
