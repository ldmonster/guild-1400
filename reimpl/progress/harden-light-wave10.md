# Wave-10 hardening — LIGHT / MIRROR / ENV-MAP cluster (W10-LIGHT)

MCP-free memory-safety + edge-case pass over the waves 6-9 reconstructions in this
cluster. No new 1:1 reconstruction. Fixes are faithful memory-safety guards (they do
not change the documented in-bounds behaviour); behavioural ambiguities are flagged
for MCP, not changed. All pre-existing golden values are byte-identical.

## Owned files

Source:
- `src/render/mirror.{h,cpp}`            (ReflectPointAcrossPlane, ClipReflectedBox, AppendMirroredPolys)
- `src/render/mirror_project.{h,cpp}`    (ReflectAndProjectVertices, CreateClippingPlanes, PrepareReflectionNode)
- `src/render/mirror_silhouette.{h,cpp}` (BuildSilhouettePoints, CreateOutline)
- `src/render/env_map_walk.{h,cpp}`      (ComputeEnvMapVertexUvs)
- `src/render/vertex_lighting.{h,cpp}`   (ComputeEnvMapReflectionUv, UnpackSkinNormal, morph kernels)
- `src/render/object_light_shade.{h,cpp}`(Finalize*, PointLightDiffuse, LightMeshVertices x2, TransformVertexLightingNormal)
- `src/render/light.{h,cpp}`             (BuildFalloffLUT, Accumulate{Point,Directional}Light, ComputeRayFalloff, shade reduce)

Tests:
- `tests/unit/mirror_render_wave6_test.cpp`
- `tests/unit/mirror_scenegraph_test.cpp`
- `tests/unit/env_map_walk_test.cpp`
- `tests/unit/object_light_shade_test.cpp`
- `tests/unit/light_band_test.cpp`  (the LightFalloff / LightAccum / LightHarden suites only)

Out of scope (NOT edited): `src/render/light_atmos.*`.

## Build / run (ASAN+UBSAN)

    cmake -S . -B build-asan -DCMAKE_BUILD_TYPE=Debug -DGUILD_BACKEND=OFF \
      -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -g"
    cmake --build build-asan --target mirror_render_wave6_test mirror_scenegraph_test \
      env_map_walk_test object_light_shade_test light_band_test -j$(nproc)

Result (ASAN+UBSAN, `ASAN_OPTIONS=detect_leaks=1`):

| target                     | checks | failures | sanitizer |
|----------------------------|-------:|---------:|-----------|
| mirror_render_wave6_test   |   107  |    0     | clean     |
| mirror_scenegraph_test     |   109  |    0     | clean     |
| env_map_walk_test          |    37  |    0     | clean     |
| object_light_shade_test    |    86  |    0     | clean     |
| light_band_test            |  2074  |    0     | clean     |

Normal `build/` (no sanitizers) is green for all five targets, and the dependent
tests `scene_lights_test / anim_relight_test / light_atmos_test / render_skeleton_test`
still pass unchanged.

## Bug fixed (FAITHFUL memory-safety guard)

### light-list walk null-deref — `LightMeshVertices` (both overloads), object_light_shade.cpp
The per-vertex point-light loop walked `pointLights[p]` for `p < pointCount` with no
null check on `pointLights`. When `pointCount > 0` and `pointLists == nullptr` (an
empty list expressed as a null base), this was an OOB / null read (ASAN-tripping).

Fix: wrap the inner loop in `if (pointLights) { ... }`. The engine never walks a null
light list, so a null base == an empty list; the guard skips it instead of
dereferencing. The in-bounds path (a real, non-null list of `pointCount` entries) is
byte-for-byte unchanged. Pinned by `ObjectLightShadeHarden.MeshZeroVertsZeroLightsNullList`.

## Edge / degenerate tests added (drive the real entries so ASAN exercises bounds)

object_light_shade_test (suite `ObjectLightShadeHarden`):
- MeshZeroVertsZeroLightsNullList — 0 verts; pointCount>0 with NULL list (the fix above), both overloads.
- MeshManyPointLightsInBounds     — 256-light walk fully in-bounds, byte stays 0..255.
- MeshSunZeroNormal               — zero-length object-space normal -> renormalize collapses -> ambient only.
- MeshSunZeroDir                  — zero sun direction -> normalize-by-zero guard -> ambient only.
- FalloffIndexBoundsAtExtremes    — over-unit normal pushes raw LUT index past 1023; clamp verified (no OOB lut read).
- BoneMatrixIdentityAndDegenerate — identity matrix (lit), all-zero matrix (collapse -> ambient), boneScale==0 (no div-by-zero).

env_map_walk_test (suite `EnvMapWalk`):
- ZeroReflectionVector            — pos==0 -> R==0 -> VectorNormalize collapse -> uv (0.5,0.5).
- ZeroMatrixProducesFiniteUv      — degenerate bone 3x3 -> finite UV, no NaN.
- LargeWalkInBounds               — N=257 skinned+gate / non-skinned walks, i*3 striding read to the buffer end.

mirror_render_wave6_test (suites `Mirror*Harden`):
- ZeroNormalPlaneLeavesPointUnchanged — degenerate (zero-normal) reflect plane.
- ZeroNormalPlane / ZeroPlanesKeepsAndExpands — ClipReflectedBox vs zero-normal plane and planeCount==0.
- ZeroCapacityAndZeroCount        — AppendMirroredPolys at full draw list / 0 polys (no entries[] over-write).
- SinglePointNoEdge               — BuildSilhouettePoints count==1.
- ConvexFanWithinPairBufferCapacity — silhouette writer stays within the engine's 8*count pair buffer.

mirror_scenegraph_test (suites `MirrorOutlineHarden` / `MirrorClipPlanesHarden`):
- ZeroOneTwoPointReject           — CreateOutline 0/1/2-point reject (writes nothing).
- CollinearPointsTerminate        — degenerate (collinear) outline terminates, frees buffers (LSAN clean).
- ConvexManyPoints                — N=12 convex outline, every pointer valid, within pair-buffer capacity.
- FrustumCapacityBoundary         — CreateClippingPlanes with the MAX (8) frustum planes lands exactly at the buffer tail (memcpy not past `16*total+8`).
- DegenerateSurfaceReturnsNullNoLeak — 0-poly mesh and coincident-vertex surface -> null, scratch freed.

light_band_test (suite `LightHarden`):
- FalloffIndexClampedForOverlongNormal — point + directional LUT index clamp for |n|=3 (raw index 3069 -> 1023).
- DirectionalZeroDir              — |dir|<1e-6 early-out, accumulator untouched.
- PointLightDegenerateDenominators — distSq==0 (1e-12 floor) and rangeParam==0 guard -> finite / no contribution.
- FalloffTableFullyFilledFinite   — all 1024 LUT entries finite and in [0,1].
- RayFalloffSmoothstepAndClamps   — smoothstep + t<=0 / t>=1 clamp branches.

## Bounds analysis — verified already-correct (no fix needed)

- `FalloffIndex` (light.cpp) / `SunFalloffIndex` (object_light_shade.cpp): clamp
  `(int)(NdotL * -1023)` to [0,1023] before indexing the 1024-entry LUT. Tested with
  an over-unit normal (raw index up to 3069) — clamp holds, no OOB.
- `CreateClippingPlanes` final buffer: sized `16*total + 8`, `total = edgeCount +
  frustumCount`; the frustum `memcpy(rec + edgeCount, .., 16*frustumCount)` ends
  exactly at `buf + 8 + 16*total`. Boundary pinned with the max (8-plane) frustum hook.
- `CreateClippingPlanes` dedup: `unique = points + 3*kept`, dedup loop reads
  `unique[s]` for `s < rawPts <= 3*kept` — in-bounds.
- `AppendMirroredPolys`: `limit = min(remaining capacity, polyCount)`, so
  `out->entries[out->count]` never writes past capacity. Pinned by the full-list /
  zero-count test.
- `ClipReflectedBox`: corner index `c*cornerStrideFloats + 2` reads the caller's
  stride-`cornerStrideFloats` buffer of 8 corners; planeCount==0 / nullptr planes is
  safe (the plane loop simply does not run).
- `TransformVertexLightingNormal`: `boneScale == 0` folds the Y reciprocal-scale to 0
  (no divide), zero-length transformed normal collapses to (0,0,0) (no normalize div).
- `VectorNormalize` (util) zero-collapse already handled in the env-map and sun arms.

## BEHAVIORAL — flagged for MCP (NOT changed; needs the decompile to confirm)

1. **Silhouette pair-buffer capacity vs. a fully-connected point set**
   (`mirror_silhouette.cpp` BuildSilhouettePoints / CreateOutline).
   The engine sizes the silhouette pair buffer at `32*count` bytes == `8*count` 4-byte
   slots, and `BuildSilhouettePoints` writes 2 pointers per silhouette edge. In the
   pathological case where EVERY ordered pair is a silhouette edge, the writer can emit
   up to `count*(count-1)` pointers, which exceeds `8*count` once `count >= 10`. This
   recon preserves the ORIGINAL'S sizing exactly (so the original has the identical
   theoretical overflow). Real inputs are de-duplicated near-convex mirror corner
   clouds whose silhouette is the hull (~`count` edges), well within `8*count`; the
   added convex-fan tests confirm in-bounds for the realistic path. Whether the
   original ever feeds a degenerate set large enough to overflow, or relies on an
   upstream invariant (the dedup tolerance / the `kept <= polyCount*3` cap), needs the
   decompile + call-site analysis via MCP. Do NOT enlarge the buffer unilaterally: that
   would diverge from the original allocation. STATUS: documented, faithful-as-is.

2. **`ComputeRayFalloff` zero-span divide** (`light.cpp` 0x42dd4c). The function
   computes `(a3-a1)/(a2-a1)` with no guard against `a2 == a1`. The original divides
   unconditionally too (it is the literal translation), and float divide-by-zero is not
   in UBSAN's default check set, so it does not trip the harness. Left exactly as the
   decompile has it; the added test exercises only the non-zero-span branches. Whether
   any caller can pass a zero span is an MCP call-site question. STATUS: faithful-as-is.
