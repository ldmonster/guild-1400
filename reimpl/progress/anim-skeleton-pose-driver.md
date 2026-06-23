# Skeletal/Object Pose Driver — `VIBE_Anim_UpdateSkeletonPose @0x5cd1d8`

Status: **DRIVER reconstructed 1:1** (control flow), leaves reused / routed through hooks.

## What landed

`src/render/skeleton_pose_driver.{h,cpp}` — the per-frame pose DRIVER, the 6.6 KB
(0x1A25) state machine `VIBE_Anim_UpdateSkeletonPose @0x5cd1d8`. Reconstructed as
`UpdateSkeletonPose(SkeletonPoseState&, SkeletonPoseHooks&, u32 time)`, reproducing the
original's control flow block-for-block (decompile-address comments throughout):

- **Time split / early-out** (0x5cd1e8–0x5cd22e): masks `time & 0x7FFFFFFF`, the sign
  bit is the `forced`/suppress-light flag (v190); equal-to-`+64` early-outs.
- **Skeletal phase** (0x5cd261–0x5cdc11): optional texture-anim advance (type-4, lit,
  forward); LOD-layer walk (the `i += 384` loop, gated by drawData+624); the 3 per-bone
  tracks (`j += 116`); per-track activity-expiry → `boundaryThisTick` (v193); the clamp
  upper-bound select (mode 0x10 vs endFrame); blend-window weight lerp/snap; the phase
  carry (mode +109 bit2 = subtract vs add); the inner `while(1)` advance ladder — reverse
  leg (0x5cd49e) and forward leg (0x5cd8c6) with the loop/ping-pong/clamp/hold branches,
  the repeat-counter decrement (`if (n){--n; if(n==2) mode|=0x10;}`), and the settle break
  (0x5cd4a8); the per-frame interpolate push; the attachment-window blend; the
  finished/boundary handler (prune + delta) and the veg-cache layer record.
- **Bone-matrix palette rebuild** (0x5cdc18): `ComputeBoneMatrices` via hook.
- **Object morph phase** (0x5cdc1d–0x5cebd7): the Catmull-Rom keyframed position / world
  translation track; its own loop/reverse/clamp advance over the 88-byte keyframes, with
  the v192 terminal that pushes the final position + world translation and frees the morph
  block (or latches when mode 0x08).
- **Vegetation light-cache gate** (0x5cebed–0x5ce466): `!morphActive && !boundary` →
  return; forced → return; non-type-4 → return; else pick the highest-priority layer
  (`FindHighestPriorityLayer`) and rebuild the veg cache (`BuildVegetationCache`).

### Recovered record offsets (cited in the header)
Per-bone track (116-byte): +0 from, +4 to, +8 phase, +12 deltaScratch, +20/24/28 attach
window, +48 attach rec, +52/56 blend window, +60 expiry, +64/68/72 blend weights, +92
settle tol, +100 phaseFrac, +104 AnimHeader, +108 repeat, **+109 mode** (0x1 loop, 0x2
reverse, 0x10 clamp, 0x20 reset-pending), **+110 flags** (0x2 serviced/reverse-leg, 0x4
settle, 0x8 finished, 0x10 hold, 0x40 attach-held). Object morph (v59): +0 count, +4 from,
+8 to, +12 phase, +16 expiry, +20.. base pos, +32.. base rot, +44 repeat, +45 mode, +46
flags, +52 phaseFrac, +56 frame array. Object: +64 lastTime, +68 baseTime, +460 skin,
+464 morph, +492 drawData, +533 type (==4 vegetation).

> KEY DISASM FINDING (CORRECTED 2026-06-11, session-characters slice): the per-track
> service gate (0x5cd30d) tests **+110 & 2** (`flags`), but the inner-loop LEG SELECTOR is
> `0x5cd490: test byte ptr [esi+6Dh], 2` — **+109 & 2** (`mode`), the SAME byte as the
> integer-phase direction (0x5cd439). The loop continuation guards are also +109
> (`0x5cda44` / `0x5cda70: test byte ptr [esi+6Dh], 2`). The earlier note claimed the leg
> selector read +110, which made the FORWARD advance leg unreachable for any serviced
> track (gate requires flags&2 set; selector flags&2 would always pick the reverse leg) —
> a working forward-looping clip was impossible. skeleton_pose_driver.cpp now selects the
> ladder leg and the continuation guards on `mode & 2`; all 10 driver vectors still pass,
> and live forward playback (the person idle clips in play/person_render) advances
> frame-by-frame through the ladder. +110 keeps the service gate (&2), the settle bit
> (0x5cd4a4, &4) and the `(<<6)>>7` bit-1 phase-rate sign extract.

## Leaves REUSED directly (math, not re-translated)
- `AdvanceFrameIndex` 0x5ccf18 — `render/skeleton.h` — called directly in both ladders.
- (the object-anim Catmull-Rom build `ComputeFrameTangents` 0x5ccf70 + `SampleObjectAnim`
  in `render/object_anim.h` remain the morph-path evaluator the engine embeds here.)

## Leaves ROUTED THROUGH HOOKS (genuine side effects / runtime context)
`Object_SetPosition` 0x5af38c, `Object_SetWorldTranslation` 0x5af50c,
`Object_PropagateDirtyFlag` 0x5af2c0, `Texture_AdvanceAnimFrames` 0x5daf78,
`Anim_ComputeBoneDelta` 0x5cba40, `Anim_SampleBoneTranslation` 0x5cc920,
`Anim_InterpolateBoneFrame` 0x5cbc10, `Anim_PruneExpiredAttachments` 0x5d0d38,
`Anim_ComputeBoneMatrices` 0x5cc0d0, `Anim_FindHighestPriorityLayer` 0x5d0e84,
`Light_BuildVegetationCache` 0x5c8560, `Memory_FreeDebug` 0x43923c, and the global frame
counter `dword_649D58`. Inert defaults in the .cpp (headless-clean).

## Gaps (rule 8) — named hooks, NOT faked
- **Per-keyframe phase rate (track +96 / morph rate)**: the engine folds
  `sign*rate*elapsed` into the fractional phase before truncating into the integer phase.
  The rate field is part of the 116-byte record but is supplied by the host (it pre-loads
  `phaseFrac` with the per-tick increment); the driver only performs the integer fold +
  carry. Reason: the rate source (track record +96) needs the live universe-time scaling
  the object/universe runtime owns.
- **Attachment lerp + hierarchy rotation push** (0x5cdae5): the VectorLerp +
  RotateVectorByHierarchy + add-base math that produces the attached child's world
  position is the attachment leaf; the genuine scene-graph push is `objectSetPosition`.
  Modelled as the hook receiving the captured `attachBase` (the exact lerp/rotate over the
  bone matrices needs the live bone palette + attachment record).
- **Object morph Catmull-Rom per-axis evaluation** (0x5cdc6d..0x5ce8f4): the per-frame
  `CatmullRomInterp` position/rotation blend is the object-anim leaf; the driver routes the
  resulting position + world translation through `objectSetPosition` /
  `objectSetWorldTranslation`. The advance ladder + the terminal/free path ARE
  reconstructed; the heavy per-axis blend stays in the object-anim evaluator.
- **Inner-ladder termination bound**: the engine guarantees monotonic phase progress via
  the exact sub-frame arithmetic; with host-owned durations a degenerate table could spin,
  so both ladders carry a generous `frameCount*4+8` guard (same convention as
  `AdvanceTrackPhase` in `skeleton_pose.cpp`).

## Tests
`tests/unit/skeleton_pose_driver_test.cpp` — suite `SkeletonPoseDriver`, **10 vectors,
39 checks, 0 failures** (headless, recording-mock hooks + synthetic skeleton/morph):
early-out on same time; palette rebuilt once per advance; reverse-leg frame step;
non-looping reverse hold re-arm at the clip end; boundary (expiry) → veg-cache + texture
advance; forced tick suppresses texture advance + veg relight (palette still rebuilt);
bone iteration order (layer,track) 0,1,2 via the settle sampler; morph terminal pushes
position + world translation and frees the block; morph latch (0x08) keeps the block +
arms reset-pending; bare tick rebuilds the palette and returns.

## Wiring (rule 13)
The frame-entity callback seam is `render/frame.h:74` (`updateAnim`, tagged
`VIBE_Anim_UpdateSkeletonPose`). The driver is a hooks-based orchestrator over a resolved
`SkeletonPoseState`; binding it to the raw frame record requires the object/universe
runtime the hooks abstract over (not yet modelled in `src/`). The existing deferral notes
in `skeleton_pose.h` / `skeleton.h` / `object_anim.h` are updated to point at this module
as the driver's reconstruction of record.
