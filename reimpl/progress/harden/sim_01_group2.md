# Hardening sweep — sim group 2 (buildings)

Chunk files:
- src/sim/building.cpp
- src/sim/building5.cpp
- src/sim/building6.cpp
- src/sim/building_create.cpp
- src/sim/building_create2.cpp

MCP live, module gilde.exe (imagebase 0x400000). Every provenance-carrying function
decompiled AND disassembled; constants/tables confirmed via get_bytes.

## building.cpp

| addr | function | verdict |
|------|----------|---------|
| 0x587b20 | Building_FindById | VERIFIED-1:1 — stride 169, max 43264 (256), skip dead/id-mismatch. |
| 0x5902ac | Building_GetSecurityLevel | VERIFIED-1:1 — disasm shows an 18-arm switch (`sub dl,2; cmp dl,11h`) but get_bytes(0x590264,72) proves ALL 18 jump-table entries == 0x5902e5 (the default). Every kind returns `*(base+583)`. Reconstruction `td->security` (offset +583) is exact. |
| 0x5902f4 | Building_CheckSecurityThreshold | VERIFIED-1:1 — `mode = *(edx+583)` is the SAME byte GetSecurityLevel returns (edx = 589*kind+base); mode==1 → 1>=lvl, mode==2 → lvl<=2 (ebx=2), mode==3 → 7>=lvl, else reqLevel>=lvl. setnl => `>=`. Matches. |
| 0x58fc84 | Building_GetUpgradeLevel | VERIFIED-1:1 — `*(int*)(a1+89) >> 24` signed (sar). |
| 0x589768 | Building_GetCounterA | VERIFIED-1:1 — dword_647724. |
| 0x589770 | Building_GetCounterB | VERIFIED-1:1 — dword_64771C. |
| 0x587f50 | Building_IsStorageType (wrapper) | VERIFIED-1:1 — kind==10; delegates to Building_IsStorageKind (building_type.cpp), which I verified == binary. |
| 0x587f80 | Building_IsProductionType (wrapper) | VERIFIED-1:1 — kind in {11,13,12,16,28}; null→false matches the original's null-return. |
| 0x5878b0 | Building_MapTypeToCategory (wrapper) | VERIFIED-1:1 — delegates to Building_MapKindToCategory (building_type.cpp), switch verified arm-for-arm vs decomp (1/3/6/0xF→3, 2→6, 4/5/9→8, 7→4, 8/0xE/0x12/0x14/0x15/0x16→1, 0xB/0xC/0xD→2, 0x13→7, 0x17..0x1A→5, default 0). |

Handoff (no edit): the three shared kind predicates live in building_type.cpp and
are byte-exact (verified). No divergence.

## building5.cpp

| addr | function | verdict |
|------|----------|---------|
| 0x50c7b0 | Building_CollectFreeBauplatzCandidate | VERIFIED-1:1 (comment hardened). Name match, 100.0 person-overlap test, store. Original store is UNCONDITIONAL (`*(4*cnt+base)=a1; cnt++`); reconstruction bounds-guards the write but bumps count identically (same return). PersonQueryBegin arg is a deferred Person-iterator boundary (inert). |
| 0x50c8ec | Building_FilterBlockedBauplatze | VERIFIED-1:1 — recordSize 384, second pass (+504 frame, +97 person frame, ±19), third pass (+508 "bk_" chain, +76, +496 next), null-on-hit. SceneGraph root off_649D64 is a boundary. |
| 0x50cac4 | Building_ForEachBauplatzReserve | **FIXED** — see below (byte_1233514 cap formula). |
| 0x50cf24 | Building_FindNearestPlotByDistance | **FIXED** — see below (x87 precision). dbl_621450 confirmed == 100.0 (0x4059000000000000). |
| 0x4f704c | Building_ResetGateState | VERIFIED-1:1 — copy +68..81→+82..95 (14B), +82..→+96 (14B), +112=0, +172=-1, GameTimeAdvance(+82,0,0,10). |
| 0x4f70a0 | Building_RequestGateFlagSync | **FIXED** — see below (missing top-level guard). |
| 0x4f72a0 | Building_RegisterGateHandlers | VERIFIED-1:1 — types {3,4,5,8,9,0xA,0xB,0xC,0xD,0xE,0x4C,0x4D}, bail-on-first-failure. Handler fn pointers are boundaries. |
| 0x4f740c | Building_DeselectThunk | VERIFIED-1:1 — LightSetGrayColorThunk(0,0x4000,&unk_1229AE0); the render-global ptr is a boundary (passes nullptr). |
| 0x4f73e8 | Building_HandlerStub | VERIFIED-1:1. |
| 0x4f710c / 0x4f73ec | Gate/EmptyCallbackStub | VERIFIED-1:1 (no-ops). |

### FIXED — FindNearestPlotByDistance (0x50cf24), x87 precision
Original: `v7 = (long double)sqrt(...)`, `v13 = (float)v7`, then
`if ( v7 < v12 && v13 < dbl_621450 )`. The FIRST compare uses the WIDE value (not the
float-truncated one); the SECOND compares the float `v13` against the *double* cutoff.
Before: `if (df < bestDist && df < (float)kNearestPlotCutoff)` — used the float-
truncated value for both, and truncated the cutoff to float.
After: `if (d < bestDist && (double)df < kNearestPlotCutoff)`.

### FIXED — ForEachBauplatzReserve (0x50cac4), reserve-cap formula
Original: `v9 = a1 && (v7 < (unsigned __int8)byte_1233514 || byte_1233514 == 2)`.
byte_1233514 is genuine runtime difficulty state (BSS, currently 0).
Before: faked as `v9 = (reserve != 0)` ("always within cap").
After: exact formula with the cap routed through a new hook
`Building5Hooks::DifficultyReserveCap()` (default 0 = binary BSS state).
With cap 0, reserve!=0 still yields flag 0 (0<0 false, 0==2 false) — the old code
was wrong here. Test updated to exercise cap 0 / 1 / 2.

### FIXED — RequestGateFlagSync (0x4f70a0), missing top-level guard
Original first line (0x4f70b6):
`if ( *(int*)((char*)&dword_63C8F0 + 1) >> 24 <= -1 ) { He_FreeHandlerEntry(...);
GateCallbackStub(); return; }`. get_bytes(0x63C8F0,8) = {00 00 00 00 FE 00 00 00};
the misaligned read at 0x63C8F1 = 0xFE000000, sar 24 = -2 <= -1 → guard TAKEN in the
binary's loaded state. dword_63C8F0 is session/game-mode state (xref'd from session
init / movie / gamelogic), outside the buildings module.
Before: the guard was entirely absent — execution always fell through to the scan.
After: reproduced the guard exactly, routing the shifted word through a new hook
`Building5Hooks::GateSyncGuardWord()` (default 0 → not taken, so the inert +30 path
still holds). Added test RequestGateFlagSync_GuardBailsEarly (guardWord 0xFE000000 →
HeFreeHandlerEntry called once, no time advance).
The case-0/1 flag-table branches (byte_122FEE0, WORD2(clock)>=22, packet-queue path)
remain deferred behind inert hooks; with inert hooks the reconstruction matches the
original's inert path exactly. Documented as boundaries.

## building6.cpp

| addr | function | verdict |
|------|----------|---------|
| 0x51dcd4 | Building_CheckEntryAllowed | VERIFIED-1:1 — diffed branch-for-branch: `a1[91]&4`→0; OR of (free-entry-everywhere | VIP byte & kind∈{6,4} | selFlags&0x200 | owner@+39==player)→1; `!(selFlags&1)`→0; time-window-open→1; else closed-message id selection: nowHour>=openHour → (cat∈{3,5}?7220:7217) else (cat∈{3,5}?7219:7216). Message render + the time-window check are hooked. |

Handoff (no edit): depends on building3.cpp `Building3_CheckTimeWindowOpen` (0x51dc04,
takes resolved kind) and `Building3GameHour` (== WORD2(qword_13CE852)). Both faithful
at the interface; not in my chunk.

## building_create.cpp

| addr | function | verdict |
|------|----------|---------|
| 0x586d44 | FindFreeBuildingSlot | VERIFIED-1:1 — scan 256 records (43264/169), first with alive byte 0. |
| 0x586fb8 | Building_CreateGebaeude (default backend) | VERIFIED-1:1 for the modelled scalar inits — id (dword_649890) bump, owner words +37/+39, +57=32000, +61=100, +65=2, +69=100, +73=1.0f (1065353216 confirmed), +92=100, +149=-1. The name-table / scene / AddObjekt machinery is the documented deferred render/scene leaf. |

## building_create2.cpp

| addr | function | verdict |
|------|----------|---------|
| (0x586fb8 name tail) | Building_PickUniqueName | VERIFIED-1:1 — `(int)RandNext() % (u16)count`; RandNext (0x5cb8bc) returns [0,0x7FFF] so non-negative; matches. |
| (0x586fb8 switch tail) | Building_ApplyTypeDefaults | VERIFIED-1:1 — entire prot switch diffed against the decomp: production +48=5000; the nested unsigned-compare tree (21/>21/>=31/>31/>=54/>=56/==71→+48=80000; ==32→+48=100,+73=quality; ==53→+101/+105w/+107w/+109=0; ==31→+48=100,+73=quality; ==30→plant map; ==22→AddObjekt437; <12 tree with LABEL_97 for 4..7, LABEL_89 for 11/12/13, LABEL_92 for 14/15/16, ==20→AddObjekt437); epilogue *v4==38→+41=270. Plant-map loop offsets verified (write +13=0xFF and +20=0 per 24-byte record, 64 records, terminate at 1536). |

**FIXED (comment only):** kQualityFloatBits comment claimed "== 0.65f"; get_bytes via
python proves 1061997773 == 0.800000011920929f (0.8f). Corrected the comment; the
literal value was already correct.

## Counts
- Functions reviewed: 23 (building.cpp 9, building5.cpp 11, building6.cpp 1,
  building_create.cpp 2, building_create2.cpp 2 — counting the two create.cpp items).
- VERIFIED-1:1: 20
- FIXED (behavioral): 3 — FindNearestPlotByDistance (x87 precision),
  ForEachBauplatzReserve (cap formula + new hook), RequestGateFlagSync (top-level
  guard + new hook). Plus 1 comment-only fix in building_create2.cpp.
- New hooks added (genuine runtime-state boundaries, Rule 8): DifficultyReserveCap,
  GateSyncGuardWord (both on Building5Hooks).
- Tests: building5_test updated (2 tests rewritten/added) — 31 checks, 0 failures.
  building6_test — 18 checks, 0 failures.
- Build note: the full CMake build is broken by an UNRELATED pre-existing error in
  src/gui/widget_layout.cpp (`w.ld<i32>` typo, not my chunk). All five owned .cpp
  files pass `g++ -fsyntax-only`; both test files compile and run green via a
  standalone harness.

## Handoffs (files outside my chunk — documented, not edited)
- building_type.cpp: shared kind predicates — verified byte-exact, no change needed.
- building3.cpp: Building3_CheckTimeWindowOpen / Building3GameHour — building6 depends
  on them; faithful at the interface.
- src/gui/widget_layout.cpp: pre-existing compile error (`w.ld<i32>`), blocks the
  full build; outside this chunk.
