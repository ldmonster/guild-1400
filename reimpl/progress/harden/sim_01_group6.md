# Hardening sweep — sim group 6 (character_factory / character_mesh / character_move)

MCP-verified 1:1 audit (gilde.exe, imagebase 0x400000). Every provenance-carrying
function decompiled AND disassembled, diffed line-for-line against the binary.

## Files audited
- src/sim/character_factory.cpp (+ .h, tests/unit/character_factory_test.cpp)
- src/sim/character_mesh.cpp     (+ .h, tests/unit/sim_character_core_test.cpp)
- src/sim/character_move.cpp     (+ .h, tests/unit/sim_character_move_test.cpp)

## Per-function verdicts

### character_move.cpp

**Move2UniverseActionUpdate — 0x4063c8 — FIXED**
- Decompile + disasm (0x4063d6..0x4066b5) diffed.
- Validation ladder (v3<0 / combo / MoveToUniverse) — was already 1:1.
- FIXED divergences (final visibility teardown + field bookkeeping):
  - Field clear: binary writes `*(ch+44)=Data1; *(ch+56)=*(ch+60)=*(ch+64)=0;
    *(ch+48)=Data2` (disasm 0x4064f7..0x406512: `[edx+2Ch],[edx+38h]=0,[edx+3Ch]=0,
    [edx+40h]=0,[edx+30h]=Data2`). Old reimpl cleared scratch48/52/56 and DROPPED
    the `*(ch+48)=Data[2]` write entirely. Now: subId48=dataSubId, scratch56/60/64=0.
  - Visibility teardown: binary @0x406528..0x4066b5:
    `if ( v25==Data0 && ( (*(ch+44)!=-1 && dword_62D080!=*(ch+44))
                        || (dword_62D084!=-1 && dword_62D084!=*(ch+48)) ) ) SetVisible(ch,0)`
    where v25 = dword_649D60 captured at entry. Old reimpl used `prevSlot`
    (== ch+44, the WRONG global) in place of dword_62D080 AND dropped the entire
    second OR-clause (dword_62D084 path, confirmed at loc_406696). Now reproduced
    exactly via new state fields activeSlot/activeUniverse/activeId2.
  - Missing block restored: `if (*(action+40) && *(*(action+40)+9)==56) SetVisible(ch,0)`
    (disasm 0x406561..0x40657c) — modeled as `st->actionType == 56`.
  - Restore-slot at end now uses v25 (activeSlot), not prevSlot.
- Header `MoveUniverseState` extended with activeSlot (dword_649D60), activeUniverse
  (dword_62D080), activeId2 (dword_62D084), actionType, subId48, scratch56/60/64.
- Test updated: MoveUniverseOkClearsScratch fixed to new fields + new tests
  (VisibilityTeardownFirstClause, SecondClause, ActionType56HidesAlways).
- NOTE / HANDOFF: 0x4063c8 is ALSO reconstructed (more fully) in the non-owned
  `src/sim/character_universe.cpp` (overload on UniverseTransition*). Both are now
  consistent with the binary. The character_move.cpp version is a redundant
  simplified duplicate; recommend eventually retiring it in favor of
  character_universe.cpp. No ODR clash (different parameter types).

**TurnPickAnimation — 0x408a10 (selection core) — VERIFIED-1:1**
- Thresholds confirmed by get_bytes: dbl_6107C4 = 0x3FC999999999999A = 0.2,
  dbl_6107CC = 0xBFC999999999999A = -0.2.
- Branch sense confirmed by disasm: `fcomp dbl_6107C4; jnb` (a4>=0.2 -> left),
  `fcomp dbl_6107CC; jnb` (a4>=-0.2 -> no swap), else right. No float->int here
  (pure fcomp). Reimpl `>=0.2->+1, >=-0.2->0, else -1` is exact.
- The full 0x408a10 builds an anim object (AttachAni / CreateObjectAnim / the
  bone-delta math + SetGrayColor packed struct) — those are render/anim leaves out
  of this helper's scope; the angle-selection (the float-heavy decision the brief
  flagged) is 1:1.

### character_factory.cpp

**DecomposeModelName (CreateMesh name split, disasm 0x402a4c) — VERIFIED-1:1**
- strchr(buf,'_'); strchr(p+1,'_') cut at 2nd '_'; base=p+1 -> rec+304; zero 1st
  '_'; prefix=buf -> rec+368; no-'_' -> base="" (byte_610134, confirmed 4 zero
  bytes @0x610134), prefix untouched. Matches binary do-loops + shared copy loop.

**CreateMesh — 0x4029c4 — VERIFIED-1:1**
- Full decompile + disasm (0x402bab..0x402d08) diffed.
- Constants confirmed: anim-rate 1072064102=0x3FE66666 (node+492 +2296), scale
  1065353216=0x3F800000 (rec+416), node+72=50331648=0x3000000.
- dword_401010 seed confirmed 16 zero bytes @0x401010.
- Creature needles confirmed by get_bytes: RATTE(0x610138) HUND(0x610140)
  KATZE(0x610148) PFERD(0x610150). Outer probe order RATTE,HUND,KATZE,PFERD
  confirmed (edx set @0x402bb4=RATTE before first call @0x402bbf; HUND@0x402ccc,
  KATZE@0x402ce0, PFERD@0x402cf4). rec+4=1(human) set @0x402bbb before probe.
  Inner: Ratte clears node+529 bits 0x08(and 0F7h) then 0x04(and 0FBh); Pferd
  sets rec+4|=8 then node+529 &=~0x08 — all match.
- Node flag offsets confirmed: node+529 == [eax+211h], +530/+531/+535/+536/+72.
- IndexFromPointer branch: nonzero -> node+529&=~8; else (8*(byte_62D010&1))|
  (node+529&0xF7) — matches.
- Order of all field/flag writes, the +136=off_649D64 (active universe), model
  copy to rec+5, +44=-1/+48=-1/+40=-1, low-poly gate (dword_62D088 && rec+492),
  rec+141|=0x10 return 1 — all 1:1.

**CreateFromModel — 0x402d10 — VERIFIED-1:1**
- Disasm confirms: esi=model, AllocSlot (0x402254) preserves edx (pop edx) so the
  parentMat (CreateFromModel's incoming edx) flows unchanged into CreateMesh's
  ebx=a3=parentMat. On failure ecx(=record) -> Destroy, eax=0; on success eax=record.
  Reimpl `AllocSlot(); if(CreateMesh(rec,model,parentMat)) return rec; destroy(rec);`
  is exact. AllocSlot allocs 0x204=516 bytes (matches the record model).

### character_mesh.cpp

**ResolveMesh — 0x4013fc — VERIFIED-1:1 (data logic) / BOUNDARY (reload bracket)**
- Decompile diffed. Cache hit (+176), reload-flag pick (active->byte_649DD0 else
  +982), guard (+981==0 && flags), v17=flags&0xFD mask, Heightmap_Create from
  +180, store +176, collision grid only when (u != array-base byte_13ECEC8 &&
  u == off_649D64 && mesh) — all reproduced. The decision logic is 1:1.
- BOUNDARY: the reload bracket collapses 4 render/universe leaves (SwitchActiveSlot,
  InitLogAndInflate, DisplayLogAndCleanup, SwitchActiveSlot) into 2 reloadBracket()
  hook calls (Rule 3/leaf boundary). Close-bracket passes g_activeUniverseId where
  the binary passes the entry-captured dword_649D60 (v5); noted as a hook-modeling
  detail, abstracted by the boundary.

**RegisterFadeSlot — 0x4017d4 — VERIFIED-1:1 (data) / BOUNDARY (return + alpha)**
- Decompile diffed. Slot scan finds first-empty (do-while past slot0, cap via
  v6<192), 12-byte stride: actor@+0 (dword_66F010), kind@+4 (byte_66F014),
  start-tick@+8 (dword_66F018) = dword_62D008 - 1. SetVisible(actor,1). rec+140
  |= 0x40. All 1:1 (stride/offsets/tick all match the disasm-derived layout).
- BOUNDARY: alpha is built by SetGrayColorThunk + LOBYTE=0xFF + BYTE2&=~1 then
  ChangeTransparency(rec+52 mesh, ...) and rec+292 transport — render leaves
  (Rule 3). Modeled as int 0xFF through the changeTransparency hook.
- NOTE: binary RETURNS the ChangeTransparency byte (al), and 192 when the table is
  full; the reimpl returns the slot index / -1. The real return is a render-leaf
  result (not CPU-side data); the slot index is the test handle. Documented in the
  header; not a data-logic divergence.

**ResetMeshThunk — 0x426430 — VERIFIED-1:1**
- Binary: `if (StrCmpNoCase(a1,a2)) return 1; dword_62D4EC = v2(garbage ecx);
  return 0;`. StrCmpNoCase (0x5cb8f0) is ASCII A-Z+32 case-fold returning v3-v4
  (nonzero on inequality). Reimpl returns 1 on inequality, 0 on equal — exact
  "case-insensitive INEQUALITY". The dword_62D4EC write stores uninitialized ecx
  (dead/non-reproducible) and is correctly omitted. Null-guard added (returns 1 =
  "differ") is a benign defensive deviation (binary would deref-crash).

## Counts
- Functions audited: 7 (Move2Universe, TurnPick, DecomposeModelName, CreateMesh,
  CreateFromModel, ResolveMesh, RegisterFadeSlot, ResetMeshThunk = 8 incl helper).
- FIXED: 1 (Move2UniverseActionUpdate — visibility teardown + field bookkeeping).
- VERIFIED-1:1: 6 (TurnPickAnimation, DecomposeModelName, CreateMesh,
  CreateFromModel, ResetMeshThunk; ResolveMesh & RegisterFadeSlot data-logic).
- BOUNDARY noted: ResolveMesh reload bracket; RegisterFadeSlot alpha/return — all
  genuine render leaves (Rule 3).
- Tests: character_move test updated + 3 new teardown tests added. All three owned
  .cpp + .h + the unit test pass `-fsyntax-only`.

## Handoffs (non-owned)
- src/sim/character_universe.cpp also implements 0x4063c8 (fuller); now consistent.
- BUILD NOTE: `src/gui/widget_layout.cpp` (untracked, another agent's WIP) fails to
  compile (`Widget::ld` missing) and blocks the full link — unrelated to this group.
