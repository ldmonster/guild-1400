# Wave-H1 hardening — render chunk 03 part A

Scope (5 files):
- src/render/mesh_transform_unowned_stubs.cpp
- src/render/mesh_transform_walk.cpp
- src/render/morph_blend_walk.cpp
- src/render/node_lod.cpp
- src/render/object_anim.cpp

Method: every function carrying a `gilde.exe 0xADDR` provenance was decompiled AND
disassembled at its address and diffed line-for-line. All constants confirmed via
get_bytes. All float->int sites checked against the disasm (ConvertX @0x5c6b08 = x87
truncate-toward-zero; bare fistp; (int) cast).

Counts: VERIFIED-1:1 = 6 · FIXED = 0 · BOUNDARY = 2 · (1 cross-file type note / handoff candidate)

All 5 owned unit tests pass unchanged after review (node_lod_select 143, object_anim 14,
morph_blend_walk 20, mesh_transform_walk 9, render_cull 32). No source or golden edits
were required — every reconstruction already matches the binary.

---

## node_lod.cpp

### SelectLodFrame — 0x5adb6c — VERIFIED-1:1
Disasm-verified (130 insns). Key points confirmed against disasm, NOT just Hex-Rays:
- Forced branch `v4 = ((u8)(4*flags) >> 6) - 1` then unsigned clamp
  `if ((unsigned)(lodCount-1) < v4) v4 = lodCount-1` — matches lines 30-32 incl. the
  unsigned compare and the 0u-1 underflow path (test ForcedZeroSelectorUnderflows…).
- Distance LOD: `dist = sqrt(dx²+dy²+dz²)` (x87), `lod = dist * (float)lodCount *
  flt_13FC774`. lodCount loaded via `fild word` (the (float)(u8)count). Matches.
- The two ConvertX sites: 0x5adc93 truncates `lod` then `fistp var_18` -> `truncLod`;
  the branch is `cmp eax,[var_18]; jg` i.e. `if (lodCount > truncLod)` (eax holds
  lodCount, reloaded at 0x5adc89). Matches line 48. ConvertX @0x5c6b08 confirmed:
  `fldcw 0x1F` (RC=chop) + frndint -> truncate toward zero; `(int)ConvertX(x)` is exact.
- Near arm: `if (0 > var_14) var_14 = 0` (fldz/fcomp/jbe at 0x5adcee). Matches line 50.
- Far arm: `v12 = (double)lodCount + flt_62807C`, flt_62807C = 0xBF800000 = -1.0
  (get_bytes confirmed). Matches kMaxLodBias.
- Frame stride 384, result = drawData+244+384*index; +8/+12 poly count/cap gate;
  +528 |= 0x40 set when `result != *(v1+460) || byte_64A068`. All mirrored.

Representation notes (intentional, documented in header, behavior-identical):
- Returns the LOD INDEX (or -1) instead of the raw frame pointer (original `result`).
- Extra `index<0 / index>=lodCount` clamps (lines 57-58) and the `!obj.frames` guard
  are no-ops for well-formed objects (the original's branch math already bounds index;
  HardenNullFramesGuard covers the impossible-state guard).

### ClassifyBoundingBoxPlanes — 0x5ad1f4 — VERIFIED-1:1
- 6-bit outcode with progressive AND masks 0xDF/0xCF/0xC7/0xC3 — exact.
- Plane test order: bit5 z>farZ(flt_13FCAFC=0.0), bit4 z<nearZ(flt_13FC76C=0.0),
  bit3 plane@13DCDD0, bit2 @13DCDC0, bit1 @13DCDB0, bit0 @13DCDA0 — matches
  fr.plane[3..0] / fr.farZ / fr.nearZ mapping. (Plane bytes are the live world-frustum
  globals; passed in as the `Frustum` param per the module's reentrant-globals design.)
- Result = `(andMask!=0)<<6 | (orMask & 0xBF)` — exact (v10/v25).
- min/max-z written to outMinZ/outMaxZ are the RAW corner min (v21) / max (v14),
  matching `*a2 = v21 / *a3 = v14`. The original ALSO merges these into the globals
  flt_13FD168 (min) / flt_13FCF3C (max); per the render module's reentrant convention
  that global merge is hoisted to the caller (frame.h runningNear/runningFar). The
  function's own written values are 1:1.

### CullNodeAgainstFrustum — 0x5ad588 — VERIFIED-1:1
- `(obj529 & 0x20) -> (signByte & 0x80) | 0x3F` force-visible — exact.
- AABB present -> ClassifyBoundingBoxPlanes; else `signByte | 0x40` — exact.
- The TransformBoundingVolume call + the vtbl+500 projected-AABB fetch are engine-
  coupled inputs supplied by the caller (hasAabb/corners), as the header documents.

---

## morph_blend_walk.cpp

### AccumulateMorphBlend — 0x5c953c (morph-active branch) — VERIFIED-1:1
Diffed against the morph-active walk (a2+380 != 0). Layer record layout confirmed:
- Layer stride 116 from a2+28 to a2+376; gate `clip(+104)!=0 && (weight(+64)&0x7FFFFFFF)!=0`.
- kf base = clip+348 + 192*frame; scale @+20/+24/+28, bias @+8/+12/+16, points @+180.
- BOTH scale and bias are pre-multiplied by the layer weight (+64): v112=(v15+20)*w …
  v97=(v15+8)*w. Reimpl multiplies s*lw and b*lw (lines 33-36). Matches.
- Point bytes read as unsigned byte -> `(double)(__int16)` (0..255). Reimpl casts
  u8->i16. Matches.
- Per-vertex blend `out[a] = (p0*S0+B0)*w0 + (p1*S1+B1)*w1` (InterpolateMorphVertex).
  Matches kernel in vertex_lighting.cpp (not in chunk; verified consistent).
- First active layer SETS (v118=1 path, `*v39 = …`), subsequent ACCUMULATE
  (`*v56 = *v56 + …`). Reimpl `first` flag — matches.
Boundary repack (documented): gate uses kf points-present + `layerWeight==0.0f` in
place of the clip-ptr / sign-masked-weight test (equivalent: +0.0/-0.0 both fail the
mask AND the ==0.0f compare). The live layer/keyframe RECORD parsing is the named
caller-supplied boundary; the SET/ACCUMULATE walk itself is 1:1.

---

## mesh_transform_walk.cpp

### TransformMeshVerticesByMatrix — 0x5c953c (static tail) — VERIFIED-1:1
Diffed against the static world-transform tails (the +0x20 depth-tracking arm at
0x5c98e5 / 0x5c9580 and the plain arm at 0x5c9b17 / 0x5c9bdb):
- `out = world.rotate(src) + world.translation` via the flat-index multiply
  `*v75 * *v74 + v75[1]*v74[4] + v75[2]*v74[8] + v74[12]` — matches
  TransformPointByWorldMatrix.
- Depth track: `flt_13FD168 = min(flt_13FD168, z)` (`if(global>=z)…`), `flt_13FCF3C =
  max(flt_13FCF3C, z)` (`if(global<=z)…`). Reimpl strict `<`/`>` updates — equal-value
  results identical. The globals are returned as nearZ/farZ out-fields (reentrant
  convention).
Structural repack (documented): the original reads the source position from the vertex
+72 pointer; the reimpl reads x/y/z directly. Math identical.

---

## object_anim.cpp

### ComputeFrameTangents — 0x5ccf70 — VERIFIED-1:1 (math) — see int-dur note
Disasm-verified (145 insns). Catmull-Rom tangent confirmed:
- `invPrev = 1/prev.dur`, `invMid = 1/mid.dur`; inSlope=(mid-prev)*invPrev,
  outSlope=(next-mid)*invMid; `tan = (in+out) * flt_628D4C`, flt_628D4C = 0x3F000000
  = 0.5 (get_bytes). Matches kCatmullRomScale.
- Stores: mid.posOut @+32/+36/+40 = tan*mid.dur; prev.posIn @+48/+52/+56 = tan*prev.dur;
  rot block @+64.. / +76.. reading rot @+20/+24/+28. All offsets verified in disasm
  (`fstp [edx+24h/28h/20h]`, `[eax+30h/34h/38h]`, etc.). Matches lines 15-16, 22-23.

INT-VS-FLOAT DUR (cross-file, behavior-identical, HANDOFF CANDIDATE):
The duration field (frame +0) is loaded by the binary with `fild dword ptr` (0x5ccf75,
0x5ccf85, etc.) — i.e. it is a SIGNED INT32 converted via (float)(int). The reimpl's
`ObjAnimFrame.dur` is a `float`. For integral durations `(float)(int)d == d`, so output
is bit-identical (all tests use integral durations: 750/100/10…). Making dur an i32
would be the strictly-faithful type but ObjAnimFrame is consumed outside this chunk
(src/play/scene_view.{h,cpp}, tests/unit/tower_glide_test.cpp,
tests/e2e/camera_flight_e2e_test.cpp) — NOT edited. Documented here as a known
type-representation difference; no observable divergence. The header already flags the
whole object-anim sample path as a faithful-equivalent reconstruction whose byte-exact
in-engine evaluator is VIBE_Anim_UpdateSkeletonPose @0x5cd1d8.

### BuildFrameTangents — 0x5cec40 — VERIFIED-1:1 (non-looping path; looping deferred)
Diffed against 0x5cec40. The reimpl (zero all tangents, then ComputeFrameTangents over
interior [1, n-2]) reproduces EXACTLY the original's non-looping arm
(flag bit45 & 0x10 || bit45 & 1) for frameCount > 2, traced field-by-field:
- frame 0 outTan never written -> 0 (matches the i==0 zero of +32/+36/+40, +64/+68/+72).
- interior i in [1,n-2]: ComputeFrameTangents(f[i-1],f[i],f[i+1]) sets f[i].outTan and
  f[i-1].inTan.
- frame n-2 inTan never written (would-be writer i=n-1 is out of range) -> 0 (matches
  the original's explicit zero of +48/+52/+56, +76/+80/+84 at i==n-2).
- frame n-1 both tangents never written -> 0 (matches the last-frame full zero).
- frameCount <= 2: reimpl loop empty + zero-all -> all tangents 0 (matches the
  original's `frameCount <= 2` arm which zeroes everything).
DEFERRED (documented in header scope): the LOOPING path — bit46&1
VIBE_Math_SnapVectorToAxis @0x5ca940 rotation snap and the wrap-to-frame[0]
ComputeFrameTangents on the boundary frames — plus the `(result+45 & 4)` gate. These
require the engine anim-flag bits + SnapVectorToAxis and are not modelled by this
non-looping reconstruction.

### SampleObjectAnim / ObjectAnimDuration — no provenance addr
Reconstruction helpers (the per-segment Hermite evaluator; the byte-exact evaluator is
UpdateSkeletonPose @0x5cd1d8). Not 1:1-addressed; left as-is (consistent with header).

---

## mesh_transform_unowned_stubs.cpp — BOUNDARY x2 (handoffs)

### TransformBoundingVolume — 0x5ad438 — BOUNDARY (untranslated placeholder)
Deliberate placeholder (returns 0). The real body (decompiled) walks the full object
record (+492/+528/+530/+531), qmemcpy's a 64-byte matrix, subtracts object pivot
fields, and calls VIBE_Transform_ComputeBoneWorldMatrix @0x5c8fac +
VIBE_Math_MatrixTransformVectors @0x5caaa4. Owned by the node_lod/transform agent (the
file is the shared single-definition point other test TUs link against; the e2e tests
and g_buildCacheCalls depend on it living here). NOT a divergence in an existing
reconstruction — flagged for the owning agent.

### BuildObjectCache — 0x5c8218 — BOUNDARY (untranslated placeholder)
Deliberate placeholder (bumps g_buildCacheCalls). The real body (decompiled) is the
light-cache builder: SelectLodFrame, VIBE_Light_PrepareObjectCache, SceneGraph walks
collecting affected lights, AllocDebug/FreeDebug, two vertex-shade arms
(byte_649D70: HSV-ish max-channel rescale via flt_628C94=0x437F0000=255.0 vs the
weighted-sum `v29*flt_628C88 + v33*flt_628C8C + v34*flt_628C90` clamped to 0xFF),
ApplyVertexShading, RecomputeForObject. Note both arms use plain `(int)` casts
(truncate) on the float channels — not ConvertX. Owned by the LIGHT module agent.
Flagged for handoff (large, multi-subsystem; out of this chunk's scope).
