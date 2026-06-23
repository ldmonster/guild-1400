# 07 — Banking, Loans & Player Finance

Reconstruction of the money/banking subsystem of *The Guild / Europa 1400* (`gilde.exe`),
as recovered in the C++ reimplementation. Everything below is grounded in the
reimplementation source; original binary addresses (`0x…`) and symbol names
(`VIBE_*`) are carried through from the reconstruction comments. Where the
original behaviour is genuinely unknown or only modeled (not byte-recovered), it
is **flagged explicitly**.

## Source files

| Area | File |
|------|------|
| Currency display formatting | `src/world/money_format.h`, `src/world/money_format.cpp` |
| Bank loan-interest facade (per-turn) | `src/world/bank.h`, `src/world/bank.cpp` |
| Bank loan **offer** generator + grant commit | `src/world/bank_loan.h`, `src/world/bank_loan.cpp` |
| City tax-rate seeding (NOT a deposit treasury — see note) | `src/world/bank_treasury.h`, `src/world/bank_treasury.cpp` |
| Treasury / bank deposit-withdraw balance model | `src/world/treasury.h`, `src/world/treasury.cpp` |
| Player / person money + civic-fee math | `src/world/player_finance.h`, `src/world/player_finance.cpp` |
| Loan-interest core + per-turn cycle | `src/world/amt.h`, `src/world/amt.cpp` |
| Per-day economy turn (interest cadence driver) | `src/play/turn_economy.h`, `src/play/turn_economy.cpp` |
| Bank/loan vertical slice (command wiring) | `src/play/slice_bank.h`, `src/play/slice_bank.cpp` |
| Bank/loan dialog (UI flow, confirms mechanics) | `src/play/dialog_bank.cpp` |
| Cash-on-hand accessor | `src/sim/person.h` (`PersonGetCashAmount`) |

---

## 1. Currency model

### 1.1 Denomination / unit

The in-game currency is the **Gulden**. The game has no sub-denomination coinage
in the arithmetic: amounts are stored and moved as plain **signed integers**
(`i32`). Negative integers represent debt (see `heldCurrency` below). The only
"denomination" concept is the per-city **currency rate scalar** used to convert a
raw stored amount into display units.

The currency glyph rendered after a formatted amount is font code-point **17
(`0x11`)** — `src/world/money_format.h:30`:

```cpp
constexpr char kCurrencyGlyph = '\x11';   // the Gulden glyph the formatter appends
```

### 1.2 The per-currency rate scalar

`gilde.exe 0x58f19c — VIBE_Money_MultiplyByRate` multiplies an amount by a
per-city currency multiplier drawn from a table chain
(`dword_649A88[dword_13CD6F2[189*currencyId] >> 16]`). The reimplementation models
this multiplier as a settable scalar (default **1**, i.e. identity) so the
arithmetic is testable in isolation — `src/world/amt.h:70-77`:

```cpp
// amount * dword_649A88[dword_13CD6F2[189*currencyId] >> 16]
using AmtRateHook = i32 (*)(i32 amount, u8 currencyId);
i32 AmtMoneyMultiplyByRate(i32 amount, u8 currencyId);
```

> **Flagged / modeled, not recovered:** the actual contents of the per-city
> currency table (`dword_649A88` / `dword_13CD6F2`) are not reconstructed; only
> the indexing formula is. In the reimplementation the multiplier defaults to 1,
> so all loan-interest and money-format figures below assume rate == 1 unless a
> rate hook is installed.

### 1.3 Display formatting — `MoneyFormatWithSeparators`

`gilde.exe 0x58f798 — VIBE_Money_FormatWithSeparators`
(`src/world/money_format.cpp:67`). The formatter:

1. Takes `|amount|`, divides by the per-currency `rate`, then **rounds half-up**
   (add `0.5`, then truncate toward zero):

   ```cpp
   // round-half-up of |amount| / rate
   v24 = trunc( (double)|amount| / rate + 0.5 );   // kMoneyFormatRoundBias == 0.5
   ```

   (`src/world/money_format.cpp:73-78`; bias constant `kMoneyFormatRoundBias = 0.5`,
   from `dbl_6269BC`, `src/world/money_format.h:33`.)

2. Renders the rounded magnitude with a `'.'` thousands separator every three
   digits, **back-to-front**, and appends the currency glyph. Negative amounts get
   a leading `'-'`. A rounded magnitude of **0 always renders `"0"` + glyph with no
   sign** (`src/world/money_format.cpp:81-86`).

The grouping core is `MoneyGroupThousands` (`src/world/money_format.cpp:26`).
Numbers below 1000 are printed bare (no separator); at/above 1000 the output
length is:

```cpp
// flt_6269C4 == 0.333333343f == 1/3
outLen = trunc( len + (len - 1) * (1/3) );
```

(`src/world/money_format.cpp:37-39`.) Separators are inserted on the
`(v8 + 1) % 3 == 0` cadence, exactly mirroring the original loop.

**Example:** `12345` with rate 1 renders as `12.345\x11`.

---

## 2. The bank

### 2.1 Bank as a contact-dispatch module (no standalone balance arithmetic)

The bank ("Geldleihe") in the original is a **UI contact-dispatch loop**, not an
arithmetic core. `VIBE_Bank_RunContactDispatchLoop (0x51da04)` fans out to the
loan / exchange / courier dialogs and *carries no standalone arithmetic of its
own* (`src/world/bank.h:4-7`, `src/world/treasury.h:6-12`). Per the treasury
header:

> "There is no standalone balance arithmetic in the binary beyond 'transfer
> amount from payer to recipient' and 'set fee field'."
> — `src/world/treasury.h:10-12`

Every balance change commits through the **command lockstep**
(`VIBE_Command_QueueRequest16` for money transfers). The reimplementation models
balances as a tracked `Treasury` struct whose mutations route through the shared
`Amt` transfer hook so the command path stays observable.

### 2.2 Deposits & withdrawals — `src/world/treasury.cpp`

The City/Guild treasuries and the bank share one balance abstraction:

```cpp
struct Treasury {
    i32 accountId = -1; // transfer endpoint object/account id
    i32 balance = 0;    // mirrored cash (the command is the source of truth)
    int currency = 0;
};
```

(`src/world/treasury.h:29-33`.)

- **`TreasuryDeposit(t, payer, amount)`** — `src/world/treasury.cpp:15`. Ignores
  `amount <= 0` (the dialogs gate the "OK" action `dword_75BF38 == 1210` on a
  positive entry), enqueues a transfer `payer -> t.accountId`, then mirrors
  `t.balance += amount`.
- **`TreasuryWithdraw(t, recipient, amount, moved)`** — `src/world/treasury.cpp:25`.
  **Clamps the withdrawal to the available balance** (`take = min(amount, balance)`;
  the cash dialogs never let you draw past the held amount), transfers
  `t.accountId -> recipient`, mirrors `balance -= take`.
- **`TreasuryTransfer(from, to, amount)`** — `src/world/treasury.cpp:44`. One
  command, clamps to `from`'s balance, mirrors both sides.

> **No deposit interest:** there is **no code path that pays interest on
> deposits**. Interest in this game is charged on *debt* only (Section 4). A
> balance left in a treasury does not grow.

### 2.3 Exchange fees (bank object fields)

`gilde.exe 0x51ca40 — VIBE_Exchange_ShowFeesDialog` lets the player edit two fee
fields on the bank object and commits them via a delta packet
(`src/world/treasury.cpp:63`):

- `object+105` — rate-exchange fee (*Wechselgebühr*)
- `object+109` — courier fee (*Kuriergebühr*)

```cpp
struct ExchangeFees { i32 objectId = -1; i32 rateFee = 0; i32 courierFee = 0; };
```

These are object fields, not formula-driven; `ExchangeSetFees` writes both via the
field-write hook (`src/world/treasury.cpp:63-75`).

### 2.4 ⚠️ Note on `bank_treasury.cpp`

Despite its name, **`src/world/bank_treasury.{h,cpp}` is NOT a bank deposit
treasury.** It is `VIBE_Office_ComputeCityTaxRates (0x5011dc)` — the deterministic
**city tax-rate seeding** pass (`src/world/bank_treasury.h:1-3`). It draws a tax
byte per city from a 16-entry seed table at a rotating RNG offset, biased down by
half the city's office level. It is documented here only to disambiguate the
filename; the actual rate table is:

```cpp
// dword_4FFA70, the 16 seed rates
kCityTaxSeedRates[16] = { 0x18,0x27,0x33,0x3F, 0x45,0x4B,0x12,0x39,
                          0x3F,0x06,0x0C,0x2D, 0x33,0x1E,0x45,0x4B };
```

(`src/world/bank_treasury.h:53-56`.) Per-city rate:
`(i8)( kCityTaxSeedRates[(base+index) % 16] - officeLevel/2 )`, with `base =
RandomModulo(16)` once per pass (`src/world/bank_treasury.cpp:23-30, 58-75`). This
belongs to the **tax** topic, not banking; cross-referenced for completeness only.

---

## 3. Loans — offers, grant, repayment, default

There are two distinct sides to the loan system, in two files:

- **Offer side** (the lender proposes loans): `bank_loan.{h,cpp}` →
  `VIBE_Command_EvaluatePurchaseAction (0x591990)` mode 3 and
  `VIBE_Credit_ConfirmLoanRequest (0x51a100)`.
- **Per-turn interest side** (the bank dunning a debtor): `bank.{h,cpp}` /
  `amt.{h,cpp}` → `VIBE_Amt_ProcessLoanRepayments (0x57b304)`.

### 3.1 Lender grant capacity — how much can be lent

`gilde.exe 0x591990 — VIBE_Command_EvaluatePurchaseAction (mode 3 == loan)`.
A lender's total grant capacity is **20 % of the lender's wealth, capped at
64 000** (`src/world/bank_loan.cpp:27`):

```cpp
// cap = wealth * 0.2;  if (cap >= 64000) cap = 64000;  then trunc to int
constexpr double kWealthCapFactor = 0.2;       // dbl_626A1C
constexpr float  kWealthCapCeil   = 64000.0f;  // flt_626A24

i32 LoanGrantCapacity(i32 lenderWealth);
```

(`src/world/bank_loan.h:35-36, 62`; `src/world/bank_loan.cpp:27-33`.) The capacity
is only granted when **`gameDay < 6`** and the lender is **not a guard/criminal
class** — those gates are supplied by the caller (`src/world/bank_loan.h:60`).

### 3.2 Per-offer amount

`VIBE_Command_EvaluatePurchaseAction` generates up to *N* offers (default
`count = 3`, `src/world/bank_loan.h:84`). Each offer is an
`(amount, term, interest)` triple (`src/world/bank_loan.h:67-71`).

The **offer base** is `min(wealth*factor, prevBase*0.5)`, truncated
(`src/world/bank_loan.cpp:38-43`):

```cpp
v41 = wealth * relationFactor(tier);   // 0.3 / 0.45 / 0.6
v40 = prevBase * 0.5;                  // kBaseScaleHalf
base = trunc( min(v41, v40) );
```

The **relation-tier factor** (`src/world/bank_loan.h:46-49`):

| Tier | Factor | Constant |
|------|--------|----------|
| `Default` (no relation) | **0.30** | `kFactorDefault` |
| `Mid` (`DispatchByType(11) & 2`) | **0.45** | `kFactorMid` |
| `High` (`DispatchByType(15) & 2`) | **0.60** | `kFactorHigh` |

The **per-offer amount** then applies an RNG jitter and an index scaling
(`src/world/bank_loan.cpp:71-84`):

```cpp
jitter = RandNext() % 30;                       // 0..29
amount = trunc( (1 - jitter*0.01) * base );     // kJitterStep == 0.01
if (i == 0)      amount = trunc(amount * 0.25);  // first offer  *0.25
else if (i == 1) amount = trunc(amount * 0.5);   // second offer *0.5
// offers 2+ : *1
```

So offer 0 is the smallest (¼ base), offer 1 is half, the rest full base, each
reduced by up to 29 % random jitter.

### 3.3 Loan term

`term = (RandNext() % 6) + 2` → **2..7 months** (`src/world/bank_loan.cpp:87`,
`src/world/bank_loan.h:69`).

### 3.4 Per-offer interest

`gilde.exe 0x591990 @591dc6` (`src/world/bank_loan.cpp:52-64`). The interest scales
linearly with the term between a 0.7× and 1.5× band of the lender's per-lender
rate field (`*(lender+101)`):

```cpp
f    = lenderRateField;                          // field101
base = f * 0.7;                                  // kRateLow  (dbl_626A4C)
span = f * 1.5 - base;                           // kRateHigh (dbl_626A54) minus base
rate = base + term * (1/6) * span;               // kTermSixth == 0.16666667
rate = trunc(rate);
// if the lender is a bank type (*(lender+2) in {6,7}):
if (bankType) rate -= trunc( (fav + (-50.0)) * 0.01 * 5.0 );   // fav adjust
```

Constants: `kRateLow=0.7`, `kRateHigh=1.5`, `kTermSixth=1/6`, the favourability
adjust uses `kFavBias=-50.0`, `kJitterStep=0.01`, `kFavScale=5.0`
(`src/world/bank_loan.h:38-44`). For a bank-type lender, higher borrower
favourability (`fav > 50`) *lowers* the interest.

### 3.5 Granting a loan — `CreditConfirmLoanRequest`

`gilde.exe 0x51a100 — VIBE_Credit_ConfirmLoanRequest` (`src/world/bank_loan.cpp:107`).
On accept the loan principal is paid out and added to the borrower family's debt:

```cpp
// if (accept && CheckResourceAmount(amount) && ShowMessageBox(1)):
EnqueueCmd15(lender, borrower, amount, currency);   // pay out principal (opcode 15)
familyDebt += amount;                                // borrower family +72 (dword +18)
```

Three guards gate the grant (`src/world/bank_loan.cpp:111`): `accept`
(player clicked yes), `resourceOk` (funds available), `confirmed` (message box
confirmed). The debt accumulator is the family-table dword at offset **+72**
(`*((_DWORD*)family + 18) += amount`, `src/world/bank_loan.h:118-126`,
`src/play/slice_bank.h:22-26`).

### 3.6 Loan command wiring (opcode 15)

The grant emits an **opcode-15** command packet
(`VIBE_Command_EnqueueCmd15 @0x494604`). Packet layout
(`src/play/slice_bank.h:85-91`):

| Field | Offset | Meaning |
|-------|--------|---------|
| opcode | `bytes[0]` | 15 (`kLoanCmdOpcode`) |
| lender id (a1) | `+0x10` | `-1` == bank sink, `-2` == repay marker |
| borrower id (a2) | `+0x14` | `*(req+176)` |
| player (a4) | `+0x1C` byte | acting player slot |
| amount (a3) | `+0x1D` dword | loan principal `*(req+180)` |

The slice applies the net effect on the borrower's **Person** record
(`src/play/slice_bank.cpp:88-105`):

- **Take:** `cash += amount` (`Person +0x0A`, clamped ≥0), `debt += amount`
  (folded accumulator `Person +0x2C`), `familyWealth += amount` (optional hook,
  the literal binary write at `family +72`).
- **Repay:** the exact inverse.

> **Flagged — folding gap:** the binary's *literal* principal write is
> `family.+18 += amount` into the **unfolded family table** (`word_13C3110`),
> which is **not** part of the determinism-folded world. The slice therefore
> applies the deterministic effect to the borrower's **Person** record (cash
> `+0x0A`, a folded debt accumulator `+0x2C`) and offers the literal family write
> as an optional installable hook (`src/play/slice_bank.h:43-54`,
> `src/play/slice_bank.cpp:36-49`).

### 3.7 Per-turn interest & default (foreclosure) — `AmtEvaluateLoan`

`gilde.exe 0x57b304 — VIBE_Amt_ProcessLoanRepayments`, core in
`AmtEvaluateLoan` (`src/world/amt.cpp:158`). Each turn, the per-turn interest base
and overdraft limit are:

```cpp
base           = VIBE_Money_MultiplyByRate(4 - lawSlot + 10, currency);  // perTurnInterest
overdraftLimit = 2 * base;
```

(`src/world/amt.cpp:161-163`; same formula surfaced as `BankInterestBase` in
`src/world/bank.cpp:10-13`.) With the identity rate hook and law-slot 2, the base
is `4 - 2 + 10 = 12` and the overdraft limit is `24`
(`src/play/turn_economy.h:83-88`).

The decision (`src/world/amt.cpp:165-172`):

```cpp
if (heldCurrency < 0) {            // the account is in debt
    debt = -heldCurrency;
    if (!hasLender && debt > overdraftLimit)
        foreclose = true;          // beyond limit, no private lender -> FORECLOSE
    else
        charge = true;             // otherwise charge `base` interest this turn
}
```

So **default consequence**: an account whose absolute debt exceeds **`2 × base`**
*and* which has no private lender backing it is **foreclosed**
(`src/world/bank.h:8-12`). Otherwise the per-turn interest `base` is charged. The
foreclosure itself is enacted by a higher-level command
(`VIBE_Amt_ProcessLoanRepayments` uses `Pair33` to foreclose,
`QueueRequestCoord27` to garnish wages); `AmtEvaluateLoan` only produces the
decision (`src/world/bank.h:36-42`, `src/play/slice_bank.h:33-36`).

The `lawSlot` is the active **finance-law slot** (the loan-interest law); a
*higher* slot lowers the base (`4 - lawSlot + 10`).

> **Note on "term":** the offer carries a `term` in months (Section 3.3) used in
> the *interest* formula, but the recovered **repayment pass charges a flat
> per-turn `base`** and forecloses on overdraft — there is **no recovered
> amortization schedule** (fixed principal-plus-interest installments over the
> term). The term influences the offered rate, not an installment plan. **Flagged
> as a possible gap**: if the original amortizes principal over the term, that
> logic was not recovered; the reconstructed pass is interest-only + overdraft
> foreclosure.

---

## 4. Interest accrual cadence

Loan interest is charged **once per game day (turn)**, inside the per-turn `Amt`
economy cycle. The cycle order is recovered from
`VIBE_GameTick_BeginPlayerRound (0x533188)` (`src/world/amt.h:191-209`):

```
1. Production        (RunProductionPass + goods distribution)
2. Prosperity        (UpdateOfficeProsperity)
3. BuildingTax       (RunBuildingTaxPass, flags=3) -> then ProcessLoanRepayments
4. LoanRepayments    (ProcessLoanRepayments 0x57b304)   <-- interest charged here
5. OfficeWages       (ProcessAllOfficeWages)
6. UpdateOffices
```

In the reimplementation driver (`src/play/turn_economy.cpp:96-107`) the
`LoanRepayments` pass:

```cpp
LoanDecision d = AmtEvaluateLoan(s.loanLawSlot, /*currency=*/0,
                                 s.heldCurrency, /*hasLender=*/false);
if (d.charge) {
    s.interestPaid += d.perTurnInterest;   // running total
    s.treasury     -= d.perTurnInterest;
    s.heldCurrency -= d.perTurnInterest;    // *** the debt GROWS each turn ***
}
```

So each day the debt compounds by `base` (the debt balance becomes more negative),
and once `|debt| > 2*base` with no lender, the next pass forecloses. The
per-day interest charged is reported as `EconomyTurnDeltas::interestThisTurn`
(`src/play/turn_economy.cpp:216`, `src/play/turn_economy.h:107`).

> **Cadence summary:** **per game-day / per turn**, interest-only, no within-day
> sub-steps. There is no separate monthly billing despite the offer's
> month-denominated term.

---

## 5. Player finances — cash, wealth, income vs. expenses

### 5.1 Cash on hand

`gilde.exe 0x58bc9c — VIBE_Person_GetCashAmount` (`src/sim/person.h:39-41`)
returns the **cash word at `Person +0x0A`** as a double. This is the spendable
balance a take-loan credits and a repay debits (`src/play/slice_bank.cpp:40-50`,
`src/play/slice_bank.h:97`). The cash word is treated as **non-negative**
(clamped at 0 on apply, `src/play/slice_bank.cpp:47-49`).

### 5.2 Stored money aggregation

`gilde.exe 0x591600 — VIBE_Person_SumStoredMoney` (`src/world/player_finance.cpp:18`)
sums the `+14` amount field of every money stack in the person's container
(`QueryFind(type=1, cat=4, good=9)`). Returns **-1** if the person record is not
found (the `Person_FindRecordById` null sentinel). This is the figure the
bribe/cash dialogs read.

### 5.3 Net worth / total wealth

Total wealth (`Person_ComputeTotalWealth`, the heavy building+currency
aggregation) is an injected value in the reconstructed leaves. It feeds:

- **Loan grant capacity** (`wealth * 0.2`, capped 64 000 — Section 3.1).
- **Top-5 wealth board** (the church "richest citizens" list) —
  `VIBE_Person_ComputeTopWealthList (0x592b50)`, `src/world/player_finance.cpp:35`.
  Scans all 768 person slots (stride 536 bytes), qualifying a slot when its id
  `!= -1` and its class byte `< 10` (excludes guards/criminals/special classes),
  and keeps the 5 highest by descending insertion sort. Wealth source is
  `dword_12CEABC[slot]` (`src/world/player_finance.h:69-90`).
- **Court fines** (`wealth * 0.03` — Section 5.5).

> **Flagged — modeled, not recovered:** `Person_ComputeTotalWealth` itself (the
> building/currency aggregation) is **not** reconstructed in these files; it is an
> injected input. The wealth *consumers* are recovered byte-for-byte.

### 5.4 Income vs. expenses (the treasury ledger)

The player's money is tracked as a running treasury balance moved each turn by the
`Amt` passes (`src/play/turn_economy.h:90-97`, `:104-109`). Per turn:

| Direction | Source | Where |
|-----------|--------|-------|
| **+ income** | office taxes collected | `BuildingTax` pass → `treasury += collected` (`turn_economy.cpp:89-94`) |
| **+ income** | work-minutes / production integral | `workMinutes` (`turn_economy.h:93`) |
| **− expense** | office wages paid | `OfficeWages` pass → `treasury -= paid` (`turn_economy.cpp:116-118`) |
| **− expense** | loan interest charged | `LoanRepayments` pass → `treasury -= interest` (`turn_economy.cpp:104`) |

The per-turn deltas are reported as `taxThisTurn`, `wagesThisTurn`,
`interestThisTurn` (`src/play/turn_economy.cpp:214-216`).

### 5.5 Civic fees & fines

- **Master-exam fee gate** — `VIBE_Amt_CheckExamFeeAffordable (0x592c18)`,
  `src/world/player_finance.cpp:80`. A favourability-scaled fee must clear a random
  threshold:

  ```cpp
  fee       = favorability * 0.01f * examLevel / requiredLevel;   // kExamFeeFavScale
  threshold = RandNext() * (1/32768) + 0.15;                       // kExamFeeRollBias
  return fee > threshold;
  ```

  (Constants `kExamFeeFavScale=0.01`, `kExamFeeRandNorm=1/32768`,
  `kExamFeeRollBias=0.15`, `src/world/player_finance.h:42-44`.)

- **Court-trial fine** — `VIBE_He_ShowFineAmount (0x4c4b60)`,
  `src/world/player_finance.cpp:94`:

  ```cpp
  fine = trunc( totalWealth * 0.03f );   // kCourtFineRate (flt_61E638)
  ```

  The message dispatch uses base id **4442 (`0x115A`)** with stride 3 per variant
  (`src/world/player_finance.h:48-50`; variant 1 renders the amount, variant 3
  pushes a literal 4 then the amount, `src/world/player_finance.cpp:114-145`).

---

## 6. UI flow confirmation (mechanics cross-check)

The bank/loan **dialog** (`src/play/dialog_bank.cpp`) and **slice**
(`src/play/slice_bank.cpp`) confirm the take-loan mechanics:

1. `VIBE_Credit_ShowTakeLoanDialog (0x51a244)` is the take-loan UI loop; the panel
   lists candidate **lenders** as rows with an amount slider and confirm/cancel
   buttons (`src/play/dialog_bank.cpp:119-165`).
2. Clicking **confirm** with a selected lender row produces a `BankInteraction`
   with `side = kTake`, the chosen `lenderId` (or `-1` bank sink),
   `borrowerId`, and the slider `amount` (`src/play/dialog_bank.cpp:321-341`).
3. `ClassifyBankInteraction` rejects a loan with `amount <= 0` or no side
   (`src/play/slice_bank.cpp:112-127`) — confirming the **positive-amount gate**.
4. The slice builds the opcode-15 packet, routes it through the **real**
   `sim::CommandQueue` lockstep codec, applies cash+debt, then advances **one
   game-day** via `RunEconomyTurn` to exercise the per-day loan-interest cascade
   (`src/play/slice_bank.cpp:170-221`).

This end-to-end path corroborates: positive-amount-only loans, opcode-15 wiring,
cash-up/debt-up on take, and per-day interest after.

---

## 7. Constants quick-reference

| Constant | Value | Origin (binary) | File |
|----------|-------|-----------------|------|
| Currency glyph | `0x11` (17) | font code-point | `money_format.h:30` |
| Money-format round bias | `0.5` | `dbl_6269BC` | `money_format.h:33` |
| Loan capacity factor | `0.20` | `dbl_626A1C` | `bank_loan.h:35` |
| Loan capacity ceiling | `64000` | `flt_626A24` | `bank_loan.h:36` |
| Relation factor (default/mid/high) | `0.30 / 0.45 / 0.60` | `0x591990` | `bank_loan.h:47-49` |
| Interest rate band (low/high) | `0.70 / 1.50` × field | `dbl_626A4C/54` | `bank_loan.h:41-42` |
| Term-sixth scale | `1/6` | `flt_626A44` | `bank_loan.h:40` |
| Loan term range | `2..7` months | `rand%6 + 2` | `bank_loan.cpp:87` |
| Amount jitter | up to `29 %` (`rand%30 * 0.01`) | `dbl_626A34` | `bank_loan.cpp:74-77` |
| Per-turn interest base | `4 - lawSlot + 10` (× rate) | `0x57b346` | `amt.cpp:161`, `bank.cpp:12` |
| Overdraft limit | `2 × base` | `0x57b304` | `amt.cpp:163` |
| Family debt accumulator offset | `+72` (dword +18) | `0x51a100` | `bank_loan.h:122` |
| Person cash field offset | `+0x0A` | `0x58bc9c` | `slice_bank.h:97` |
| Exam-fee fav scale / bias | `0.01` / `0.15` | `flt_626A8C` / `dbl_626A94` | `player_finance.h:42,44` |
| Court-fine rate | `0.03` | `flt_61E638` | `player_finance.h:45` |
| Top-wealth board size | `5` | `0x592b50` | `player_finance.h:47` |

---

## 8. Open questions / flagged unknowns

- **Currency rate table contents** (`dword_649A88` / `dword_13CD6F2`): indexing
  recovered, values not. Reimpl defaults the multiplier to 1.
- **Loan amortization:** no recovered installment schedule; the per-turn pass is
  interest-only + overdraft foreclosure (Section 3.7). The offer's month-term
  affects only the *offered rate*. Whether the original repays principal over the
  term is **unconfirmed**.
- **Deposit interest:** none found — interest is debt-only.
- **`Person_ComputeTotalWealth`:** the wealth aggregation itself is injected, not
  reconstructed in these files.
- **Foreclosure / wage-garnish enactment:** `AmtEvaluateLoan` yields the *decision*
  (`foreclose` / `charge`); the actual `Pair33` foreclose and `QueueRequestCoord27`
  wage garnish commands are higher-level and not reconstructed here
  (`src/play/slice_bank.h:33-36`).
