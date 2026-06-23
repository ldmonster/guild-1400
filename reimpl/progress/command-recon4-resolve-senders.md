# Command recon4 — target-resolution + entity-action senders

Cluster: the `VIBE_Command_ResolveTarget*` console/history target resolvers and
the `VIBE_Command_Send*` entity-action packet emitters. New files (namespace
`guild::sim`):

- `src/sim/command_recon4_resolve.{h,cpp}` — the 10 `ResolveTarget*` resolvers.
- `src/sim/command_recon4_senders.{h,cpp}` — `CommandRetZero` + the 5 `Send*`,
  plus (wave 2) `QueueRequestTransform64`, `EnqueueLawAction`,
  `EnqueueDuelChallenge`, the 0x4c1810 law table, and (wave-2 part 3)
  `CombatSetUnitFormationMode` (0x48980c gap closed).
- `tests/unit/command_recon4_resolve_test.cpp` — 16 tests, 28 checks.
- `tests/unit/command_recon4_senders_test.cpp` — 12 tests, 55 checks.
- `tests/unit/command_recon4_senders2_test.cpp` — 22 tests, 661 checks
  (wave 2 + wave-2 part 3).

All checks pass; combined link is ODR-clean.

## Reconstructed 1:1

Target-resolution scoring/selection logic (the in-scope core). All share the
parse prologue (StripNameTokens + ParseInt → mode m∈{0,1,2}, clamp to 1 unless
exactly one stripped token with value ≤2), the kind dispatch (kind==1 confirm via
FindRecordById + flag gate; kind==2 reject where present; kind==0 table scan),
and the back-write `*(params + 8*index + 4) = chosenEntityId`. The 768-slot
person/scene selection table (gilde.exe `word_12CE910`, 536-byte stride) and the
cross-module leaves are routed through `Recon4ResolveHooks` (inert defaults). The
SCAN bounds, profession-range constants, wealth comparison, the top-5 bubble-sort
tie pool, and the random-start + stride iteration are reconstructed exactly.

- 0x4f9238 ResolveTargetClergy — clergy rank prof79∈[0x1E,0x21]; mode 0 active /
  mode 1 IsPersonType / mode 2 person-then-carried fallback.
- 0x4f9518 ResolveTargetPersonByName — flag9==1, role prof76∉{0,15,26,21}, not in
  selection list.
- 0x4f989c ResolveTargetPersonAlt — same as ByName but flag9==0.
- 0x4f9c20 ResolveTargetPersonScoped — kind==2 rejects; role filter only.
- 0x4f9f74 ResolveTargetBestRated — requires 1 token, value≤2; linear scan, max
  ComputeTotalWealth (init −10,000,000); optional group filter.
- 0x4fa50c ResolveTargetCraftWorker — profession prof74∈[13,18].
- 0x4fa818 ResolveTargetByStatGroup — predicate by mode (valid/person/carried),
  top-5 wealth pool (keys init −10,000,000, slots −1), RandomModulo(5) pick.
- 0x4fac80 ResolveTargetByProfessionRange — prof74∈[19,69]∖({31..33}∪{40..45}∪
  {52..57}), same top-5 pool / random pick.
- 0x4faab8 ResolveTargetWoundedPerson — status kind==0, linear scan, first match.
- 0x4faf54 ResolveTargetRandomCarried — random stride∈{3,5,7,11} (dword_4F8C30) +
  random start, walk for a carried item not in the selection list; the outer
  do/while continues while the walk's final index i+1<768 (faithful).

Senders (control flow + packet field layout 1:1; queue/UI/text/person leaves via
`Recon4SenderHooks` with a recording `emit` sink for byte-for-byte test checks):

- 0x493a08 CommandRetZero — `return 0` (renamed from RetZero to avoid ODR clash
  with the byte-identical guild::sim::RetZero @0x4dc070, charaction_steps2).
- 0x5675ac SendEntityActionA — disease/contagion notify: SlotReset28(cmd 118,
  submsg 375) + BeginDeltaPacket + AppendDeltaField(4,1,entId,slotOff 37) +
  State22; carried message (tblKind 6/7) → SendEntityMessage(…1418).
- 0x567944 SendEntityActionB — two SlotReset28 (cmd 118 then 119) + delta +
  QueueRequestArgs25(entId,90,32,2,0) + carried message 3255.
- 0x567e24 SendEntityActionC — office/person; SlotReset28(cmd −126, fields 2/3/2)
  + carried message 3261.
- 0x56801c SendEntityActionD — office arm SlotReset28(cmd −126, fields 2/0/4);
  person arm emits no packet, only carried message 3259.
- 0x568278 SendMapEntityAction — resolve target; AppendDeltaField(1,1,value,
  slotOff 453), value = col%2 + col>>1.

## Wave 2 — the three formerly-OMITTED senders (now reconstructed 1:1)

All three were unblocked by dedicated disassembly passes (the Hex-Rays artifacts
that forced the original omissions were resolved at the instruction level):

- **0x4952b4 QueueRequestTransform64** — opcode-0x40 (wire size 47)
  transform-delta packet. The "uninitialized qmemcpy length v4" was ecx=0x80 set
  at 0x4952c7 and preserved across `VIBE_Building_FindSlotByProt` (0x5851fc
  push/pops ebx/ecx/esi/edi/ebp): an exact 0x80-byte copy of rec into a local.
  Deltas vs the canonical slot: i32 at +0x10/+0x14/+0x18 (two's-complement
  wraparound), f32 at +0x20/+0x24/+0x2C/+0x38. Each float delta is a
  self-contained `fld/fsub/fstp m32` triple; under any live x87 PC (24/53/64
  bit) the result is provably bit-identical to a single IEEE-754 binary32
  subtraction, so plain `float` math is exact (incl. −0/inf/NaN). Packet:
  +0=0x40, +0x10=building (dl), +0x11=prot word, +0x13/+0x17/+0x1B int deltas,
  +0x1F/+0x23/+0x27/+0x2B float deltas; +0x01..+0x0F left for EnqueuePacket's
  ring header. Field-for-field round-trip match with the already-reconstructed
  receiver `ExAdjustObjectTransform` (0x49BC50, command_apply4.h).
- **0x4c2218 EnqueueLawAction** — bard poem/story enqueue (`rec@eax` person
  slot, `aux@edx`, `lawIdx@bx`; sole caller 0x549a0f in
  VIBE_BardDialog_PerformPoem 0x5495a8). The "v11 read before assignment" was
  edx = entry->arg, preserved across `BuildingType_GroupFromCode` (all switch
  cases are `mov al,imm8; ret`); `VIBE_Coord_ConvertX` (0x5c6b08) is frndint
  with RC forced to truncate → plain `(i32)` cast; `flt_61E584` == 252.0f.
  Always emits EnqueueCmd15(−1, id, aux, byte_6477A1) +
  QueueRequestArgs25(id, 456, 0x20000, 4, 0); then type 0 → stat delta
  `trunc(min(rand%8+8, 252.0f−stat))` via BeginDeltaPacket /
  AppendRawField(1,1,delta,0x80+arg) / State22; type 1 → 768-slot person scan
  (stride 536, self-skip, group match by GroupFromCode of the +0x164 code) with
  one QueueRequestCoord27(id, slotId, rand%4+3, 0) per match; type 2 → nothing.
  Returns 40*lawIdx on the type!=0/1 exit, 0 elsewhere (caller discards eax).
- **0x4c1810 `kLawActionTable`** — the 51 × 40-byte law/poem descriptor table,
  recovered verbatim with get_bytes (`{type, pad, arg, flag, textId, name[32]}`).
  Its +4 flag column is byte-identical to the independently recovered
  `guild::world::kLawTypeVariantFlag` (law_text.cpp) — cross-validated by test.
- **0x538224 EnqueueDuelChallenge** — 276-byte (0x114) duel-group packet via
  `VIBE_Command_QueueRequest39` (0x494c30), inner command id 0xA2/162. The
  garbled `v6`/`v11` were ECX = the QueryBegin record, callee-saved across all
  intermediate calls; `v24[40]=a2` is the push-ecx prologue save (ECX is not an
  argument). Layout: +0x08=3, +0x0C=challenger id (dword_12CE914[word_63CC5C]),
  +0x14=−1, +0x18 clock+15min (real `GameTimeAdvance`), +0x26=0xA2, +0x30=2,
  +0x34=opponent id, +0x38=owner-row +4 id, +0x7C=9, +0x80..+0xFF −1 fill (two
  OVERLAPPING 16-slot ranges meeting at +0xBC), +0xC0..+0xCC ≤4 opponent-group
  ids (768-row scan, +0x16C owner-ptr column, pre-incremented cursor), +0x100 =
  *(obj+2), +0x104/+0x108=−1, +0x10C = *(rec+1). mode byte *(obj+0x28): mode==2
  runs CombatSetUnitFormationMode(pkt,2) then (pkt,1), else one call — now the
  REAL reconstructed 0x48980c (wave-2 part 3, below), no longer an inert hook.
  **PRESERVED ORIGINAL DEFECT** (rule 1): the find loop at 0x538243 calls
  `VIBE_Person_IterNext` (0x586a6c), which preserves ECX (pop ecx @0x586bce) —
  the call site never reloads the record, so the loop re-tests the STALE
  QueryBegin record forever if it matches the local player's word (debug-key-
  only path, key 0x25 in 0x4bf054). The reconstruction reproduces exactly this
  contract; `DuelIterNextLoopDefectContract` guards it (asserts IterNext fires
  once per equal pass on the unchanged record and that termination comes only
  from external state change).

## Wave-2 part 3 (2026-06-11) — 0x48980c gap closed + loop-defect re-verified

### 0x48980c VIBE_Combat_SetUnitFormationMode — reconstructed 1:1

`guild::sim::CombatSetUnitFormationMode(u8* pkt, u8 mode)` in
`command_recon4_senders.cpp`. Recovered prototype (from both caller pairs,
0x538343/0x538351 in EnqueueDuelChallenge and 0x4ed726/0x4ed72f in
VIBE_NpcAction_CityFormationMoveStep 0x4ed018):

    __usercall VIBE_Combat_SetUnitFormationMode(pkt@<eax>, mode@<dl>)

`pkt` is the 276-byte duel/formation group packet the caller is building on
its frame; the eax return is dead at every site (both callers overwrite eax on
the next instruction). The 239-instruction disasm decodes as:

- **+0x110 store** — any NONZERO mode first stores the zero-extended mode
  dword at pkt+0x110 (`xor eax,eax; mov al,dl; mov [esi+110h],eax`
  @0x4899bc..0x4899c0); mode 0 skips it (`test dl,dl; jnz` @0x489815).
- **Dispatch** — mode 1 (0x48981d) runs two passes with opcode **342**/delta
  byte **0xA8** each (0x489882+0x489948 / 0x4898ac+0x48996d); modes 2 and 3
  share one arm (`cmp dl,3; jz loc_4899E8` @0x489b69 jumps into the mode-2
  body) with opcodes **344 then 352** and delta byte **0xD2** (0x489a44+
  0x489b09 / 0x489a6e+0x489b33); any other nonzero mode returns right after
  the +0x110 store (0x489b6f).
- **Each pass** (4 instruction-identical bodies differing only in the two
  immediates): (1) scan pkt+0x80..+0xBC for the first −1 dword (16 slots,
  result −1 when full, loc_4899CB); (2) unit = `dword_6498F0[slot]` (slot −1
  reads dword_6498F0[−1] = the dword @0x6498EC — OOB, modeled by the
  `playerSlotUnit` hook; the save layout says the table has only 8 entries,
  so slots 8..15 are OOB too); (3) store *(unit+4) into the slot — **slot −1
  writes pkt+0x7C** (edi = −4), clobbering the duel packet's byte-9 field
  (preserved 1:1, test-guarded); (4) `VIBE_Combat_ResetObjectHighlights(unit)`
  0x485b94; (5) `QueueRequest17(*(unit+4), −1, 1, opcode, byte_6477A1, 0)`
  0x49465c (new `Recon4Emit::QueueRequest17` event); (6)
  `VIBE_Building_AdjustStockAndNotify(*(u16*)unit, −*(i32*)(unit+0x24))`
  0x57d5b4, st0 return discarded (`fstp st`) — its IDA prototype's `int@<ecx>`
  third arg is SPURIOUS (0x57d5b4 push/pops ECX and never reads it before
  `mov cx,...` @0x57d6e4), so the hook is `(u16, i32) -> float`; (7)
  `BeginDeltaPacket(unit, *(unit+4))` + `AppendDeltaField(1, 1, &deltaByte,
  bx = LOWORD(unit) + 0x83 − LOWORD(dword_11AA474))` — a pointer-low-word
  computation of the unit's entity-pool-relative byte offset +0x83 (low-16
  arithmetic; carries cannot reach bx) — + `QueueRequestState22()`.

**What was garbled and how disasm resolved it:** Hex-Rays emitted
"uninitialized" locals (v9/v14/v20/v22) for the 2nd and 4th slot scans and for
the shared AppendDeltaField tail because the original stages
`lea ecx,[esi+80h]` BEFORE each intermediate QueueRequestState22 call
(0x4898e9/0x489aaa) and reuses ECX AFTER it — valid only because ECX is
callee-saved in this binary's convention. It also folded the +0x110 store into
a bogus `result = a2`. The raw disasm is unambiguous and is the reference.

**Live-state leaves** routed through appended `Recon4SenderHooks` fields
(`playerSlotUnit`, `unitProtWord`, `unitStock24`, `resetObjectHighlights`,
`adjustStockAndNotify`); the queue primitives report through the recording
`emit` sink (`QueueRequest17` op added). Deterministic cores reused from
`combat_drivers.{h,cpp}` (`FindFirstFreeFormationSlot`, `kFormOpLine/A/B`) —
no ODR duplication.

**Wiring (rule 13):** `EnqueueDuelChallenge` now calls the real
`CombatSetUnitFormationMode` directly at both 0x538343/0x538351 sites (the
mode==2 → calls (2, 1) split and the 0x5383dc single-call arm); the
`combatSetUnitFormationMode` hook field is gone. The other caller
(0x4ed018 CityFormationMoveStep) is not yet reconstructed; when it lands it
calls the same function.

**Tests** (suite `CmdRecon4Senders2`, +7 tests): FormationMode1GoldenSequence
(full per-pass emission order + field golden vectors), FormationMode2And3Opcodes
(344/352 + 0xD2, +0x110 = 2 vs 3), FormationModeZeroIsPureNoOp (+0x110
untouched), FormationModeOtherOnlyStoresMode (modes 4/7/255),
FormationModeFullListWritesByte7C (slot −1 → pkt+0x7C clobber + OOB read),
FormationModeMinusOneIdRefillsSameSlot (id −1 keeps the slot free → same index
twice), FormationModeScanSkipsOccupiedSlots; plus the duel goldens updated to
the real mutation (+0x80/+0x84 ids, +0x110 mode, opcode sequences).

### Infinite-loop defect re-verified at instruction level (claim CONFIRMED)

Verdict: the documented defect is REAL, exactly as carried in the code
comments. Evidence from the raw disasm:

- Loop cursor is **ECX**: `mov ecx, eax` @0x53823d (QueryBegin result), then
  the loop head @0x538243 reads `mov dx, [ecx+27h]` (the record's typeWord)
  and compares it to the live local-player word
  (`mov eax, ds:dword_6498E4; mov ax,[eax]; and eax,0FFFFh` @0x538245..51).
- `VIBE_Person_IterNext` 0x586a6c **saves ECX in its prologue and restores it
  in its epilogue** (`push ecx` @0x586a6d ... `pop ecx` @0x586bce). It returns
  the NEW cursor **in EAX** (`mov ecx, edx` @0x586bb4; `mov eax, ecx`
  @0x586bb6) and advances only the GLOBALS dword_6498BC/dword_13CE298/
  dword_6498DC — it never writes dword_6498E4 or the record at [ecx].
- The call site @0x53825a **discards EAX**: the next instructions are
  `test ecx,ecx; jnz loc_538243` (@0x53825f/0x538261) — re-testing the
  PRESERVED, unchanged, nonnull ECX and looping back to re-compare the SAME
  `[ecx+27h]` against the SAME live global.
- Therefore: if the first QueryBegin record's word @+0x27 equals the local
  player's word, no in-loop value can ever change → **the original spins
  forever**. If it differs, the loop body never executes IterNext at all and
  exits immediately with that record. Termination depends ONLY on external
  state (the live global changing under the loop), which is what the
  reconstruction models and `DuelIterNextLoopDefectContract` asserts.

No code/comment fix was needed — the existing implementation and documentation
match the instructions exactly.

### Wave-2 named gaps (rule 8, addr + reason)

- ~~0x48980c VIBE_Combat_SetUnitFormationMode~~ — **CLOSED** (wave-2 part 3,
  above).
- **0x494c30 VIBE_Command_QueueRequest39** — StagePendingBlock(276, pkt) +
  cmd-39 wrapper; not yet reconstructed in command_codec → `queueRequest39`
  hook (inert default 0).
- **0x5851fc VIBE_Building_FindSlotByProt** — the live slot table
  (dword_13C3B50, 7952/building, 62 × 0x80 slots, key word@+0) is not owned
  state yet → `findSlotByProt` hook (inert default: static zero slot; the
  original never null-checks — all live callers guarantee a hit).
- **0x586a6c VIBE_Person_IterNext / 0x591730 VIBE_GameObject_ResolveOwnerOrParentB**
  — live person-query cursor / owner resolution → hooks, inert defaults.
- **0x48980c leaves (wave-2 part 3)** — `playerSlotUnit` (the dword_6498F0
  handler/unit table; live save-block state), `unitProtWord`/`unitStock24`
  (unit-record field reads), `resetObjectHighlights` (0x485b94: live scene
  object iteration), `adjustStockAndNotify` (0x57d5b4: the in-tree
  `Building_AdjustStockAndNotify` in building_stock.cpp takes the RESOLVED
  record, while the original resolves the word_12CE910 row from the ax word
  itself — binding needs the live table) → hooks, inert defaults; the control
  flow, packet mutation and emission order around them are 1:1.

## Rule-6 flags

None. The only external tech these touch (text rendering, RNG, person queries,
command queue) is engine-internal; no new third-party dependency is implied.

## Wiring notes

- **DONE** — the 10 `ResolveTarget*` handlers are now dispatched via the
  reconstructed `funcs_4FD5D2` table (gilde.exe **0x6343d8**) by the reconstructed
  `VIBE_History_ParseContext` (**0x4fd44c**) in `src/sim/history_parse.{h,cpp}`;
  signature `(kind@al, params@edx, name@ecx, index@ebx, out@a5)` preserved. The 5
  table slots whose targets are still unreconstructed (0 Guard 0x4f8fac, 1 Official
  0x4f90e0, 7 BestThief 0x4fa178, 8 SelectedStat 0x4fa290, 9 BuildingStat 0x4fa3bc)
  sit behind `HistoryParseHooks` (inert default: return 0). ParseContext is itself
  wired to its in-tree caller `world::HistoryParseTextSecondPass` (0x4fdcec, call
  sites 0x4fdf1f/0x4fe04e) via `MakeHistorySubstResolver`. Full table map + tests
  (20 tests / 88 checks): see `progress/history-parse-context.md`.
- Install `Recon4ResolveHooks` from the live person/scene table (`word_12CE910`
  family), `VIBE_Person_FindRecordById` (0x58bc6c), `VIBE_Person_ComputeTotalWealth`
  (0x591f7c), `VIBE_Entity_IsNotInSelectionList` (0x4f8e1c), `VIBE_Math_RandomModulo`
  (0x58b89c), `VIBE_Text_StripNameTokens` (0x4f8d98)+`VIBE_Util_ParseInt` (0x5dc070),
  `VIBE_Text_RenderFormattedMessage` (0x59f99c).
- The 5 `Send*` + `CommandRetZero` are command-action handlers; install
  `Recon4SenderHooks` from the queue primitives (EnqueuePacket 0x49388c,
  BeginDeltaPacket 0x493a94, AppendDeltaField 0x493aec, QueueRequestSlotReset28
  0x4948c8, QueueRequestState22 0x494750, QueueRequestArgs25 0x494810), the UI
  dispatchers (MapView_PanelDispatcher 0x5441d0, Amt_RunOfficeOverviewWindow
  0x5575c8), and He_SendEntityMessage (0x4c5c54). The `emit` sink is for tests; in
  production each emit maps to the real primitive call.
- **Wave-2 wiring (DONE in `src/sim/wire_recon45.cpp` InstallRealRecon45Wiring):**
  `enqueuePacket` → the shared real `CommandQueue::EnqueuePacket` (so
  QueueRequestTransform64 stages onto the live ring), `personSlotId` →
  `g_personIds[]` (dword_12CE914 column), `personSlotBuildingCode` → the +0x164
  byte of `g_persons[]` (byte_12CEA74 column), `tblRowTypeWord` →
  `g_persons[].marker` (word_12CE910 column). Inert remainders are documented
  in-place (save-block globals byte_6477A1 / word_63CC5C / *0x6498E4 /
  qword_13CE852; the dword_12CEA7C pointer column; the named gaps above).
- **Wave-2 pending parents** (callers not yet reconstructable end-to-end):
  - QueueRequestTransform64: `npc_market.h` `queueTransform64(const MarketSlot*,
    u8)` (the NpcAction_RunMarktSupervisorStep 0x4e8bdc call-site shape) can be
    pointed at `guild::sim::QueueRequestTransform64` once the MarketSlot →
    0x80-raw-record packing exists; the other parents
    (Amt_BuildGuildOfficeMenu 0x481db8, Building_RandomizeStockTransforms
    0x584680) are unreconstructed.
  - EnqueueLawAction: the live caller is VIBE_BardDialog_PerformPoem 0x5495a8
    (`gui/bard_dialog.cpp` `BardCommandSink::Recite(self, lawByte)` — call site
    0x549a0f builds rec = &word_12CE910 + 0x218*word_63CC5C; the aux/ecx source
    at 0x549a01 is not yet recovered, so binding the sink would guess an
    argument — left pending, rule 8).
  - EnqueueDuelChallenge: the sole caller is debug key 0x25 in
    VIBE_DebugKey_ToggleUpdateFlags 0x4bf054, already decoded as
    `cheat_recon.h` `DebugToggle::DuelChallenge` ("the caller routes to the
    command queue") — the router can now target
    `guild::sim::EnqueueDuelChallenge` when the debug-key dispatcher is wired
    to live state (dword_11BC274, the selected object).
