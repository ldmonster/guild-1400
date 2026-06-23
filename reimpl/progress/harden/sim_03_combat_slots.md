# Hardening sweep — sim combat_slots {2,3,4}

Chunk owner files:
- `src/sim/combat_slots2.cpp`
- `src/sim/combat_slots3.cpp`
- `src/sim/combat_slots4.cpp`

MCP (IDA Pro, gilde.exe @ 0x400000) used to decompile + disasm every provenance-
tagged function and confirm every FP/int constant via `get_bytes` / `get_global_value`.

## Counts
- VERIFIED-1:1: 22
- FIXED: 2
- BOUNDARY (binary-level aliasing not expressible in the chosen record abstraction): 2

Tests: combat_slots2 (+4) and combat_slots4 — 154 checks / 0 failures; combat_slots3 —
159 checks / 0 failures. All three owner `.cpp` recompile cleanly. (An unrelated
`combat_battle.cpp` ambiguous-`EvaluateAttack` build error is from a CONCURRENT agent's
uncommitted edits to a file outside this chunk — that file does not include any
combat_slots2/3/4 header; my objects build clean.)

---

## combat_slots2.cpp

### FIXED — RoundTier / AccumulateThreatStats  (gilde.exe 0x57eb64)
Disasm 0x57ebcc..0x57ec45: `GetCashAmount` returns x87 st0, `fmul ds:flt_625994`
(cash*0.1f at 80-bit), clamp via `fcomp`, then **`fstp [esp+..var_C]` where var_C is a
32-bit `float`** (v12/v14), and only then `VIBE_Coord_ConvertX(); (int)v12`.
- Before: `static_cast<int>(std::trunc(clamped))` — truncated a *double*.
- After: spill `clamped` through `float` first (`float spilled = (float)clamped;
  trunc(spilled)`), matching the binary's 32-bit float round-trip before ConvertX.
- ConvertX @0x5c6b08 confirmed RC=11 (CW high byte 0x1F) = truncate-toward-zero.
- Constants confirmed: flt_625994=0x3dcccccd(0.1f), flt_62599C=0x40e00000(7.0f),
  flt_625998=0x42c80000(100.0f). The clamp branch order + the `(double)cash * (double)0.1f`
  promotion (both sides promote the SAME float to double) match. Tier goldens unchanged
  (float round-trip doesn't move any in-range value).

### VERIFIED-1:1
- `AccumulateThreatStats` accumulation/normalization (0x57eb64) — eligibility gate,
  per-tier sums, underfull double-compare, and the `do..while(result!=8)` normalize
  (incl. the in-place avgMaxOut/maxOutSum aliasing, value-equivalent in the record model).
- `FindNearestWareObject` (0x48b4d4) — raw 3D min-distance, v3=0/v13=1e6 init, `<` compare.
- `FindNearestConquerTarget` (0x48b5d8) — relation-weighted score (enemy x10 /
  ally-by-other x15 / self x25 / unowned x1) + self-owned final reject. dbl_61B8B4=10.0,
  dbl_61B8A4=15.0, dbl_61B8AC=25.0 confirmed; branch nest matches.
- `CountFightingUnits` (0x49080c) — state != 0 && != 4.
- `ShouldPressAttack` (0x49080c) — `foe/friend > 0.5 || rng<=0x14`. dbl_61BAEC=0.5 confirmed.

## combat_slots3.cpp

### VERIFIED-1:1
- `FindObjectDef` (0x485a54), `FindActiveTarget` (0x485b1c),
  `ResetObjectHighlights` (0x485b94) — ring scan (stride-23 / <230) + class 1/2 hp gate
  + free-slot fallback, at the record-abstraction level.
- `CloseSelectionWindows` (0x4870f0), `DestroyOrderSlotWindows` (0x488828) — teardown
  order (remove window, clear=-1, destroy widget) matches.
- `GatherPlayerUnits` (0x489578) — `while (v1<32 && v4<31)` scan/gather caps; cap 31.
- `ResetSelectionState` (0x48938c) — highlight/mesh clear, drag reset, 32 windows, form.
- `FindSquadRowForTeam` / `CollectOrderSlotIds` (0x48c15c) — byte rosterCount scan;
  +192 player / +128 generic 16-entry sub-rows.
- `RunBattleFrameLoop` (0x48c5e8) — per-frame call order + the dword_631614 force-step gate.
- `AnyOrderUnitMoving` (0x48c648) — active && id && unit+296, 16-slot cap, short-circuit.
- `BeginBattleOrCacheState` (0x492e6c) — duel/pending/default branch + rec+852/+856 writes.

### BOUNDARY — damage-number table reset/destroy
- `ResetDamageNumberTable` (0x48751c) and `DestroyDamageNumbers` (0x487548).
- Disasm: the table is one interleaved global at B5F6C0, record stride **0x14 (5 dwords)**;
  fields age@+0(C0)/handle@+4(C4)/flag@+8(C8) but **widget@+0x18(D8)** — i.e. the widget
  cell of record N physically lands inside record N+1's 20-byte window. Also the reset
  loop does `add idx,0x14` BEFORE the first write and stops at `idx==0x140`, so it writes
  records **1..16 and never touches record 0**; Destroy reads `widget[idx]` at records
  0..15 while clearing fields at records 1..16 (a one-record skew).
- The reconstruction (and tests) model clean, independent logical records
  (`std::vector<DamageNumberRecord>`), which deliberately abstracts away this raw-memory
  aliasing and cannot byte-faithfully reproduce it without redesigning to a flat-dword
  model. No live caller depends on these (grep: referenced only within the module). Per
  Rule 8, left as a documented divergence rather than a partial (still-not-faithful)
  off-by-one. HANDOFF: a future flat-memory model of dword_B5F6C0[] could make these 1:1.

## combat_slots4.cpp

### FIXED — SpawnDamageNumber divisor constant  (gilde.exe 0x487300)
Original: `v6 = (double)amount / (*(float*)(a1+28) * dbl_61B1C4)`.
- `get_bytes 0x61b1c4` = `7b 14 ae 47 e1 7a 84 3f` = **exact double 0.01**
  (0x3f847ae147ae147b), NOT the float 0.01f.
- Before: `kDmgNumScale = 0.01f` (float) → `(double)(0.01f)` = 0x3f847ae140000000 ≠ 0.01.
- After: `kDmgNumScale` is now `constexpr double = 0.01` and the .cpp multiplies by it
  directly. Value path otherwise confirmed: ttl seed 0x42800000=64.0f; ConvertX truncate;
  free-slot scan (idx 0 then stride-5) maps to record indices 0 then 1.. — matches.

### VERIFIED-1:1
- `DistanceToTargetXZ` (0x4864a0) — `sqrt(dx*dx + 0.0*0.0 + dz*dz)`, float spill order.
- `TriggerEscapeAction` (0x485c0c) — `RandomModulo(10) < cowardice`; flee path (clear
  highlights, -cowardice delta, class-6/7 + objdef shout) vs stop delta (flag=1).
- `UpdateDamageNumbers` (0x48736c) — live(value!=0) scan, off-screen hide, create/reposition
  label, ttl += dbl_61B1DC, expire on `<= 0.0`. dbl_61B1DC=0xbfe0...=−0.5 confirmed
  (caller-supplied ttlStep). `FormatDamageText` kind 1/2/else sprintf chars 92/93,94/95.
- `IsNoWeaponDefType` / `EvalUnitAttackMove` (0x491324) — weaponClass 1/2 + dead-target
  −1; no-weapon-type set {0,340,342,344,366,370,374}; busy gate; attack. (NOTE: the
  original's inner "close-distance" guard is `!objdef || dist>range` reusing the SAME
  def pointer; the recon input models the inner predicate as `!hasTarget` — a caller-
  contract abstraction. The full command/heightmap/random pursuit tail of 0x491324 is
  deferred command-coupling, out of this leaf's scope.)
- `ProcessShotAndBomb` (0x4bfb68) — armed gate, word=33, shot/melee/bomb dispatch paths.
- `PursuitPressesAttack` / `PursuitRecordShouldClear` (0x48c400) — `scaled>=20 || rng<=30`;
  clear unless state 3/4 or state2+live, gated duelMode>1.
- `ResolveTargetEntityRef` (0x57ea8c) — bit0 event-pick (+bit1 owner-byte stamp) vs person
  spawn; 0xFFFF→null; bit2+ctx+objekt entrance spawn.
- `BloodPoolYOffset` (0x48c708 / 0x4a4944) — `(double)RandInt(360) * (float)piA * (double)piB`
  written through float. flt_61B930/61CD60=0x40490fdb(pi-f); dbl_61B934/61CD64=
  0x3f76c16c16c16c16(1/180). Both spawners identical.
