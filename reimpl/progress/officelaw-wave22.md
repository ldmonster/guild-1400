# Wave-22 — Office wages + max-wanted-level + person-query-by-good (W22-OFFICELAW)

**Agent:** W22-OFFICELAW · **Date:** 2026-06-16 · **MCP:** live (`gilde.exe`, imagebase 0x400000)

Reconstructed three genuinely-missing logic bodies from
[`coverage-audit-wave21.md`](coverage-audit-wave21.md) (the office-wage formula,
the law/wanted-level roll, and the person-query-by-good dispatcher), 1:1 from the
Hex-Rays decompile + `get_bytes` constants. All three were previously listed as
STILL-MISSING (no `.cpp` body cited the function-start range).

---

## Reconstructed (3 targets)

| addr | name | new module | bytes |
|------|------|-----------|------:|
| 0x57b480 | VIBE_Amt_ComputeOfficeWages | `src/world/office_wages.{h,cpp}` | 570 |
| 0x4c2ba0 | VIBE_Gesetz_ComputeMaxWantedLevel | `src/world/wanted_level.{h,cpp}` | 158 |
| 0x5929f0 | VIBE_Person_QueryByGoodType | `src/sim/person_query.{h,cpp}` | 105 |

### 0x57b480 — VIBE_Amt_ComputeOfficeWages → `world/office_wages.cpp`
The per-office wage computation. Reconstructed control flow:
1. **early-out gate** (0x57b4f1): vacant (`word_12CE910 == -1`), saturated
   (`byte_12CE912 >= 10`), disabled (`!byte_12CEA918`), or no titles
   (`!byte_12CEA76 && !byte_12CEA79`) → `valid = false`.
2. **lawRate** from `VIBE_Gesetz_GetRecord(14, ...)` float field (0x57b50d).
3. **seat B** wage first (0x57b53d → `*(a3+8)`), then **seat A** (0x57b574 →
   `*(a3+4)`), each `trunc(rank * 100.0f * 32.0f * lawRate)`.
4. if `a2 == 0` → return compute-only (0x57b5aa).
5. dispatch: queue each non-zero seat wage (0x57b5de / 0x57b601); when
   `byte_12CE912 == 3`, draw `RandomModulo(bonusByte+2)` and queue
   `6400*(roll+bonusByte)` + `RequestBuildOp90(bonusByte)` (0x57b64d).

**Rounding (brief's key concern):** the wage chain is the x87 sequence
`(double)rank * flt_6258AC * flt_6258B0 * v20` then `VIBE_Coord_ConvertX` (0x5c6b08
= `frndint` under round-toward-zero, RC=0b11) + `(int)`. This is **truncation
toward zero**, NOT round-to-nearest. Golden-pinned: `7*100*32*0.33f = 7392.0002…
→ 7392` (truncates, does not round up). `OfficeWageTrunc` mirrors ConvertX exactly.

**Constants (get_bytes):** `flt_6258AC` @0x6258AC = `00 00 C8 42` = **100.0f**;
`flt_6258B0` @0x6258B0 = `00 00 00 42` = **32.0f**.

The live office/building array reads (536-byte stride: `byte_12CEA76/79`,
`word_12CE910`, `byte_12CE912`, `byte_12CEA918`, `VIBE_Office_GetDefinition`
ranks, `dword_12CE914`) are folded into `OfficeWageInput` by the caller; the two
command emissions route through a settable `OfficeWageCommandSink` (the original
commits through `VIBE_Command_QueueRequest16` / `VIBE_Command_RequestBuildOp90` +
the `dword_641DA4` pending counter). The RNG bonus draw routes through
`OfficeWageRandFn` (default `guild::util::RandomModulo`).

### 0x4c2ba0 — VIBE_Gesetz_ComputeMaxWantedLevel → `world/wanted_level.cpp`
The guard-station scan that sets a perpetrator's max wanted level. Reconstructed
1:1: scan up to 256 building slots (stride 169) for buildings that are alive
(`*v2`), owned by the same faction (`*(u16*)(v2+39) == *a1`), and **type 7**
(`*(byte*)(dword_13CE294 + 589 * *v2) == 7`); for each, `v6 =
VIBE_Building_SumWorkstationByCategory(b, 10, 1)`. A building whose `v6 == 75`
**short-circuits the whole scan to 0.75** (0x4c2c20); else the max is tracked and
the result is `(float)((double)maxSum * flt_61E588)` (0x4c2be6), narrowed to float
exactly as the original's `return (float)(...)`.

**Constant (get_bytes):** `flt_61E588` @0x61E588 = `0A D7 23 3C` = **0.01f**.

The 256-slot building enumeration routes through a `GuardBuildingAccessor`
(alive/owner/type/sumCat10 callbacks) so the scan is testable without the live
array. The scalar tail (`WantedLevelFromMaxSum`) matches `world/law.cpp`'s
abstracted `GesetzComputeMaxWantedLevelFromSum` numerically; this module is the
full faithful scan (different symbol — no ODR clash with law.cpp).

### 0x5929f0 — VIBE_Person_QueryByGoodType → `sim/person_query.cpp`
A 5-way dispatcher on `(goodType - 1)`: each arm calls
`VIBE_Person_QueryBegin(seed, 1, 5, key)` (filter op 5, count 1) with a good-type
key — **{1→15, 2→23, 3→24, 4→25, 5→26}** (0x592a0f…0x592a4f). `default` → null
(0x592a02). Built **on top of the existing** `guild::sim::PersonQueryBegin`
(entity.cpp, 0x586c20) — REUSED, not redefined.

`seed` (the original's `a2`/`esi`) forwards as PersonQueryBegin's seed; the
current C++ PersonQueryBegin re-bases the iterator to the array head before the
first match (matching the original's `dword_6498DC = dword_13CE298`), so the seed
does not change the match set — documented in the .cpp.

---

## Callees walked to genuine leaves (verified, not reconstructed here)

| addr | name | status |
|------|------|--------|
| 0x5c6b08 | VIBE_Coord_ConvertX | x87 round-toward-zero `frndint`; modeled by `OfficeWageTrunc` / `WantedLevelFromMaxSum` (truncation). Leaf. |
| 0x47f008 | VIBE_Office_GetDefinition | rank = `HIBYTE`, bonus = `BYTE2` of the office-def word; folded into `OfficeWageInput`. Already reconstructed as `OfficeGetDefinition` (world/office). |
| 0x4c244c | VIBE_Gesetz_GetRecord | already reconstructed (`world/law.cpp`); lawRate is its record-14 float field. |
| 0x5904fc | VIBE_Building_SumWorkstationByCategory | the cat-10 sum; routed through `GuardBuildingAccessor.sumCat10`. (Owned by the building/sim cluster.) |
| 0x586c20 | VIBE_Person_QueryBegin | already reconstructed (`sim/entity.cpp`); REUSED by person_query. |
| 0x494630 | VIBE_Command_QueueRequest16 | command-queue boundary; routed through `OfficeWageCommandSink.queueRequest16`. |
| 0x495b58 | VIBE_Command_RequestBuildOp90 | already reconstructed (`sim/command_apply7.cpp`); routed through `OfficeWageCommandSink.requestBuild`. |
| 0x58b89c | VIBE_Math_RandomModulo | already reconstructed (`util/math_random.cpp`); default RNG for the bonus draw. |
| 0x5857fc | VIBE_GameObject_QueryFind | dispatch gate (type-277 object); folded into `OfficeWageInput.dispatch`. Already reconstructed (entity.cpp). |

No new leaf needed reconstruction; every callee was either already in the tree or
is a genuine boundary (command queue) modeled by a hook.

---

## Wiring / handoff (rule 13)

- **office_wages**: the real driver is `VIBE_Amt_ProcessAllOfficeWages` @0x57b6bc
  (per-turn wage cycle; `world/amt.h` already cites it). It walks every office
  record `a1`, fills an `OfficeWageInput` from the 536-stride arrays + office-def
  ranks + law(14), and calls `ComputeOfficeWages` with `dispatch` = (person +
  type-277 object resolved). The engine installs `SetOfficeWageCommandSink` at
  boot to forward to the lockstep command queue. (Handoff documented in the .cpp;
  `amt.cpp`'s pre-existing *abstracted* `AmtComputeOfficeWages` is left in place —
  distinct symbol, no clash — and can be retired once 0x57b6bc adopts this body.)
- **wanted_level**: caller is `VIBE_Gesetz_EvaluateViolation` @0x4c2c5c
  (`world/law.cpp` `GesetzEvaluateViolation`). The engine binds
  `GuardBuildingAccessor` to the live building array + `Building_Sum…`. law.cpp
  keeps its scalar `GesetzSetGuardStationSumFn` hook for the abstracted path; this
  module is the full scan for callers that have the building array.
- **person_query**: the concrete engine function behind the
  `personQueryByGoodType` hooks in `scene_recon2_orchestrator`, `building2/4`,
  `npcaction9`, and `gamelogic_recon5_resolve_stat`. Those hook fields can now
  point at `guild::sim::PersonQueryByGoodType`.

---

## Tests

`tests/unit/world_officelaw_test.cpp` — **12 tests / 52 checks, all pass**
(built in-tree as `world_officelaw_test`, linked against the real `guild` lib):
- wage seat formula golden (incl. the 7392 truncation pin) + ConvertX
  truncation-toward-zero (incl. negatives);
- wage early-out gate (vacant/saturated/disabled/no-titles);
- compute-only vs dispatch (one QueueRequest16 per non-zero seat; zero seat
  emits nothing);
- state-3 bonus branch (`6400*(roll+base)` = 19200 with roll 1, base 2; +1
  RequestBuildOp90);
- wanted-level scalar tail (0/0.75/0.5/1.0 + 75-cap) + building scan filters
  (alive/owner/type-7) + max + 75 short-circuit (later slots never read);
- person-query good-type key table {15,23,24,25,26}, out-of-range → null,
  in-range dispatch.

Golden integers cross-checked with python3 (x87 chain + truncation). Build stays
green (`Built target guild`, `Built target world_officelaw_test`).
