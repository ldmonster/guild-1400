# Hardening sweep — world_00_bank (Bank / Loan / Treasury / Caravan cargo)

Chunk files:
- src/world/bank.cpp
- src/world/bank_loan.cpp (+ bank_loan.h)
- src/world/bank_treasury.cpp (+ bank_treasury.h)
- src/world/caravan_cargo.cpp (+ caravan_cargo.h)
Tests: tests/unit/bank_treasury_test.cpp, tests/unit/caravan_cargo_test.cpp

MCP module gilde.exe (imagebase 0x400000) LIVE. Every provenance-carrying
function decompiled AND disasm-verified; every FP/int constant confirmed via
get_bytes/get_global_value.

## Constant verification (get_bytes — all byte-exact)
| addr | bytes | value | used as |
|------|-------|-------|---------|
| 0x626a1c | 9a 99 99 99 99 99 c9 3f | 0.2 (f64) | kWealthCapFactor |
| 0x626a24 | 00 00 7a 47 | 64000.0 (f32) | kWealthCapCeil |
| 0x626a2c | 00 00 00 00 00 00 e0 3f | 0.5 | kBaseScaleHalf |
| 0x626a34 | 7b 14 ae 47 e1 7a 84 3f | 0.01 | kJitterStep |
| 0x626a3c | 00 00 00 00 00 00 d0 3f | 0.25 | kBaseScaleQuarter |
| 0x626a44 | ab aa 2a 3e | 0.16666667 (f32, 1/6) | kTermSixth |
| 0x626a4c | 66 66 66 66 66 66 e6 3f | 0.7 | kRateLow |
| 0x626a54 | 00 00 00 00 00 00 f8 3f | 1.5 | kRateHigh |
| 0x626a5c | 00 00 a0 40 | 5.0 (f32) | kFavScale |
| 0x626a60 | 00 00 48 c2 | -50.0 (f32) | kFavBias |
| 0x6477a1 | 00 | 0 | default currency/ctx byte |
| 0x623f30 / 0x623f28 | 9a 99 99 99 99 99 f1 3f | 1.1 | kCaravanMarketPriceFactor / dbl_623F28 |
| 0x4ffa70 (16 B) | 18 27 33 3f 45 4b 12 39 3f 06 0c 2d 33 1e 45 4b | seed table | kCityTaxSeedRates |

## Per-function results

### bank.cpp
- **BankInterestBase** (0x57b346) — VERIFIED-1:1. `MultiplyByRate(4-lawSlot+10, currency)`
  confirmed at 0x57b346; overdraft = 2*base at 0x57b348. Thin facade over
  AmtMoneyMultiplyByRate (amt.cpp, out of chunk; already correct).
- **BankEvaluateLoan / BankApplyLoanStep** (0x57b304 VIBE_Amt_ProcessLoanRepayments) —
  VERIFIED-1:1 structurally. Per-account: held<0 → foreclose iff (no lender &&
  no creditor && |held| > 2*base) else dun lender; then charge base when officeOwner==1.
  Facade delegates the decision to AmtEvaluateLoan; bank.cpp adds no divergent
  arithmetic. The transfer charge (ownerAccount → -1 sink, amount=base) matches the
  QueueRequest16 charge.

### bank_loan.cpp (all in VIBE_Command_EvaluatePurchaseAction 0x591990, mode 3)
- **LoanGrantCapacity** — **FIXED** (0x591ad8..0x591b8d). Was computing the
  wealth*0.2 cap and the 64000 clamp in DOUBLE. Disasm shows var_34 (`fstp [var_34]`,
  4-byte, typed `float` in the stack frame) and var_38 are SINGLE-precision floats:
  `v47=(float)(wealth*0.2); if (v47>=64000.0f) v46=64000.0f else v46=v47`; the
  64000 compare is `fcomp flt_626A24` (float). The truncation `(i64)v46` (ConvertX→fistp)
  follows. Changed to compute `cap` as `float`, compare against `64000.0f`, then
  `Trunc((double)cap)`. Goldens (20000, 64000) and a float-boundary probe (320003→64000) pass.
- **LoanOfferBase** — VERIFIED-1:1 (0x591c6b..0x591cf1). `v41 = wealth*factor` and
  `v40 = prevBase*0.5` are both DOUBLE (var_64/var_6C, 8-byte); min via
  `(v41>=v40)?v40:v41`; ConvertX truncate. factor (var_30) is a single-precision
  bit pattern (0.3f/0.45f/0.6f) — reimpl uses float kFactor* promoted to double, equivalent.
- **LoanComputeInterest** — VERIFIED-1:1 (0x591db2..0x591df6, fav adj 0x591f37..0x591f6f).
  `fild field101 (signed int); base=field*0.7; span=field*1.5-base;
  rate=base+(term*1/6)*span; trunc`. bankType: `rate -= (fav+(-50))*0.01*5.0; trunc`.
  Operation order and the ConvertX truncations match exactly. Goldens (1366; 964) pass.
- **LoanGenerateOffers** — VERIFIED-1:1. RNG order/count matches: per iteration
  RandNext()%30 (amount jitter), then RandNext()%6+2 (term); no extra draws. The
  per-index scaling is keyed on the loop counter ecx: i==0 → *0.25 (dbl_626A3C),
  i==1 → *0.5 (dbl_626A2C, via the loc_591EC8 cmp ecx,1 path), i>=2 → none. Matches.
- **CreditConfirmLoanRequest** (0x51a100) — VERIFIED-1:1 for the arithmetic core.
  Gate `accept && CheckResourceAmount && ShowMessageBox`; on pass: EnqueueCmd15
  (lender=*(a1+1), borrower=*(a2+176), amount=*(a2+180), currency=byte_6477A1) FIRST,
  then `if (FamilyRecord) FamilyRecord[+18 dword == +72] += amount`. Side-effect order
  (command then debt bump) and the null-guard match. Returns bool (modeling convenience;
  binary is void). The message/quickjump/FreeHandlerEntry are UI side effects (deferred).

- **DOC FIX (bank_loan.h)**: the favorability-interest gate was mis-documented as
  "*(lender+2) in {6,7}". Disasm (0x591dfd..0x591f0e) shows the gate is
  (lenderClass = byte_12CE912[536 * *(u16*)(lender+39)] NOT in {6,7}) AND
  (borrower v52[2] IN {6,7}). Those class/kind tables are not modeled here; the
  caller resolves the gate and sets `lenderIsBankType`. Header comment corrected.

### bank_treasury.cpp (VIBE_Office_ComputeCityTaxRates 0x5011dc)
- **CityTaxHalveOfficeLevel** — VERIFIED-1:1 (0x501245..0x501250). `(byte-(byte>>31))>>1`
  == signed `/2` toward zero == C `x/2`. Negative-byte goldens correct.
- **CityTaxRateFor** — VERIFIED-1:1 (0x50123d..0x501263). idx=(base+i)%16 (signed
  idiv; base∈[0,15], i≥0 ⇒ always non-negative ⇒ matches the reimpl's normalized
  modulo). rate = seed[idx] (dword `>>24`/sar) - (byte2/2); stored as signed byte (al).
  Seed table byte-exact vs 0x4ffa70.
- **ComputeCityTaxRates** — VERIFIED-1:1. base=`(u16)RandomModulo(16)` masked `&0xFFFF`;
  count<=0 ⇒ return base&0xFFFF (loop skipped, no writes); loop returns the LAST full
  (untruncated) `(rate-bias)` int (`mov eax,ebp` @0x50125d), NOT the stored byte.
  Goldens (fill bytes 63/69/75/18/57, ret 57; empty→masked base; byte wrap) all correct.
- bank_treasury_test.cpp golden vectors already encode binary behavior — no change.

### caravan_cargo.cpp
- **CaravanInitSlotTables** (0x50854c) — VERIFIED-1:1 (behavioral). Both cargo grids
  reset to empty (good-id packed -1, objectId -1, storageSlot -1, dataPtr 0). The
  binary's aliased packed-struct writes and the screen-coord/aux-column tables
  (122E064/098/764/798, 122EDxx, 122E3xx) are owned by gui/trade_panel.cpp (documented
  out of scope); the observable empty-grid state is faithful.
- **CaravanComputeCargoValue** (0x53ff3c) — **FIXED**. Was accumulating in DOUBLE
  with a DOUBLE unit price. Disasm (0x53ffd0..0x540003) shows BOTH the per-slot unit
  price (var_24) AND the running accumulator (var_2C) are 4-byte SINGLE-precision
  floats: each pass does `fild DataPtr; fmul [float unit]; fadd [float accum];
  fstp [float accum]`. Changed `unit` and `total` to `float` (factor/override applied
  in single precision, accumulation single-precision). ctx selection, market(1.1) vs
  priceMul, mode 2/4 override (re-lookup at sourceCtx101, no factor), and goodId
  `>>16` (sar, signed) all confirmed unchanged.
- **GOLDEN FIX (caravan_cargo_test.cpp)**: ComputeCargoValue_OwnerMarket expected the
  f64 artifact 968.0000000000002. With the binary's float math the unit prices
  (50*1.1, 90*1.1, 110*1.1) round to exactly 55/99/121 and the sum is float-exact
  968.0. Golden + comment corrected to 968.0 (binary truth). Other goldens
  (900.0, 250.0) are float-exact already; all pass.
- **CaravanLoadFromStorage** (0x53f6bc) — **FIXED (price math) + BOUNDARY (gates)**.
  Disasm shows the emitted per-unit price QueueRequest17 receives is
  `v38 = (i64)trunc(v57 / a6)` where v57 is the FLOAT unit price and a6 is the carrier
  cost divisor. The reimpl previously emitted the un-divided, un-truncated double unit
  and had no a6 parameter. Fixes: unit computed as `float`; added `carrierCost` (a6,
  default 1.0) param; emitted `unitPrice` is now `i32 = trunc((double)unit/carrierCost)`
  (CaravanLoadLine.unitPrice changed double→i32). Existing load goldens (a6=1.0:
  100, 160, 0) pass unchanged.
  BOUNDARY: the original also gates each emit on VIBE_Dialog_CheckResourceAmount,
  on VIBE_Interaction_InvokeHandlerSlot60 (handler can skip the slot), and may flip
  a6→1.0 when |delta| < VIBE_Money_MultiplyByRate(1, ctx) (0x53f86a, 0x53f8ba,
  0x53f8d1..0x53fcf4). Those reach the dialog/interaction/money subsystems (out of
  this module). The capacity/price/destination decision and the trunc(v57/a6) unit
  price are reproduced; the resource/handler gates are documented, not faked.

## Counts
- Functions audited (provenance-carrying): 11
  (bank: BankInterestBase, BankEvaluateLoan/BankApplyLoanStep@0x57b304;
   bank_loan: LoanGrantCapacity, LoanOfferBase, LoanComputeInterest, LoanGenerateOffers,
   CreditConfirmLoanRequest;
   bank_treasury: CityTaxHalveOfficeLevel, CityTaxRateFor, ComputeCityTaxRates;
   caravan: CaravanInitSlotTables, CaravanComputeCargoValue, CaravanLoadFromStorage)
- VERIFIED-1:1: 9
- FIXED: 3 (LoanGrantCapacity float; CaravanComputeCargoValue float accum; CaravanLoadFromStorage price /a6+trunc, unit float)
- Golden fixes: 1 (caravan ComputeCargoValue_OwnerMarket 968.0000000000002 → 968.0)
- Doc fixes: 1 (bank_loan.h fav-gate condition)
- BOUNDARY: 1 (CaravanLoadFromStorage resource/handler/a6-mutation gates)

## Build / test
- All 4 sources + 2 test files compile clean (g++ -std=c++17 -Isrc -Iinclude -Ishim).
- caravan_cargo_test: 67 checks, 0 failures.
- bank_treasury_test: 55 checks, 0 failures.
- Standalone loan-arithmetic driver: all pass (incl. float-precision boundary 320003→64000).
- NOTE: the full `guild` lib does NOT currently link due to a PRE-EXISTING,
  UNRELATED compile error in src/sim/character_recon5_transport.cpp:29 (invalid
  static_cast<intptr_t> from TObject*) — another wave's in-progress file, outside
  this chunk. My files were verified by isolated compilation + standalone test links.

## Handoffs (no edits made to these — all verified still passing)
- tests/unit/world_trade_player_test.cpp (out of chunk) exercises LoanGrantCapacity
  (100000→20000, 500000→64000). Both goldens remain valid under the float change
  (float-exact); verified by a standalone driver.
- CaravanLoadFromStorage signature gained a trailing `carrierCost` param with a
  DEFAULT of 1.0f, so all existing 9-arg call sites still compile and behave
  identically. CaravanLoadLine.unitPrice changed double→i32 (it is now an integer,
  matching the binary's (i64)trunc emit).
- External CaravanLoadLine / CaravanLoadFromStorage / CaravanComputeCargoValue
  consumers, BUILT & RUN against the changed module — all pass:
    * tests/integration/caravan_cargo_itest.cpp  — 7 checks, 0 failures
      (incl. the float-precision 12*1.1 + 3.5*1.1 == 91.3 case, eps 1e-4).
    * tests/e2e/caravan_cargo_e2e_test.cpp        — 0 failures.
  NOTE: the `.unitPrice` references in src/ai/meister_*.cpp and src/play/*.cpp are a
  DIFFERENT struct (trade-item / trade-command), not CaravanLoadLine — unaffected.
