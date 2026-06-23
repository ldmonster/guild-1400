# Personnel, Recruitment & Labor Costs (Wages)

This document describes the personnel/labor economy of the Guild reimplementation
(`gilde.exe`, *The Guild / Europa 1400 / Die Gilde*): how employees and workers
are recruited, what recruitment costs, how the roster is tracked, how wages are
computed and paid, and the hire/fire/raise/lower turnover paths.

All formulas, constants, offsets and addresses below are taken verbatim from the
ported source. The original-binary addresses (e.g. `0x55d674`) are the absolute
`gilde.exe` addresses recorded in the port's header comments.

> **Scope / caveats.** Several of the dialog-driven, command-queue-driven, and
> cross-module pieces are reconstructed as *hooks* (function pointers with inert
> defaults) rather than fully translated bodies. Where a number cannot be read
> off the code (because it lives behind a hook or in an unreconstructed leaf), it
> is flagged explicitly as **UNKNOWN / hooked**. The wage payment *cadence* in
> particular is only partly determinable from these files (see
> [Payment cadence](#payment-cadence)).

---

## Source files

Primary (the assigned set):

- `src/sim/recruit.cpp`, `src/sim/recruit.h` — recruitment eligibility / proximity.
- `src/sim/recruit_cost.cpp`, `src/sim/recruit_cost.h` — recruitment **cost formula** and candidate-eligibility filter.
- `src/sim/personnel.cpp`, `src/sim/personnel.h` — **wage-by-category** computation.
- `src/sim/personnel_recruit2.cpp`, `src/sim/personnel_recruit2.h` — avatar-appearance pool + animal spawners (not labor-cost; documented briefly for completeness).
- `src/sim/person_personnel2.cpp`, `src/sim/person_personnel2.h` — employer/relation lookups, asset worth, debt-ratio, staff-book rows.

Wage/salary/hiring logic found by grep across the tree:

- `src/world/amt.cpp`, `src/world/amt.h` — **office wages** (`AmtComputeWage` / `AmtComputeOfficeWages`), the per-turn wage pass.
- `src/world/guild_election.cpp`, `src/world/guild_election.h` — `ProcessAllOfficeWages` 768-seat wage cycle.
- `src/play/turn_economy.cpp`, `src/play/turn_economy.h` — office-wage pass wired into the turn cycle; treasury bookkeeping.
- `src/sim/charaction_steps6.cpp`, `src/sim/charaction_steps6.h` — guild-master **hire (Einstellen)** / **fire (Entlassen)** coroutines that actually pay per-worker wages.
- `src/sim/npcaction6.cpp`, `src/sim/npcaction6.h` — `RaiseSalaryCmd` / `LowerSalaryCmd` NPC salary-adjust commands.
- `src/ai/meister_economy.cpp`, `src/ai/meister_economy.h` — `HireStaffDecision` / `TrainStaffDecision` AI staffing gates.
- `src/gui/personnel_gui.cpp`, `src/gui/personnel_gui.h` — pay-worker dialog, hire-confirm dialog, recruitment-offer window / bribe-bonus roll.
- `src/sim/types.h` — Person record layout and `PersonField` byte offsets.

---

## 1. Recruitment

### 1.1 Proximity / binding eligibility — `RecruitCheckRecruitProximity` (0x55d5c0)

`src/sim/recruit.cpp:12` validates whether `candidateId` may be recruited by
`recruiterId`. It returns one of several negative error codes, or `1`/`0`:

```text
-1024  either id not found
-1025  recruiter is not a live actor   (kPfIsPlayer byte +8 == 0)
-1026  candidate is not a live actor   (kPfIsPlayer byte +8 == 0)
-1027  candidate already bound to a different employer
-1028  recruiter already bound to a different employer
 1     if |officeRank(recruiter) - officeRank(candidate)| < 5
 0     otherwise
```

The **employer field** is the dword at Person offset `+0x5C` (`kPfRelationBase`,
relation slot 0); `-1` means "unbound" (`src/sim/recruit.cpp:24-31`). Final
acceptance hinges on the office-rank distance being strictly `< 5`
(`src/sim/recruit.cpp:33-35`), using `PersonComputeOfficeRank(marker, 0)`.

### 1.2 Candidate eligibility filter — `PersonEvaluateCandidateEligibility` (0x5596f8)

`src/sim/recruit_cost.cpp:182` is the large bit-flag matcher the recruit/AI/query
passes use to decide whether a candidate person passes a 24-byte filter descriptor
(`CandidateFilter`, `recruit_cost.h:77`, `sizeof == 0x24`). Returns `1` (eligible)
or `0`. Key gates (all reading the candidate's Person columns by byte offset):

- Self-match (`ref == cand`) → ineligible (`:188`).
- Candidate slot must be **live** (`marker != -1`), **not already recruited**
  (`kPfRecruited` byte `+0x1B1` == 0), and not the current recruit-window focus
  (`g_recruitFocus`, dword_6498E4) (`:192-195`).
- Empty filter (`filter == null` or `flagsA() == 0`) → eligible (`:198`).
- `KindOf(cand) >= 10` → ineligible (`:201`).
- Gender / family / profession / office / jail / class-set / favor-window /
  office-rank-distance gates follow, each keyed off a specific filter bit
  (`:222-345`). Notable ones:
  - **Unemployed gate** (flagsB bit `0x10`): candidate's employer (`+0x5C`) must
    not resolve to a live player record (`:231-235`).
  - **Favor window** (`filter->favorActive`): favorability must lie in
    `[favorMin, favorMax]` (`:324-330`), via the hooked `Favorability(ref, cand, 1)`.
  - **Office-rank distance** (flagsD bit `0x08`): `|rRank - cRank| <= span`, where
    `span = (flagsA & 0x7C00000) >> 22` (`:336-339`).

`KindOf` class values used throughout: `5`, `6`, `7` are "citizen class"
(`IsCitizenClass`, `:180`); the matcher also special-cases kinds `0` and `1`.

### 1.3 Recruitment cost formula — `RecruitComputeRecruitmentCost` (0x55d674) — EXACT

`src/sim/recruit_cost.cpp:71`. Returns `0` if either id is not found, otherwise a
**fee clamped to `[2, 25]`**. Recovered float/double constants (exact bytes from
`.rdata`, `recruit_cost.cpp:20-25`):

```cpp
constexpr float  kWealthRatioScale = 5.0f;   // flt_624A44
constexpr float  kFavorBase        = 100.0f; // flt_624A48
constexpr float  kFavorScale       = 0.1f;   // flt_624A4C
constexpr double kCashNumerator    = 24.0;   // dbl_624A54
constexpr double kRepHighThresh    = 210.0;  // dbl_624A5C
constexpr double kRepMidThresh     = 168.0;  // dbl_624A64
```

Steps (`recruit_cost.cpp:82-146`):

1. `w0 = Wealth(candidate.link[+0x60])`, `w1 = Wealth(candidate.link[+0x64])`
   (each `0` if the link record is absent). `Wealth` is
   `VIBE_Person_ComputeTotalWealth(marker, recruiterRec)` — **hooked**.
2. `wCand = Wealth(candidate)`, treated as `0` if `<= 0` (sign probe then
   recompute, matching the original).
3. `peak = max(wCand, max(w0, w1))`.
4. `wRec = Wealth(recruiter)`.
5. `a = peak / wRec * 5.0 + 1.0`  (the **wealth-ratio** term).
6. `fav = Favorability(recruiter.marker, candidate.marker, mode=1)` — **hooked**;
   `b = (100.0 - fav) * 0.1`  (the **favor** term).
7. `cash = PersonGetCashAmount(candidate.marker)`;
   `feeF = 24.0 / cash * (a + b)`.
8. `fee = lrint(feeF)` — **round-to-nearest-even** (the binary uses `fistp`; the
   port uses `std::lrint`, *not* truncation, `recruit_cost.cpp:118-122`).
9. **Reputation bump**: for each of the 5 reputation bytes
   `candidate[+0x80 .. +0x84]` (`kPfReputation`):
   `> 210 → fee += 2`; else `> 168 → fee += 1` (`:127-134`).
10. Debug/cheat hook `VIBE_DebugCmd_DispatchByType(16, rec, buf)`: if its result
    `& 2`, `fee /= 2` (`:136-140`, **hooked**, default 0).
11. **Clamp**: `fee > 24 → return 25`; `fee <= 2 → return 2`; else `fee`
    (`:142-146`).

Compactly:

```text
a   = peak / wealth(recruiter) * 5.0 + 1.0
b   = (100.0 - favor(recruiter, candidate)) * 0.1
fee = round( 24.0 / cash(candidate) * (a + b) )
fee += sum over 5 reputation bytes: (byte>210 ? 2 : byte>168 ? 1 : 0)
fee  = (debug & 2) ? fee/2 : fee
fee  = clamp_special(fee)   // >24 -> 25 ; <=2 -> 2 ; else fee
```

So a recruitment fee is **2..25 units**, driven by the candidate's wealth relative
to the recruiter, the recruiter→candidate favorability, the candidate's cash, and
the candidate's reputation. (Note: the clamp is asymmetric — values `>24` snap to
`25`, not `24`.)

### 1.4 Hire-confirm dialog — `RunHireConfirmDialog` (0x55d9c9 / window `privillegien\werbung2`)

`src/gui/personnel_gui.cpp:207`. Calls `computeRecruitmentCost(candidateId)`
(the formula above, via hook), opens the offer window, renders the cost into
string `0x18E5`, and runs a confirm/cancel frame loop. The actual money debit is
behind the engine command queue and is **hooked**, not translated here.

### 1.5 Recruitment offer / loyalty bribe — `RecruitOfferBonusRoll` (0x55e263)

`src/gui/personnel_gui.cpp:55`. When a candidate accepts, a **loyalty bonus**
(`count`) and a flavor-string index are rolled by the candidate's office-rank
delta:

```text
rank <= 0 : count = rand(3)+1 ,  idx = rand(5)
rank == 1 : count = 1        ,  idx = rand(5)+5
rank == 2 : count = 2        ,  idx = rand(4)+9
rank >= 3 : count = 3        ,  idx = rand(3)+12   (count clamped to <=3)
```

`RecruitOfferComputeMode` (`:44`) selects the offer-window mode 1..4 from the
handler's "rejected/away" byte +187 and "offer-pending" byte +186.

---

## 2. Personnel roster — how employees are tracked

There is no dedicated "employee list" struct per building; the roster is encoded
across the **Person record array** and a small set of side tables.

### 2.1 Person record and employer binding

The Person array is `word_12CE910` @ `0x12CE910`, **stride 536 bytes, 768 slots**
(`src/sim/types.h`; `kPersonStride == 536`, `kPersonCapacity == 768`). Relevant
fields (`PersonField`, `types.h:82-105`):

| Offset | Name | Meaning |
|---|---|---|
| `+0x02` | `kPfKind` | class byte (5/6/7 = citizen; `>=10` non-recruitable) |
| `+0x04` | `kPfId` | person id |
| `+0x08` | `kPfIsPlayer` | live-actor flag |
| `+0x5C` | `kPfRelationBase` | relation/**employer** id array, slot 0 = employer (`-1` = unbound) |
| `+0x60` | `kPfSuperiorId` | office-superior id |
| `+0x80` | `kPfReputation` | 5 reputation bytes (used in cost formula) |
| `+0x165` | `kPfProfession` | profession byte |
| `+0x166` / `+0x169` | `kPfOffice` / `kPfOffice2` | office seat bytes |
| `+0x1B1` | `kPfRecruited` | "already recruited" flag (excludes from candidate pool) |

The **employer relationship** is therefore the relation array at `+0x5C`. A worker
is "bound" to an employer when relation slot 0 holds that employer's id; `-1` means
free. `PersonFindEmploymentRelation` (`person_personnel2.cpp:108`, 0x58d95c) walks
the whole 768-slot array looking for any kind-6/7 (employer-class) record that
references `rec`'s id in its primary relation ids (`+92/+96/+100`,
`kPf2RelArray = 0x5C`) or its extended relation array (`+104..+120`, 5 dwords),
returning `0` (employer/relation exists) or `1` (orphaned). Kind 6/7 records
"are already employers" and short-circuit to `0` (`:109-111`).

`PersonFindActiveByEntity` (`person_personnel2.cpp:40`, 0x5920b0) finds the active
person bound to a scene entity (`+364` `kPf2EntityPtr`) whose kind is one of
`{1, 2, 6, 7}` — the employer/worker-class set.

### 2.2 Per-building worker slots (member array)

In the hire/fire coroutines, the building/master handler record (`HeRecord*`)
carries a **12-slot member array at `+176` (stride 4)** representing free recruit
slots; a slot value `> 0` means "a worker can be recruited here"
(`charaction_steps6.cpp:658-660`). Hiring decrements one slot (`:708`), firing
decrements the single slot at `+176` (`:776`). The cap on production staff is read
from AiPlayer fields `+561 + +562` (`meister_economy.h:118`).

### 2.3 Staff / mercenary book

The staff/mercenary book is `dword_11BC772` @ `0x11BC772`, **stride 45, 512
records** (`person_personnel2.h:52-62`). Only two columns are modeled:
`+0x00` = booked person id, `+0x13` = "active/valid" flag. It is consulted as an
overflow fallback by `PersonFindEmploymentRelation` only when the person table is
nearly full (`768 - liveCount < 32`, `person_personnel2.cpp:156`). The staff-book
**UI** rows are built/destroyed by `PersonnelBuildBookRow` (0x53b5d0) and
`PersonnelDestroyBookRowWidgets` (0x53ba4c) — widget plumbing, not economy
(`person_personnel2.cpp:294`, `:323`).

---

## 3. Wages / salaries

There are **two distinct wage systems** in the code:

1. **Office wages** — periodic pay to the holders of two office seats, computed
   from office rank and a finance-law rate, paid in a per-turn pass.
2. **Worker/employee wages** — per-worker amounts computed by job category and
   building base value, paid (and clawed back as severance) by the guild-master
   hire/fire coroutines.

### 3.1 Office wages — `AmtComputeWage` (0x57b480) — EXACT

`src/world/amt.cpp:122`, constants `src/world/amt.h:38-40`:

```text
kWageRankScale = 100.0f   // flt_6258AC
kWageBaseScale =  32.0f   // flt_6258B0

wage = trunc( officeRank * 100.0f * 32.0f * lawRate )
     = trunc( officeRank * 3200.0 * lawRate )
```

`lawRate` is the finance-law field (`Gesetz(14)`, modeled as `wageLawRate`,
default `0.10f` in `turn_economy.h:82`). `trunc` is round-toward-zero
(`AmtTrunc`, `amt.h:83`). With the defaults (rank, `lawRate = 0.10`), one office
seat pays `rank * 320` units per cycle.

`AmtComputeOfficeWages(rankA, rankB, lawRate, officeAccount, commit)`
(`amt.cpp:130`) computes both seats; a seat with `rank <= 0` yields `0`. When
`commit`, each non-zero wage is enqueued as a transfer **from `-1` (treasury/bank
sink) to `officeAccount`** via `AmtCommitTransfer` (`:142-146`) — i.e. the office
account is *credited* the wage; in the turn driver the player **treasury is
debited** by the same amount (`turn_economy.cpp:116-118`).

### 3.2 Office-wage cycle — `ProcessAllOfficeWages` (0x57b6bc)

`src/world/guild_election.cpp:280`. Iterates **all 768 seats**
(`kOfficeWageSeats == 768`, `guild_election.h:183`) and invokes the per-seat pay
hook (`VIBE_Amt_ComputeOfficeWages(seat, 1, ctx)`); returns the seat count. The
per-seat math is `AmtComputeOfficeWages` above. The full 768-seat sweep is the
actual driver; `turn_economy.cpp` only exercises the two-seat math directly.

### 3.3 Worker wages — `PersonnelComputeWageByCategory` (0x594d70) — EXACT

`src/sim/personnel.cpp:20`, constants `personnel.cpp:12-13`:

```cpp
constexpr float kWageMultLow  = 3.0f;  // flt_626B2C  — production/craft categories
constexpr float kWageMultHigh = 9.0f;  // flt_626B30  — administrative (cat 10/11/12)
```

Formula (`personnel.cpp:20-32`):

```text
cat  = MapActionToCategory(aiTypeByte)                 // hooked (g_categoryFn)
base = ComputeItemBaseValue(building, sActionByte, 0, arg)   // hooked (g_baseValueFn)
mult = (cat == 10 || cat == 11 || cat == 12) ? 9.0 : 3.0
wage = (float)(base * mult)
```

i.e. the wage is the **building's base item value × 3.0** for production/craft
jobs, or **× 9.0** for administrative categories 10/11/12. `sActionByte` is the
sign-extended action byte; `aiTypeByte` is the type-table byte the original reads
from `dword_13CE294 + 589*actionByte` (the AiPlayer/type-def table).

> The two leaves it depends on — `VIBE_Building_ComputeItemBaseValue` (0x58f328)
> and `VIBE_BuildingType_MapActionToCategory` (0x58a25c) — live in the building
> module and are **hooked** here (`personnel.h:19-23`). So the absolute wage
> magnitude is *not* fully determinable from these files: it is `3×` or `9×` an
> **UNKNOWN building base value**.

### 3.4 Worker wage payment — guild-master hire/fire coroutines

Per-worker wages are paid inside the **Einstellen (hire)** and **Entlassen
(fire)** guild-master coroutines (`charaction_steps6.cpp`). The wage is computed
via the `computeWage(cityIndex, classByte, subMethod)` hook
(`charaction_steps6.h:123-125`, = `PersonnelComputeWageByCategory`).

**Hire — `RunMeisterEinstellen` (0x4dd5b4)** (`charaction_steps6.cpp:633`):

- State 1: once the appointment time is due, compute `wage = computeWage(selfCity,
  classByte, -1)` (`:651`). **Affordability gate**: if
  `sumCurrencyHeld(cityIndex) < wage`, send message `5709` and bail (`:652-655`).
  Then require a free member slot (`+176[i] > 0`) (`:656-662`), queue the
  guard-target request, advance the clock +1s, go to state 2.
- State 3 (payment): recompute `wage`, decrement the member slot, and **pay the
  wage**: `queueRequest16(recruitId, cityPersonId, wage, 0)` (`:712-713`), then
  **add the wage to the family purse**: `*(fam + 76) += wage` (the `+19` dword,
  `:714-716`). Message `6072` is sent for market categories (kind 6/7).

**Fire — `RunMeisterEntlassen` (0x4ddac4)** (`charaction_steps6.cpp:726`):

- Structural twin of hire, single-slot (count-1) member scan. `wage =
  computeWage(selfCity, classByte, sub)` where `sub = +169 >> 24`
  (`He_SubMethodByte`, the severance sub-method, `:743-745`). Same affordability
  gate (message `5710` on failure, `:746-748`).
- State 2 (severance): if the member slot is occupied, recompute the severance
  `wage`, decrement the slot, and pay `queueRequest16(seqEntityId, cityPersonId,
  wage, 0)` (`:773-781`), then move the worker back to their origin and reschedule
  `+10..+19` minutes (`rand(10)+10`, `:796-799`).

So **per-worker pay and severance use the same category-based wage formula**;
hire credits the family purse by the wage, fire pays a severance computed with the
sub-method byte.

### 3.5 NPC salary adjustments — `RaiseSalaryCmd` / `LowerSalaryCmd` (0x575da0 / 0x5760a8)

`src/sim/npcaction6.cpp`. These are NPC-AI commands that move money as a function
of the actor's **total wealth** (the salary is *defined* as a fraction of wealth,
`npcaction6.h:13`):

```text
salary = (int)( totalWealth * (RandomModulo(n) + 1.0) * 0.01 )
```

- **Raise** (`:125`): gated by `RandomModulo(0x100) <= recordById[+130]` (the
  raise-willingness stat, `Per_RaiseStat`); `n = 3`; emits
  `queueRequest16(personId, -1, amount, currencyByte)` (`:132-145`).
- **Lower** (`:151`): gated by the **employed flag** `recordById[+44] != 0`
  (`Per_SalaryFlag`); `n = 4`; emits `queueRequest16(-1, personId, amount, ...)`
  with the **ids swapped** (`:158-168`).

The "has-salary / employed" flag is the dword at `recordById[+44]`
(`npcaction6.cpp:12,57`). `ShowPositionCmd` (`:199`) also gates on this employed
flag.

### Payment cadence

- **Office wages**: paid once per turn cycle. The per-turn pass order
  (`AmtRunTurnCycle`, `amt.cpp:218-231`, from `VIBE_GameTick_BeginPlayerRound`
  0x533188) is: `Production → Prosperity → BuildingTax → LoanRepayments →
  **OfficeWages** → UpdateOffices`. So office wages are deducted **every player
  round/turn**, after tax and loan interest (`turn_driver.cpp:114-115`,
  `game_day.cpp:36`). One "turn" here is a game-day economy tick.
- **Worker wages**: paid **event-driven**, *at the moment of hire* (and a severance
  at fire) by the Einstellen/Entlassen coroutines — these are appointment-clocked
  (`He_ApptTime`), not a fixed per-day payroll deduction in the translated code.

> **UNKNOWN / flagged.** A recurring *ongoing* per-day wage deduction for already-
> hired workers is **not present in these translated files** — the worker-wage
> code paths seen here charge at hire/fire time. The harvest/production "wage"
> records in `world/event4.cpp` / `world/event5.cpp` (`HarvestWageRun` 0x4f3b34,
> with `wage = (Dword(h,20)+Dword(h,24)) * base`, `event4.cpp:151`) credit
> *production* wages into a wage accumulator (`+440`) rather than debiting a
> standing payroll. Whether the engine also runs a periodic standing-payroll pass
> beyond office wages is **not determinable from the reconstructed code** and is
> flagged as unknown.

---

## 4. Employee skills / levels and their effect on cost & productivity

- **Recruitment cost** is sensitive to candidate **wealth**, recruiter→candidate
  **favorability**, candidate **cash**, and candidate **reputation** (5 bytes at
  `+0x80`), per §1.3 — *not* to a numeric "skill level". Office **rank**
  (`PersonComputeOfficeRank`) gates *eligibility* (proximity `< 5` in §1.1; the
  configurable `span` in §1.2) and drives the recruit-offer **loyalty bonus**
  (§1.5).
- **Wage** scales with the **building's base item value** and the **job category**
  multiplier (3× vs 9×, §3.3) — i.e. by the *role*, not an individual skill stat,
  in the translated wage formula.
- **Productivity:** these specific files do not contain a per-employee skill →
  output-rate formula. Production/output is computed in the building/event modules
  (e.g. `HarvestWageRun` / `event5.cpp` production bodies) and the Meister AI's
  production command (`meisterai.cpp`), and the staff *count* feeds the AI hire
  gate (§5). A direct "employee skill level multiplies units produced" formula is
  **not present in the assigned/grepped files** and is flagged as **UNKNOWN here**
  (it lives in the building/production module, outside this topic's scope).
- The AI **TrainStaff** decision (`TrainStaffDecision`, `meister_economy.cpp:199`)
  implies a training/skill progression exists: train iff `!busyFlag`,
  `RandomModulo(100) >= threshold`, no existing trainer, `trainerCount < 3`, and
  `budget >= 38400` (`kTrainStaffMinBudget`). The *effect* of training on output
  is not in these files (**UNKNOWN / out of scope**).

---

## 5. Hiring decision & turnover (AI)

### 5.1 `HireStaffDecision` (0x45c670 core)

`src/ai/meister_economy.cpp:184`. Decide whether the Meister AI hires a worker:

```text
hire  iff  staffCount < staffCap
      AND  ( staffCount == 0
             OR ( !busyFlag
                  AND wage <= budget
                  AND RandomModulo(0x48) >= 36 - 2*staffCount ) )
      AND  !hasHandler
```

(`:186-196`.) `staffCap` is `AiPlayer +561 + +562`; `busyFlag` is `+456 & 8`;
`hasHandler` means a worker handler already exists. The random draw
(`RandomModulo(0x48)`, i.e. mod 72) is only consumed when `staffCount > 0`. The
gate gets *harder* to satisfy as `staffCount` rises (the threshold `36 - 2*count`
falls, but the comparison is `>=` against a 0..71 roll). First worker
(`staffCount == 0`) is always hired if affordable slot/handler permit.

### 5.2 Turnover / firing

- **Firing** is the `RunMeisterEntlassen` coroutine (§3.4): it pays a **severance**
  (the category wage with the sub-method byte), moves the worker back to origin,
  decrements the member slot, and reschedules `rand(10)+10` minutes later
  (`charaction_steps6.cpp:726-799`).
- The "already recruited" flag `kPfRecruited` (`+0x1B1`) marks a person as taken
  so they drop out of the candidate pool (§1.2).
- `PersonCheckDebtRatioCritical` (`person_personnel2.cpp:217`, 0x591ff0) drives
  insolvency turnover: a person is "critically in debt" when `net = currency +
  reserve <= 0` and `|net| / (totalWealth - net) > threshold`, where
  `threshold = 0.07` for employer-class (kind 6/7) or `0.20` otherwise
  (`:227`, `dbl_626A6C` / `dbl_626A64`). This gates foreclosure/release paths
  elsewhere.

---

## 6. `personnel_recruit2` — note

`src/sim/personnel_recruit2.{cpp,h}` despite the name is **not labor-cost code**:
it contains the **avatar appearance pool** (`aNevmmaven` @ 0x62EF4C, 248-byte
stride × 32 slots; `Avatar_AllocSlot` 0x484598 scales 14 float columns by
`1/1000`; `Avatar_Save`/`Avatar_Load`) and the **animal species spawners**
(`Animal_SpawnDog/Cat/Sheep/Cow/Livestock`). It is included only for completeness;
no wage, recruitment cost, or roster logic lives here.

---

## Summary of the load-bearing numbers

| Quantity | Value / formula | Source |
|---|---|---|
| Recruitment fee | `round(24/cash·(a+b)) + rep-bumps`, clamped `[2,25]` | `recruit_cost.cpp:71` |
| └ wealth term `a` | `peak/wealth(recruiter)·5.0 + 1.0` | `recruit_cost.cpp:110` |
| └ favor term `b` | `(100.0 − favor)·0.1` | `recruit_cost.cpp:113` |
| └ reputation bump | `>210 → +2`, `>168 → +1` (×5 bytes) | `recruit_cost.cpp:127-134` |
| Proximity accept | `|officeRank Δ| < 5` | `recruit.cpp:35` |
| Office wage | `trunc(officeRank · 100 · 32 · lawRate)` | `amt.cpp:122` |
| Office-wage seats | 768 per cycle | `guild_election.cpp:281` |
| Worker wage | `buildingBaseValue · (cat∈{10,11,12} ? 9.0 : 3.0)` | `personnel.cpp:20` |
| NPC salary adjust | `wealth · (rand+1) · 0.01` | `npcaction6.cpp:138` |
| Hire-AI random gate | `RandomModulo(72) >= 36 − 2·staffCount` | `meister_economy.cpp:194` |
| Debt-critical threshold | `0.07` (kind 6/7) / `0.20` | `person_personnel2.cpp:227` |
| Pay-worker display clamp | `min(held, 4800) >> 5` | `personnel_gui.h:58` |
