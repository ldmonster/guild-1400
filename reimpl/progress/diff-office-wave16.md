# Wave-16 TRUE 1:1 binary diff — W16-OFFICE cluster

Cluster: `src/world/` office / election / guild / council / townhall. MCP live;
each rule core decompiled (`0x...`) and compared line-for-line against `src/`.
Resolved the wave-12 NEEDS-LIVE-MCP flag (kPromotionCost reqCode==7 OOB) and found
+ fixed a second divergence in the guild-master election winner pick.

## FIXED

### 1. office.cpp — promotion-cost matrix is a FLAT stride-7 table, not float[7][7]  (wave-12 flag, RESOLVED)

`VIBE_Office_CanPromoteRank` @0x47e0f8 (decompiled) ends with:

```c
*a3 = dword_62EBCC[7 * (dword_62EC8E[v6] >> 24) + (dword_62EC8E[v5/4] >> 24)];
//                 7 * reqCode(officeType)        + reqCode(targetRank)
```

i.e. `dword_62EBCC` is read as a **flat 1-D array, stride 7, index = 7*row+col**,
where `row = reqCode(officeType)` and `col = reqCode(targetRank)` EACH range **0..7**
(the def-table reqCode byte legitimately reaches 7 for office types 28..34). Max
index = `7*7+7 = 56` → 57 float slots.

`get_bytes @0x62EBCC` shows the labelled matrix region holds only **48 floats**
(0x62EBCC..0x62EC8B) and is **immediately followed in the binary** by 2 gap bytes
then `dword_62EC8E` (the office-def table, @0x62EC8E). So flat indices >= 48 — which
ARE reachable: `(reqOffice=7, reqTarget=4/5/6)` → flat[53/54/55], and
`(reqOffice=0..3, reqTarget=7)` → flat[7/14/21/28] — read into that **adjacent
def-table memory**. This is intentional shipped behaviour (the original's globals are
contiguous), not a bug; the exact values (some denormal/garbage reinterpreted from
def-table bytes) must be reproduced bit-for-bit.

Reachability (simulated over the guards, off/target reqCode pairs): includes
`(7,4)=flat53`, `(7,5)=flat54`, `(7,6)=flat55`, and `(0..3,7)=flat7/14/21/28`. The
prior reconstruction declared `static const float kPromotionCost[7][7]` (indices
0..6 only) and read `[row][col]` — **a genuine OOB on valid input** (UBSAN:
`office.cpp:257 index 7 out of bounds for type 'float [7]'`), and unable to
represent col==7 or row==7 at all.

FIX: replaced with the exact **57-entry `kPromotionCostBits[57]`** recovered via
`get_bytes @0x62EBCC` (stored as raw u32 bit patterns, `memcpy`-bitcast at read so
the denormal/garbage tail values are byte-exact), and read it flat:
`*outCost = PromotionCost(7 * row + col)`. The 7x7 region (rows 0..6, cols 0..6) is
unchanged from the prior values, so every previously-pinned cell is byte-identical;
the reqCode-7 cells are now correct rather than OOB.

Notable recovered reqCode-7 values:
* `(reqOffice=7, reqTarget=5)` → flat[54] = **5.0**  (officeType 28, rank 10, target 19)
* `(reqOffice=7, reqTarget=4)` → flat[53] = **1.401298e-45** (bits 0x00000001; ot 28, rank 10, target 13)
* `(reqOffice=1, reqTarget=7)` → flat[14] = **1.0**  (officeType 1, rank 3, target 28)

Pinned by `OfficeTableGolden.PromotionCostReqCode7Cells` (new) and the unchanged
`PromotionCostInRangeCells`. ASAN+UBSAN clean (was the wave-12 UBSAN trip).

### 2. election.cpp — winner pick seeds running-best with the INCUMBENT's wealth, not candidate[0]  (NEW divergence)

`VIBE_Amt_ElectGuildMaster` @0x481228, winner loop @0x4812ff (disasm-confirmed):

```
4812f8: call ComputeTotalWealth   ; the INCUMBENT (RecordById)'s wealth
4812fd: mov  ecx, eax             ; running-best (v14) = incumbent wealth   <-- SEED
4812ff: xor  esi, esi             ; winner ptr (v10) = NULL
        ... loop edx=0..4*v1:  if (cand_wealth > ecx) { ecx = cand_wealth; v10 = cand }
        ... loc_4813AB (incumbent record == NULL): xor ecx,ecx; jmp 4812ff  (seed = 0)
4813xx: if (v10 && v10 != RecordById) { AddTableEntry; notify }            ; install gate
```

So: the running-best is **seeded with the incumbent's total wealth** (or 0 when
there is no incumbent record), the winner pointer **starts NULL**, the loop runs
over **all** candidates from index 0, and a candidate becomes winner only when its
wealth is **strictly greater** than the running best. A candidate must therefore
**out-earn the incumbent** to win; if none does there is **no winner and no install**.

The prior reconstruction seeded `bestWealth = candidateWealth[0]`, started `k=1`,
and always set a winner (candidate[0] by default) — so it would pick/install the
wealthiest candidate even when the incumbent out-earns them all. DIVERGENCE.

FIX: `ElectionRunGuildMaster` now takes `incumbentWealth` / `incumbentValid`
(defaulted, so existing callers compile); seeds `bestWealth = incumbentValid ?
incumbentWealth : 0`, starts the winner ptr at `bestIdx = -1` (NULL), iterates from
index 0, and leaves `winnerIndex == -1` / `!install` when nobody beats the seed.
The three pre-existing trial tests still pass (they used `incumbentValid` defaulted
to false → seed 0 → identical to the old positive-wealth behaviour for those cases,
and the incumbent==winner case still resolves to `!install`). Pinned by new goldens
`WorldElection.{IncumbentWealthSeedNoCandidateOutEarns,
IncumbentWealthSeedChallengerOutEarns}`.

## VERIFIED-1:1 (decompiled, matches — no change)

* `OfficeCanPromoteRank` @0x47e0f8 — all guards (a1!=0, *a1!=0xFFFF, targetRank
  1..29, officeType<30, rank<30, `BookCat(rank)+1 >= BookCat(target)`,
  `BookCat(officeType) < BookCat(target)`) match line-for-line; only the cost-read
  was fixed (item 1).
* office-def table `dword_62EC8E` (446 bytes) and the category/rank decode
  (`reqCode = byte[12*rank+3]`, `bookCat = byte[12*rank+4]` via `byte_62EC92`):
  `get_bytes` confirms the baked `kOfficeDefTable` is byte-exact; the accessors'
  offsets match `byte_62EC92[12*v]` / `dword_62EC8E[v]>>24`.
* `GuildGetEligibility` @0x481bcc — rank in [0x1E,0x21], `+459 & 4` → -1,
  `+404 >= 3` → 1 else -2. Constants (kGuildL1JoinCost=3, flag 0x4) exact.
* `GuildCheckRankLevel2` @0x481c18 — switch 30→23..33→26, master query + standing
  `< 2`, `+459 & 8` → -1, `+404 >= 5` → 1 else -2. (kGuildJoinCost=5, flag 0x8.)
* `GuildCheckRankLevel3` @0x481cf0 — same switch, standing `< 3`, `+459 & 0x10` →
  -1, `+404 >= 8` → 1 else -2. (kGuildL3JoinCost=8, flag 0x10.) The per-level
  standing threshold (<2 vs <3) is correctly folded into the master predicate.
* `GuildRankToQueryCategory` @0x481c18 — 30→23, 31→24, 32→25, 33→26, else -1.
* `ElectGuildMaster` member/candidate predicates @0x481228 — member = `+39 != 0xFFFF
  && (rec[90] & 1)==0`; candidate = member with employer office-type `+356` in
  [13..18]; quorum `members >= 3 && collected >= 1`; 16-slot dedup cap. All match.
* council tally/relation cores (`CouncilTallyRemoval` / `CouncilRelationDelta`,
  extracted from the 56 KB `VIBE_Office_RunCouncilSession` @0x49dd8c): the rule
  comments cite the exact `dword_11AB094`/`v205..v207` mapping; outcome
  `removed = yes > no` and the per-vote relation deltas (-40/+10/+5 kept,
  -40/+10/-20 removed) are unchanged and consistent with the documented decompile.
  The full cutscene/.esc/.sbf shell remains command/GUI-owned (deferred, as marked).

## Deferred (unchanged, documented)

* `VIBE_Office_RunCouncilSession` @0x49dd8c full body — cutscene/voice/GUI shell
  (sim/command owned); only the deterministic tally/relation/winner rule cores are
  reconstructed here (faithful to the extracted slice).
* `OfficeCanRunForOffice` @0x47e3b8 second-holder (partner) validation — needs the
  live council code's partner holder entry (RecordById==-1 short-circuit modeled).

## Build / tests

Cluster test targets build and pass (normal `build/`): `office_table_golden_test`
(201 checks), `world_trial_test` (163, incl. the 2 new election goldens),
`office_cluster_harden_test`, `world_trial_e2e_test`, `election_candidacy_test`,
`law_apply2_test`, `court_council2_test(+e2e)`, `slice_council_test`,
`wire_election_test`, `guild_assignment_test(+e2e)`, `guildstate_recon_test`,
`world_amt2_test(+e2e)`, `world_amt_office_test(+e2e)`, `world_office_flow_test(+e2e)`,
`wire_meister_loc_test`. The promotion-cost path is ASAN+UBSAN clean.

NOT MINE (sibling clusters, transient during the run): `world_law_test`
(`WorldLawW14.{FullLawTableByteExact,AllRecordFieldDecode,Record1...Drift}`, 13
failures) belongs to the LAW cluster (`gesetz_flow.cpp` / `world_law_test.cpp`,
sibling-modified) — my office checks in that file pass. `history_mission.cpp`
(HISTORY cluster) had a transient `MissionCompletionOutcome::kReload/kClose` compile
break mid-run; it cleared on retry.

NOTE: a linter reverted office.cpp's wave-12 `TableByte`/`TableDword` bounds-checked
helpers back to direct `kOfficeDefTable[...]` indexing (intentional, left as-is).
The promotion-cost fix does not depend on them and is independently bounds-safe
(guards force officeType/targetRank < 30 → reqCode <= 7 → flat index <= 56 < 57).
