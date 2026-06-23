# Wave-16 TRUE 1:1 binary diff — character + charaction cluster (W16-CHAR)

MCP was LIVE. This wave did a line-for-line diff of the reconstructed `src/sim/`
character/charaction cluster against the gilde.exe Hex-Rays decompile, verified every
numeric constant/table with `get_bytes`, fixed every divergence to match the binary, and
golden-pinned the corrections. Resolved the wave-12 NEEDS-LIVE-MCP queue.

Excluded (other owners, confirmed not touched): character_factory (W15), charaction_misc
(orchestrator already fixed FadeAlpha), charaction_npcaction_recon* (W16-AI),
character_render2-5 (render owner), charaction_steps2-8 (wave-12 hardened; not in the
wave-16 named-target list).

ConvertX heads-up applied throughout: VIBE_Coord_ConvertX @0x5c6b08 truncates toward zero
(RC=11). Every float->int site in the cluster was checked; all are either C-cast `(int)`
(== truncation, matches ConvertX) or bare `fistp` (round-nearest-even, no ConvertX). No
divergence on rounding mode was found — see per-function notes.

---

## Personally verified (W16-CHAR direct diff) — all VERIFIED-1:1

### charaction_motion.cpp — VIBE_Command_Dispatcher 0x40a4d4
The full walk-on-path monolith. Decompiled and compared line-for-line. Constants
confirmed by `get_bytes`:
- dbl_61098C (pi/6 bucket doubling) = 0x4000000000000000 = **2.0** (motion's kFac200) ✓
- dbl_610944 (missed-threshold) = 0x3FF3333333333333 = **1.2** (kMissedThresh) ✓
- dbl_61093C (indoor duration scale) = 0x4008000000000000 = **3.0** (kIndoorDurScale) ✓
The motion (monolith) and walk (split-form) constants are correctly DISTINCT — the
monolith uses 2.0 / 1.2, the Rotation form uses 1.5 / 1.4 (verified below). The rotation
arms (subtractive snap `nv<target`, additive snap `nv>target`, `+264=0` only in the
aligned else-branch), the gap-skip waypoint scan (bound `next<cap` tested before deref —
wave-12 over-read fix preserved), and the segment-duration ladder (20/40 normal, 8/24
final, ×3.0 indoor) all match. `VIBE_Object_SetWorldTranslationXYZ` passes the heading as
a reinterpreted dword (no float->int). VERIFIED-1:1.

### charaction_walk.cpp — WalkOnPathRotation 0x409b2c (split form)
Constants confirmed by `get_bytes`:
- dbl_6108CC (pi/6 bucket doubling) = 0x3FF8000000000000 = **1.5** (kFac150) ✓
- dbl_610884 (missed-threshold) = 0x3FF6666666666666 = **1.4** (kMissedThresh) ✓
- dbl_610814 (indoor duration scale) = **3.0** (kIndoorDur) ✓
VERIFIED-1:1.

### charaction_motion.cpp — VIBE_Math_AngleToTargetSigned 0x5b6d1c
The geometric core: facing/toTarget normalize (Y zeroed), dot clamp
(`dbl_6286F8`=-1.0, `SLODWORD<0x3F800000`), AcosGuarded, pi/2 rotate sign test with
`HIBYTE ^= 0x80`. Returned as a float in the low dword (no float->int). VERIFIED-1:1.

### character_recon5_morph.cpp — CheckAniMorph 0x403764 / FadeOutSlots 0x4015cc
**Critical ConvertX site, VERIFIED correct.** The morph-key blend:
`v14 = (double)v33 + flt_6102F0; VIBE_Coord_ConvertX(); v35 = (int)v14;` — the bias
`flt_6102F0` = 0x3F000000 = **0.5** (float), and `(int)` after ConvertX truncates. The
reconstruction's `b.frames = (int)v14` is the SAME truncation. The fade path:
`v15 = v12 * dbl_61000C; ConvertX(); LOBYTE(v13) = (int)v15` — value255 in [0,255],
`(int)` truncates, matches. Fade constants: dbl_610004 = 0x3F947AE147AE147B = **0.02**
(1/50), dbl_61000C = **255.0**, flt_610014 = **255.0**. All VERIFIED-1:1.

### character_move.cpp — Move2UniverseActionUpdate 0x4063c8 / TurnPickAnimation 0x408a10
Turn thresholds: dbl_6107C4 = 0x3FC999999999999A = **0.2**, dbl_6107CC =
0xBFC999999999999A = **-0.2**. Universe-move field clears (+44/+48/+52/+56) and the
invalid-combo gate match. VERIFIED-1:1.

### avatar.cpp — Avatar_LookupById 0x4859b0 / Avatar_FindOrAllocForPerson 0x4859e0
23-dword (92-byte) stride, bound 230 (==23×10 → 10 entries), id == high word of dword[0]
at byte +2. Binary uses signed `(int)dword>>16` vs sign-extended int16 id; reconstruction
uses unsigned u16==u16 — behaviorally identical (same 16-bit equality). VERIFIED-1:1.

### character_state.cpp — turn-stride predicates + work-season gate
IsActiveTypeForTurn 0x452660 decompile matches exactly (id=*(a1+4), modulo by
`(u8)byte_63CC1D` unsigned, standalone=dword_764CE0, myTurnSlot=dword_764CF4,
localTurnSlot=dword_63CC20). Work-season floats confirmed by `get_bytes`:
flt_6476FC = {8,7,8,9,20,21,20,19}, flt_64770C = {20,21,20,19,0,0,0,0}. VERIFIED-1:1.

### character_social.cpp — FindNearbyInRadius 0x40507c / UpdateIdleSocial
Proximity scan (kCharacterCapacity, group/world match, type-45 exclusion, mesh-gate,
box-tolerance, 64-byte/16-ptr cursor cap). VERIFIED-1:1.

### character_path.cpp — slot/AI/path leaves
AllocSlot 0x402254, AllocSlotAtIndex 0x4022c8, FindNearbyWide 0x406e68,
WaitSlotCallback 0x4062c0, PickWaitAnimation 0x406344, ClampTileCoord,
ResolvePathEndpoints. WaitSlotCallback (qmemcpy 0x3C0, 64-byte stride, count at +128,
`slotRec[count]=actorId`) and PickWaitAnimation (`v5[(u16)((int)rand % (u16)count)]`)
match the decompile exactly. VERIFIED-1:1.

---

## Delegated diffs (parallel sub-agents) — fixes applied

### character_recon2_cmds.{cpp,h} (13 fns) + character_recon4_flags.{cpp,h} (2 fns)
All 15 VERIFIED-1:1, no edits needed. Float consts confirmed: flt_616EA4=180.0,
flt_616EA8=1/pi, flt_5CA2B0={0,0,1}. The LookAtCharacter/LookAtObject angle conversions
route through ConvertX and use truncating `(int)` (golden pins 114, not round-to-115).
Tests: 40 cases, 138 checks, 0 failures.

### character_recon4_avatar.{cpp,h} + character_recon_tavern.{cpp,h} — FIXED (table extents)
RESOLVED the wave-12 NEEDS-MCP flag on EnsureObjectAvatar/EnsureBuildingAvatar table
bounds. The functions index by raw type with no bound, so the bound is the table size.
Real extents recovered from VIBE_Object_DestroySpawnedEntities @0x4fff10 (disasm: `cmp
ecx,2DBh` and `cmp ecx,48h`, both reset to -1):
- **object type table = 731 entries** (was sized 256) → `kObjectTypeCount=731`,
  objectTable[731+4], objectNames[731*65].
- **building type table = 72 entries** (was 256) → `kBuildingTypeCount=72`,
  buildingTable[72+3], buildingNames[72*589].
- slot count = 64 confirmed (FindFreeSlot @0x4266f4, `>=62976`, stride 984).
FindTavernTargetSlot 0x4d5c60 VERIFIED-1:1 (4 guards, 12-byte slot stride, IsObjectForTurn
gate, 16-cap). Updated stale "max type" tests; added TableExtents golden (731/72/64/65/589).
Tests: avatar 44 checks / tavern 30 checks, 0 failures.

### character_recon5_spawn.{cpp,h} + character_recon5_transport.{cpp,h} — FIXED
MoveToUniverse 0x402d3c, AttachTransport 0x402e40, UpdateTransportAttach 0x402f70,
SpawnOfficeStaffActor 0x57c744, SpawnAtBuildingEntrance 0x57c8f0 — VERIFIED-1:1
(float op-order, deflate-bit OR-construction `byte_62D010==0x01`, the quirky 2-byte-read
/1-byte-advance name-copy, field offsets +44/+48/+52/+56/+172/+93/+97). No float->int
casts in this cluster (pure int/ptr orchestration + float arithmetic stored as floats).
**FIXED PreloadSceneAnimations 0x50650c:** slice count is **16**, not 18 (PreloadAniSet
called with count=16 and 16 name pointers, stride 48) — corrected kSlices[16] and the
count arg. Added 4 goldens (head-variant copy quirk + slice-count=16).
Tests: spawn 28 checks, transport 33 checks, 0 failures.

### character_mesh.{cpp,h} + character_query.{cpp,h} — FIXED (CollectByOwner 1-based)
**FIXED CollectByOwner 0x4b99ac:** the binary indexes the result buffer with a
PRE-incremented counter (`dword_11BB69C[++v6]`), so the first match lands at **out[1]**,
not out[0]; out[0] is a reserved/null slot (confirmed via xrefs + get_bytes that
0x11BB6A0 == dword_11BB69C[1]). The reconstruction wrote from index 0 — corrected to
write `out[matches]` after pre-increment in both branches. Fixed the wrong golden in
sim_character_core_test.cpp and added `CollectByOwnerWritesAtIndexOneBase`.
Const confirmed: flt_61003C=2.0f, dbl_610044=3.0; the coincident-vector RNG is `%2 - 1`
(disasm `mov ecx,2; idiv; dec edx`, the Hex-Rays `% v13` was a decompiler artifact).
All other query/mesh fns (CountActiveUniverse, CountByOwner*, CountWithTransport,
CollectNearbyAtTile, IndexFromUniverse 0x426724, FindFreeSlot 0x4266f4, FindByPredicate,
FindByMesh, ResolveMesh 0x4013fc, RegisterFadeSlot 0x4017d4, ResetMeshThunk) VERIFIED-1:1
(no float->int truncation sites; all conversions are int->float fild). Tests:
sim_character_core 23 cases / e2e 2, 0 failures.

### charaction.{cpp,h} + charaction_brawl.{cpp,h} — FIXED (handler catalog)
BrawlStep 0x4d201c VERIFIED-1:1 (5-hit knockout gate `>=5u`, 24-tick AP cadence, negated
AP `-(+180)`, HIBYTE relation offsets +184/+185, packet gating). Pure integer/word math,
no rounding concern.
**FIXED RegisterHandlers 0x40be30:** the action-handler catalog diverged from the binary.
Corrected the (type, step, animName, argCount) tuples 1:1: added type 23
(SoundActionUpdate, argc 0); type 51 argc 1→3 (Move2Universe); type 52 argc 0→5 (UseGate);
type 56 placeholder→RotateInterpolate (argc 1); reordered to the binary's declaration
order. Added RegisterHandlersCatalogGolden pinning all 16 tuples.
DEFERRED (signature/scope): type 45/57/58 bind WalkUpdate/QueueWalk2RndDummy/
Command_Dispatcher (non-ActionStepFn sigs owned by charaction_walk/charaction_misc);
the InsertActionVararg type-45/51 validation guards read global path/scene tables out of
this module (debug-only guards, no-op on valid input). Tests: brawl 9 cases / actionqueue
boundary 15, 0 failures.

---

## Test status (normal build/, all 0 failures)
guild lib: built clean. Cluster tests re-built + run green:
recon4_avatar(17) recon_tavern(14) recon5_spawn(15) recon5_transport(11)
sim_character_core(23)/e2e(2) sim_charaction_brawl(9) actionqueue_boundary(15)
recon2_cmds(29) recon4_flags(14) recon5_morph sim_charaction_motion_e2e character_path.
All motion/walk/morph/path verification tests pass; no regressions.

## Notes
- A transient `guild` link break in src/world/history_mission.cpp (missing
  MissionCompletionOutcome::kReload/kClose) was observed by sub-agents mid-wave (a
  concurrent wave-16 agent's in-progress edit, outside this cluster). It was resolved by
  that owner; the final `guild` build is green. No W16-CHAR files touched it.
- Wave-12 hardening fixes (waypoint over-read bound, memcpy unaligned loads) all preserved
  and re-verified against the decompile.
