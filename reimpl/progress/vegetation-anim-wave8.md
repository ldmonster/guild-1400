# Vegetation animation (wave 8) — `render/vegetation_anim.{h,cpp}`

Owner: W8-VEG. Status: **reconstructed 1:1** (the real per-frame vegetation
update) + **documented dead-end** (no wind sway exists in the original).

## TL;DR — there is NO vegetation wind sway in Die Gilde

The brief asked to find the per-frame "wind-driven vertex wobble" on vegetation
(a sine sway analogous to the water wave grid). **It does not exist in the
binary.** Verified across the entire vegetation transform/animate chain — every
function is a STATIC bone-matrix transform or a dynamic-light relight, with no
time term, no phase accumulator, and no `fsin`/`fcos`:

| addr | function | role | time/sine? |
|------|----------|------|-----------|
| 0x5c8560 | `VIBE_Light_BuildVegetationCache` | per-frame veg update (the gate target) | NO |
| 0x5c9d04 | `VIBE_Mesh_TransformPackedVertices` | i8 unpack + bone matrix mul | NO |
| 0x5c8c40 | `VIBE_Transform_PointToBoneLocalSpace` | point → bone-local | NO |
| 0x5c8b38 | `VIBE_Transform_PointThroughBoneChain` | bone-hierarchy walk | NO |
| 0x5c8fac | `VIBE_Transform_ComputeBoneWorldMatrix` | bone world matrix | NO |
| 0x5c6f90 | `VIBE_Light_ApplyToCachedVertices` | dynamic-light accumulate (LUT atten) | NO |
| 0x5adb6c | `VIBE_Mesh_SelectLodFrame` | LOD frame pick (distance) | NO |

Contrast: the WATER grid `VIBE_Floor_AnimateWaterVertices @0x5be428` DOES carry
four sine phase accumulators advanced by `dt` each frame
(`render/water_vertices.{h,cpp}` + `render/floorwater.{h,cpp}`, modulus 2π). The
vegetation path has nothing of the kind — confirmed by reading both side-by-side.

### What actually makes vegetation change frame-to-frame
1. **Seasonal texture-set swap** on `pfl_`/`vg_`/`!vg_` mesh members. This is the
   `vg_`/`pfl_` path the brief pointed at — it is a TEXTURE swap, not a vertex
   animation. Already reconstructed in **wave-4** (`render/texture_set_table.*`,
   `sim/object_lifecycle2.h::ObjectHideFoliageDecor` @0x506388, gate via
   `VIBE_Util_StrncmpN` @0x5e9ee0; season byte → set index directly). See
   `progress/materials-wave4.md`.
2. **Per-frame dynamic-light relight** — vegetation is the one decor class
   (object type byte +533 == 4) the skeleton pose driver relights EVERY frame, so
   moving light sources (fire, etc.) re-shade leaves. This IS reconstructed here.

## The "vegetation light-cache gate" (what the pose driver references)

`progress/anim-skeleton-pose-driver.md` cites a "vegetation light-cache gate" at
0x5cebed..0x5ce466 in `VIBE_Anim_UpdateSkeletonPose @0x5cd1d8`. That gate (already
reconstructed in `render/skeleton_pose_driver.cpp:447-465`) decides whether to
call `VIBE_Light_BuildVegetationCache @0x5c8560`:
```
if (!morphActive && !boundaryThisTick) return;   // nothing changed
if (forced) return;                              // suppress on forced ticks
if (objectType != 4) return;                     // type-4 == vegetation only
if (!drawData || !skin || (drawFlags & 0x40)) return;
if (vegCacheLayer == 0) vegCacheLayer = FindHighestPriorityLayer()+1;
BuildVegetationCache(obj, vegCacheLayer);         // 0x5ce45f
```
So the gate is purely a "should I relight this vegetation mesh now" decision —
not an animation phase. This module reconstructs the `BuildVegetationCache`
target itself.

## What this module reconstructs 1:1

`render/vegetation_anim.{h,cpp}`:

- **`UnpackVertexComponent` / `UnpackVertexPosition`** — the i8 packed-vertex
  unpack from `VIBE_Mesh_TransformPackedVertices @0x5c9d04` (loc_5C9D7B):
  `v = ((double)(i16)byte + flt_628CDC) * flt_628CD8`. Computed in double then
  narrowed (matches the x87 FILD/FADD/FMUL → float store).
- **`ComputeVertexIntensityUnlit`** — `BuildVegetationCache` loc_5C87FE
  (`byte_649D70 == 0` arm): grayscale byte `(int)(G*0.59 + R*0.30 + B*0.11)`,
  saturating to 0xFF at ≥255, written to vertex +70. Double accumulation matches
  the x87 result (e.g. seed 200 → 199, not 200).
- **`ComputeVertexIntensityLit`** — loc_5C86D7 (`byte_649D70 != 0` arm): three
  bytes (+68 B / +69 G / +70 R). If `max(r,g,b) > 255.0` (dbl_628CAC), rescale all
  three by `255.0/max` (flt_628CA4) before truncating; per-channel store order
  matches the decompile (+70←rgb[0], +69←rgb[1], +68←rgb[2]).
- **`BuildVegetationCache` driver shape** — seed every vertex rgb to
  (200,200,200) (flt_64A074/78/7C), transform (hook), accumulate dynamic light
  (hook), then quantize via the lit/unlit arm (`byte_649D70` selects).

### Recovered constants (get_bytes, bit patterns decoded)
| addr | name | value | meaning |
|------|------|-------|---------|
| 0x628CDC | flt_628CDC | -128.0 | packed-vertex unpack bias |
| 0x628CD8 | flt_628CD8 | 0.00784313771... (1/127.5) | packed-vertex unpack scale |
| 0x628C9C | flt_628C9C | 0.30000001 | luminance R weight (vert +48 / .x) |
| 0x628C98 | flt_628C98 | 0.58999997 | luminance G weight (vert +52 / .y) |
| 0x628CA0 | flt_628CA0 | 0.10999999 | luminance B weight (vert +56 / .z) |
| 0x628CA4 | flt_628CA4 | 255.0 | lit-clamp rescale numerator |
| 0x628CAC | dbl_628CAC | 255.0 | lit-clamp threshold (max channel) |
| 0x64A074/78/7C | flt_64A074.. | 200.0 | per-vertex base colour seed |

### Vertex record (80-byte / 20-float stride, as the cache walks it)
- float[0..2] (+0..+8) bone-transformed position (written by TransformPackedVertices)
- float[12..14] (+48/52/56) RGB colour scratch (seeded, light-accumulated, read)
- byte +68 B intensity, +69 G intensity, +70 R intensity (quantized output)
- source i8 packed position: base+184 indexed `[3*i+0..2]` (u8 each)

## Leaves ROUTED THROUGH HOOKS (rule 8 — named, not faked)
`VegCacheHooks`:
- `transformVertices` → `VIBE_Mesh_TransformPackedVertices @0x5c9d04` bone-matrix
  mul (needs the live bone palette + `VIBE_Transform_ComputeBoneWorldMatrix`).
- `accumulateLights` → `VIBE_SceneGraph_WalkAndInvoke @0x5ac738` +
  `VIBE_Light_ApplyToCachedVertices @0x5c6f90` (needs the live scene graph +
  the `flt_1405110` light-attenuation LUT). Adds dynamic light into rgb[] scratch.

These are the genuinely runtime/data-coupled leaves — same modelling approach as
`render/water_vertices.h` injecting `FindGroupMember`. Inert (null) by default →
headless-clean.

## Tests
`tests/unit/vegetation_anim_test.cpp` — suite `VegetationAnim`, **42 checks,
0 failures**: unpack golden vectors (0/1/127/128/255); unpack position axis
order; unlit intensity (200→200, 100→100, 41.9→42, 0, saturate 300→255); lit
no-clamp; lit clamp (300/150/75 → 255/128/64); lit channel-store order
(+68/69/70 = B/G/R); driver seed→transform→light ordering (seed visible to
transform hook, +20 light → 220); lit arm writes three channels; null-hooks
seed-only quantize (200→200).

## ROUNDING FIX (true 1:1 diff pass — binary beats the prior goldens)

The wave-8 quantize goldens pinned TRUNCATION (200→199, 41.9→41, 127.5→127), but
the disasm of both quantize arms shows a **bare `fistp`** with no preceding
`VIBE_Coord_ConvertX` — i.e. **round-to-NEAREST-EVEN** under the default x87
control word, NOT truncate-toward-zero:
- unlit store @0x5c8844 `fistp dword ptr [eax]` (the `cmp eax,0FFh` saturation
  reads the ROUNDED value).
- lit stores @0x5c8764 / 0x5c8775 / 0x5c8786 (three bare `fistp`).

`ComputeVertexIntensityUnlit` / `ComputeVertexIntensityLit` were FIXED to
`(int)std::lrint(...)` (the codebase's established bare-fistp convention, see
`render/tile_lighting.cpp:305`). Concrete deltas (verified with an x87-faithful
oracle): 200→**200** (sum ≈199.99999702), 100→**100**, 41.9→**42**, 220→**220**;
lit clamp 150*0.85=127.5→**128**, 75*0.85=63.75→**64**. Goldens + header docs
updated to the binary truth.

## Handoff / wiring (rule 13)
The per-frame caller is already in place: `render/skeleton_pose_driver.cpp`'s
vegetation gate (0x5cebed) calls `buildVegetationCache(state, layer)`. The
host/orchestrator binds that hook to `BuildVegetationCache` (this module),
supplying the two `VegCacheHooks` callbacks from the live object runtime:
- `transformVertices` ← the object's bone palette (skeleton.h /
  `ComputeBoneWorldMatrix`).
- `accumulateLights` ← the scene-graph light list walk + `ApplyToCachedVertices`.

**CityView3D does NOT apply any vegetation vertex sway before project** — there is
none to apply. The veg-specific per-frame work is exactly: (a) the wave-4 seasonal
texture-set swap (already wired in the scene refresh), and (b) this light-cache
relight, gated per-object by the pose driver to type-4 meshes. No edit to
`city_view3d.*` / `universe_render.cpp` is required for a "sway" because the engine
has none; the relight rides the existing pose-driver gate.

## Dead-ends documented (rule 8)
- No wind/sine sway: evidenced above (7 functions read, zero time terms). NOT
  invented.
- The full per-vertex dynamic-light accumulation math (`ApplyToCachedVertices`
  @0x5c6f90, the LUT `flt_1405110` attenuation, the type-7 light-source nodes) is
  a large data-coupled lighting leaf shared with `BuildObjectCache @0x5c8218`; it
  is routed through the `accumulateLights` hook rather than re-translated here
  (it is a lighting subsystem, not vegetation-specific — belongs to a future
  light-cache module, not this one).
```
