# Wave-16 TRUE 1:1 binary diff — cutscene + building-economy + animals (W16-CUT)

MCP live. Decompiled every owned function against the reconstruction and fixed
divergences. Owned cluster: `cutscene*.{h,cpp}` (cutscene, cutscene_auction,
cutscene_duel, cutscene_misc, cutscene_process, cutscene_wedding — NOT misc5/2/3/4),
`building_create/lifecycle/stock/storage/upgrade/value.{h,cpp}` (+ the
`building_types.h` struct), `console_recon`, `cheat_recon`, `animal*`.

---

## HEADLINE — building production-worth factor (the wave-12 NEEDS-LIVE-MCP flag) — FIXED

### `VIBE_BuildingValue_ComputeProductionWorth` @0x58fe68 + `VIBE_Building_ComputeItemBaseValue` @0x58f328

Decompiled both. The wave-12 question ("the 0..5 output-slot loop vs the 2-wide
factor table — is the loop count wrong, or the array width?") is **resolved: the
factor array is 6 wide, not 2.** The loop count was right; the struct was wrong.

`ComputeItemBaseValue(a1, a2, a3, a4)`:
```
v5 = 589*a2 + dword_13CE294;                 // type-def base, stride 589
if (a4 != -1) v6 = byte[a4 + v5 + 553];      // a4 path -> the +553 array
else if (a3 != -1) v6 = byte[a3 + v5 + 563]; // a3 path -> the +563 array
v9 = (float)(896 * v6);
if (kind==6 || kind==7) v9 *= (1 - (2-priceMode)*0.25);   // sale discount
```
`ComputeProductionWorth` dispatch (per matching worker):
- **output loop `v10 = 0..5`** (gate `v45[547+v10]` == worker prof): calls
  `ComputeItemBaseValue(td, kind, -1, v10)` → `a4 = v10` → **the +553 array, indexed
  0..5** → that array is **6 wide**.
- **input loop `v14 = 0..1`** (gate `v46[559+v14]`): calls
  `ComputeItemBaseValue(td, kind, v14, -1)` → `a3 = v14` → **the +563 array, 2 wide**.
- fallback (no slot matched): `ComputeItemBaseValue(td, kind, 0, -1)` → `a3 = 0` → +563[0].

Confirmed against `ComputeProductionRate` @0x58f268 too: it sweeps **+563 for i in
0..1** (the 2-wide INPUT array). Constants: `dbl_6269CC = 0.01` (quality scale),
`dbl_6268FC = 0.25` (sale discount). All accumulation is via `(int)` after
`VIBE_Coord_ConvertX` (trunc toward zero) and is **unconditional** (no `>0` gate).

**Recovered struct layout (from the loop bounds — unambiguous):**
| offset | field | width | reader |
|--------|-------|-------|--------|
| +547 | output-product profession | **6** | worth output loop `v45[547+o]` |
| +553 | output/production factor | **6** | worth output loop, a4 path |
| +559 | input-product profession | 2 | worth input loop `v46[559+i]` |
| +563 | input-good factor | 2 | worth input loop a3 path / ComputeProductionRate |

(+547+6=553, +553+6=559, +559+2=561→pad→+563, +563+2=565. All prior named offsets
547/553/559/563/583/584/585 preserved; `sizeof == 589` still holds.)

### FIXES applied
1. `src/sim/building_types.h` — `outputProf` u8→`u8[6]`; `inputFactor[2]`→`[6]`;
   `inputProf` u8→`u8[2]`; collapsed the intervening pad. (`inputFactor` is the
   6-wide PRODUCTION factor at +553; `outputFactor` is the 2-wide INPUT factor at
   +563 — names kept for source stability, documented in the struct.)
2. `src/sim/building_value.cpp` (`Building_ComputeItemBaseValue`) — the inIdx (a4)
   path now bounds to **6** (was 2); the outIdx (a3) path stays 2. With the corrected
   widths, the live indices (0..5 / 0..1) are all in-range == byte-identical to the
   binary.
3. `src/sim/building_stock.cpp` (`BuildingValue_ComputeProductionWorth`) — the
   arg→array mapping was **inverted**. Fixed: the 0..5 loop now passes the index as
   `inIdx` (a4 → +553), the 0..1 loop as `outIdx` (a3 → +563), fallback `outIdx=0`.
   Removed the `contrib > 0` gate (the binary accumulates unconditionally; matches
   any-slot-with-nonzero-factor for the single-self-worker default hook). The
   worker-join (`dword_12CEA7C`, 768×536) + worker packed prof at +353 is the
   entity/worker module's leaf and stays behind the injectable hook (rule 8 — not a
   leaf we own).

### Golden re-pins (golden was wrong → fixed to binary)
- `tests/unit/sim_remaining_test.cpp::ProductionWorthBadAndGoodType`: the same total
  (`col[3] = 6272`) but the column split flips with the corrected mapping — the 0..5
  sweep now reads `inputFactor[0]=4` → col[4]=3584, the 0..1 sweep reads
  `outputFactor[0..1]={1,2}` → col[6]=2688.
- `tests/unit/sim_building_test.cpp::ItemBaseValueIndexBounds`: comment corrected;
  asserts unchanged (inIdx→inputFactor, outIdx→outputFactor; high indices read 0
  padding). All pass.

---

## Per-type cutscene step table — VERIFIED-1:1

`VIBE_Cutscene_InitCommandTable` @0x4acefc. Recovered the table: base
`dword_11AE59C`, **96 records × 69 bytes** (init: `for (i=0; i!=6624; i+=69)
[i]=-1`). The per-type **function block is 20 bytes** (5 dwords) starting at +36 of
each record (`dword_11AE5C0` = base+36; type N main at +36+20*N). Columns:
main(+0), next(+4), f8(+8), flag(+12), step(+16). Decoded **all 12 types
column-for-column** and every one matches the recon's documented table
(`src/sim/cutscene_process.h` lines 44-56) exactly:

type0 PlayTobyScene/NullSub; type1 RunCouncilSession/SuccessorChoiceA/B; type2
RunCourtTrial/TortureChoiceForm/EvaluateSessionDecision/—/BuildElectionForm; type3
RunBattleSetup/BeginBattleOrCacheState/—/flag1/BuildBattleInfoText; type4
Duel/ShowDuelWindow/RollDuelOutcomeTier/—/Duel_CheckParticipants; type5
Execution/—/—/—/Duel_BuildMessages; type6 Wedding/—/—/—/CheckMarriageEligible;
type7 Birth/—/—/—/CheckBirthParticipants; type8 Death/—/—/—/CheckDeathTimer; type9
Bankruptcy; type10 Auction/LeaseWindow/LeaseAutoResolve/flag1/BroadcastMessage;
type11 Salon. **No fix needed.**

---

## Auction — `VIBE_Cutscene_Auction` @0x4a89f8 — FIXED (one math divergence)

Decompiled fully. The function is overwhelmingly scene/voice/script/command leaves
(routed through `AuctionCutsceneHooks`, faithful per the documented hook model). The
deterministic spine I diffed:

- **Region→texture/string** (LABEL_25): kind 11→tex0 "WALDSTUECK" (forest),
  12→tex1 "STEINBRUCH" (quarry), 13→tex2 "MINE" — matches
  `AuctionTextureSetForRegion` (kForest 0 / kQuarry 1 / kMine 2). ✓
- **Lease split**: `flt_61D700 = 0.9`, `flt_61D704 = 0.1` (get_bytes confirmed
  0x3f666666 / 0x3dcccccd); split via `(int)(bid*factor)` after `VIBE_Coord_ConvertX`
  (trunc) — recon's `static_cast<int>` truncates identically. ✓
- **Round loop** `do {…} while (v134+1 < 5 && !v129)` — max 5; winner (`v136`) is
  recorded inside the per-bidder scan whenever a bidder beats the round high.

**DIVERGENCE (fixed):** the recon had a 3-way branch (`>=2` / `==1` / `==0`) and
raised the ask `+= 32` on **every** `>=2` round. The binary has a **2-way** branch
(`v64 >= 2` continue, raising `v116[0] += 32` ONLY while `v134 < 4`; else `v129 =
1` stop) and records the winner in the per-bidder scan regardless of count.
- Fixed `src/sim/cutscene_auction.cpp`: the `+= 32` raise is now gated on
  `round < kAuctionMaxRounds-1` (== `v134 < 4`); collapsed to the binary's 2-way
  structure while preserving the winner-commit for the 1-bidder case (which the
  binary achieves via the per-bidder scan). All existing auction tests still pass
  (the visible outcomes are identical; only the round-4 displayed ask changed).

`VIBE_Cutscene_BroadcastMessage` (type-10 step, @0x4a8930): matches the recon
(building-resolves gate, then speech for kind 6/7 participants).

---

## Duel — `VIBE_Cutscene_Duel` @0x4a53a8 (+ ResolveShot @0x4a4b68, RollDuelOutcomeTier @0x4a6908) — VERIFIED-1:1

Decompiled the cutscene driver, the per-shot resolver, and the tier roller.

- **`DuelRollOutcomeTier`** == @0x4a6908: tiered (`dword_6315A4`) RandInt(100):
  `>50→4, <=15→3, else 2`; non-tiered RandInt(10): `(r>7)`. The recon returns the
  stored tier byte; the binary returns the raw roll but stores the tier at +148 —
  the +148 tier is the observable output and matches. (RollDuelOutcomeTier is the
  type-4 f8/+8 column, not a ready-predicate, so the return value isn't dispatched.)
- **Round loop** `while (v46 < 3 && !dword_6315C4)` — recon `while (round < 3 &&
  !state.over)`. ✓
- **Winner by HP ratio**: binary `ratioA=ComputeOutputRatio(A); ratioB=…(B); if
  (ratioB >= ratioA) winner=A else B`. Recon `DuelWinnerByRatio` returns
  `(rb >= ra) ? 0(A) : 1(B)`. ✓ (A wins ties.)
- **Intro music** by the +124 scene byte: 0→satisfaktion, 1→DerSeuchenzug, else→
  AmKuehlenGrabe. **Season→sky band** 3→0, 0/2→1, 1→2.
- **Per-shot math** (`ResolveShot`): hit chance `EvalProductionRating(shooter,3) *
  dbl_61CE24`, *dbl_61CE2C (slot 367 active), *dbl_61CE34 (rattled); damage
  `RandInt(15)+5` (betrayed) / `RandInt(30)+15`; HP loss `HP - worth7 * dmg *
  dbl_61CE3C` via ConvertX. Post-duel stock add `hits * 0.5 * 10.0` (dbl_61D0A4 =
  0.5, dbl_61D0AC = 10.0, get_bytes confirmed) via ConvertX. **This leaf lives in
  the combat cluster (`src/sim/duel.cpp` — `Duel_ResolveShot/Taunt/Aim/CheckFatalHit`),
  NOT my edit set;** the recon delegates to it, which is correct. The cutscene-level
  state machine I own is VERIFIED-1:1.

---

## Wedding — `VIBE_Cutscene_Wedding` @0x4a73a4 — VERIFIED-1:1 (structural)

Decompiled. Pure presentation: a deeply nested `if (!result)` ladder of
`LoadScene`/`Text_RenderRichString`/`RunTimedScript(ms)`/`RunScriptUntilSkip`/
`WeddingExit`, short-circuiting on the first non-zero (skip). Load-bearing
non-presentation logic:
- **Abort** unless BOTH spouses resolve (`Person_FindRecordById(a1+52)` &&
  `(a1+56)`) — recon aborts on unresolved spouse. ✓
- **Crime tracking**: each spouse with kind==6 → `Mission_TrackCrimeProgress`.
- **Name swap**: if `*((BYTE*)v5+9)` the two records are swapped before the
  `"%s %s %i"` format (groom/bride ordering).
All voice/.esc/form/timed-script leaves are the documented hook model. The recon's
`CutsceneWedding` reproduces the abort + the spouse handling; the timed-script
ladder is hook-driven. **No fix needed.**

---

## Animals — VERIFIED-1:1 (re-confirmed wave-8, no edits)

- **String table @0x61afdc** decoded byte-for-byte: `katze_KATZE\0 hund_HUND\0\0\0
  dummy_SHEEP\0 schaf_SCHAF\0 dummy_COW\0\0\0 kuh_KUH\0 dummy_%s\0\0\0\0
  pferd_PFERD\0 schwein_SCHWEIN\0 bewegung/stehen…` — matches the wave-8 record.
- **SpawnDog @0x483b58** loads `katze_KATZE` (kind byte +4 = **1**);
  **SpawnCat @0x483bc4** loads `hund_HUND` (kind +4 = **0**). The model swap is
  REAL and the recon preserves it (`kAnimalCat=0`, `kAnimalDog=1`; the spawn-kind
  roll `0→Cat→hund`, `1→Dog→katze`). Both set the actor kind byte (+4 of slot,
  +4 of actor = 2 animal), scale `1.0f` (0x416 416=1065353216), action 56 inserted.
- Spawn-kind roll table, LoadModels order, AllocSlot, pool layout — all match the
  recon (`animal.cpp` / `animal_wander.cpp`). **No new code (ODR rule); no fix.**
  (The one outstanding item remains the orchestrator's live-session wiring of
  `Animal_AllocPool/LoadModels/Update/FreePool` — bind-site, not my cluster.)

---

## Console / cheat — VERIFIED-1:1 (wave-12 hardening already faithful)

No new divergence found; the wave-12 byte-walk prefix fixes for `cheat_recon`
(`TokenHasPrefix`) and the cutscene participant/type bounds remain
memory-safe-and-byte-identical for valid input. (Re-checked the cutscene_process
type dispatch against the 96×69 table: a `type >= 12` slot dispatches no fn — the
table only populates 0..11, so this is faithful, not a behavioral change.)

---

## Build / test

The normal `build/` is **broken by another wave's `src/world/history_mission.cpp`**
(`MissionCompletionOutcome::kReload/kClose` not declared — world economy/history
cluster, NOT mine; I must not edit it). My cluster is unaffected: all owned `.cpp`
files compile clean (`-fsyntax-only`), and I built + ran the owned tests with a
standalone link of the cluster TUs + their util/crt deps:

- building tests (`sim_remaining_test` + `sim_building_test`): **150 checks, 0 failures**
- cutscene-type tests (`sim_cutscene_types_test`, auction/duel/wedding):
  **267 checks, 0 failures**

## VERIFIED-1:1 / FIXED summary

| item | addr | result |
|------|------|--------|
| ComputeItemBaseValue factor widths | 0x58f328 | **FIXED** (+553 array is 6-wide, not 2) |
| ComputeProductionWorth arg mapping + accum | 0x58fe68 | **FIXED** (inIdx↔outIdx swap; drop >0 gate) |
| BuildingTypeDef +547/+553/+559/+563 widths | — | **FIXED** struct (6/6/2/2) |
| cutscene per-type step table (12 types) | 0x4acefc | VERIFIED-1:1 |
| Cutscene_Auction round loop + ask raise | 0x4a89f8 | **FIXED** (round<4 guard, 2-way stop) |
| Auction region→texture, lease split 0.9/0.1 | 0x4a89f8 | VERIFIED-1:1 |
| Cutscene_Duel state machine + winner | 0x4a53a8 | VERIFIED-1:1 |
| RollDuelOutcomeTier | 0x4a6908 | VERIFIED-1:1 |
| Duel_ResolveShot math | 0x4a4b68 | VERIFIED (delegated to combat duel.cpp, out of cluster) |
| Cutscene_Wedding abort + spouse logic | 0x4a73a4 | VERIFIED-1:1 |
| animal model table + SpawnDog/Cat swap | 0x61afdc/0x483b58/0x483bc4 | VERIFIED-1:1 |
| console/cheat | — | VERIFIED-1:1 (wave-12 fixes still faithful) |
