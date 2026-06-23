# Wave-16 TRUE 1:1 binary diff — world mission/event/location/tutorial cluster

MCP LIVE. Line-for-line diff of the reconstructed cluster against the gilde.exe
Hex-Rays decompile + recovered data tables (`get_bytes`). Every provenance address
was decompiled and compared; divergences were fixed to the binary and pinned with
goldens. The two wave-12 NEEDS-LIVE-MCP items are resolved below. Normal `build/`
is green; all 28 cluster test suites pass (0 failures).

Owned cluster: `src/world/` mission*.{h,cpp}, event*.{h,cpp}, location*.{h,cpp},
tutorial*.{h,cpp}, mood_religion.{h,cpp}, history_mission.{h,cpp} + their tests.

---

## Wave-12 queue — RESOLVED

### Q1 — `season = day % 4` vs `& 3` (VIBE_GameTime_GetSeasonFromDay 0x58339c)
**VERIFIED-1:1 — keep `% 4`, do NOT change to `& 3`.**
Disasm @0x58339c:
```
mov edx, eax ; mov edx,[edx] ; mov eax,[eax]
sar edx, 1Fh ; idiv ecx (ecx=4) ; mov al, dl ; retn
```
This is signed division (`cdq`/`sar` sign-extend then `idiv`), returning the **signed
remainder** in `dl`. C++ signed `%` truncates toward zero and yields the same signed
remainder, so `day % 4` is byte-identical to the binary for all inputs, including a
negative day (e.g. day=-1 -> -1). A `& 3` would differ on negative days (would give 3),
so it would be a divergence. Confirmed `event3.cpp` (SetActorAnimById) and `event4.h`
`SeasonFromDay` both use `% 4`; updated their provenance comments to record the disasm.
event5 has no season indexing. Season-table consumers (`SetActorAnimById` 0x4f01d0,
`NightWatchmanAnnounceRun` 0x4f0250) re-verified 1:1 (see Event3/4/5 below).

### Q2 — He-record misaligned access (sim/he.h alignment UB)
**VERDICT: not a real OOB — unaligned-but-in-bounds x86 read, faithful to the original.
Fix belongs to the SIM cluster (sim/he.h), NOT this cluster.**
`HeRecord` is `GUILD_PACKED` (alignment 1) and 514 bytes (`+0x202`). The `He_*` reference-
returning accessors read dwords/words at byte offsets +82/+172/+176/+358..+361 — all
**within** the 514-byte record. They are in-bounds reads at non-4-aligned addresses.
Binding a `T&` to a misaligned address is UB per the C++ standard (what UBSAN's
`-fsanitize=alignment` flags), but on 32-bit x86 these unaligned dword reads are
well-defined hardware behavior and are exactly what the original byte-addressed record
code does (`mov`/`movzx` at raw `[base+off]`). It is NOT a heap/stack out-of-bounds (no
ASAN finding). A standard-clean fix is memcpy-based accessors (as
`mission_requirement*.cpp` already use via `ReadDwordAt`/`ReadWordAt`); it must land at
the `sim/he.h` source of truth for ODR/consistency. Documented for the sim owner;
event3/4/5 file-local `Dword/Word/Time` helpers mirror that same sim pattern and are
unchanged here per ownership rules.

---

## Cross-cutting cleanups (build hygiene)
- ODR: `enum class MissionCompletionOutcome` / `MissionDecodeCompletion` — no clash;
  `history_mission.h` already `#include`s `mission_rules.h` and reuses the symbol
  (verified, no redefinition). `MissionCompletionOutcomeFor` does not exist anywhere.
- `MissionRewardInfo::voiceIndex` was split into `specialVoiceIndex` (rec+0x0C / paramB)
  and `rewardVoiceIndex` (rec+0x10 / paramC) by the reward fix below. Two stale test
  refs updated to `specialVoiceIndex` (both asserted against paramB):
  `tests/integration/world_mission_save_itest.cpp`, `tests/e2e/world_mission_save_e2e_test.cpp`.

---

## FIXED divergences (corrected to the binary, golden-pinned)

### mission.cpp — MissionRequirementAdvance (VIBE_Mission_TrackCrimeProgress 0x539054)
Return value diverged. Binary `mov eax,1` **unconditionally** once the slot is found and
its type is trackable (`{11,19,23,28,40}`); the `inc dword[eax+0x1C]` happens **only**
when `slot.type == crimeType`. The reimpl returned `false` for the trackable-but-
mismatched case. Fixed: return `true` whenever (slot occupied + trackable), increment
+0x1C only on the type match. Golden updated in `world_events_test.cpp`.

### mission_rules.cpp — MissionResolveReward (descriptor text/voice ids)
Dialogs hold the record pointer as `&byte_63CD4C[v]` (= rec+4), so:
- name text id  = `*(rec+8)+1`  = **paramA + 1** (was wrongly `value+1`)
- body text id  = `*(rec+8)+2`  = **paramA + 2** (was wrongly `value+2`)
- special voice = `*(rec+0x0C)` = **paramB** (RunSpecialDialog `_AUFTRAEGE_VERGABE_HS_%.2d`)
- reward voice  = `*(rec+0x10)` = **paramC** (RunRewardSummary `_AUFTRAEGE_ERFOLG_HS_%.2d`)
`voiceIndex` split into `specialVoiceIndex`/`rewardVoiceIndex`. Golden updated in
`world_content_test.cpp` (nameTextId=paramA+1, bodyTextId=paramA+2, special/reward voices).

### mission_requirement.cpp — MissionReqCheckCumulativeStats (0x539534)
Final gate was `(sum + count) >= threshold`; binary is `sum >= threshold`. The Hex-Rays
`v5 + v4` addend `v4` is `ecx`, which is `xor ecx,ecx`'d right after the first
CountGuildMembers(21) call and preserved (push/pop) across the rest, so it is always 0.
Fixed to `sum >= threshold`; golden `CheckCumulativeStats_Golden` updated.

### mission_requirement_event_recon.cpp — MissionReqCheckGuildMemberCount (0x5394d4)
Takes THREE register args (eax=person, edx=objective, ebx=row). Clan-size byte is
`person[13]` (`movzx esi,[ecx+0Dh]`); the hold timer is in the separate objective record.
The reimpl conflated both into one `objective` and read `objective[13]`. Fixed the leaf
signature to `(person, objective, row)`, read clan size from `person[13]`, timer on
`objective`; the dispatcher already passes `(person, objective, row)`. 3 goldens updated.

### mission_recon3_evaluate.cpp — cases 38/39 selector source (0x5398c4 dispatch)
Cases 38 (CheckStatCombo) / 39 (CheckMultiStat) pass the PERSON record (eax=ecx=person)
as the selector source, not the objective. Reimpl passed `objective`. Fixed to pass the
person record (the leaf reads only its +0 owner-key word). 49-case switch otherwise
VERIFIED-1:1 (master gate + special short-circuit signed `dh in [0,5]` confirmed `jl/jle`).

### location.cpp — ClassifyLocationKind (VIBE_Building_EnterAndDispatch 0x51defc)
**Significant divergence — the type→kind mapping was fabricated.** The real dispatcher
reads the building object's **16-bit type word** (`mov ax,[esi]`) and binary-searches:
- ThiefGuild = 84 (0x54), Church = 229/230 (0xE5/0xE6), Production = 247 (0xF7),
  Tavern = 288 (0x120). There is **no residence loop** (Residence was invented).
Old codes (6/8/14/22/2/7/19/4/16/10) are not real. Rewrote the switch to the real codes
(else -> Idle), re-cited provenance to 0x51defc, marked Residence unreachable. Goldens
updated in `location_test.cpp` (`ClassifyKnownAndUnknownCodes`) and the `ClassifyKind`
block in `world_trade_route_test.cpp`.
ContactDispatch (registration-order first-match, clicked==0 -> nothing) VERIFIED-1:1.

### townhall_location_recon.cpp — citizenship family fan-out (0x51fa37)
`byte_63CC1D` is the **max number of family grants**, not the slot count. Binary scans a
fixed 768 slots and breaks when the *emitted-grant counter* reaches `byte_63CC1D`
(`if (byte_63CC1D <= v48) break;`). Reimpl treated it as the scan length clamped to 768.
Fixed: scan [0,768), emit on status 6/7 + "other", break when emitted == cap. Renamed
`FamilyMemberCount()` -> `FamilyGrantCap()`; 2 new goldens (cap-stop, zero-cap).

### tutorial_steps.cpp — missing +8 nameByte8 table field
The builders write `name[4] = N` (+8, idx2 low byte); this was not modeled. Recovered:
MainIntro=4, MainOutro=5, Ch1={0,1,2,2,2,1,1,1,1,1,3}, Ch2={0,1,1,1,1,1,1,1,3}. Added
`u8 nameByte8` to `TutorialNodeSpec`, populated all rows, `BuildChain` writes +8. Golden
added (`TutCh12.BuildChainWritesNameByte8`).

### tutorial_chapters345.cpp — Ch4 intro node kind (0x59a608)
Ch4 intro node `kind` was 4; binary writes 3 (`*(_DWORD*)v2 = 3` @0x59a65d — same as Ch3
intro; only the Ch4 *outro* writes 4). Fixed table to 3; golden had pinned the wrong
value (4) -> corrected to 3 (golden conflicted with binary -> fixed both).

### mood_religion.cpp — ParseSetReligion second-number loop (VIBE_Cheat_ParseSetReligion 0x4fbf70)
The two numeric fields use **different** parse loops: the first stops benignly at `'_'`/NUL;
the second (0x4fc085) returns 0 on ANY non-digit (no `'_'` stop), terminating only at the
3-digit cap or NUL. The reimpl shared one parser, wrongly accepting e.g. `-EVANGELISCH_5_6_7`.
Split into `ParseFirstDigits`/`ParseSecondDigits` per loop. Golden
`ParseSetReligionSecondNumberRejectsTrailingSep` added. The wave-12 `strncmp` name-match
hardening re-confirmed byte-identical to the original `memcmp(p,name,strlen(name))` (names
have no embedded NUL within `len`).

---

## VERIFIED-1:1 (decompiled, matches; representative — full set in agent notes)

### Mission
- mission.cpp: FindBySource 0x53846c, PickAndRegisterRandom 0x538668, SlotRegister 0x53872c.
- mission_save.cpp: SlotSetSingle 0x538638, SaveSlotTable 0x53b148, LoadSlotTable 0x53b25c
  (mode byte + per-slot {+0:1,+4:4,+8:14,+24:4,+28:4,+32:1}, 128 slots, stride 36).
- mission_rules.cpp: FindDescriptorByValue 0x5387f1/0x53ac81 (stride 24, +4 byte),
  HistoryRewardSelection 0x538ec6, HistoryRewardMode 0x538f8c, DecodeCompletion 0x53ad50
  (1/2/3 -> Failure/Info/LoadSession), ResultIsLoadSession 0x53ae6a.
- mission_requirement.cpp: 0x539160, 0x539380, 0x5393ec, 0x53945c, 0x539c44 (AccumulateTimer),
  0x53963c, 0x5396dc, 0x539d14, 0x539da0.
- mission_requirement_event_recon.cpp: 0x539138, 0x5391d8, 0x539230, 0x5392f0, 0x539708,
  0x539728, 0x539778, 0x5397bc.
- mission_reward.cpp: FinishByOwner 0x539e48, RunResultDialog 0x53ae46/0x53ae6a.

### Event
- event.cpp: CountByCategory 0x538550 (a1>5 -> -1; category = byte +5), FindByCategory 0x5385b0,
  PickRandomByCategory 0x538680 (dedicated mission LCG `dword_122F49C`:
  1103515245*s+12345; pick = (HIWORD>>16 %0x7FFF)%count).
- **Event table 0x63CD48 byte-exact:** `dword_5383F0 = 48` records, stride 24, value=+4,
  category=+5. `kDefaultImage[1152]` 0-diff vs get_bytes; EventTableLoadDefault memcpy of
  1152 bytes with count 48 matches the active table (the 1536-byte capacity is backing
  slack). Category histogram over 48: 0xFF->1, 0->4, 1->8, 2->8, 3->8, 4->12, 5->7.
  Name table 0x64A7FC byte-exact. (Wave-12 "all rows category 0x17" comment was wrong —
  0x17 is at +9 = paramA low byte; comments fixed in event.cpp/event.h/world_events_test.cpp.)
- event_effects/event_fire: FireRaidComputeDuration 0x4ee804 (0.001/1.5 clamp),
  PriceStateMachine 0x4ef408, BuildingProductionTrigger 0x4f16b8, FireRaidRun 0x4ee960.
- event_bindings.cpp: LookupNameToId/IdToName 0x5f4910/0x5f494c (8-entry stride-33),
  register/remove (7 slots, 0x39C), LoadEventBindings round-trip.
- event2.cpp: ReadHelpStep/ReadAdviceId bound-checked.
- event3/4/5: season-table consumers SetActorAnimById 0x4f01d0, NightWatchmanAnnounceRun
  0x4f0250. **Season tables byte-exact:** flt_6476FC {8,7,8,9} (kSeasonAnimBase ==
  kMorningHour), flt_64770C {20,21,20,19} (kEveningHour). Hour window
  `hd < m || m+1.0 <= hd`, `season==2` Herbst branches, RandomModulo draw counts all match.
  No other event3/4/5 function references the season tables (xref-checked).

### Location
- ProductionContactMenu 0x52388c, ThiefGuildContactMenu 0x525070 (gates shop 0x200 /
  back-room 0x400 / thief rank>=2). TavernComputeCardGame 0x516b78 (0.66/-0.02/-0.10).
  ChurchComputeDonation/Indulgence/Confession 0x521674/0x521bac/0x522ae8 (indulgence clamp
  `>3200 && >=320000 -> 320000` else `<=3200 -> 3200`; constants byte-exact).
- location_recon3_dialogs: bribery/TradeTransport/TradeSearch/ResidenceMistress/Stammtisch/
  DarkCorner (dbl_621918=0.0013333333333333335, dbl_621920=0.25; good-type tables
  0x507F90/0x507F9C). townhall buy/agenda/officeInfo + law/apply + citizenship eligibility
  (wealth>=16000) + grant delay (rand%3+1, tutorial +5). tavern card slot geometry
  (x=28, +58/col, wrap every 8, sprite 29 then 70), pile/footer windows.

### Tutorial
- tutorial.cpp: AdvanceStepForms 0x5978d8, SetActiveChapter 0x597b5c, IsInactive 0x597b94,
  FreeStepChain 0x597e30. **kFormResource[4] byte-exact @0x626de8:**
  `tutorial\left_form`, `\right_form`, `\top_form`, `\bottom_form` (end sentinel >=4 -> -4).
- tutorial_steps builders 0x597f5c/0x598874/0x597e80/0x59b318 (all fields, post-nameByte8 fix).
- tutorial_chapters345 builders 0x598fc4/0x59a608/0x59aa20, runner 0x4db8ac (kAnim3B
  resolution table 800/1024/else decoded), OpenActiveCharBuilding 0x596fa4.
- tutorial_mission: ResetChapterPointer/Shutdown/panel guards/ShowReminder/slider geometry
  (x=w/2-200, y=5) / slider value (clamp + `(int)((dur-e)/dur*400)`, flt_626DE4=400.0,
  ConvertX truncate) / 4-phase arrow machine; RunRewardSummary 0x539fd8, ChooseHistory
  0x538b28, HistoryReward 0x538db4.

### Mood / religion / history
- ComputeMoodLevel 0x467a50 (`v=value*0.25; >2.0 && >=6.0 -> 6; <=2.0 -> 2; else (int)v`),
  ClampMoodDelta 0x594afc, SelectMoodColor 0x4664d8 (3x5 threshold table byte-exact),
  RandomModulo 0x58b89c, RandomFloatScaled 0x58b910 (flt_62675C). Religion-name table
  @0x4f8d3c "KATHOLISCH"/"EVANGELISCH" stride 16. history_mission RunCompletionDialog
  reuses mission_rules MissionDecodeCompletion (0x53ad50).

### Float->int rounding audit (per wave-15 ConvertX heads-up)
All float->int sites in the cluster either route through `VIBE_Coord_ConvertX` @0x5c6b08
(truncate toward zero -> modeled as plain `(int)` / `static_cast<i32>` trunc, correct) or
operate on values that are exact integers at the conversion point (season tables, mood
0.25 power-of-two), where rounding mode is moot. No nearbyint/round-to-nearest divergence
found in this cluster.

---

## Build / test status
- Normal `build/` builds clean (no errors).
- All 28 cluster test suites pass, 0 failures: mission_requirement_test (33),
  mission_recon3_evaluate_test (43), mission_requirement_event_recon_test (31),
  world_mission_save_test (954), world_mission_save_itest (23), world_mission_save_e2e_test
  (18), world_content_test (116), world_trade_route_test (126), event2_test (57),
  world_event_bindings_test (75), world_events_test (111), eventtable_recon_test (19),
  event3_test (69), event4_test (39), event5_test (37), location_test (123),
  location3_test (99), location4_test (103), world_location_test (107),
  location_recon3_dialogs_test (88), townhall_location_recon_test (68),
  tavern_cards_location_recon_test (99), tutorial_mission_test (81),
  world_tutorial_chapters345_test (132), world_tutorial_core_test (30),
  tutorial_recon3_stepvoice_test (28), mood_religion_test (68), history_mission_test (42).

## Still deferred (addr + reason)
- He-record alignment-clean accessors: deferred to the SIM cluster owner (sim/he.h source
  of truth) — alignment-only UB, faithful x86 unaligned in-bounds read, no real OOB
  (see Q2). No silent skip in this cluster.
