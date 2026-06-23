# Wave-21 — Bank-Meister AI calculator (`src/sim/ai_meister_bank`)

**Agent:** W21-AIBANK · **Date:** 2026-06-16 · **MCP:** live (`gilde.exe`, imagebase 0x400000)

Reconstructs the **Bank-Meister** per-frame AI "brain" branch — the one remaining
top-level `VIBE_Ai_Calc*` Meister calculator the wave-19 fleet
(`ai_meister_calc_{diebe,farming,wache,angriff,ambush}`) left uncovered.
Wave-19's `DispatchMeisterCalc` explicitly **no-ops** `MeisterRoutine::kBank`
("their own planner module owns them"); this wave supplies that body.

## Reconstructed (1:1 from the decompile + disasm)

| addr | name | reimpl | file |
|------|------|--------|------|
| 0x459264 | VIBE_Ai_CalcBankmeister | `CalcBankmeister` | src/sim/ai_meister_bank.cpp |

Sole top-level calculator in scope. All control flow, the two RNG draw sites and
their order, the signed-truncating integer divisions, the float thresholds, the
float→int truncation, and the per-tick done-flag are 1:1 with `0x459264`.

### What it does (faithful summary)
1. `m+0x1C8 & 8` set → return (already ran this tick).
2. `Gesetz_GetRecord(13)` → `LawRecord.threshold` (field +0x18) = the mandated
   cash-reserve / interest target. Law 13 threshold = **16** (byte-verified from
   `g_lawTable` default row 13, offset 24 = 0x10).
3. He handler-list probe `FindFirstHandlerByFilter(2,0,0x23,3,bldgId)` + iterate →
   count of pending credit/loan handlers (`i`).
4. **Reserve / interest-rate** state machine toward the law target
   (`BankmeisterNewRate`): `|cash-target|>3` → single ±1 step; `<=3` → randomised
   ±(rand%3) step gated on handler count and bounds (downward iff handlers==0 &&
   target-2<cash, clamp ≥max(target-2)≥4; upward iff handlers>3 && cash<target,
   clamp ≤target+3); otherwise hold (no command).
5. **Per-denomination vault balancing** (loop i=1..3, `BankmeisterReserveTier`):
   count coins of denom i at the bank scene root; `< 3*budget/20` → MINT to
   `5*budget/20` (`*1.03`); `> 6*budget/20` → MELT to `4*budget/20` (`*1.03`).
   `netMoved` accumulates the raw deltas (+mint, −melt).
6. **Final top-up / draw-down** (`BankmeisterFinalBalance`): recount the default
   currency; `< 1.5*budget` && `budget-netMoved>0` → MINT `(budget-netMoved)*0.8`;
   else if owner-person kind byte ∉ {6,7} && `2*budget < count` → MELT
   `count − 2.25*budget`.
7. Set `m+0x1C8 |= 8`.

## Reused (extern / header-only, NO ODR, NO redefinition)

- **`src/sim/ai_recon_brain.h`** — the pure Bankmeister decision **cores** already
  existed there header-only (`BankmeisterNewRate`, `BankmeisterReserveTier`,
  `BankmeisterFinalBalance`) but were **never cited in a `.cpp`**, so the coverage
  audit still flagged 0x459264 as missing (header-only ≠ reconstructed). This wave
  supplies the **full body** that drives those cores from real records, captures
  the command boundary, and wires the dispatch — closing the gap. The FP constants
  (1.03/1.5/2.25/0.8) and the ConvertX truncation live inside those cores; verified
  byte-exact (`dbl_6198C8/D0/D8/E0`).
- **`src/world/law.h` `GesetzGetRecord` / `LawRecord`** (0x4c244c, world/law.cpp) —
  the law table read for the reserve target.
- **`src/sim/ai_meister_internal.h` `aimei::resolveHandle/makeObjHandle`** (wave-19,
  header-only) — the 64-bit-safe building-record pointer-column handle model.
- **`src/util/math_random.h` `RandomModulo`** (== `VIBE_Math_RandomModulo` 0x58b89c).
- **`src/sim/entity.h`** record arrays (`g_persons`, `g_personIds`, `g_objects`).

Owned new symbols are all `Bank`-prefixed (`CalcBankmeister`, `BankCommand`,
`BankCmdSink`, `BankAiLeaves`, `g_bankCmdSink`, `g_bankLeaves`, `ResetBankAiState`)
— grepped src/**, no clash with any existing definition.

## Data model & fidelity decisions

- **Records by raw byte offset** through local `rd*/wr8` helpers at the exact
  decompiled offsets (same convention as the rest of the codebase): meister
  +0x16C bldg-ptr / +0x1B8 budget / +0x1C8 flags2; building +1 id / +0x27 owner /
  +0x65 cash / +0x5D scene-root; owner person +2 kind.
- **`dword_12CE914[536*owner]` → `g_personIds[owner]`.** The binary interleaves the
  id column with the 536-byte person record (`dword_12CE914 == person+4`); the
  reimpl flattens it into the parallel `g_personIds[]`, so the owner-actor id is
  `g_personIds[owner]`. Verified against the `imul …,218h` (×536) disasm at
  0x4593da / 0x459631 and the wave-19 `personId(i)` model.
- **`byte_12CE912[536*owner]` → `g_persons[owner]+2`** (the 6/7 melt gate reads the
  OWNER person's kind byte, not the building's). Verified from loc_459660 disasm.
- **Float→int via ConvertX truncation.** `VIBE_Coord_ConvertX` (0x5c6b08) sets the
  FPU CW to round-toward-zero then `frndint`+`fistp` = truncation. The reused cores
  use `(int)(double*k)` which truncates toward zero — identical. (Golden vector:
  `(25-0)*1.03 = 25.75 → 25`, `(40-20)*1.03 = 20.6 → 20`, `(100-5)*0.8 = 76 → 76`,
  `400 − 2.25*100 = 175`.)
- **Signed truncating `/`** for all `k*budget/20` thresholds (matches `idiv`).
- **RNG draw order preserved (rule 1).** The original draws `RandomModulo(3)`
  **only in the single reserve branch that runs**. The body decides which branch
  the core will take and draws exactly once into the matching arg (other = 0,
  never read); `|diff|>3` and hold paths draw nothing — so the global RNG state
  advances identically to the binary.

## Boundaries surfaced as data (rule 8 — NEVER faked)

- **Lockstep command queue.** The original emits via
  `Command_BeginDeltaPacket`/`AppendDeltaField`/`QueueRequestState22` (a "set
  reserve column 0x65 to N" delta packet — the building-ptr low word cancels the
  `dword_11AA474` base, leaving col == 0x65) and
  `EnqueueBuildingActionStart("Bankmeister")` + two `EnqueueCmd15` legs +
  `EnqueueBuildingActionEnd` (a mint/melt coin op), all funnelling into
  `Command_EnqueuePacket` (0x49388c) — the session lockstep queue. As with the
  wave-19 `MeisterCmdSink`, each emit is captured into a **`BankCmdSink`**
  (distinct API → its own sink type: `BankCommand{kReserveSet|kCoinOp}`); the live
  bridge drains it into the real `CommandQueue`. Null sink → logic runs, capture
  skipped (still faithful).
- **Two engine leaves (function-pointer hooks `BankAiLeaves`):**
  `heCountMatching(filterCode,key)` (the He handler-list count loop, 0x4c63f8 /
  0x4c6278) and `coinCountAtLocation(sceneRootId,denom)` (`GameObject_CountAtLocation`
  0x58f1f4 → `GameObject_QueryFind`/`IterNext`). Null hooks take the original's
  null/zero path (no handlers / no coins). The live bridge wires them to the real
  reimpl leaves.

## Wiring (rule 13)

`DispatchMeisterCalc` (wave-19 `src/sim/ai_meister_core.cpp`) currently no-ops
`MeisterRoutine::kBank`. **One-line handoff** (that file + `src/ai/aiplayer.cpp`
are not owned by this wave): in `DispatchMeisterCalc`, the `kBank` case should call

```cpp
guild::sim::CalcBankmeister(meisterRec);
```

with `guild::sim::g_bankLeaves` / `g_bankCmdSink` set by the engine bridge
(exactly as it already sets `g_meisterLeaves` / `g_meisterCmdSink` for the wave-19
Calc routines). The real call edge is `VIBE_Ai_EvaluateMeister (0x4533a8)` →
`ClassifyMeisterRoutine` (already maps AiPlayer class 5 → `kBank`) → dispatch.

## Tests

`tests/unit/ai_meister_bank_test.cpp` — 12 golden suites (synthetic deterministic
person/object/owner state, injected leaf hooks, captured `BankCmdSink`, seeded
`Srand` for the random reserve branches), **70 checks, 0 failures**:

| test | covers |
|------|--------|
| AlreadyDoneSkips | flags2 bit-8 early return, no emits |
| ReserveStepUpWhenBelow / ReserveStepDownWhenAbove | `|diff|>3` ±1 step |
| ReserveRandomDownClamped / ReserveRandomUpClamped | `<=3` random branches, clamp bounds (seeds 1..6) |
| ReserveNoStepInBand | in-band hold (no reserve command) |
| CoinBalanceMintMeltDefault | mint(25)+melt(20) tiers, default mint(76), netMoved math, 4 coin queries |
| DefaultMeltWhenFlush | final melt = count − 2.25*budget = 175 |
| DefaultMeltSkippedForKind6 | owner kind 6 blocks the final melt |
| NullLeavesTakeZeroPath | null leaves → zero-return paths (3 denom mints @12 + default mint @11 + ±1 reserve) |
| NoSinkRunsClean | null sink: logic runs, done-flag set, no crash |

Standalone build (the shared `guild` lib + `tests/unit/*.cpp` are auto-globbed by
CMake; the module also links into the normal build):
`g++ -std=c++17 ai_meister_bank.cpp entity.cpp math_random.cpp rand.cpp law.cpp`
compiles `-Wall -Wextra` clean and the suite passes.

## Reconstructed vs genuine leaves

- **Reconstructed (this wave):** the full `CalcBankmeister` orchestration — flag
  gate, law read, building/owner field reads, the reserve state-machine drive
  (with faithful RNG draw order), the 3-tier vault balancing loop + netMoved
  accumulator, the final top-up/draw-down with the owner-kind 6/7 gate, the
  done-flag write, and the command-boundary capture for every emit. The pure
  arithmetic cores were reused from `ai_recon_brain.h` (now first cited in a
  `.cpp`, closing the audit gap).
- **Genuine leaves (hooks, NOT reconstructed here):** He handler-list count
  (0x4c63f8/0x4c6278 — reads `byte_11D6040` 332-byte handler array, an independent
  subsystem) and `GameObject_CountAtLocation` (0x58f1f4 — the scene/object query
  iterator). Surfaced via `BankAiLeaves`; the bridge wires them to the reimpl
  leaves already modelled elsewhere in `src/`.

## Scope note — Production calculators NOT in this wave

`VIBE_Ai_CalcMeisterProduction` (0x4537e8) and `VIBE_Ai_CalcMeisterCraftProduction`
(0x4542b8) are the other two `Ai_Calc*` siblings without a `.cpp` body, but they
are the **Production** family (`MeisterRoutine::kProduction`/`kCraftProduction`/
`kPlanProduction`), which wave-19 + `src/ai/aiplayer.h` explicitly assign to "their
own planner module" — out of scope for this Bank-focused wave and not flagged as
new logic in `coverage-audit-wave20.md`'s missing list. Flagged here for a future
`ai_meister_production` wave; the `ClassifyMeisterRoutine` mapping already exists,
so each only needs its body + the matching `DispatchMeisterCalc` case.
