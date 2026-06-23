# Wave-H1 1:1 Hardening — chunk world_00 (master)

Chunk = 23 .cpp files under `src/world/`. Every provenance'd function was decompiled
AND (where Hex-Rays could be wrong) disassembled via IDA MCP and diffed line-for-line
against `gilde.exe`. Per-cluster detail lives in the sibling reports:

- `world_00_amt.md`     — amt_economy2 / amt_enforcement / amt_goods / amt_recon_office_window (+ amt.cpp, amt_slot_table verified by lead)
- `world_00_bank.md`    — bank / bank_loan / bank_treasury / caravan_cargo
- `world_00_city.md`    — city / city_load / city_population_census / city_satisfaction_grid
- `world_00_council.md` — council / council_session / court_council2
- `world_00_crime.md`   — crime / data_load
- `world_00_economy.md` — economy / economy_quality / economy_tick

NEVER committed. progress/INDEX.md untouched. Only chunk files + their tests edited.

## Lead-agent direct verification (amt.cpp + amt_slot_table.cpp)

`amt_slot_table.cpp` — all 8 primitives VERIFIED-1:1 against disasm:
- 0x47ff14 GetOfficeType, 0x56e894 FindSlotByCoord, 0x56e8c8 FindSlotByObjectId,
  0x56e8ec FindRecordByKey (stride 6 dwords = 24B), 0x56e910 FindSlotAtPoint (AABB,
  half = size>>1), 0x56e974 FindFreePlacement (free-probe + (size×size) box sweep),
  0x56e850 FindOfficeTypeRecord (35-word stride, terminator read-before-advance).
  Occupied test `*(int*)(rec+10)>>24 != -1` matches; all return forms match.

`amt.cpp` — tax/wage/loan arithmetic VERIFIED-1:1:
- flt_625888 = 0x3c23d70a = 0.0099999998f (tax rate scale) confirmed via get_bytes.
- flt_6258AC = 100.0f, flt_6258B0 = 32.0f (wage scales) confirmed.
- Tax (0x57abd8 et al): `(float)rate*0.01f * (float)acct`, clamp `<=0 -> 0`, then
  `VIBE_Coord_ConvertX` = truncate-toward-zero — recon `AmtTrunc` via (long long)
  cast matches. Wage (0x57b480): `(double)rank * 100.0f * 32.0f * lawRate` then
  ConvertX truncate — matches. Loan (0x57b304): 3-condition foreclosure gate
  (`!hasLender && creditorId==-1 && |held| > 2*base`) matches.
  The live-state dispatch/gating is parameterized into input structs (boundary).

## RESTORED FIXES (lead agent re-applied — sub-agent edits to these files did NOT
## persist to disk; re-derived from the binary and verified)

NOTE: the bank- and city-cluster sub-agents reported source fixes that were reverted
before disk commit (uniform checkout mtime; clean `git diff`). The economy sub-agent's
economy.cpp edits were likewise lost (but economy_quality/economy_tick persisted).
The lead agent re-did the lost fixes directly:

### FIXED — caravan_cargo.cpp `CaravanComputeCargoValue` (0x53ff3c)
Disasm: `v13` (accumulator) and `v15`/`v14` (unit prices) are ALL declared `float`;
the MAC `v13 = (double)DataPtr * v15 + v13` narrows to float every iteration. The
recon accumulated in `double`. Fixed: float accumulator + float-narrowed unit.
- Golden FIX: `ComputeCargoValue_OwnerMarket` expected the f64 artifact
  `968.0000000000002`; float accumulation gives float-exact `968.0` (verified with a
  python f32 oracle). Corrected source + golden to the binary truth.
- Constants confirmed: dbl_623F30 = dbl_623F28 = 0x3ff199999999999a = 1.1.

### FIXED — caravan_cargo.cpp `CaravanLoadFromStorage` (0x53f6bc)
Disasm 0x53fbb6/0x53fbc9: emitted unit price is `v38 = (__int64)(v57 / a6)` with
ConvertX truncation toward zero; `a6` is the carrier-cost divisor. The recon emitted
the un-divided, un-truncated raw price. Fixed: added `carrierCost` param (default 1.0,
backward-compatible — no external callers, verified via grep), `unit` narrowed to
float, emitted price = `(i64)trunc(price / carrierCost)`. Qty = free space (v56).
(The resource-amount / handler-slot / dialog / command-queue gates reach the
interaction subsystem — documented BOUNDARY, not faked.)

### FIXED — bank_loan.cpp `LoanGrantCapacity` (0x591990 @0x591af3..0x591b7f)
Disasm: `v47 = (double)wealth * dbl_626A1C` is stored to a `float` (narrow), the
ceiling compare is on the float, and `v43 = (__int64)v46` truncates the FLOAT v46.
The recon computed the whole cap in `double`. Fixed to float intermediate + float
compare + trunc. All loan tuning constants re-confirmed byte-for-byte: 626A1C=0.2,
626A24=64000.0f, 626A2C=0.5, 626A34=0.01, 626A3C=0.25, 626A44=1/6, 626A4C=0.7,
626A54=1.5, 626A5C=5.0f, 626A60=-50.0f. LoanOfferBase/LoanComputeInterest/
LoanGenerateOffers (RNG order: amount-jitter %30 draw, then term %6 draw)
VERIFIED-1:1.

### FIXED — city_satisfaction_grid.cpp `CityBuildSatisfactionGrid` (0x5788d4)
Disasm 0x578a7f..0x578a93: the ring inner loop pre-increments the byte index by 24
(one cell) BEFORE the store. Because the grid field bases differ by exactly the cell
stride, the read at `flt_12349B0(+40)` and the SatB-write at `+16` of cell+1 hit the
SAME address — i.e. the spread is a `+=` into `SatDenom(cx,cy)` (+= contrib) and
`SatWeight(cx,cy)` (+= contrib*0.5), NOT the cross-field `=` the recon used (which
corrupted every cell). Fixed to the byte-exact `SatDenom += / SatWeight +=`.
Golden `BuildSatisfactionWeightGolden` value 20 unchanged (centre+ring both hit the
centre cell); rationale comment corrected.

### FIXED — city_satisfaction_grid.cpp `CityAddCrimeToGrid` / `CityRemoveCrimeFromGrid` (0x577fc4 / 0x578110)
Two bugs each, confirmed against disasm:
(a) Control flow: the original calls `VIBE_Coord_WorldToCityTile` but IGNORES its
    return (no test of eax at 0x578032) — the centre + ring writes ALWAYS run with
    whatever (x,y) the tiler leaves (RNG fallback survives off-map). The recon had a
    spurious `if(!WorldToCityTile) return` early-return. Removed.
(b) Ring write target: the inner loop pre-increments the byte index by 24 before the
    store (0x5780d2), so each ring write lands at `GridCrimeArea(cx, cy+1)`, not
    `(cx,cy)`. Fixed both Add and Remove.
- Golden FIX: `city_satisfaction_grid_test.cpp` — added `AreaRing()` helper (ring
  cells shifted +1 in y) and pointed the AddCrime ring / edge-clamp assertions at the
  shifted cells. AddRemove round-trip still nets zero (both shift identically).

### FIXED — city_load.cpp `CityGetDistrictCoord` (0x5783c4)
Disasm: `cmp al,48h; jl` is a SIGNED byte compare against 72 with NO low-side bound,
and `movsx esi, al` SIGN-extends the id. The recon used an unsigned `>= 72` compare
(rejects high bytes that the signed gate would pass). Fixed to the signed-byte gate;
kept a low-side guard that returns 0 for the negative/high ids the binary would OOB on
(documented divergence — those ids are unreachable; the OOB read can't be faithfully
reproduced without the adjacent BSS — Rule 8 boundary).

## Sub-agent fixes that PERSISTED (verified on disk)

- amt cluster (amt_economy2/enforcement/goods/recon_office_window): 5 FIXES landed —
  ComputeOfficeRenderOffset 5-entry stretch table, FindNextActiveBuilding inverted
  generation compare + wrong staleness field, ComputeBuildingRivalryScore wrong region
  operands, GoodsRunDistributionPass branch-B owners-clear fold + real variant-index
  (0x589cb0), EnforceLawViolations double RNG. Tables @0x5526b0/@0x63d584 etc.
  byte-confirmed. See world_00_amt.md.
- data_load: WorldLoadBuildingAndObjectData (0x5835f8) FIXED — removed an extra branch
  (the binary gates only on file-open failure, ignores the read count). See world_00_crime.md.
- economy_quality / economy_tick: 7 FIXES landed — ClampRatio narrow-then-compare,
  InterpolatedLawScore/FillLawRangeRatios per-field fild-to-double, TickPriceLevel +
  CityTickStatsAndBroadcast 80-bit-then-narrow EMA + missing capDivisor persist,
  ComputePopulationTrend float-temp clamp chain. See world_00_economy.md.

## VERIFIED-1:1 (no churn) — counts from the cluster reports

- amt:     18 verified  (+5 fixed)
- bank:     9 verified  (+ caravan/loan fixes above)
- city:     9 verified  (+ the restored city fixes above)
- council: 11 verified  (council/court — RNG tiebreak draw order confirmed)
- crime:   11 verified  (Straftat/Beweis slot tables; stride 45, count 512)
- economy: 10 verified  (+7 fixed in quality/tick)

## Build / test status

The full `guild` lib does NOT link due to a PRE-EXISTING break OUTSIDE this chunk:
`src/gui/widget_layout.cpp` (uses a nonexistent `Widget::ld<>()`, another wave's WIP)
and `src/sim/charaction_steps6.cpp`. None of my 23 files are involved.

All 23 chunk sources compile clean in isolation (g++ -std=c++17 -fsyntax-only). The
restored fixes were verified by standalone test links against the test framework:
- city_satisfaction_grid_test: 203 checks, 0 failures.
- caravan_cargo_test:           67 checks, 0 failures.
- loan-capacity driver (incl. float boundary 320003->64000): pass.
- CityGetDistrictCoord assertions (0/71 ok, 72/100000/-1 reject): pass by inspection.

BOUNDARY / handoffs (out-of-chunk, not edited — documented for owning waves):
- src/gui/widget_layout.cpp, src/sim/charaction_steps6.cpp — pre-existing build breaks.
- office.cpp `OfficeCollectSuccessorCandidates` stores IDs vs binary's record ptrs
  (council report); PersonRelEntry width (sim/person_relations.h); office≥0x25
  dead-code fallback table read — all net-observable-identical, see cluster reports.
