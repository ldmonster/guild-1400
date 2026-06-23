# Harden sweep — sim group 5 (character_ai.cpp + character.cpp)

MCP-verified each provenance-carrying function against decompile + disasm of gilde.exe
(imagebase 0x400000). Float->int, fixed-point, RNG, struct offsets, side-effect order,
return-incl-edx, and exact switch arms checked. DISASM used as reference of record where
Hex-Rays collapsed __usercall register args.

## character_ai.cpp

### FindNearestTarget — 0x4526d8 — VERIFIED-1:1
- Candidate gate (0x452847) `(p!=self && type==6) || type==7 || (class∈{2,3,4,5} &&
  (hostA||hostB))` matches the source's if/else-if chain (operator-precedence equivalent).
- Favourability weight (0x45277e): `(flt_619140 - rankDelta*flt_619138) * flt_61913C`.
  Confirmed bytes: flt_619140=0x41200000 (10.0), flt_619138=0x3f000000 (0.5),
  flt_61913C=0x3dcccccd (0.1f), flt_619144=0x42080000 (34.0). Original stores weight in
  a `float` (v20) then promotes to double — source `NearestTargetRankWeight` returns float
  and is promoted identically. Match.
- **ConvertX truncation site (0x4528e7..0x4528f0)**: `fld v22; call ConvertX; fistp v23`.
  Verified ConvertX@0x5c6b08 sets x87 RC=truncate (HIBYTE ctrl=0x1F) and `frndint`s toward
  zero; the subsequent `fistp` stores the already-integral value exactly. Source
  `(int)ConvertX(bestScore)` matches (trunc-toward-zero, util/coord.cpp confirmed).
- Walk-step clamp (0x452917..0x452947): `|dist|/3+1` clamped to [2*dbg+15, 2*dbg+35] as a
  byte; idiv/sar verified; dword_63C744 (debugSpeed) = 0. Match.
- Combined affinity gate (0x45289b): `affinityPlaneHi/2 + rankPlaneHi >= -(u16(rand(0x20))
  +50)` — signed sar, signed /2, u16 cast. Match.
- All delta-field offsets (0x211/0x212/0x20C/0x210/0x214, raw 529, field 532) and the
  curTarget!=-1 disengage/engage branches match line-for-line, incl. edx (id) plumbing.

### UpdateGuardBehavior — 0x452d38 — FIXED
- **FIX 1 (dropped branch, re-dispatch table flag):** source only executed
  `aiMethodExecuteSelected` when `v12 != v9`; the binary (0x452efa→0x452f22) ALSO executes
  it when v12==v9 **and** `byte_B572A1[4*(5*v12 + 32*v9)] & 2`. Restored the OR-branch via
  a new inert hook `aiMethodTableByte(v9,v12)` (default 0). Evidence: disasm 0x452f22
  `shl eax,3; add eax,edx; shl eax,2; add eax,edx; test byte_B572A1[eax*4],2; jnz`.
- **FIX 2 (signedness):** prev-method `v9` is `movsx eax, bl` (0x452ef2) — sign-extended.
  Source read it as `u8` and compared unsigned. Changed to
  `int v9 = (signed char)AU8(actor,306)`. `v12 = actor[303] >> 24` via `sar edx,18h` (signed)
  — already correct.
- Roster spawn loop (0x452d5f..0x452e75, 256 buildings * 169B records, sprintf of guard
  name) stays a BOUNDARY: gated on building globals dword_13CE298/dword_13CE294/
  qword_13CE852/unk_7D43B0 + byte_619148 fmt — data not in this slice; routed through the
  inert queueRequestGuardTarget61 hook (empty roster). Documented.

### UpdateLowPolyMesh — 0x40244c — VERIFIED-1:1 (one precision note)
- Control flow (full vs proxy, +141&2 forced-low, +136==byte_13ECEC8 LOD-anchor, cell
  vis) matches all arms. Offsets 76/80/84 (pos), 533 (suspend byte), 140 bit 0x20, 141
  bit 2, 44/48 cells, 460/132/136/140 verified.
- Distance test is pure x87 (fld/fmul/faddp/fsqrt; fcomp flt_6100FC) — flt_6100FC bytes
  0x44bb8000 = 1500.0 confirmed. Source computes in `double` not x87-80bit; the operation
  sequence + `<` compare (disasm `jnb`→near-when-below) are identical; only the extended-
  precision of intermediates differs (immaterial for a 1500-unit LOD threshold). Tolerances
  2.0 / 0.050000001 match. Walk-anim sub-mesh attach (0x4025fd..) is the W19-OBJANIM render
  boundary (documented handoff, unchanged).

### LoadObjectAnimation — 0x426488 — FIXED
- **FIX (oam path prefix):** binary always formats `sprintf(path,"%s%s.oam",dword_1406110,
  a2)` (0x4265d3) — the prefix global is the first "%s". Source branched on
  `oamPathPrefixIsSet` and passed a hardcoded `""`, discarding any real prefix. Added a
  `const char* oamPathPrefix` hook (dword_1406110, default "") and now format with it
  directly. Behaviour identical when prefix is empty; now faithful when host installs one.
- Descriptor v17 flag-bit order (BYTE1: 0x10/~0x10/2/8/4; BYTE2:2), FindFreeMeshSlot/
  LoadStreamToStock gate, AttachToBone(meshRoot+244), the +45 result flag bits all match.
- StripPathAndExt(v16) result is dead in the original (never re-read) — safely omitted.

### SetVisible — 0x401894 — VERIFIED-1:1
- `if (actor && actor[52])` mesh gate; RestoreObjectStates; +100 suspend; BuildObjectCache
  on show; +292 sub-mesh RestoreObjectStates + cache; +492 low toggle; +140 bit 0x20
  clear-on-show/set-on-hide; UpdateLowPolyMesh; else ErrorLog. All offsets + order match.

### SetAllFreezeState — 0x40238c — FIXED
- **FIX (per-actor SwitchActiveSlot args):** source passed `(u, 0, 0, arg)`; binary passes
  `(eax=index, edx=1, ecx=actorPtr, edi=arg)`. Verified the call ABI (eax/edx/ecx/edi) from
  SwitchActiveSlot@0x5b4a24 (`ebp=eax; [var_8]=dl; push ecx; push edi`; arg1 is read at
  0x5b4a46) and from the return-path call. IndexFromPointer@0x426724 pushes/pops edx+ecx
  (preserves both), so edx=1 (set for its own call at 0x4023e3) and ecx=actorPtr (0x4023db
  `mov ecx,edx`) fall through. Changed to `universeSwitchActiveSlot(u, 1, actor, arg)`.
- Loop over 512 slots, a1==2 drop-proxy (DrawSubMeshes/DetachAndRelease/clear +492) vs
  a1<=1 refresh, dword_62D088 = 2-state, return SwitchActiveSlot(anchor,1,2-state,arg) all
  match. (anchor = dword_649D60.)

### StandUp — 0x405504 — VERIFIED-1:1
- null→returns input; no state(+296)→0; latch +400=1; do/while unlink while +40; +140&0x10
  → CreateSampleLoopAction; return 1. Exact.

## character.cpp

### CharacterUpdate — 0x405148 — VERIFIED-1:1 (skeleton, documented deferrals)
- Count gate (dword_62D094==0 → 0), 0..511 walk, null/+140&4 skip, +296 hasAction →
  DispatchCurrent else idle-social (character_social.cpp), return 1. Matches. CheckAniMorph,
  +141&0x20 sit-teardown, transport(+73), low-poly refresh, idle-anim attach, collision-grid
  rebuild, FadeOutSlots, frame brackets are render/anim/pathfinder leaves (deferred, as
  documented in the header).

### TurnStepActionUpdate (type 7) — 0x406a18 — BOUNDARY (modeled skeleton)
### TurnToTargetActionUpdate (type 53) — 0x40618c — BOUNDARY (modeled skeleton)
### TakeObjectActionUpdate (type 49) — 0x405c88 — BOUNDARY (modeled skeleton)
### DropObjectActionUpdate (type 50) — 0x405f28 — BOUNDARY (modeled skeleton)
### LoadAnimActionUpdate (type 54) — 0x406250 — BOUNDARY (modeled skeleton)
- These five per-action handlers are explicitly documented (character.h) as coroutine
  *skeletons*: their real bodies are render/anim/transform-math orchestration
  (AttachAni/AttachMovementAni/CreateObjectAnim/AttachItemToBone/CheckQueueReady/
  CheckAniMorph, RotateVectorByHierarchy/VectorAngleBetween/AngleToTargetSigned/
  SnapVectorToAxis/PointThroughBoneChain, FreeObjAnimData/PruneExpiredAttachments). They
  route through the separate CharActionHooks (owned by charaction.h — NOT this chunk) with
  mock hooks the unit test installs. Per Rule 3/8 the anim/transform leaves are genuine
  boundaries; the skeleton's coroutine state-machine (first-call attach → poll → unlink) is
  the reconstructed/testable surface and matches the originals' gate shape (`+12` first-call
  flag, `+240` arm flag, completion → UnlinkEntry). Verified the ConvertX truncation site in
  TurnToTarget (0x406225: `v4 = angle*flt_610560*flt_610564; ConvertX(); InsertAction(...,(int)v4)`)
  exists and is NOT modeled by the skeleton (it passes the pre-quantized arg). Full 1:1
  reconstruction is out of scope for the hardening sweep (needs the transform-math + anim
  subsystems and would break the established hook contract + passing tests). Flagged here.

## Counts
- VERIFIED-1:1: 5  (FindNearestTarget, UpdateLowPolyMesh, SetVisible, StandUp, CharacterUpdate)
- FIXED: 3         (UpdateGuardBehavior [2 fixes], LoadObjectAnimation, SetAllFreezeState)
- BOUNDARY: 5      (Turn/TurnToTarget/Take/Drop/LoadAnim action handlers — modeled skeletons)

## Files touched (owned)
- src/sim/character_ai.cpp  (UpdateGuardBehavior dispatch+signedness, SetAllFreezeState
  args, LoadObjectAnimation oam prefix; +2 inert hook defaults)
- src/sim/character_ai.h    (added hooks: aiMethodTableByte, oamPathPrefix)
No non-owned files changed. character.cpp/.h unchanged (verdicts only). Both owned files
compile clean (-fsyntax-only); character_ai_test.cpp + sim_character_test.cpp still compile
against the new (inherited-default) hook fields. NOTE: the full lib build currently fails on
an UNRELATED untracked file src/gui/widget_layout.cpp (another agent's WIP, `w.ld<i32>`),
not caused by this chunk.
