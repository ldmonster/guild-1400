# Harden sweep — render cluster 3 (skeleton math)

Files: `src/render/skeleton.cpp`, `src/render/skeleton_pose.cpp`
Tests: `render_skeleton_test`, `render_skeleton_pose_test` (+ e2e). All green (4/4).

Method: every `gilde.exe 0xADDR` function in both files was decompiled AND disassembled,
then diffed line-for-line (control flow, float->int sites, x87-80bit vs float
accumulation rounding, struct strides, constants, return values).

---

## skeleton.cpp

### 0x5ccf18 AdvanceFrameIndex — VERIFIED-1:1
Branch ladder matches the disasm exactly. Signed compares (`jle` = cur<=first,
`jl` = cur<last), the clamp leg `if v8<=count-1 return v8 else count-1` (disasm
`cmp edx,eax; jg loc_..return result`), reverse leg, hold/loop leg all identical.

### 0x5cbc10 InterpolateBoneFrame — FIXED (3 x87 rounding sites)
The translation-accumulation math (3 phases, segment stride, the
v35<v27 / v35==v27 gates, the lead `1 - v29/durFrom`, trail `num/durTo` with
num = (v35==v27)?(v31-v29):v31, the final 3x3 rotate by idx 99..109 and base add
idx 19/20/21) all match the disasm. Phase-2 accumulation is float (each step
`fstp [var_40]`), correctly modelled with `float`. Float-diff-before-multiply is
preserved (original `fsub; fstp var_50` rounds the segment delta to float first;
C++ `float - float` does the same).

Three precision divergences fixed to match the binary:
- **lead (v30)**: was `double`; original computes it in x87 then `fstp [var_24]`
  (0x5cbd0a) rounds to a 32-bit float before the segment multiply. Now
  `float lead = (float)(1.0 - phaseNum/durFrom)`.
- **trail (v32)**: was `double`; original `fstp [var_1C]` (0x5cbe2e) rounds to
  float before the trailing multiply. Now `float trail`.
- **final rotate + base add**: was `(float)(dot_double + base)` (one round). The
  original rounds each 80-bit dot to float via `fstp [edx..]` (0x5cbf3b) BEFORE
  reloading and adding the base (`fld; fadd [eax+4Ch..]`, 0x5cbf47..). Now the dot
  is rounded to float first, then `out = rx + bone[19]` etc. (the trailing
  `float + float` add rounds once more, matching).

Note: the source is the established "extracted math core" — it takes pre-derived
(frames, bone, from/to/phaseNum/phaseEnd) instead of the engine's object/track
records (a2+104 -> +348 frame array, v35=*(a2+0x0C), v29=*(a2+0x10), v31=a4,
flag a5&1 gate, and the `VIBE_Object_SetPosition(v33,&out)` writeback + the
no-op early return). Those record-plumbing parts are the driver's job
(skeleton_pose_driver). The arithmetic is now byte-faithful.

### 0x5c8eb4 AccumulateBoneMatrices — VERIFIED-1:1
Parent link +0x1F8 (504); root zeroes +444/+448/+452; non-root: acc[12..14] +=
idx27/28/29 (bytes 108/112/116), +444 scratch = local(120/124/128) + (base
76/80/84 - pivot 108/112/116). The v5/v6/v7 80-bit holds are exact (direct float
loads, no rounding loss), so `float` locals reproduce the result. Recursion order
`MatrixTransformVectors(acc, local=frame+396, out|tmp)` then recurse on parent
matches.

### 0x5c8fac ComputeBoneWorldMatrix — VERIFIED-1:1
`if(!pivot) return Accumulate(...)`; else subtract pivot[19/20/21] then
pivot[30/31/32] from acc[12..14] (each `float - float`, per-step float round
matching the disasm fstp pairs), re-apply pivot 3x3 (pivot+99) via
MatrixTransformVectors. Acc translation column read at var_4C+0x30/34/38 == acc[12..14].

### 0x5c8ab4 RotateVectorWithFrame — VERIFIED-1:1
Gate `test [frame+528],80h` == signed `signByte < 0` (equivalent). Non-rotate path
`RotateVectorByHierarchy(frame,vec,out)`; rotate path rotates the hierarchy result
by basis 3x3 (idx 99/103/107 | 100/104/108 | 101/105/109). Each output is an
80-bit dot rounded once to float at fstp — modelled with double + single (float)
cast. The original returns `&v9` (address of a stack local = the pre-basis vector),
but BOTH callers (0x5c812b VIBE_Light_CollectAffectedObject, 0x5f68a1
VIBE_Mirror_PrepareReflectionNode) overwrite eax immediately after the call, so the
return value is dead — the reconstruction returning `out` is observationally identical.

---

## skeleton_pose.cpp

### 0x5cd1d8 AdvanceTrackPhase — FIXED (forward-branch arithmetic) + doc corrected
The full UpdateSkeletonPose @0x5cd1d8 (1777 insns, 185 blocks, ~20 leaf calls) is a
scene-graph state machine; its byte-for-byte 1:1 reconstruction is
render/skeleton_pose_driver.cpp (the live-tree function). `AdvanceTrackPhase` is a
DERIVED, narrowed helper modelling only the non-reverse forward leg of the inner
while(1), used for unit testing the phase arithmetic. Two arithmetic divergences
from that forward leg were corrected to the disasm:
- **clamp leg**: was `phase = (dur>0)?dur-1:0`; original `*(v5+8) = v41 - 1`
  (unconditional). Now `phase = dur - 1`.
- **loop leg**: was `phase -= max(dur-1,0)` (subtracts dur-1 from phase); original
  `*(v5+8) -= b + phase - dur + 1` collapses to `phase = dur - 1 - b` (a fixed
  value), b = bit1 of the unmodelled +110 byte (0 on forward entry). Now
  `phase = dur - 1`.
The misleading "reconstructed byte-for-byte" provenance comment was rewritten to
state this is a derived helper and point to skeleton_pose_driver.cpp as the 1:1
driver, and to enumerate what the helper deliberately omits (+110 active bit,
ComputeBoneDelta/SampleBoneTranslation tolerance breaks, repeatCount(+108)
decrement). The defensive frameCount/null guards and loop cap remain (W11-ANIM
fail-safes; do not affect valid input).

### 0x5c953c TransformMeshVertices — VERIFIED-1:1 (non-morph scope) / BOUNDARY
Reconstructs the two non-morph vertex loops of VIBE_Mesh_InterpolateMorphVertices
(the `*(a2+380)==0` paths). attachClamp loop = the `*(v105+529)&0x20` branch with
the flt_13FD168/flt_13FCF3C near/far bounds; plain loop = the no-clamp branch.
Transform delegates to TransformPointByWorldMatrix (src=vertex+72 -> world matrix
indexed [0..14], out to vertex +0/+4/+8 = x/y/z). Bounds: original
`min(global,z)`/`max(global,z)` via `>=`/`<=` (equal-case is a no-op), matched by
`z < *outNear`/`z > *outFar`. The globals are 4-byte floats (`fld`/4-byte mov), so
`float*` out-params are correct.
- BOUNDARY: the morph branch (`*(a2+380)!=0`, calls VIBE_Anim_ComputeMorphWeights
  @0x5c9394 and the int16 morph-delta unpack) is NOT reconstructed here — separate
  morph reconstruction (morph_blend_walk / mesh_transform_walk). Documented.
- BOUNDARY: the original does not reset the bounds globals inside this function
  (they accumulate across all meshes per frame, reset by the caller). The
  reconstruction resets outNear/outFar at entry to surface per-call bounds for
  testability — a deliberate extraction choice, single-mesh-faithful.

### 0x5c9054 ComputeMeshVertexLightingNonSkinned — VERIFIED-1:1 (non-skinned scope)
Reconstructs the v6==0 ("no skin palette") branch of VIBE_Mesh_ComputeVertexLighting:
per lit vertex (+77 flag), reflect the position about the m3x3-rotated normal
(normal from vertex+72 -> +12/+16/+20), write env UV to +32/+36. The reflect /
normalize / flt_628CC0 scale math lives in ComputeEnvMapReflectionUv (other file,
golden-tested by RenderSkeleton.EnvMapReflectionUv). m3x3 order
{m0,m1,m2,m4,m5,m6,m8,m9,m10} matches the disasm's v20/v23/v26 | v21/v24/v27 |
v22/v25/v28 column reads. Lit-flag gate, +72 normal, +32/+36 write all match.
- BOUNDARY: the v6!=0 skinned branch (VIBE_Anim_FindHighestPriorityLayer + the
  *(v6+184) skin-delta byte unpack with flt_628CC8/CC4) is not reconstructed —
  documented deferral (needs the bone-palette delta data + layer search).

---

## Counts
- VERIFIED-1:1: 5  (AdvanceFrameIndex, AccumulateBoneMatrices, ComputeBoneWorldMatrix,
  RotateVectorWithFrame, ComputeMeshVertexLightingNonSkinned)
- FIXED: 3 sites in InterpolateBoneFrame (lead/trail/final float-rounds) +
  2 sites in AdvanceTrackPhase (clamp/loop phase) + 1 doc correction
- BOUNDARY (documented deferrals, leaf data/calls out of tree): morph branch of
  0x5c953c, skinned branch of 0x5c9054, full driver of 0x5cd1d8 (lives in
  skeleton_pose_driver.cpp), object/track record plumbing of 0x5cbc10.
- Tests: render_skeleton_test, render_skeleton_pose_test, +2 e2e — all pass.
