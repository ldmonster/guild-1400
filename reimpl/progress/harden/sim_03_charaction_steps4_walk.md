# Harden sweep — sim chunk 03: charaction_steps4.cpp + charaction_walk.cpp

MCP live (gilde.exe, imagebase 0x400000). Every provenanced function decompiled and
diffed line-for-line; float->int / ConvertX / constant-byte / RNG-draw classes checked.

## Build note (HANDOFF — not in my ownership)
`cmake --build . --target guild` currently FAILS to link libguild.a due to a
pre-existing compile error in **src/sim/character_recon5_transport.cpp:29**
(`invalid static_cast from TObject* to intptr_t`). That file is owned by another
agent. My two files both **compile clean** in isolation (verified: their .o objects
rebuild with zero diagnostics). Tests could not be executed end-to-end until the
transport file is fixed; my source+golden edits compile.

---

## charaction_steps4.cpp

### JourneymanRecruitStep — gilde.exe 0x4d07b8 — VERIFIED-1:1 (structure)
Clock +4min, resolve +176/+172, state==-2 || !entity -> free (falls through),
state 0 + packet applied -> render + quickjump + free. Control flow matches.
The recruit message/named-object emit is host-collapsed into the quickjump hook
(BOUNDARY: render-bridge data, rule 3/8 — opaque text+object emit). No numeric bug.

### BuyObjectStep — gilde.exe 0x4d16a4 — VERIFIED-1:1
state -2 free / nonzero return / 0 scan. QueryBegin(self,1,4,cityIdx), category 6
filter, up to 64, RandomModulo(count) as u16, AdjustMood(-50), cityCategory==6 gate,
quickjump. Matches (the Hex-Rays `v5 < 64` loop-cap noise is the count clamp).

### WaitThenMoveStep — gilde.exe 0x4d1168 — **FIXED** (2 divergences)
Evidence: get_global_value dbl_61EAE4=0x3fe0000000000000 (0.5),
dbl_61EAEC=0x4050000000000000 (64.0), flt_61EAF4=0x3f000000 (0.5); ConvertX@0x5c6b08
truncates (frndint w/ RC=trunc).
1. **Threshold** was bare integer `willingness`; binary (0x4d11dd) computes
   `threshold = (double)(i16)willingness * 0.5 + 64.0` and compares `(double)roll >=`.
   Fixed to the double formula.
2. **Accept-branch value** was `2*fee` (copied from the refuse branch). Binary accept
   (0x4d12a9) = `(int)trunc((double)fee * 0.5)` (flt_61EAF4 + ConvertX). Refuse branch
   (0x4d1221) = `2*fee` (integer) — that one was already correct.
   Fixed accept to `(int)((double)fee * 0.5)`.
Golden updated (charaction_steps4_test.cpp WaitAccept/WaitRefuse): WaitAccept now
threshold=100*0.5+64=114, roll 200 accept, cmd15 value 25 (was 100); WaitRefuse
threshold=200*0.5+64=164, value 20 (unchanged).

### GossipBroadcast — gilde.exe 0x4d0b84 — **FIXED** (bribe value) + BOUNDARY notes
Evidence: disasm 0x4d0ce3 `fild;fmul flt_61EAD4;mov eax,4;call ConvertX;fistp;call
RandomModulo`. flt_61EAD4=0x3c23d70a (~0.01). ConvertX leaves eax, so RandomModulo's
modulo arg = 4 (from `mov eax,4`), result u16 then `+2`, `imul` by scaled wealth.
- **Bribe** was `wealth * (RandomModulo(0)+2)`; binary =
  `trunc((double)wealth * 0.0099999998) * (RandomModulo(4)+2)`. Fixed: scale +
  truncation added; modulo arg corrected 0 -> 4 (keeps RNG stream + value 1:1).
- BOUNDARY: loyalty gate `byte_12CE918[2*idx] >= 2` and the `word_12CE910[idx]!=-1`
  empty-slot test are folded into the host `findPersonById` hook (table data not in
  tree). Outer rumor render (3365 template, `3367+rand(3)` variant) is opaque emit.
Golden updated (GossipSendsRumor): wealth 10 -> 1000 so trunc(1000*0.01)=10, *(0+2)=20.

### DuelArmCombatant — gilde.exe 0x4cfab4 — **FIXED** (missing RNG draw)
Evidence: disasm 0x4cfb6a `mov eax,8` / 0x4cfb6f ConvertX / **0x4cfb7b
`call RandomModulo`** unconditional, result discarded. ConvertX leaves eax => arg 8.
- Added the unconditional `randomModulo(8)` draw after productionRating (was missing
  entirely — RNG stream desync). Result discarded, matching the binary.
- Verified office-rank branch (0x4cfbde): OfficeHolder nonzero -> rank = holder byte
  (var_23, host-side BYTE extraction via hook); else RandomModulo(3)+1. coord27(-20),
  highlight(rank=var_14 via aliased read), clock stamp, cmd29(-1). All match.
  (rank when self has no office is uninitialized in the binary -> reimpl uses 0; not
  observable in tests.)

### DuelResolveStep — gilde.exe 0x4d0438 — VERIFIED-1:1
Packet gate (+132==-1 or status), `result = state+2`, +132=-1, switch 0/1 (disarm+
rearm if flags&2, free), 3 (disarm+rearm, no free), 4 (move-39 pair + disarm+rearm),
default return. cmd25(456,0,4,1024) disarm, +2min, cmd29(-1). Matches.

### DuelDispatch — gilde.exe 0x4cfc24 — VERIFIED-1:1 (structure)
FindFirstHandlerByFilter(1,1,+172), combatants partner[+176]/[+172], terminal-or-
missing teardown (destroy slot, rearm partner cmd29, free), state 0 open dialog
(+2 days, ++state), state 1 deadline>0 -> rearm; else window match + result:
1210 intro / not-1155 rearm / 1155 fallthrough teardown. Matches; UI/voice/card are
host-bridge (BOUNDARY rule 3).

### MasterExamPromptStep — gilde.exe 0x4d0d98 — VERIFIED-1:1 (structure)
terminal>=-2 teardown; compare vs +68, cmp==-1 not-due return; state 0 fee=
wealth*flt_61EADC -> create/render -> state1; state1 result 1210 charge (cmd15 +
broadcast 8) / 1155 broadcast -6 -> teardown. flt_61EADC=0x3cf5c28f confirmed. fee
flows into opaque render/emit hooks (BOUNDARY). Control flow matches.

### MasterExamDecideStep — gilde.exe 0x4d0f58 — VERIFIED-1:1 (structure)
terminal teardown; compare vs +68; state 0 fee=trunc(wealth*flt_61EAE0) -> +176 ->
create/render -> state1; state1 compare vs +82 (>0 teardown), window+result 1210
charge (cmd16 + slotReset28) / 1155 teardown. flt_61EAE0=0x3d4ccccd confirmed. Matches.

---

## charaction_walk.cpp

### WalkRotateTowardHeading / WalkOnPathRotation — gilde.exe 0x409b2c — **FIXED** (constants)
All rotation constants confirmed byte-exact via get_bytes:
dbl_610884=1.4, dbl_61088C=0.04, dbl_610894=-0.04, dbl_61089C=0.90909(10/11),
flt_6108A4=0.4, dbl_6108AC=2π/3, dbl_6108B4=π/2, dbl_6108BC=π/3, dbl_6108C4=π/6,
dbl_6108CC=1.5, dbl_6108D4=0.14, dbl_6108DC=0.2, dbl_6108E4=2π.
- Control flow VERIFIED: three-way on +264 (<0 / >0 / ==0), bucketed step (π/2,π/3,
  π/6 -> 0.2/0.14/{0.04,0.04*1.5}), fast*0.4, fmod 2π wrap, overshoot snap (delta>0
  nv>target, delta<0 nv<target), aligned snap. Threshold branch (==0): mesh+76/80/84
  minus state+292/296/300, dist > segLen*1.4 -> anim bit3. Matches reimpl field map.
- **FIXED**: tightened kRunThresh/kBucketHalfPi/kBucketPi3/kBucketPi6/kTwoPi from
  truncated literals to the full-precision decoded doubles (1:1 with the .rdata bytes;
  avoids comparison-edge drift).
- BOUNDARY: run-threshold ramp latch `*(avatar+420)=0.4f when |delta|>2π/3 && !mounted`
  (0x409ced/0x409e86) is a move-speed render latch the reimpl does not model on the
  WalkAvatar struct — documented, deferred (render side, rule 3-adjacent).
- BOUNDARY: the binary recomputes +264 via Math_AngleToTargetSigned each tick; the
  reimpl uses the stored turnAngle (documented in source). Geometric oracle is host.

### WalkSegmentDuration — gilde.exe 0x4093b0 (advance block) — VERIFIED-1:1
`cap-1 > idx || cap<=1` -> 40/20 (mounted/not); else 24/8; indoor *dbl_610814.
dbl_610814=0x4008000000000000=**3.0** confirmed (kIndoorDur=3.0 correct; the header
"1.2" comment was stale — code uses 3.0). Test WalkSegmentDuration(1,4,true,true)=120.

### WalkAnimSpeed — gilde.exe 0x40a0b8 (speed block) — VERIFIED-1:1
base*mul*tileFactor*ramp; mul = tile 6/11 ? stairs/water : flat, mounted variants.
flt_6108F0=2.2, F4=2.5, F8=1.7, FC=1.9 confirmed; flt_6108EC ramp=0.0099999998.
Multiply order matches disasm 0x40a433/45f/495/4c1.

### WalkStep — gilde.exe 0x4093b0 — VERIFIED-1:1 (advance core)
++idx, idx>=cap -> finish; TileToWorld sample; turn angle + segLength recompute;
write anim target/duration, set flags110 |=2 &=~8, flags109 &=~0x20. Duration uses
the verified WalkSegmentDuration logic. The full binary has extra repath/arrival
branches host-owned (FindNewPath/FindWaypoints/heightmap) — BOUNDARY (rule 3, mesh).

### WalkUpdate — gilde.exe 0x40a0b8 — **FIXED** (indoor tile factor)
Evidence: disasm 0x40a3e3 `v22=0.69999999`; 0x40a3fb reads **dword_62D07C**
(=0x3f800000=1.0, config-set runtime global) when `mesh+44==-1 && mesh+512`.
- **FIXED**: indoor tileFactor was a fabricated `mounted ? 1.0 : 1.2`. Binary uses
  dword_62D07C (default 1.0), NOT mounted-dependent. Replaced with kIndoorTileFactor
  =1.0 (the global's static-init value). The `1.2` had no byte backing.
- Speed-block control flow (tileAhead && !=13 && anim) + footstep + ramp += 0.01
  matches. Lazy attach / abort / no-path teardown match (host hooks, BOUNDARY).

---

## Counts
- VERIFIED-1:1: 9  (Journeyman, BuyObject, DuelResolve, DuelDispatch, ExamPrompt,
  ExamDecide, WalkRotate/OnPathRotation control-flow, WalkSegmentDuration,
  WalkAnimSpeed, WalkStep core)  [10 functions; rotation counted once]
- FIXED: 5 functions — WaitThenMoveStep (threshold + accept value), GossipBroadcast
  (bribe scale/trunc + RNG arg), DuelArmCombatant (missing RNG draw), WalkOnPathRotation
  (constant precision), WalkUpdate (indoor tile factor).
- Goldens fixed to binary: 3 (WaitAccept, WaitRefuse comment, GossipSendsRumor).
- BOUNDARY (rule 3 render/mesh or table data not in tree): recruit message emit,
  Gossip loyalty/empty-slot table + rumor render, Duel UI/voice/card, Exam fee render,
  Walk run-ramp latch (+420), Walk angle oracle, WalkStep repath/heightmap.

## HANDOFF
- src/sim/character_recon5_transport.cpp:29 compile error blocks libguild.a link
  (not my file). Needs the other agent to fix before the full suite runs.
