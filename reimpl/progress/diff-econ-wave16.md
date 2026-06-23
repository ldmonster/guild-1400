# Wave-16 TRUE 1:1 binary diff — world economy / trade / finance (W16-ECON)

MCP LIVE. Extends `oneone-econ-wave14.md` (in-tree audit) by decompiling every
flagged economy/price/tax/loan/production/route function and comparing
line-for-line against the reconstruction. Cluster owned: `src/world/` amt*,
economy*, market_price*, market_stall, exchange*, trade_player/route,
tradetransport, trade_recon*, production, tax, treasury, bank*, player_finance,
caravan_cargo, world_economy*.

## Headline
The wave-14 NEEDS-LIVE-MCP queue (8 items) is RESOLVED against the live binary.
**7 of 8 are VERIFIED-1:1** (no source change needed — the in-tree model already
matched the decompile, including every ConvertX-truncate site). **1 genuine
divergence FIXED**: the loan/foreclosure gate (item 8) was missing the third
binary condition (`creditorId == -1` marker). Fixed + golden-pinned.

All recovered FP constants re-confirmed by `get_bytes` (bit patterns below).

## ConvertX-truncate audit (wave-15 heads-up)
The wave-15 note (VIBE_Coord_ConvertX @0x5c6b08 sets RC=truncate then frndint,
i.e. float->int via truncation) was checked at EVERY economy float->int site. The
reconstruction already models all of them as `trunc` (or `static_cast<i32>` of a
non-negative value, which is identical). **No round-vs-trunc divergence found.**
Sites confirmed:
- Tax 0x57ab2b->0x57ab30 (`(int)v8` after ConvertX) — clamped >=0, trunc. ✓
- MarketPrice cache write 0x58f628->0x58f62d (`(int)v20`) — trunc. ✓
- RouteCartCost 0x592277/0x592298/.. — every fee + final `+0.5` ConvertX-trunc. ✓
- MoneyConvertToDisplayCoord 0x58f189 — `a1/rate + 0.5` then trunc (round-half-up
  via bias, NOT nearbyint). ✓
- Production 0x59077d/0x5907b5/0x590843/0x5909db — all `(int)` after ConvertX. ✓
- ExchangeCourier net 0x51cd8e — `(int)(base - feeFloat)` ConvertX-trunc. ✓
- Loan offer loop 0x591d4e/0x591d7f/0x591df1/0x591f6a + cap 0x591b6e — all trunc. ✓

## Queue resolution (8 items)

### 1. Tax CollectTradeIncome 0x57aa88 — VERIFIED-1:1
Decompile: `v22 = (v21<=0)?0:v21; VIBE_Coord_ConvertX(); v24=(int)v8`. The cast is
preceded by ConvertX (truncate) and the value is already clamped >=0, so
`tax.cpp` `static_cast<i32>(v22)` (trunc-toward-zero) is exact. Single-precision
float chain (v19/v18/v20/v21/v22) matches `TaxComputeTradeIncome`. The "law slot
full" early-out is `count>=6` over the 6-byte law block (do/while at 0x57aab1),
matching `activeFinanceLaws >= 6`. The int-extremes saturation envelope stays
documented by `TradeIncomeIntExtremesNoTrap`. No change.

### 2. AmtComputeRoleButtonYs 0x556df1 — VERIFIED-1:1 (div-by-zero confirmed)
Decompile 0x556e10/0x556e1e: `v63 = v60 % (dword_5526AC-1); v60 /= dword_5526AC-1`
with NO count==1 guard (engine guarantees count>=2). 0x556ee2:
`v62 += (--v63 >= 0) + v60 + 33` — predecrement-then-test. `amt_recon_office_window.cpp`
`AmtComputeRoleButtonYs` reproduces both exactly (`% (count-1)`, `/ (count-1)`,
`--rem; y += (rem>=0)+step+33`). No change.

### 3. AmtFindFirstNonEmptyRole 0x556d2b / AmtOverviewFindClickedSlot 0x557970 — VERIFIED-1:1
- 0x556d2b: `if (!dword_63D57C[5*dword_63D618])` — unbounded `rowCounts[5*current]`
  read with no guard (caller contract). Matches source line 66.
- 0x557980: `while (v31[i] != v19) { ++i; if (i>=8) goto LABEL_22; }` — the pre-count
  `slotObjectIds[0]` read happens before the bound check; loop bound fixed at 8.
  Matches `AmtOverviewFindClickedSlot`. No change.

### 4. BuildingComputeMarketPrice 0x58f3d0 — VERIFIED-1:1 (recipe div-by-zero confirmed)
Full decompile compared against `market_price_model.cpp`:
- cached path: `(double)(32*base)*currency*0.01`, `*1.5 if flag==3`. ✓
- divisor floor: `v29 = divisor + (divisor==0)`. ✓
- type-need accumulate weights 28*32*(1/12)*(1/60), no-type=4.0. ✓
- category 23 returns `v28 * flt_626974`; flt_626974 == 2.2 == kPriceGain
  (0x400CCCCD) — confirmed identical to v24, so source's `kPriceGain` is correct. ✓
- recipe leg divides by `*(u16)(v3+54)` (raw `rec.divisor`, NOT the +1 floored
  copy). On divisor==0 the original does an FP divide by 0.0 (inf/nan) — NO guard;
  source reproduces it (double divide). ✓
- cache write 0x58f62d: `(int)v20` after ConvertX, `v20 = v28*2.2*(1/32)` — trunc.
  Matches `static_cast<i32>(cacheVal)`. ✓
Constants re-pinned by get_bytes (see table). No change.

### 5. Economy tick 0x579098 / 0x57919c — VERIFIED-1:1 (FP divide envelope confirmed)
- 0x579098 TickPriceLevel: first-tick branch keyed on `test ecx,0x7FFFFFFF`
  (flt_641DA8 != +/-0.0). Seed path `flt_641DAC=target; flt_641DA8=target*0.75`;
  EMA path `flt_641DAC=(target-flt_641DAC)*0.5+flt_641DAC`. Broadcast spread
  `(flt_641DAC-flt_641DA8)*1.03*0.000712...`. Final `(int)ConvertX(flt_641DAC)`. ✓
- 0x57919c CityTickStatsAndBroadcast: same EMA structure, alpha 0.5, seed *0.95,
  11-dword broadcast body. ✓
- The `g_capDivisor==0` / `hi-lo==0` FP divides live in PopulationTrend (0x57a008)
  and FillLawRangeRatios (0x57aa30); both reproduce the bare FP divide (no guard).
  After the first-tick seed flt_641DA8 = target*0.75 is non-zero in the normal
  envelope. No change. Constants re-pinned (table). No change.

### 6. RouteComputeCartCost 0x592220 — VERIFIED-1:1 (default-mode confirmed)
Decompile: `if ((double)a1 > 0.0)` (float compare) clamp to [32000,256000];
`switch(mode){1:*0.05; 2:*0.1; 3:*0.15;}` (each ConvertX-trunc); default (mode 0)
leaves `v8=0`; `return (int)ConvertX((double)v8 + 0.5)`. So mode None -> trunc(0.5)=0,
NO unhandled-mode trap. `trade_route.cpp` `RouteComputeCartCost` matches exactly
incl. the `default: fee=0` and the final `Trunc(fee + 0.5)`. RouteMode enum
(None=0,Slow=1,Medium=2,Fast=3) maps to flt_626A7C/78/74 = 0.05/0.1/0.15. ✓
No change.

### 7. Loan offer loop 0x591990 — VERIFIED-1:1 (draw order + index scaling confirmed)
Full decompile (VIBE_Command_EvaluatePurchaseAction) traced:
- cap: `wealth*0.2`, clamp `>= 64000.0 -> 64000.0`, ConvertX-trunc. ✓
- offer amount: `v55 = trunc((-(rand%30)*0.01 + 1.0) * base)`. **First RNG draw is
  rand%30.** ✓
- per-offer index scaling: offer index 0 (`v23==0`, break target) `v55 = trunc(v55*0.25)`
  (dbl_626A3C=0.25); offer index 1 (`v23==1`) `v55 = trunc(v55*0.5)` (dbl_626A2C=0.5);
  else no scale. Matches `LoanGenerateOffers` i==0 -> *0.25, i==1 -> *0.5. ✓
- term: `v25 = (rand%6)+2`. **Second RNG draw is rand%6+2.** Draw order
  (rand%30 then rand%6) matches the source's RandNext()%30 then RandNext()%6. ✓
- interest: `base=field101*0.7; rate=base + term*(1/6)*(field101*1.5 - base)`,
  ConvertX-trunc; bankType adj `-(fav + (-50.0))*0.01*5.0`. Matches
  `LoanComputeInterest` (kRateLow 0.7, kRateHigh 1.5, kTermSixth 1/6, kFavBias -50,
  kJitterStep 0.01, kFavScale 5.0). ✓
No change.

### 8. BankApplyLoanStep 0x57b304 / CreditConfirmLoanRequest 0x51a100 — DIVERGED -> FIXED
Decompile (VIBE_Amt_ProcessLoanRepayments 0x57b304):
```
v4 = MultiplyByRate(4 - lawSlot + 10, currency);   // base
result = SumCurrencyHeld(...);                       // held
if (result < 0) {                                    // 0x57b3cb
  if (!dword_12CEA7C[i]            // no lender ptr            0x57b411
      && dword_12CE96C[i] == -1    // no creditor on record
      && abs(result) > 2*v4)       // |held| > 2*base
    QueueRequestPair33(...);       // FORECLOSE, goto LABEL_3 (skip charge)
  else if (dword_12CEA7C[i])       // lender present           0x57b41b
    QueueRequestCoord27(.. -10);   // dun lender (-10 relation)
}
if (*officeStorage == 1)           // 0x57b3d1 (independent of held sign)
  QueueRequest16(.. v4 ..);        // charge base interest
```
DIVERGENCE: the binary foreclosure gate has THREE conditions; the reconstruction
only modelled `!hasLender && |held| > 2*base` and was missing the
`dword_12CE96C[i] == -1` creditor marker (xref shows dword_12CE96C is the
per-person creditor field). It also did not distinguish the lender-dun branch.

FIX (`amt.cpp`/`amt.h` `AmtEvaluateLoan`, threaded through `bank.cpp`/`bank.h`
`LoanAccount`):
- Added `noCreditor` parameter (`dword_12CE96C[i]==-1`, default true =
  "no creditor on record"). Foreclosure now requires `!hasLender && noCreditor &&
  debt > overdraftLimit`. When a creditor IS on record the original skips the
  foreclosure goto and falls through to the charge — now reproduced.
- Added `LoanDecision.dunLender` (0x57b41b: lender present -> -10 relation),
  set when `hasLender` and the holder owes.
Default `noCreditor=true` keeps the non-cluster caller
`src/play/turn_economy.cpp:100` (bind-site, not edited) source-compatible; its
behavior is unchanged (no creditor recorded == the prior path).

GOLDEN: `world_amt_test.cpp` new `WorldAmtLoan.ForeclosureGateRequiresNoCreditorMarker`
(+9 checks): (no lender, no creditor, |debt|>limit) -> foreclose;
(no lender, creditor on record) -> NO foreclose, charge; (lender present) ->
dunLender + charge, never foreclose. Existing thresholds test unchanged (uses the
true default and still forecloses).

## Recovered FP constants (get_bytes, this wave)
| symbol | addr | bytes (LE) | value | role |
|---|---|---|---|---|
| flt_626948 | 0x626948 | 0A D7 23 3C | 0.01 | currency scale |
| flt_62694C | 0x62694c | 00 00 C0 3F | 1.5 | flag==3 mult |
| flt_626950 | 0x626950 | 00 00 E0 41 | 28.0 | type weight A |
| flt_626954 | 0x626954 | 00 00 00 42 | 32.0 | type weight B |
| dbl_62695C | 0x62695c | ..B5 3F | 1/12 | twelfth |
| dbl_626964 | 0x626964 | ..91 3F | 1/60 | sixtieth |
| dbl_62696C | 0x62696c | ..E0 3F | 0.5 | half |
| flt_626974 | 0x626974 | CD CC 0C 40 | 2.2 | cat-23 mult (==kPriceGain) |
| flt_626978 | 0x626978 | 00 00 00 3D | 1/32 | cache scale |
| dbl_6268DC | 0x6268dc | ..E0 3F | 0.5 | display-coord round bias |
| flt_626A04 | 0x626a04 | 00 00 70 42 | 60.0 | minute scale |
| flt_6476FC | 0x6476fc | 8,7,8,9 | work start hours |
| flt_64770C | 0x64770c | 20,21,20,19 | work end hours |
| flt_626A74/78/7C | 0x626a74.. | 0.15/0.1/0.05 | route Slow/Med/Fast rate |
| dbl_626A84 | 0x626a84 | ..E0 3F | 0.5 | route cost base |
| dbl_626A1C | 0x626a1c | ..C9 3F | 0.2 | loan wealth cap factor |
| flt_626A24 | 0x626a24 | 00 00 7A 47 | 64000.0 | loan cap ceiling |
| dbl_626A2C | 0x626a2c | ..E0 3F | 0.5 | offer1 scale / base half |
| dbl_626A34 | 0x626a34 | ..84 3F | 0.01 | jitter step |
| dbl_626A3C | 0x626a3c | ..D0 3F | 0.25 | offer0 scale |
| flt_626A44 | 0x626a44 | AB AA 2A 3E | 1/6 | term sixth |
| dbl_626A4C | 0x626a4c | ..E6 3F | 0.7 | rate low |
| dbl_626A54 | 0x626a54 | ..F8 3F | 1.5 | rate high |
| flt_626A5C | 0x626a5c | 00 00 A0 40 | 5.0 | fav scale |
| flt_626A60 | 0x626a60 | 00 00 48 C2 | -50.0 | fav bias |
| flt_6256C8/E4 | | 0.5 | price/city EMA alpha |
| dbl_6256CC | 0x6256cc | ..E8 3F | 0.75 | price first-tick seed |
| dbl_6256EC | 0x6256ec | ..EE 3F | 0.95 | city first-tick seed |
| dbl_6256D4/F4 | | 1.03 | broadcast scale A |
| dbl_6256DC/FC | | 0.000712507... | broadcast scale B |
| dbl_6220D0 | 0x6220d0 | ..9E 3F | 0.03 | courier fee factor |
All match the existing `constexpr`/byte goldens; no drift.

## Files changed (cluster-owned only)
- `src/world/amt.cpp` — AmtEvaluateLoan: 3-condition foreclosure gate
  (`noCreditor`), `dunLender` flag, full 0x57b304 provenance comment.
- `src/world/amt.h` — LoanDecision.dunLender; AmtEvaluateLoan `noCreditor=true`.
- `src/world/bank.cpp` — thread `acct.noCreditor` into AmtEvaluateLoan (2 sites).
- `src/world/bank.h` — LoanAccount.noCreditor (dword_12CE96C marker).
- `tests/unit/world_amt_test.cpp` — +ForeclosureGateRequiresNoCreditorMarker (9 checks).

## Build status
My 5 changed files compile cleanly (verified by standalone g++ -Iinclude -Isrc
-Ishim). world_amt_test (99 checks), world_amt_e2e_test (16), bank_treasury_test
(55) all built and ran 0 failures BEFORE a concurrent law/mission-cluster agent
introduced an unrelated ODR break in `src/world/mission_rules.h`
(MissionCompletionOutcome redefinition) that fails the shared `guild` lib link.
That file is NOT in this cluster and NOT in my git status; it will be fixed by its
owner. No economy-cluster source/test regression.

## Net
Queue 8/8 resolved: 7 VERIFIED-1:1 (no change), 1 FIXED (loan foreclosure gate +
golden). Whole cluster's ConvertX float->int sites confirmed trunc. No constant
drift.
