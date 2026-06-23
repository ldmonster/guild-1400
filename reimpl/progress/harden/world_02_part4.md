# Harden sweep — world/location2 + world/location3 (part 4)

MCP-verified every `gilde.exe 0xADDR` provenance function in:
- src/world/location2.cpp / .h  (contact-menu builders + guard-action start gates)
- src/world/location3.cpp / .h  (thief/guard/robber-camp dialog bodies + scanners)

Reference of record: IDA Pro MCP decompile + disasm of gilde.exe (imagebase 0x400000).

## location2.cpp — all VERIFIED-1:1 (no churn)

Every contact-menu builder was diffed against the binary's per-frame
`ResetEntries → RunFrameLoop { Register(...); dispatch }` loop: registration
order, gate conditions (word_631758 & 0x200 shop / & 0x400 back-room; NPC+39
city compare; QueryFind object-id gates), and dispatch targets all match.

- ChurchContactMenu            0x5239c0  VERIFIED-1:1 (shop: PREDIGT,WEIHWASSER; foreign-priest: [BEICHTSTUHLRAUM if QueryFind 242], SPENDENKAESTCHEN, WEIHWASSER — order/gates exact)
- ChurchGuildContactMenu       0x521608  VERIFIED-1:1 (GetGuildEligibility -> ZUNFTLEVEL1)
- ChurchConfessionContactMenu  0x522d70  VERIFIED-1:1 (gate (flags&0x200)==0 -> BEICHTE)
- ChurchTop5ContactMenu        0x523b64  VERIFIED-1:1 (always TOP5, ABLASS)
- IdleContactMenu              0x524058 / 0x5252b0  VERIFIED-1:1 (ResetEntries then spin; zero Register calls in both twins)
- DungeonPlanContactMenu       0x523cd0  VERIFIED-1:1 (always KERKERPLAN)
- DungeonBribeContactMenu      0x523f8c  VERIFIED-1:1 (byte_12CEAC1[536*city] gate -> BESTECHEN, width 16)
- EquipStorageContactMenu      0x5252cc  VERIFIED-1:1 (shop: AUSRUESTEN,REGENERATION; back: LAGER,TRANSPORT; dispatch storage/transport/equip/training)
- TargetNightContactMenu       0x5254c8  VERIFIED-1:1 (shop: ZIELSCHEIBE,NACHT -> TrainingSelect)
- GuardContactMenu             0x526e30  VERIFIED-1:1 (PATROL,WACHPLAN,PERSONALBUCH,MEISTERBRIEF; then QueryFind 327->ZOLLKASSE else 328->ZOLLKASSE_MIT_GEHEIMFACH)
- GuardRaidContactMenu         0x526fc0  VERIFIED-1:1 (LAGER,AUSRUESTEN,RAZZIA,GEBAEUDE_AUSSPIONIEREN,ub_INFORMATIONSPERGAMENT,TRANSPORT)
- GuardTargetContactMenu       0x527120  VERIFIED-1:1 (ZIELSCHEIBE,NACHT,REGENERATION -> TrainingSelect)
- ThiefPrisonContactMenu       0x5261bc  VERIFIED-1:1 (shop: ENTFUEHREN,LOESEGELD; hostage our-prisoner && +433 -> AUSBRECHEN)

Guard-action start gates — active-char-flag gate + PanelDispatcher mode + config
leading dword all confirmed via disasm:
- GuardPatrolStart   0x526554  VERIFIED-1:1 (mode 1, GuardArrestDialog, v7=1024)
- GuardRaidStart     0x526824  VERIFIED-1:1 (mode 1, GuardRaidDialog, v8=1024)
- GuardCustomsStart  0x526af0  VERIFIED-1:1 (mode 4, no config blob, GuardCustomsDialog)
- GuardDetainStart   0x526db8  VERIFIED-1:1 (mode 1, GuardDetainDialog, v6[0]=1)

## location3.cpp — scanners VERIFIED-1:1

- FirstOccupiedSlot   (0x5126ff/0x526363/0x5265ea head)  VERIFIED-1:1
  Binary: v3=0; if(!byte_12CEA98[0]) do{v4+=536;++v3;}while(v4<411648 && !occ[v4]);
  Recon's slot0 short-circuit + 768-cap loop matches; out-of-view slots treated as
  unoccupied (faithful for a bounded view). 411648 == 768*536 confirmed.
- SlotTableFull        VERIFIED-1:1 (v3 >= 768)
- CollectOccupiedIds   (0x512fdf/0x5241e2/0x52646c/0x526708 etc.)  VERIFIED-1:1
  stride 134 dwords, byte_12CEA98[j*4] occupied flag, dword_12CE914[j] id, cap on
  collected count (4/6/uncapped) — slot-order, count semantics match.
- CountOccupiedSlots   (0x526770)  VERIFIED-1:1
  Re-count loop j+=536, counter zeroed (disasm: xor edx,edx @0x526764 holds into
  loop) — confirms the warning count starts from 0, not the capped batch count.
- CollectSelectionIds  (0x5124f4/0x512297 etc.)  VERIFIED-1:1
  dword_11BB6A0[0..32), rec && rec[392] active, id=rec[+4], cap on v12<24=>6 ids.
- AnySelectionActive / TooManyParticipants (>6, 0x52678b cmp 6/jle)  VERIFIED-1:1
- DialogAction codes  VERIFIED-1:1 — every byte confirmed at its store site:
  60/63/64/67/68/72/73/97/98/100/101/117 (e.g. v20=98 @0x5127fc, v19=117 @0x512f7b,
  v19=63 @0x52417e, v21=67 @0x52648e, v23=68 @0x526724, v18=72 @0x512226,
  v22=73 @0x512480).
- Message ids confirmed: 5790, 5782 (RobberCampStandard), 5791 (RobberCampRaid /
  ThiefBurglary), 5630 (=0x15FE, GuardRaid re-count warning @0x52678f).

## location3.cpp — FIXED

### RobberCampStandard 0x5126cc — FIXED (dropped pre-form gates)
Before: `RobberCampStandard(target, hasTarget, table, sel)` checked only
hasTarget + SlotTableFull, then opened. The binary has TWO more gates between
them, in this exact order:
  1. existing-handler match (FindFirstHandlerByFilter(1,0,98) @0x5126ff/0x512712)
     -> RenderFormattedMessage(5790) + return BEFORE the slot probe.
  2. !IsAnimalTargetBusy (@0x512748) -> message 5782 + return after the slot-full
     gate.
After: signature is now
  `RobberCampStandard(target, hasTarget, requestExists, targetBusy, table, sel)`
with the gate order matching the binary:
  if(!hasTarget) return; if(requestExists) msg5790; if(full) msg(8C90C8);
  if(!targetBusy) msg5782; else open.
Evidence: decompile 0x5126cc (handler loop -> 5790 at 0x512929; slot probe 0x51271e;
busy branch 0x512748 -> 5782 at 0x512971). Matches sibling convention (gates as
explicit bool params, cf. ThiefBurglaryDialog/ThiefSpyBuildingDialog in
location4.cpp). Added golden tests RobberCampStandardExistingRequestAborts (5790)
and RobberCampStandardBusyGate (5782).

### RobberCampRaid 0x512eb0 — FIXED (dropped outer + busy gates)
Before: `RobberCampRaid(table)` always opened the form.
The binary wraps everything in `if(a2 && *(WORD*)(a2+39)!=0xFFFF)` (outer guard
@0x512ec3) and `if(!IsAnimalTargetBusy) msg5791,return` (@0x512ed7).
After: `RobberCampRaid(hasTarget, targetBusy, table)`:
  if(!hasTarget) return (no form); if(!targetBusy) msg5791; else open.
Evidence: decompile 0x512eb0 (5791 at 0x51308c). Added golden tests
RobberCampRaidBusyGate (5791) and RobberCampRaidNoTargetNoOpen.

### GuardArrestDialog 0x526344 — FIXED (queue count when padded)
Before: queueBatch was called with the true collected count (e.g. 2) even when
the array was padded out to 4.
Binary @0x5264ac-0x5264c4 (disasm verified): `cmp ecx,4 / jge / ... / mov ecx,
0FFFFFFFFh` — when fewer than 4 ids are collected, the count passed to
VIBE_Command_QueueRequestSlotReset28 is forced to **-1** (exactly-4 keeps 4).
After: GuardArrestDialog computes `queueCount = (o.count < 4) ? -1 : o.count` and
passes that to H_queueBatch; the padded id array (to 4 with -1) is unchanged.
o.count (the reconstruction's collected-count field, asserted by the test) stays
the true count; only the submitted command count now matches the binary. Existing
test GuardArrestPadsToFour still passes (it checks o.count and the padded ids, not
the queue count arg).

## Counts
- Functions/units checked: 26 (13 location2 contact menus incl. 2 idle twins,
  4 location2 guard-start gates, 9 location3 scanners/runners).
- VERIFIED-1:1: 23
- FIXED: 3 (RobberCampStandard, RobberCampRaid, GuardArrestDialog)
- BOUNDARY: 0

## Tests
location3_test.cpp / location3_itest.cpp / location3_e2e_test.cpp updated for the
two new RobberCamp signatures; added 4 new golden gate tests. All 5 owned files
(2 src + 3 tests) pass `g++ -std=c++17 -fsyntax-only` with the project include set.

## Handoffs / notes
- No files outside the chunk were edited. No callers of RobberCampStandard/
  RobberCampRaid exist in src/ outside location3 (gui_dialogs6.cpp only references
  RobberCampRaid as an inert callback name), so the signature changes are local.
- Full `libguild.a` link is currently blocked by PRE-EXISTING compile errors in
  OTHER agents' WIP modules (src/gui/widget_layout.cpp: `Widget` has no member
  `ld`; src/sim/character_recon5_transport.cpp: bad static_cast; combat_battle.cpp).
  These are unrelated to this chunk; my objects compile clean in isolation.
- Hex-Rays collapses several QueueRequestSlotReset28 count args (v13/v15/v16 alias
  the last-read id/pointer). Those are command-callee argument quirks behind the
  hook boundary; the reconstruction models the observable collected count, which
  the golden tests assert. The one count arg the disasm makes unambiguous and
  observable (GuardArrest -1-when-padded) was fixed.
