# Harden — render_06_cluster4: skeleton_pose_driver.cpp

File: `src/render/skeleton_pose_driver.cpp`
Function: `UpdateSkeletonPose` (gilde.exe **0x5cd1d8** `VIBE_Anim_UpdateSkeletonPose`,
`__usercall al=fn(obj@eax, time@edx)`, size 0x1A25, 1777 insns, 185 BBs, 20 callees).
Test target: `skeleton_pose_driver_test` — **12 tests / 49 checks, all green.**

The reconstruction is the documented ORCHESTRATOR abstraction: the heavy leaf math
(bone sample/interpolate/delta, Catmull-Rom morph eval, vector rotate, the genuine
scene-graph pushes, the texture/veg/light side effects) is routed through
`SkeletonPoseHooks`; the DRIVER control flow (the per-track frame/phase advance ladder,
the bone iteration order, the boundary/finish gating) is reconstructed in-line. The
sweep diffed that DRIVER control flow line-for-line against the decompile + disasm.

## FIXED (2 control-flow divergences confirmed against the disasm)

### FIX 1 — vegetation-cache search gate INVERTED (0x5ce411 / 0x5ce419)
Disasm:
```
5ce411  cmp [esp+104h+var_38], 0     ; var_38 = v180 = vegCacheLayer
5ce419  jz short loc_5CE451          ; v180==0 -> SKIP FindHighestPriorityLayer, BuildVegCache(obj,0)
5ce428  call VIBE_Anim_FindHighestPriorityLayer   ; only reached when v180 != 0
```
The engine calls `FindHighestPriorityLayer` **only when a layer was already recorded
this tick (v180 != 0)**; on the zero path it jumps straight to
`BuildVegetationCache(obj, 0)`.

- Before: `if (st.vegCacheLayer == 0) { ...FindHighestPriorityLayer... }`  (inverted)
- After:  `if (st.vegCacheLayer != 0) { ...FindHighestPriorityLayer... }`

`BuildVegetationCache` is still called unconditionally afterward (matches the fallthrough
to `loc_5CE451`). New regression test `VegCacheGateSkipsSearchWhenZero` locks the
skip-when-zero sense (search not called, BuildVegCache called with 0).

### FIX 2 — morph reverse-leg dispatch keyed on PHASE instead of TOFRAME (0x5ce93b)
Disasm:
```
5ce230  mov ebx,[esi+0Ch]   ; phase
5ce235  jl  loc_5CE93B      ; phase<0 -> reverse terminal dispatch (else LABEL_116)
5ce93b  cmp dword ptr [esi+8], 0   ; *** +8 == TOFRAME ***
5ce93f  jle loc_5CE97F      ; signed toFrame<=0 -> clamp/loop/hold block
5ce941  mov eax,[esi+4] / dec eax / jnz 0x5cea19   ; fromFrame!=1 -> step back
5ce94e  test cl,10h / ...   ; fromFrame==1 && clamp -> finish (frame 0)
```
The reconstruction tested `m.phase <= 0` (which is vacuously true here, since the leg is
only entered with phase<0) — the binary keys the terminal block on **toFrame<=0**, and
otherwise takes either the `fromFrame==1 && clamp` finish or the step-back (`fromFrame--`).

- Before: `if (m.phase <= 0) { clamp/loop/hold }`  then unconditional AdvanceFrameIndex.
- After:  `if (m.toFrame <= 0) { clamp/loop/hold } else if (m.fromFrame==1 && (mode&0x10))
  { fromFrame=0; phase=0; finished } else { flags &= ~0x10; fromFrame--; }`

The `phase += dur(toFrame)` add in the step-back (0x5cea19, reads the 88-byte keyframe
`+0` duration) stays host-owned — see BOUNDARY below. New regression test
`MorphReverseClampToFrameGate` locks the toFrame-gated clamp terminal + free path.

## VERIFIED-1:1 (DRIVER control flow that already matched the binary)

- 0x5cd1e8–0x5cd22e split-time / sign-bit `forced` (`a2<0`) / `now==prev` early-out,
  `*(a1+64)=masked time`, `elapsed = now-prev`. v191/v193/v180 cleared.
- 0x5cd6fd texture-advance gate `!forced && hasSkin && (drawData+2317 & 0x40)==0 &&
  objectType==4`.
- 0x5cd278 LOD loop bounded by drawData+2316; 0x5cd2ce 3-track loop (j 0/116/232).
- 0x5cd30d track gate (+104 header && +110&2); 0x5cd335 expiry arms v193.
- 0x5cd359 clamp-bound select (`mode&0x10` -> frameCount-1, clears deltaScratch when
  !deltaEncoded; else endFrame); 0x5cd391 `mode &= ~0x20`.
- 0x5cd3cf blend window: lerp when `blendTo>now`, else snap to blendEnd + finish if 0.
- 0x5cd40c phase carry: ConvertX TRUNCATES; `(i32)carry` truncation matches; reverse
  bit (+109&2) selects `phase -= ci` (0x5cd450) vs `phase += ci` (0x5cd77e);
  `phaseFrac = carry - (float)ci` (0x5cd48d). reverseSign = `(flags<<6)>>7` = `(flags&2)?1:0`.
- 0x5cd490 inner ladder leg selector `test byte [esi+6Dh],2` = **+109 (mode)**, not +110.
- 0x5cd7a3 reverse step-back (`firstFrame<toFrame`): fromFrame--, AdvanceFrameIndex,
  phase += dur(toFrame). 0x5cd7af loop->bounce (mode&1): fromFrame=first, flags&=~0x10,
  mode&=~2, phase=-p, DecRepeat. 0x5cd809 hold/clamp.
- 0x5cd8c6 forward leg: `phase>=dur(fromFrame)`; 0x5cd8d7 mid-step; 0x5cd8e3 clamp
  one-shot (v161=advLast, finished, phase=dur-1); 0x5cd921 loop->reverse with the exact
  `phase -= (reverseSign + phase - dur(fromFrame) + 1)`; 0x5cd989 hold/wrap + ComputeBoneDelta.
- AdvanceFrameIndex mode arg = `BYTE1(*(v5+108))` = +109 = mode; runs for all 3 forward
  terminal sub-branches (0x5cd916).
- 0x5cd4a8 settle (+110&4): `dur(fromFrame)/2` integer div default, override with phase
  when `0<=phase<dur`; `fabs(x-tolPos[0])<tol && fabs(z-tolPos[2])<tol`; on settle
  phase=sample, finished, flags|=8 (0x5cd545).
- 0x5cda17 loop-continuation guard: both direction tests `test byte [esi+6Dh],2` = +109.
- 0x5cd55c LABEL_31 interpolate (deltaEncoded path).
- 0x5cd582 attachment window: capture base once (flags&0x40), lerp/rotate/push, release
  on window end. Offsets +20/+24/+28/+48 and v22[19..21] base confirmed.
- 0x5cd5d6 boundary bookkeeping: finished -> clear +110&2; `mode&8` -> arm +109|=0x20,
  else v189=1, ComputeBoneDelta(!deltaEncoded), PruneExpiredAttachments.
- 0x5cd65f v180 recorded only when v193 && !v189.
- 0x5cdc18 ComputeBoneMatrices always after the skeletal pass.
- 0x5cdc1d morph gate (+464 && +46&2); 0x5cdc3a `mode &= ~0x20`; 0x5ce184 expiry arms v191.
- 0x5ce19b morph phase carry mirrors the skeletal rule (reverse bit +45&2; +46-based sign).
- 0x5ce275 LABEL_116 terminal: SetPosition + SetWorldTranslation, `mode&8` latch
  (+46&=~2, +45|=0x20) else free (0x5ce370). Latch/free offsets verified (tests 8/9).
- 0x5cebed final gates: `!v191 && !v193` / `forced` / `objectType!=4` / drawData+hasSkin
  +(drawData+2317&0x40) early-returns; all return al=1.
- DecRepeat idiom (+108 dec; on 2->1 set clamp 0x10 on +109/+45) verified at every site.

## BOUNDARY (rule 8 — data not modelled in the resolved view; not faked)

- **Morph forward/reverse per-keyframe DURATIONS** (`*(v59+56) + 88*k + 0`). The 88-byte
  object-anim keyframe array (`PoseMorphAnim::frames` is an opaque `const void*`) is not
  resolved here, so the morph forward leg (0x5cea3b: `phase<dur(fromFrame)` early-out, the
  `phase -= dur` / `phase += dur` folds, the loop->reverse `v121=toFrame` write,
  ComputeFrameTangents at 0x5ceb27) and the reverse step-back phase add (0x5cea19) remain
  host-owned via the position hooks. The branch *structure* of the reverse leg is now
  faithful (FIX 2); the duration-dependent phase arithmetic is the boundary. The skeletal
  durations ARE modelled (`PoseAnimHeader::durations`) so the skeletal ladder is full 1:1.
- **Per-track phase RATE** `*(v5+96)` / morph `*(v59+48)`: the `phaseFrac += sign*rate*
  elapsed` multiply-accumulate is host-pre-loaded into `phaseFrac` (the rate field is not
  in the resolved track view); the driver performs the truncating integer fold 1:1.
- **Catmull-Rom morph position eval** (0x5cdc82..0x5ce735) and the genuine scene-graph /
  asset / light side effects: routed through hooks with inert defaults (documented in the
  header). None faked.
- **Defensive `while(1)` bound**: the two inner ladders carry a `frameCount*4+8` guard
  (the binary is unbounded `while(1)`). It cannot trip on valid host data; it only
  prevents a spin on degenerate/zero-duration tables, matching the AdvanceTrackPhase
  convention in skeleton_pose.cpp.

## Counts
VERIFIED-1:1 blocks: ~30 cited address ranges.  FIXED: 2 (0x5ce419 gate inversion;
0x5ce93b morph reverse toFrame gate).  BOUNDARY: 4 (morph keyframe durations, phase rate,
Catmull-Rom/side-effect leaves, defensive ladder bound).  Tests: 12 (10 prior + 2 new),
49 checks, all green.
