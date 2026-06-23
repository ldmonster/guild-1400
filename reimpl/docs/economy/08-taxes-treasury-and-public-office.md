# 08 — Taxes, City Treasury & Public Office Economy (Amt)

> *"Amt"* is German for **public office / authority**. In *The Guild / Europa 1400*
> the "Amt" (the office/council subsystem) is what runs the per-turn city economy:
> it levies taxes, pays office-holders' wages, services loans, updates business
> prosperity, and enforces laws. This document covers that subsystem and the
> treasury/bank money-movement primitives it commits through.

Everything below is grounded in the actual reimplementation source. All money
mutations in the original `gilde.exe` route through the command/network lockstep
(`VIBE_Command_QueueRequest16` for transfers, `BeginDeltaPacket`/`AppendDeltaField`/
`QueueRequestState22` for object-field writes). The reimplementation reproduces
the **deterministic arithmetic byte-faithfully** and routes the commits through
installable hooks, so the logic is standalone-testable. Where the live game state
(tables, RNG) cannot be reproduced in isolation it is surfaced as a caller-supplied
view or hook — flagged below as such.

---

## Source files

| File | Role |
|------|------|
| `src/world/tax.cpp` / `tax.h` | Trade-tax formula core (`VIBE_Tax_CollectTradeIncome` 0x57aa88) |
| `src/world/amt.cpp` / `amt.h` | The Amt per-turn economic passes: all five tax kinds, wages, loan threshold, goods distribution, event buildings, the per-turn cycle order |
| `src/world/amt_economy2.cpp` / `amt_economy2.h` | Office-administration leaves: slot reset, guild-member highlight, office-info text, render offset, **building-rivalry payout**, richest-building cache |
| `src/world/amt_enforcement.cpp` / `amt_enforcement.h` | Law-violation / reputation-decay enforcement sweep (`VIBE_Amt_EnforceLawViolations` 0x57bf20) |
| `src/world/amt_slot_table.cpp` / `amt_slot_table.h` | Office placement slot-table search primitives |
| `src/world/office.cpp` / `office.h` | Office definition table, holder table, candidacy / promotion / succession rules |
| `src/world/office_prosperity.cpp` / `office_prosperity.h` | Per-turn business prosperity recompute (`VIBE_Amt_UpdateOfficeProsperity` 0x57b718) |
| `src/world/office_assign.cpp` / `office_assign.h` | Holder-table mutation rules (candidacy, assign, transfer, swap, release) |
| `src/world/treasury.cpp` / `treasury.h` | City/Guild treasury balance ops + exchange fee fields |
| `src/world/bank_treasury.cpp` / `bank_treasury.h` | City tax-rate seeding (`VIBE_Office_ComputeCityTaxRates` 0x5011dc) |
| `src/world/bank.h` | Bank loan facade over `AmtEvaluateLoan` |
| `src/world/law_types.h` | Law (`Gesetz`) and `OfficeHolder` record layouts |

---

## 1. Tax model

### 1.1 What is taxed

There are **five tax kinds**, enumerated in `amt.h:93`:

```cpp
enum class TaxKind { Trade, Guild, Building, Property, Staff };
```

Each kind reads its **rate from a city finance-law book record** (`Gesetz`, the
law table `unk_631E98`, 36-byte records — see `law_types.h:18-58`) and its taxable
**account base from the live AiPlayer columns**. The mapping (from
`OfficeTaxInput`, `amt.h:120-131`):

| Tax kind | Rate source (law id) | Taxable base (live column) |
|----------|----------------------|----------------------------|
| Trade    | `Gesetz(8)` threshold (+24) | `dword_12CEAAC[player]` (trade account worth) |
| Guild    | `Gesetz(9)` | `dword_12CEAAC[player]` |
| Building | `Gesetz(10)` | `dword_12CEAAC[player]` |
| Staff    | `Gesetz(11)` | `dword_12CEAB0 + dword_12CEAB4` (staff value sum) |
| Property | `Gesetz(12)` | `dword_12CEAB8[player]` (only taxed when `> 0`) |

> Note: the rate threshold is the law record's `threshold` field at byte +24
> (`LawRecord::threshold`, `law_types.h:51`), read as a percentage 0..100.

### 1.2 The core tax formula

Every one of the five kinds uses the **same arithmetic**. From `tax.cpp:11-18`
(`TaxComputeTradeIncome`) and identically `amt.cpp:40-47` (`TaxComputeIncome`):

```cpp
// gilde.exe 0x57aa88 inner arithmetic:
//   v18 = (float)rate; v20 = v18 * 0.01f; v19 = (float)account;
//   v21 = v20 * v19;   v23 = (v21 <= 0) ? 0 : v21;  result = trunc(v23)
i32 TaxComputeIncome(i32 rate, i32 account) {
    float fRate  = (float)rate;
    float scaled = fRate * kTaxRateScale;   // * 0.01f
    float fAcct  = (float)account;
    float raw    = scaled * fAcct;
    float clamped = (raw <= 0.0f) ? 0.0f : raw;
    return AmtTrunc((double)clamped);       // round-toward-zero truncation
}
```

So the tax amount is:

```
tax = trunc( max(0, rate * 0.01 * account) )
```

**The rate scale constant** is a single-precision `0.01f` literal shared by all
five tax kinds. Its exact IEEE-754 value is *not* exactly 0.01 — it is bit
`0x3C23D70A` (`amt.h:36-37`):

```cpp
constexpr float kTaxRateScale = 0.0099999998f; // flt_625884/8, 6258A0/A4/A8
```

`tax.h:19` defines the same constant as the rounded literal `0.01f` (same bits).
The multiplies are **single-precision** in the original (float locals), and the
reimplementation keeps the float chain so the truncated integer result is
bit-faithful (`tax.cpp:6-10`, `amt.cpp:35-39`).

**Truncation** (`AmtTrunc`, `amt.cpp:31`) is round-toward-zero, matching the
original's `VIBE_Coord_ConvertX` (0x5c6b08, `frndint` with RTZ):

```cpp
i32 AmtTrunc(double x) { return (i32)(long long)x; }
```

### 1.3 Collection: who pays and the gating rules

`TaxCollectIncome` (`amt.cpp:54-79`) wraps the formula with the original's gates:

```cpp
int TaxCollectIncome(TaxKind kind, i32 rate, int activeFinanceLaws, i32 account,
                     i32 payer, i32 recipient, int flags, TaxLine* out,
                     i32* cityAccumulator) {
    if (activeFinanceLaws >= 6)      // (1) finance-law book slot full -> bail
        return 0;
    i32 amount = TaxComputeIncome(rate, account);
    if (account > 0) {               // (2) only a positive account is taxed
        if (flags & 1) AmtCommitTransfer(payer, recipient, amount, 0); // (3a) enqueue transfer
        if (flags & 2) *cityAccumulator += amount;                     // (3b) city totals
    }
    return 1; // a line is "produced" (slot open) even if account <= 0
}
```

Three gates, all matching the originals:

1. **Finance-law book full** — if `activeFinanceLaws >= 6`, no tax is collected
   (`amt.cpp:59`, `tax.cpp:49`). The finance-law book has **6 active-law slots**;
   when full the whole pass short-circuits and returns 0. (`activeFinanceLaws` is
   the count of active laws in the kind's finance-law book slot, 0..6.)
2. **Positive account** — money only moves / accumulates when `account > 0`
   (`amt.cpp:65`). A non-positive account still "produces a line" but transfers
   nothing.
3. **Flags** — bit0 (`flags & 1`) enqueues the actual money command
   (`AmtCommitTransfer(payer, recipient, amount, 0)`); bit1 (`flags & 2`) adds the
   amount into the city tax accumulator. The originals enqueue
   `VIBE_Command_QueueRequest16`; **payer/recipient order varies per kind but the
   magnitude is always `amount`** (`amt.cpp:67-72`).

> The taxed party is the player/building account (`payer`); the recipient is the
> city/office side. The originals pass `-1` for the city/treasury sink endpoint
> (`amt.h:65`).

### 1.4 Aggregated per-office collection

`TaxCollectOfficeAllTaxes` (`amt.cpp:81-114`, `VIBE_Tax_CollectOfficeAllTaxes`
0x57b214) runs all kinds for one office building in a **fixed dispatch order**:

```
1. Guild  OR  Trade   (Guild if the office building is the "guild" kind, else Trade)
2. Building
3. Property            (only when propertyValue > 0)
4. Staff
return total of all collected amounts
```

The `isGuildOffice` flag mirrors the original's `VIBE_Amt_IsOfficeBuildingValid()`
check (`amt.h:121`). The property gate mirrors the original's
`dword_12CEAB8[player] > 0` test before the call (`amt.cpp:102-107`).

### 1.5 City tax-rate seeding (`bank_treasury`)

Tax *rates* themselves are periodically re-seeded by
`VIBE_Office_ComputeCityTaxRates` (0x5011dc, `bank_treasury.h:80-87`,
`ComputeCityTaxRates`). Once per "sync buildings and offices" pass
(`VIBE_Scene_SyncBuildingAndOffices` 0x501274), for each city:

```
base = RandomModulo(16)                       // one roll per pass
for (i = 0; i < count; ++i)
    rate = kCityTaxSeedRates[(base + i) % 16] // rotating offset, +1 per city
    out[i] = (i8)( rate - (officeDefLevel / 2) ) // biased down by half the office level
```

The 16-entry seed table (`bank_treasury.h:53-56`, `dword_4FFA70`):

```cpp
constexpr u8 kCityTaxSeedRates[16] = {
    0x18, 0x27, 0x33, 0x3F, 0x45, 0x4B, 0x12, 0x39,
    0x3F, 0x06, 0x0C, 0x2D, 0x33, 0x1E, 0x45, 0x4B,
};
```

The office-level bias `CityTaxHalveOfficeLevel` (`bank_treasury.h:62`) is a signed
round-toward-zero halving (`eax -= eax>>31; eax >>= 1`), and the final subtraction
and store are **8-bit** (`mov [edi-1], al`) so the result wraps to a signed byte
exactly as the original (`bank_treasury.h:64-69`). So higher-ranked city offices
*lower* the tax rate.

---

## 2. The city / public treasury

### 2.1 Money-movement primitives (`treasury.cpp`)

The City/Guild treasuries, the bank (*Geldleihe*), and the goods exchange/contor
are **UI contact-dispatch modules** in the original — there is no standalone
balance arithmetic beyond "transfer amount from payer to recipient" and "set fee
field" (`treasury.h:5-12`). The reimplementation models a tracked balance whose
mutations route through the **same Amt transfer hook** the tax/wage passes use.

`Treasury` (`treasury.h:29-33`) is `{ accountId, balance, currency }`. The three
operations (`treasury.cpp`):

```cpp
// Deposit: payer -> treasury. Negative/zero ignored (dialogs gate on a positive entry).
i32 TreasuryDeposit(Treasury& t, i32 payer, i32 amount) {        // treasury.cpp:15
    if (amount <= 0) return t.balance;
    AmtCommitTransfer(payer, t.accountId, amount, t.currency);
    t.balance += amount;
    return t.balance;
}

// Withdraw: treasury -> recipient, CLAMPED to the available balance.
i32 TreasuryWithdraw(Treasury& t, i32 recipient, i32 amount, i32* moved) { // treasury.cpp:25
    i32 take = amount;
    if (take > t.balance) take = t.balance;   // never draw past the held amount
    AmtCommitTransfer(t.accountId, recipient, take, t.currency);
    t.balance -= take;
    ...
}

// Transfer between two treasuries (city <-> guild): one command, both mirrors updated.
i32 TreasuryTransfer(Treasury& from, Treasury& to, i32 amount) {  // treasury.cpp:44
    i32 take = amount;
    if (take > from.balance) take = from.balance;
    AmtCommitTransfer(from.accountId, to.accountId, take, from.currency);
    from.balance -= take; to.balance += take;
    return take;
}
```

The `balance` is a **read-only mirror** — in the live game the object field
committed over the network is the source of truth; the mirror is updated only when
the commit succeeds (or no hook is installed, i.e. single-player) (`treasury.h:31`,
`30-33`).

### 2.2 Treasury income sources and expenditures

The treasury balance is moved by:

**Income (into the treasury):**
- **Taxes** — the five tax kinds (§1) committed with `flags & 1`, recipient = the
  city/office account (the originals pass `-1` for the sink).
- **Deposits** — manual deposits via `TreasuryDeposit` (the cash dialog gated on
  `dword_75BF38 == 1210` "OK" with a non-zero edit field — `treasury.cpp:13-14`).
- **Loan interest** charged to debtor buildings (§5).
- **Exchange fees** — the `rateFee` (+105) and `courierFee` (+109) fields the
  exchange dialog edits (§2.3); these are object fields, not direct balance moves.

**Expenditure (out of the treasury):**
- **Office-holder wages** (§4) — `AmtComputeOfficeWages` enqueues each wage as a
  transfer with `payer = -1` (the treasury) to the office account
  (`amt.cpp:142-147`).
- **Withdrawals** — manual withdrawal via `TreasuryWithdraw`, clamped to the
  balance.
- **Building-rivalry payouts** (§6) — `ComputeBuildingRivalryScore` enqueues
  payouts with `payer = -1` (`amt_economy2.cpp:343`).

> The exact set of treasury endpoints (which `accountId` is "the city" vs "the
> guild") is **live game state** and is supplied by the engine, not modeled here.
> The reimplementation only knows "transfer between these endpoint ids".

### 2.3 Exchange fee fields

`ExchangeSetFees` (`treasury.cpp:63-75`, `VIBE_Exchange_ShowFeesDialog` 0x51ca40)
commits two fee fields through the field-write hook on OK:

```cpp
// on OK: BeginDeltaPacket(obj, obj+1);
//        AppendDeltaField(4,1, &rateFee,    105);   // Wechselgebuehr
//        AppendDeltaField(4,1, &courierFee, 109);   // Kuriergebuehr
//        QueueRequestState22();
```

`ExchangeFees` (`treasury.h:58-62`) mirrors `rateFee` (object+105) and `courierFee`
(object+109). No arithmetic — pure field writes.

---

## 3. Public offices (Amt): what they are and how they work

### 3.1 The office definition table

The office catalogue is the baked 446-byte blob at `dword_62EC8E`
(`office.cpp:22-61`), **37 records × 12 bytes at a +2 byte skew**. Per record
(`office.cpp:11-13`):

```
+0 id   +1 reqCode(1..7)   +2 bookCat(1..9)   +3 cost   +4..7 flag   +8..11 textId
```

- **`reqCode` (1..7)** — the requirement code, == `OfficeGetCategoryByRank`
  (`office.cpp:133`). Selects rank requirements (§3.3).
- **`bookCat` (1..9)** — the book/category id, drives promotion-progression rules.
- **`flag` (dword[1])** — non-zero == "promotable pair head" (a two-holder office).
  Used by candidacy/assign (`office.h:39`, `office_assign.cpp`).

The table encodes a tiered office hierarchy: records 1..9 are the three lowest
book-categories (reqCode 1..3, costs 5/7/10), rising through reqCode 4..6 (costs
20/25/30/50/70/100 — note the byte +3 `cost` column at `office.cpp:33-50`) up to
record 0x1B (reqCode 9, cost 0x64=100). Record 36 carries the textId `"GOD\0"`
(`office.cpp:59`).

> The reimplementation does **not** attach human-readable office names; only the
> numeric `id`/`reqCode`/`bookCat`/`cost`/`textId` are recovered. The text strings
> live in the game's localized text arrays (deferred).

### 3.2 The office-holder table

`OfficeHolder` (`law_types.h:130-148`, `byte_B59848`) is a 24-byte record;
**30 active slots** (720 bytes) for the candidacy/assign scans, **37 slots**
(888 bytes) for the transfer/add-entry scans:

```cpp
struct OfficeHolder {
    u8  holder;     // +0   holder character id
    i32 city;       // +4   city / slot owner (-1 == vacant)
    u8  type;       // +8   office type
    i32 rank;       // +12  rank-level (< 4 to be assignable)
    u8  state;      // +16  state (3 == vacant/electable, 1 == filled, 4 == cleared)
    i32 secondary;  // +20  secondary holder/owner id (-1 == none)
};
```

A slot is **open for a new candidate** iff `city == -1 && state == 3 && rank < 4`
(`office.cpp:193-196`, `SlotIsOpen`).

### 3.3 Candidacy, promotion & succession rules

These are pure rules over the holder/def tables (`office.cpp`):

- **`OfficeCanRunForOffice`** (0x47e3b8, `office.cpp:200`) — true iff the person is
  valid, not already a candidate (`+360`), and a vacant matching-type holder slot
  exists.
- **`OfficeCanPromoteRank`** (0x47e0f8, `office.cpp:219`) — validates rank
  progression via the def `reqCode`/`bookCat`, and writes the **promotion cost**
  from the 7×7 float matrix `kPromotionCost` (`office.cpp:64-72`,
  `dword_62EBCC`). The cost values are negative reputation/favor deltas (e.g.
  `-2.5`, `-5.0`, `-10.0`) gating cross-book promotions; `+1.0` / `0.0` mark
  same-or-lower transitions.
- **`OfficeGetRankRequirements`** (0x47fb30, `office.cpp:261-288`) — by `reqCode`,
  yields **age / money / office-count requirements** to qualify for a rank:

  | reqCode | age | money | reqOffices |
  |---------|-----|-------|------------|
  | 1,2,3 | 16–28 | 32 000 – 192 000 | 1–2 |
  | 4,5   | 21–30 | 160 000 – 640 000 | 2–4 |
  | 6     | 24–30 | 640 000 – 1 600 000 | 3–5 |

- **`OfficeCollectSuccessorCandidates`** (0x47f858, `office.cpp:291`) — collects up
  to 6 successor person-ids whose held rank is the next in the office book.

### 3.4 Holder-table mutations (`office_assign.cpp`)

The mutation rules (`VIBE_Office_*` 0x47e1b8..0x47ee44) drive the slot state
machine: `OfficeApplyForCandidacy` (0x47e1b8), `OfficeAssignToCandidate` (0x47e4e0,
increments slot `rank`), `OfficeTransferHoldership` (0x47e870, the full seat
hand-over with `+358`/`+359`/`+361` person office-field bookkeeping),
`OfficeSwapHolders` (0x47ec64, **charges `cost` to the swapping person**:
`a->money -= req.cost`, `office_assign.cpp:446`), and
`OfficeReleaseCharacterHoldings` / `OfficeClearCharacterHoldings` (vacate on
death/retirement). All money/notify side-effects route through `OfficeCommandHook`
/ `OfficeNotifyHook` (`office_assign.cpp:51-77`).

### 3.5 Office placement slot table (`amt_slot_table.cpp`)

A separate **64-entry × 24-byte** slot table (`kAmtSlotCount = 64`,
`amt_slot_table.h:50`) used by the office overview/placement windows. Search
primitives are fixed-stride linear scans with the "occupied" test
`marker (+13) != 0xFF` (`amt_slot_table.h:53-54`): `AmtFindSlotByCoord`,
`AmtFindSlotByObjectId`, `AmtFindRecordByKey`, `AmtFindSlotAtPoint` (AABB
hit-test, half-extent `size>>1`), and `AmtFindFreePlacement` (two-stage footprint
clearance probe). `AmtGetOfficeType` (0x47ff14) resolves an office type to its
book/category via `OfficeDefBookCat` (`amt_slot_table.cpp:17-23`). These are pure
spatial/lookup helpers with **no economic effect**.

---

## 4. Salaries / wages paid to office holders

`AmtComputeWage` (`amt.cpp:122-128`, `VIBE_Amt_ComputeOfficeWages` 0x57b480):

```cpp
// wage = trunc( officeRank * 100.0f * 32.0f * lawRate )
i32 AmtComputeWage(int officeRank, float lawRate) {
    double v = (double)officeRank
             * (double)kWageRankScale   // 100.0f  (flt_6258AC)
             * (double)kWageBaseScale   //  32.0f  (flt_6258B0)
             * (double)lawRate;
    return AmtTrunc(v);
}
```

So the wage formula is:

```
wage = trunc( officeRank * 100 * 32 * lawRate )   =   trunc( officeRank * 3200 * lawRate )
```

The constants (`amt.h:39-40`): `kWageRankScale = 100.0f` (`flt_6258AC`,
`0x42C80000`), `kWageBaseScale = 32.0f` (`flt_6258B0`, `0x42000000`). `lawRate`
is the **`Gesetz(14)` rate field** (`amt.h:148`). The two float constants and the
float `lawRate` are promoted into a **double multiply** in the original; the
reimplementation matches with a double accumulate (`amt.cpp:118-121`).

**Per-office payment** — `AmtComputeOfficeWages` (`amt.cpp:130-149`) computes **two
seat wages** per office building (the two office-holder rank bytes at `+A76` and
`+A79`, `amt.h:144-146`). A seat with rank 0 / no holder yields a zero wage
(`amt.cpp:135-136`). When committing, each non-zero wage is enqueued as a transfer
**from the treasury (`payer = -1`) to the office account**:

```cpp
if (wp.wageA) AmtCommitTransfer(-1, officeAccount, wp.wageA, 0);  // amt.cpp:143-146
if (wp.wageB) AmtCommitTransfer(-1, officeAccount, wp.wageB, 0);
```

This is driven each turn by `VIBE_Amt_ProcessAllOfficeWages` (0x57b6bc) in the
`OfficeWages` pass (§7).

---

## 5. Loan repayments (the bank pass)

`AmtEvaluateLoan` (`amt.cpp:158-174`, `VIBE_Amt_ProcessLoanRepayments` 0x57b304):

```cpp
LoanDecision AmtEvaluateLoan(int lawSlot, u8 currency, i32 heldCurrency, bool hasLender) {
    i32 base = AmtMoneyMultiplyByRate(4 - lawSlot + 10, currency); // per-turn interest base
    d.perTurnInterest = base;
    d.overdraftLimit  = 2 * base;
    if (heldCurrency < 0) {                       // the holder is in debt
        i32 debt = abs(heldCurrency);
        if (!hasLender && debt > d.overdraftLimit) d.foreclose = true;  // beyond limit, no lender
        else                                       d.charge    = true;  // charge interest
    }
    return d;
}
```

Formula:

```
interestBase   = MultiplyByRate(4 - lawSlot + 10, currency)
overdraftLimit = 2 * interestBase
```

`AmtMoneyMultiplyByRate` (`amt.cpp:23-27`, `VIBE_Money_MultiplyByRate` 0x58f19c)
scales by a per-city currency multiplier (`dword_649A88[dword_13CD6F2[189*cid] >> 16]`);
in isolation it is the identity (multiplier 1) and a settable hook
(`amt.h:70-77`). A debtor building whose `|debt|` exceeds the overdraft limit with
**no private lender** is **foreclosed**; otherwise `interestBase` is charged each
turn (`bank.h:30-46`, `BankApplyLoanStep` enqueues the charge: payer =
`ownerAccount`, recipient = `-1` = the bank/office sink).

---

## 6. The Amt economy-2 leaves (`amt_economy2.cpp`)

These are the office-administration leaves driven by the office panels and the
game tick. The economically significant one is **building rivalry**:

### 6.1 Building rivalry "raid" payout

`ComputeBuildingRivalryScore` (`amt_economy2.cpp:296-360`,
`VIBE_Amt_ComputeBuildingRivalryScore` 0x57bc60). The acting building rolls
against every *foreign* active building (id != -1, type ∉ {6,7,8}, type < 10,
different city) and, on a won roll, queues a **money payout + a reputation delta**:

```
supply = self.workstationSum * 0.0125 + 1.0
base   = rival.workForce * 0.25
if rival.cityRegion == self.cityRegion:  repDelta = +0.05; base *= supply
else:
    f = (self.cityRegion==2) ? 0.8 : (rival.cityRegion==self.cityRegion2 ? 0.7 : 0.6)
    base = f * supply * base;  repDelta = -0.05
if RandRoll() * 2.0 <= base:                          // roll won
    wf = max(0, rival.wealth / self.wealth * 160)
    payout = trunc( (wf + 160) * base )
    QueueRequest16(-1, rival.id, payout, currency)    // payer = -1 (treasury/sink)
    if 0.05 < rival.reputation < 0.95: QueueArgs26(rival.id, 460, repDelta)
```

Constants (`amt_economy2.h:48-62`): `kRivalrySupplyScale = 0.0125` (`flt_6258C4`),
`kRivalryDistanceScale = 0.25` (`flt_6258C8`), the city-distance factors
`0.6/0.7/0.8` (`dbl_6258CC/D4/DC`), `kRivalryRollScale = 2.0`, `kRivalryWealthScale
= 160.0`, `kRivalryPayoutScale = 10.0`, and the `±0.05` reputation deltas
(field 460, gated by the 0.05/0.95 reputation band).

### 6.2 Richest-building cache

`FindNextActiveBuilding` (`amt_economy2.cpp:226-269`, 0x57bb50) is a generation-
validated cache over the 768-record building table that returns the **max-wealth
active production building** (type < 10). When no building is found it returns a
default wealth of `3200` (`amt_economy2.cpp:248-249`). Its output feeds the
prosperity pass's `cityMax` reference (§8).

### 6.3 Other leaves (non-economic)

`TriggerOfficeNotice` (0x483570, re-post a slot vacant), `ResetGuildSlots`
(0x480b50, two-phase slot rebuild), `HighlightGuildMembers` (0x48311c),
`BuildOfficeInfoText` (0x483414, promotion-list message), `ComputeOfficeRenderOffset`
(0x4834e4, office-marker x-offset), `LookupSelectionInfoText` (0x507b18),
`OpenOfficeWindow` (0x5546a0). These are slot/UI/render plumbing with no money
movement.

---

## 7. Law-violation enforcement (`amt_enforcement.cpp`)

`EnforceRun` (`amt_enforcement.cpp:62-141`, `VIBE_Amt_EnforceLawViolations`
0x57bf20) is the enforcement arm of the Amt. It runs **once every fourth Amt turn**
(`gameTurn % 4 == 0`, `amt_enforcement.cpp:67`), reads **law #2** (the
"Strafgesetz" trigger), and branches on its threshold:

**Qualifying predicate** (`EnforceQualifies`, `amt_enforcement.cpp:50-60`): person
present, `0 < profClass < 10`, `officeType ∉ {6,7,8}` (guard/office-holder classes
are exempt), and `reputationWord >= 12`.

**Branch A — reputation-decay sweep** (`law.threshold == 2`,
`amt_enforcement.cpp:80-101`): each qualifying person for whom
`RandFloatScaled() * 0.2 > reputation(+460)` gets:
- a random reputation penalty `RandFloatScaled() * 0.5 + 0.25` queued as command
  460,
- a flag commit (`flag12 == 0`) at record +12.

**Branch B — law-violation sweep** (`threshold != 2`,
`amt_enforcement.cpp:102-139`): the per-person guard byte gate
(`(guardHighByte >> 24) == guardSel`) selects the population, then each is run
through `GesetzEvaluateViolation(law 2, value, victim=-1, perp, extra)` (which on
a caught violation queues a crime-creation command, i.e. an arrest). A reputation
`> 0.1` is **clamped down by `-0.1`** (command 460).

Constants (`amt_enforcement.h:35-48`): roll scale `0.2` (`flt_6258F0`), penalty
`* 0.5 + 0.25` (`flt_6258F4/F8`), clamp level `0.1` / clamp value `-0.1`, minimum
reputation word `0x0C`.

> Enforcement does **not** directly move money — its effects are reputation
> deltas, flag commits, and crime records. But it is wired into the tax pass:
> `RunBuildingTaxPass` internally calls `EnforceLawViolations` (`amt.h:215`).

---

## 8. Office / business prosperity (`office_prosperity.cpp`)

`ProsperityUpdateBuilding` (`office_prosperity.cpp:62-90`,
`VIBE_Amt_UpdateOfficeProsperity` 0x57b718) recomputes a **prosperity score** for
each active business building once per Amt turn from the owner's wealth, the
building's three production-room values, and a city-wide reference max:

```cpp
// averaged wealth (integer divide):
avg = (ownerWealth + room0 + room1 + room2) / (count(room != 0) + 1)   // office_prosperity.cpp:24-33

// prosperity score (float ratio, double blend):
ratioF = (float)avg / (float)ownerWealth;  ratio = min(ratioF, 1.0)
score  = ratio * 0.5  +  0.5 * (1.0 - ownerWealth / cityMax)            // office_prosperity.cpp:40-60
```

The blend weight `0.5` (`kProsperityBlend`, `dbl_6258B4`) is used twice
(`office_prosperity.h:24`). The score is the value the building's prosperity field
`+480` trends toward. Each building's update emits three commits in order
(`office_prosperity.cpp:78-88`):

1. **`WealthField`** — write the averaged wealth to field `+476`.
2. **`ProsperityDelta`** — `-(currentField480 - score)` queued to field `+480`
   (`QueueRequestArgs26`).
3. **`AiDecayDelta`** — AI-method field `+180` decays by `×0.95`
   (`kProsperityAiDecay`, `dbl_6258BC`): `-(field180 - field180 * 0.95)`.

### How holding office affects income / prosperity

The prosperity score is a function of **owner wealth vs the city's richest
building** (`cityMax`, supplied by `FindNextActiveBuilding`, §6.2): the second term
`0.5 * (1 - ownerWealth/cityMax)` *rewards being poorer* relative to the richest
rival, while the first term `ratio*0.5` rewards a building whose room throughput
keeps pace with the owner's wealth. A higher score raises the `+480` field that
drives the business's standing. **Office rank feeds income directly through the
wage formula** (§4: `wage ∝ officeRank`) — holding (and promoting in) an office
linearly increases the wage paid out each turn.

---

## 9. The per-turn Amt cycle

`AmtRunTurnCycle` (`amt.cpp:218-231`) documents the fixed heartbeat order from
`VIBE_GameTick_BeginPlayerRound` (0x533188). Each turn the Amt runs
(`amt.cpp:210-216`, `amt.h:191-209`):

| # | Pass | What it does |
|---|------|--------------|
| 1 | **Production** | `RunProductionPass` (0x57d448) — recompute production; internally calls `RunGoodsDistributionPass` (0x57dd84) |
| 2 | **Prosperity** | `UpdateOfficeProsperity` (0x57b718) — §8 |
| 3 | **BuildingTax** | `RunBuildingTaxPass` (0x57b9ac, flags=3) — §1; internally calls `EnforceLawViolations` (§7) |
| 4 | **LoanRepayments** | `ProcessLoanRepayments` (0x57b304) — §5 |
| 5 | **OfficeWages** | `ProcessAllOfficeWages` (0x57b6bc) — §4 |
| 6 | **UpdateOffices** | office slot bookkeeping |

`RunProductionPass` internally calls `RunGoodsDistributionPass`, and
`RunBuildingTaxPass` internally calls `EnforceLawViolations`; those nested passes
are part of their parent's translation, not separate top-level slots
(`amt.cpp:210-216`).

### Goods distribution & event buildings (auxiliary)

- `AmtGoodsDistributionSpawnCount` (`amt.cpp:186-195`, 0x57dd84): when active
  production buildings are below threshold (~652.8), spawn replacements to top the
  city toward 40: `< 30 active → (40-active)/2`; `< 40 → rand(2)+1`; else 0.
- `AmtEventBuildingQualifies` (`amt.cpp:202-207`, 0x57e2a4): a candidate building
  with `occupiedRooms` (3..7) production rooms qualifies when a `d256+1` roll is
  `< kEventProbTable[occupiedRooms-3]`, table `{96,75,40,23,9,0}` (`amt.h:53`).

---

## 10. Unknowns / deferred (flagged — not invented)

- **Treasury endpoint identity** — which `accountId` is "the city treasury" vs "the
  guild treasury" is live game state set by the engine; only the transfer
  endpoints are modeled (`treasury.h:30-33`, §2.2).
- **Per-kind payer/recipient direction** — `TaxCollectIncome` notes the
  payer/recipient order "varies per kind" but only the *magnitude* (`amount`) is
  recovered byte-faithfully; the precise endpoint mapping per kind is the original
  command-queue caller's, not reproduced here (`amt.cpp:67-69`).
- **`VIBE_Money_MultiplyByRate` multiplier** — the per-city currency multiplier
  table is live state; modeled as a settable scalar (default 1)
  (`amt.h:70-77`).
- **Office text/name strings** — only numeric ids/textIds are recovered; the
  localized names live in the game's text arrays (deferred, `office.cpp:5-13`).
- **`OfficeCanRunForOffice` partner check** — the optional second-holder validation
  is modeled as the common (no-partner) success case; a partner-required slot needs
  the live council code's partner entry (`office.cpp:209-215`).
- **Dialog/window UI** — all of the contact-loop / window code
  (`VIBE_*_RunContactLoop`, `VIBE_Amt_OpenOfficeWindow`, etc.) is deferred; only the
  rules cores are recovered (`treasury.h:18-20`, `amt_economy2.cpp:400-413`).
- **Production-pass internals** — `RunProductionPass` body beyond the goods-
  distribution math is not detailed in these files.

---

## Summary of key formulas

```
tax        = trunc( max(0, rate * 0.01 * account) )            // all 5 kinds
wage       = trunc( officeRank * 3200 * lawRate )              // officeRank * 100 * 32 * lawRate
loanBase   = MultiplyByRate(4 - lawSlot + 10, currency)        // overdraftLimit = 2 * loanBase
cityTax[i] = (i8)( kCityTaxSeedRates[(base+i)%16] - officeLevel/2 )
prosperity = min(avg/ownerWealth, 1)*0.5 + 0.5*(1 - ownerWealth/cityMax)
             where avg = (ownerWealth + Σrooms) / (nonzeroRooms + 1)
```

Gates: tax collected only when the finance-law book slot is **not full**
(`activeFinanceLaws < 6`) and **`account > 0`**.
