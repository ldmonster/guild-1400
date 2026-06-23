# Hardening sweep — world/guild chunk

Chunk files:
- `src/world/guild.cpp`
- `src/world/guild_assignment.cpp`
- `src/world/guild_rank.cpp`
Tests: `tests/unit/guild_assignment_test.cpp` (+ verified `tests/integration/guild_assignment_itest.cpp`,
`tests/e2e/guild_assignment_e2e_test.cpp`, `tests/unit/office_cluster_harden_test.cpp` still build/pass).

MCP module gilde.exe, imagebase 0x400000. Every provenance-carrying function was
decompiled AND (where Hex-Rays collapses `__usercall` regs) disassembled and diffed
line-for-line. Global tables read via get_bytes.

## Table verification (get_bytes @ the cited addresses)
- `dword_47DE24` (0x47DE24, 12B) = `00*12` -> {0,0,0}.            VERIFIED.
- `dword_47DE30` (0x47DE30, 16B) = `00*16` -> {0,0,0,0}.          VERIFIED.
- `dword_47DE40` (0x47DE40, 24B) = `FF*24` -> {-1,-1,-1,-1,-1,-1}. VERIFIED.
- `dword_47DE58` cluster (0x47DE58..): first 24B are `00`, then the byte run at
  0x47DE70 = `06 05 04 03 02 01` (followed by code `8B C0 ...`). The category-key
  table the orchestrator uses aliases this run — see AssignGuildMembers FIXED below.

## guild_rank.cpp
- `GuildGetEligibility` — gilde.exe **0x481bcc**. rank gate 0x1E..0x21; flag (+459 & 4)
  -> -1; money (+404) >= 3 -> 1; else -2.  **VERIFIED-1:1**.
- `GuildCheckRankLevel3` — gilde.exe **0x481cf0**. rank gate; switch 30..33 ->
  23/24/25/26; master gate (standing +583 >= 3, folded into the predicate); flag
  (+459 & 0x10) -> -1; money (+404) >= 8 -> 1; else -2.  **VERIFIED-1:1**.

## guild.cpp
- `GuildRankToQueryCategory` / `GuildCheckRankLevel2` — gilde.exe **0x481c18**. switch
  30->23,31->24,32->25,33->26; master gate (standing +583 >= 2); flag (+459 & 8) -> -1;
  money (+404) >= 5 -> 1; else -2.  **VERIFIED-1:1**.
- `GuildLevel2Dispatch` — gilde.exe **0x520ac8**. v1==1 -> join dialog; v1==-1 ->
  message box; else -> skill check.  **VERIFIED-1:1**.

## guild_assignment.cpp

### ComputeGuildAssignment — gilde.exe 0x47ff5c  — FIXED (1 edge-case divergence)
Verified line-for-line via decompile + disasm at 0x480104 / 0x480282 / 0x4801b7 /
0x4804c6:
- Pass-A member gate is on the SECONDARY (`v11 = secondary id != 0 && v10 = secondary
  record != null`); primary record (esi/RecordById) looked up but only id used. OK.
- Voter eligibility = `voter && [+358] eligible && ![+433] excluded`. OK.
- WIN test `bucket[0] <= bucket[1]`; deltas ap0->-20, ap1->+20, else +4. OK.
- LOSE deltas: ap0 emits extra -10, then ap1->+10 else -4. OK.
- Pass-B candidate collect: cap `min(SuccCount, 4)`; candidate id = object +4; single
  candidate installs directly; tie-break `RandomModulo(nCand)` drawn EXACTLY ONCE,
  walk `while (best != tally[j]) j=(j+1)%n`, exhaust -> bestIdx. OK.

  **FIX (0x480298 `test esi,esi; jz`)**: the WIN per-voter relation loop in the binary
  is guarded by `if (RecordById)` (= primary record non-null), and the install passes
  the primary RECORD pointer (0 when null). The reconstruction emitted relations and
  installed `targetId` unconditionally.
  - before: relation loop unguarded; `install(key, targetId, 1)`.
  - after:  `if (primary.valid) { ...relations... }` and
            `install(key, primary.valid ? targetId : 0, 1)`.
  - evidence: 0x480298 guards the loop; 0x4802ed `mov edx,esi` installs the primary
    record ptr (esi), which is 0 when FindRecordById returned null. (LOSE branch at
    0x4801cb has NO such guard and derefs unconditionally — left as-is; identical when
    primary valid, and modeling a null-deref crash is not required.)

### PersonHasOfficeObject — gilde.exe 0x480abc  — FIXED (wrong scan target + signature)
Disasm 0x480abc..0x480b35:
- state(+16)!=2 -> 0. OK.
- `edx = FindRecordById(+20 secondary)`, `eax = FindRecordById(+4 primary)`. Gate:
  `secondary && [sec+8] && ![sec+433] && primary && [pri+8] && collectCount>0`. OK.
- **FIX**: the original scans the **CollectByCategory holder buffer** (`v12[v10/4+1]`
  == each collected entry's +4 == PrimaryId) for one equal to the secondary id — it
  does NOT scan the office-object table. The reconstruction scanned `gx.objects[].id`.
  - before: `for k in gx.objects: if objects[k].id == secondary -> true`.
  - after:  signature now takes `(entry, const OfficeHolder* collected, int
            collectedCount, gx)` and scans `PrimaryId(collected[j]) == secondary`.
  - evidence: 0x480b27 `mov edx,[esp+eax+var_94]` / `cmp edx,[ecx+14h]` over a 24-byte
    stride buffer (var_94 = the CollectByCategory output), comparing entry +4 vs +20.
  - Golden `PersonHasOfficeObjectPresent` / `...ExcludedSecondary` rewritten to supply
    a collected buffer (entry +4 == secondary id) instead of an object list.

### CheckGuildMastersPresent — gilde.exe 0x48091c  — VERIFIED-1:1
Disasm 0x480a19: state-2 member gate identical (secondary +8 hasBuilding, !+433
excluded, primary +8 hasBuilding, then scan collected +4 for == secondary -> bit0);
state-3 object scan (byte_12CEA78 category match, byte_12CE912 role 6/7 & !byte_12CEAC1
busy -> bit2); trailing person scan (+2 profession 6/7 & !+433 excluded -> bit2);
returns `(flags & 2) != 0`.  Matches.

### AssignGuildMembers — gilde.exe 0x480634  — FIXED (category keys + member branch)
Disasm 0x4807d0..0x480866:
- **FIX 1 (category-key table)**: keys passed to CollectByCategory are
  `HIBYTE(*(_DWORD*)&v20[c+21])` for c=0..5 = byte at (c+24) of the aliased
  {06 05 04 03 02 01} run -> **{6,5,4,3,2,1}**. The ComputeGuildAssignment rank arg
  `*(int*)&v20[c+21] >> 24` is the SAME value (top byte < 0x80) and is unused by the
  callee (it reads only count + buffer).
  - before: `kGuildCategories = {{14,0x1E},{18,0x1F},{20,0x20},{21,0x21},{23,0x22},{34,0x23}}`.
  - after:  `kGuildCategories = {{6,6},{5,5},{4,4},{3,3},{2,2},{1,1}}`.
  - evidence: get_bytes 0x47DE58 + qmemcpy(v20,dword_47DE58,24) spill alias; the
    catKey computation at 0x480696 `HIBYTE(*(_DWORD*)&v20[v26+21])`.
- **FIX 2 (member-branch gates/install)**: the binary's else-if gates on the SECONDARY
  record and the final else installs the PRIMARY record:
    if (sec && [sec+8] && pri && [pri+8])  -> set bit0
    else if (sec && [sec+8])               -> install(key, 0, 4)
    else                                   -> install(key, PRIMARY, 1)
  - before: else-if gated on `primary`; final else installed `SecondaryId(entry)`.
  - after:  else-if gates on `secondary`; final else installs `PrimaryId(entry)`.
  - evidence: 0x4807f4 (edx=secondary) sets bit0; 0x480815 `test ecx,ecx`(=secondary)
    -> loc_480866 install state 4; loc_48081F installs var_28 = the PRIMARY record ptr,
    state 1.

### HasOccupiedOffice — gilde.exe 0x480cb4  — FIXED (budget timing + loop bound)
Decompile 0x480cb4:
- **FIX**: budget (`v0`, init 7) is decremented ONLY when a category-7 seat has
  `city == -1` (a vacant guild-master seat). When `city != -1` but FindRecordById is
  null (dead person) the loop falls to LABEL_5 WITHOUT touching the budget. The scan
  starts at v1=216, steps -6, and terminates at LABEL_5 when v1 reaches 0 — index 0 is
  never processed.
  - before: `for (i=count-1; i>=0; i-=6)`; `--budget` on EVERY category-7 seat (incl.
    dead-person and including index 0).
  - after:  `for (i=count-1; i>0; i-=6)`; live -> return true; dead-person -> continue
    (budget untouched); `city==-1` -> `if (--budget==0) return false`.
  - evidence: 0x480cde break only on city!=-1; 0x480ce2 dead-person `goto LABEL_5`;
    0x480d03 `--v0` reached only on the city==-1 fall-through; 0x480cf2
    `if (!(v1*4) || !v0) return 0`.

### AssignSlotData — gilde.exe 0x56ea50  — FIXED (missing +16 write)
Decompile/disasm 0x56ea50: occupied test `*(int*)(slot+10)>>24 != -1` (marker +13 !=
0xFF); (x,y) match at +8/+9; free-slot claim stamps `dword_63D734++` at +0; type word
at +10; marker 1 (typeWord!=0) / 0xFF (typeWord==0, release object); size +12 =
`v18 ? *(v18+66) : 0`. All matched.
- **FIX**: the non-`forceNew` branch writes `v13[4] = -1` (the +16 dword) before the
  marker branch — the reconstruction skipped it ("engine scratch").
  - after: `v13.pad14[2..5] = 0xFF` (bytes 0x10..0x13).
  - evidence: 0x56eab9 `mov dword ptr [v13+10h], -1` (decompile `v13[4] = -1`).
- BOUNDARY: `v19`/`noMatchNew` derives from FindOfficeTypeRecord's leftover edx (the
  `__usercall @<eax>` leaves edx undefined in Hex-Rays); surfaced as the `noMatchNew`
  input. `forceNew` model-load (VIBE_Plant_LoadVegetationModel /
  VIBE_Object_DetachAndRelease) is render-owned -> modeled as objectId release.

## Counts
- Functions audited: 10 (4 rank/dialog + 6 assignment).
- VERIFIED-1:1: 6  (GuildGetEligibility, GuildCheckRankLevel3, GuildCheckRankLevel2 /
  GuildRankToQueryCategory, GuildLevel2Dispatch, CheckGuildMastersPresent — plus the
  bulk of ComputeGuildAssignment).
- FIXED: 5 functions
  (ComputeGuildAssignment WIN-guard; PersonHasOfficeObject scan+signature;
  AssignGuildMembers category-keys + member branch; HasOccupiedOffice budget+bound;
  AssignSlotData +16 write).
- BOUNDARY notes: AssignSlotData edx-derived noMatchNew + render-owned model load;
  successor install passes object id (hook model) vs the binary's object base pointer.
- Tests: guild_assignment_test 77 checks / 0 fail; itest 12/0; e2e 17/0;
  office_cluster_harden_test compiles clean. All chunk sources compile.

NOTE (handoff): `src/sim/combat_battle.cpp` (another chunk) currently fails to compile
(ambiguous `EvaluateAttack` overload), which blocks the full `libguild` relink. My
chunk was verified by linking my fresh objects against the rest of the prebuilt guild
objects (combat excluded). Not my chunk; left untouched.
