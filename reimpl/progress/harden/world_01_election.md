# Hardening sweep — world / election cluster

Chunk files:
- `src/world/election_candidacy.cpp` (+ `.h`)
- `src/world/election.cpp` (+ `.h`)
- `src/world/election_form.cpp` (+ `.h`)
- `src/world/guild_election.cpp` (+ `.h`)

Tests touched:
- `tests/unit/election_candidacy_test.cpp` (no change needed — vacant-seat vectors)
- `tests/unit/wire_election_test.cpp` (no change needed)
- `tests/unit/world_amt2_test.cpp` (FIXED golden — see below)
- `tests/integration/election_candidacy_itest.cpp` (FIXED golden — incumbent field)
- `tests/e2e/election_candidacy_e2e_test.cpp` (FIXED golden — incumbent field + seed)

MCP-decompiled originals (imagebase 0x400000): 0x481228, 0x480e4c, 0x481b38,
0x480d08, 0x480da8, 0x4813d0, 0x481978, 0x47ff5c, 0x57d448, 0x57b6bc, 0x483198,
0x4832d0, 0x49dbe8, 0x49dc18, 0x47e750 (AddTableEntry, signature), 0x47e870
(TransferHoldership, holder-field proof), 0x58b89c (RandomModulo).

---

## FIXED

### CollectGuildCandidates — 0x480e4c  (`election_candidacy.cpp`)
Three divergences fixed:

1. **Incumbent holder field +20 -> +4.** The original copies the type-29/-28
   holder entries and reads the incumbent id from entry **+4** (`v37`/`v39`,
   `[esp+...var_44]`/`var_2C`), not +20. Proven by `VIBE_Office_TransferHoldership`
   @0x47e870 LABEL_30: `dword_B5984C[6*slot] = personRecord+4` — i.e. the office
   apply writes the new holder id into the **+4 `.city`** dword (`dword_B5984C`
   = byte_B59848+4), while +20 (`dword_B5985C` = .secondary) gets the applicant id
   `*(a1+7)`. The election incumbent lookup reads +4.
   - before: `secMaster/secDeputy = h.secondary;` (+20)
   - after:  `secMaster/secDeputy = h.city;` (+4)

2. **Vacant winner-pick seed INT_MIN -> 0.** `loc_4810C6` is `xor ecx,ecx` (seed
   0) when no incumbent record; `0x480fa8 -> mov ecx,eax` seeds the incumbent's
   ComputeTotalWealth otherwise. The winner replaces only when STRICTLY greater
   (`0x480fcc jle`). The recon seeded `INT_MIN`, letting any candidate (even
   wealth <= 0) lead a vacant seat.
   - before: `i32 bestWealth = -2147483647 - 1;` (ignored incumbent entirely)
   - after:  `i32 bestWealth = (haveIncumbent && incumbentWealthValid) ? incumbentWealth : 0;`
   - Added optional params `incumbentWealth=0, incumbentWealthValid=false` (mirrors
     `election.cpp`'s already-correct `ElectionRunGuildMaster`). Default keeps the
     existing all-vacant unit vectors intact.

3. **Winner rank-high hardcoded 0 -> recovered.** The rank-distance gate is
   `abs((incRec+353 SAR 24) - (winRec+353 SAR 24)) < 6` (0x4810d9/0x4810dc SAR;
   0x4810df sub; cdq/xor/sub abs; cmp 6). The recon hardcoded the winner's
   rank-high to 0, collapsing the gate to `incRank < 6`. Now the collected
   candidate's `rankHigh` is stored and used; the abs is computed with signed
   int8 interpretation to match the `sar`.
   - Added `excluded` to `CanvassPerson` (the `employer != *(WORD*)dword_6498E4`
     runtime exclusion at 0x480ea1/0x480f11, previously omitted from the member gate).

### ElectGuildMasterFull / CalcZunftElection (RunElection) — 0x481228 / 0x4813d0  (`guild_election.cpp`)
**Winner-pick seed.** Both originals seed the running-best wealth (ecx) with the
incumbent's ComputeTotalWealth (`0x4812f8 -> mov ecx,eax`) — or 0 via
`loc_4813AB xor ecx,ecx` when no incumbent record — and start the winner pointer
(esi) NULL, replacing only on STRICTLY greater wealth (`0x48131c jle`). The recon
seeded `bestWealth = candWealth[0]` with `bestIdx = 0`, so it always reported a
winner and **installed even a candidate poorer than the incumbent**.
   - before: `int bestIdx = 0; i32 bestWealth = candWealth[0];`
   - after:  incumbent wealth recovered from the swept pool by personId
     (FindRecordById/ComputeTotalWealth model); `bestIdx = -1`,
     `bestWealth = incumbentRecordExists ? incumbentWealth : 0`; `if (bestIdx<0)
     return r;` -> no winner, no install. Install gate unchanged (`v9 && v9 != v8`).

Golden fixes (binary is truth):
- `world_amt2_test.cpp::NoInstallWhenWinnerIsIncumbent`: incumbent(200) is the
  wealthiest; no candidate STRICTLY out-earns the 900 seed -> winner NULL.
  `winnerId` 200 -> **-1** (install already false either way).
- `election_candidacy_itest.cpp::FarRankSwapAgainstRealTable`: incumbent set via
  `.secondary` -> **`.city`** (+4 holder field).
- `election_candidacy_e2e_test.cpp`: install hook + readbacks `.secondary` ->
  **`.city`**; term2/term3 pass the incumbent's wealth as the seed; term3
  `winnerId` 103 -> **-1** (incumbent is wealthiest, no strict out-earner).

---

## VERIFIED-1:1

- **ElectionRunGuildMaster / ElectionIsMember / ElectionIsCandidate / ElectionCommit
  — 0x481228** (`election.cpp`). Member gate `+39!=0xFFFF && (rec[90]&1)==0`;
  candidate office-type [13..18]; dedup cap-16; quorum (v2>=3 && v1>=1); seed
  (incumbent wealth / 0) + strictly-greater pick verified against disasm 0x4812db..
  0x481330 and loc_4813AB. (This sibling was already correct — no churn.)
- **IsOfficeBuildingValid / OfficeBuildingQueryCategory — 0x481b38.** Handler-match
  fans CalcZuenfte 0x1E..0x21 -> 0; switch {1->24,2->25,3->26,5->23, default 0};
  member-present -> 1.
- **NotifyOfficeComputeArg / NotifyPersonComputeArg / NotifyBroadcast — 0x480d08 /
  0x480da8.** Profession gate {5,6,7}; offset 560/525 by +9 byte; arg = msg+offset;
  office has no null guard, person guards `if(result)`. Template ids 6182/6184
  (office), 6190 (person). 768-slot (stride 536) role-6/7 broadcast.
- **ZunftCategoriesForRank / CalcZunftElection table — 0x4813d0.** HIBYTE switch
  exact: 1E{14,34,56,23,39} 1F{18,46,59,24,51} 20{20,58,62,25,63} 21{21,64,65,26,69};
  candidate band [v78>>24 .. v76>>24]; member gate (no dword_6498E4 exclusion here).
- **OfficeShouldRemove / OfficeRankBandFor — 0x481978.** Removal = !record ||
  !(rec+8) || officeType != rec+361 || heldRank<low || heldRank>high; band table
  1C:1-6 1D:40-45 1E:34-39 1F:46-51 20:58-63 21:64-69 22:13-18 — exact.
- **GuildAssignDeltaWin / GuildAssignDeltaLoseFor / GuildCountByState /
  GuildSuccessorPick — 0x47ff5c.** Win deltas {0:-20,1:20,2:4}; lose {0 emits -10
  first; 1:+10 else -4}; state count at +16; tie-break: seed tie=1/best=-1/idx=-1,
  strict-max clears tie, on tie one `RandomModulo(n)` draw then forward-walk to a
  tally==best with bestIdx fallback. RNG draw count/order matches `(u16)(RandNext()
  % n)` (RandomModulo @0x58b89c = `(int)RandNext() % a1`, one draw).
- **RunProductionPass — 0x57d448.** Person sweep prof==18 -> set state 8 (768
  persons, stride 268w); building sweep prof<10 && productionFlag -> tick; on the
  daily-output boundary (`flt_625984 == 0xBF800000 == -1.0f`, confirmed) with a
  worker slot -> hire ("magd_FRAU"/"stallbursche_MANN"); tail goods pass. Float
  boundary modeled as `atBoundary`; 64-batch RefreshGuildState/Sleep is a
  non-deterministic side-effect (not modeled).
- **ProcessAllOfficeWages — 0x57b6bc.** 768-seat loop, ComputeOfficeWages(seat,1,..)
  per seat (kOfficeWageSeats=768).
- **SaveAemter / LoadAemter — 0x483198 / 0x4832d0.** Count dword (37) then 37
  records of {+0 byte, +4 dword, +8 byte, +12 dword, +16 byte, +20 dword} = 15 B;
  stream 4+37*15 = 559. Load checks count==37. Field offsets exact.
- **VotePanel::Mark / VotePanel::Init / VoteMarkerX / VotePanelMarkerY — 0x49dbe8 /
  0x49dc18.** AddVoteMarker x=68-10*n, y=140, icon 1162. BuildVotePanel init resets
  the 3 column counters + headers 3861/3862/3863; columns x={32,62,47}, y=10*n+80,
  icon 1162, codes 0/1/2.

---

## BOUNDARY (deferred / out of tree)

- **dword_6498E4 runtime exclusion** (0x480ea1/0x480f11 in CollectGuildCandidates;
  also dword_6498E4+4 in CalcZuenfte's spawn branch). A live global pointer
  (statically 0). Modeled as the `CanvassPerson.excluded` flag; the actual
  excluded-id resolution is sim/runtime owned.
- **ElectionFormStep / ElectionFormRun** (`election_form.cpp`). A reconstruction FSM
  harness sequencing GUI/cutscene leaves; not a single binary function. The leaf
  bodies it wraps are deferred GUI window-builds: BuildElectionForm 0x4a0610,
  BuildTortureChoiceForm 0x4a3dc8, BuildSuccessorDialogA/B 0x4a003c/0x4a01a4
  (VIBE_Object_AddToWindow / RenderFormattedMessage / BuildSpeechPacket /
  SendEntityMessage — GUI/transport leaves, rules 3-5 boundary). The tally core it
  calls, CouncilElectWinner, lives in `world/council.*` (outside this chunk).
- **Per-function side effects** of the election functions (VIBE_Person_QueryBegin/
  IterNext walk, ComputeTotalWealth, AddTableEntry/RequestBuildOp69 command commit,
  NotifyPerson/OfficeMessage entity transport, the CalcZuenfte building-spawn
  branch at 0x4815ac with RandomModulo/Transform/Sprintf/QueueRequestSlotReset28)
  are sim/io/command/render owned and routed through pools + hooks by design; the
  deterministic decision rules are what is recovered and verified here.

---

## Counts
- Functions/units examined with provenance: 21
- VERIFIED-1:1: 16 (election core + notify + zunft table + office update + assign
  deltas/tie-break + production + wages + save/load + vote panel/marker)
- FIXED: 2 functions (CollectGuildCandidates 0x480e4c — 3 divergences;
  RunElection/ElectGuildMasterFull/CalcZunftElection 0x481228/0x4813d0 — 1
  divergence) + 4 goldens (world_amt2, itest, e2e x2)
- BOUNDARY: ElectionForm FSM harness + deferred GUI leaves + runtime-global
  exclusion; engine/command/transport side effects (by design).

All four chunk source files compile (`-fsyntax-only` clean); all touched test
files compile. Full library build is currently blocked by an unrelated in-progress
error in `src/gui/widget_layout.cpp` (outside this chunk).
