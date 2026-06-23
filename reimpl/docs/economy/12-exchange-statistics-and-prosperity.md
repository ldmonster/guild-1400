# Exchange, Economic Statistics & Prosperity Metrics

This document covers four loosely-coupled subsystems recovered from
`gilde.exe` (Die Gilde / Europa 1400):

1. The **goods/money exchange** ("Geldleihe\Wechsel" — the counting-house
   currency/goods barter desk), not a stock market.
2. The **economy statistics report** that periodically broadcasts up/down
   trend lines for four economic categories.
3. The per-building **prosperity score** (`UpdateOfficeProsperity`).
4. The city-level **population census** and **district satisfaction grid**
   that produce the broadcast economic indicators.

A short closing section traces how these metrics feed back into prices, AI and
the broadcast/event path.

## Source files

| File | Origin (`gilde.exe`) | Role |
|---|---|---|
| `src/world/exchange.h` / `.cpp` | `0x51bb4c`, `0x51ca40`, `0x51cbdc`, `0x58f1d0` | Currency rate table + goods/courier/fee exchange arithmetic |
| `src/world/exchange_loop.h` / `.cpp` | `0x51ce1c`, `0x51da04` | Goods-exchange panel loop core + bank contact router |
| `src/world/statistics.h` / `.cpp` | `0x579ad0` (accum), `0x57a900` (tax window) | Per-good float accumulator reduction into 4 category totals |
| `src/world/statistics_report.h` / `.cpp` | `0x579ad0` (decision logic) | Cadence gate, trend ids, weakest-sector argmin, severity, luxury branch |
| `src/world/statistics_full.h` / `.cpp` | `0x579ad0` (assembly + broadcast walk) | Combines the decision pieces; recipient broadcast count |
| `src/world/office_prosperity.h` / `.cpp` | `0x57b718` | Per-building prosperity score + AI decay |
| `src/world/city.h` / `.cpp` | `0x577a9c`, `0x50704c`, `0x5dc070`, `0x507144` | City records + 28-good economy parameter seed |
| `src/world/city_population_census.h` / `.cpp` | `0x578abc`, `0x577e74`, `0x5783e4` | Per-round population census, wealth grid, snapshot |
| `src/world/city_satisfaction_grid.h` / `.cpp` | `0x577e04`, `0x577fc4`, `0x578110`, `0x578240`, `0x5788d4`, `0x579460`, `0x5794e0` | 8×8 district crime/satisfaction grid |

Cross-referenced (feedback path, not re-documented in full):
`src/world/economy_tick.h` (price-level EMA, population trend),
`src/world/economy.h` (price deltas), `src/world/amt.h`/`amt.cpp` (turn cycle),
`src/play/turn_economy.cpp` (driver), `src/sim/turn_driver.cpp` (pass order),
`src/world/types.h` (record layouts).

---

## 1. The exchange ("Geldleihe\Wechsel") subsystem

### 1.1 What it is

The "exchange" here is **not a stock market or commodity exchange (Börse)**. It
is the *counting-house currency-and-goods barter desk* — the in-game
`Geldleihe` (money-lending) building's exchange dialogs. The header comment is
explicit (`exchange.h:1-17`): it covers the "Geldleihe/Wechsel" dialogs, a
**per-currency rate table**, a **goods-for-goods swap**, a **courier
(remittance) fee**, and a **fee-editing dialog**. There is no order book, no
share price, no market clearing. Foreign cities have their own currency and the
desk charges a spread when a player operates outside their home city.

The recovered core is purely the *arithmetic + command commits*; the GUI shell
(window/slot layout, drag, rendering) is explicitly deferred.

### 1.2 Per-currency rate table — `PriceByRate`

The conversion primitive is a flat per-currency multiplier table
(`dword_649A88[]`, runtime-seeded). `exchange.cpp:18-22`:

```cpp
// gilde.exe 0x58f1d0 — VIBE_Money_PriceByRate: dword_649A88[cur] * amount.
i32 ExchangePriceByRate(i32 amount, u16 currency) {
    if (currency < g_rateTable.size())
        return g_rateTable[currency] * amount;
    return 0;
}
```

So a quantity `amount` in currency `cur` is worth `table[cur] * amount` home
units. `PriceByRate(1, cur)` is the per-unit value of currency `cur`.

### 1.3 Recovered FP constants (`exchange.h:27-28`)

```cpp
constexpr double kExchangeSpread = 0.01;  // dbl_622068 — goods-exchange spread
constexpr double kCourierFactor  = 0.03;  // dbl_6220D0 — courier fee rate
```

### 1.4 Courier (remittance) fee — `ExchangeCourier` (`0x51cbdc`)

Charges a 3% fee on a foreign-currency move, clamped to a per-unit minimum
(`exchange.cpp:42-70`):

```text
base = PriceByRate(rate, amountCurrency)          // gross
span = base * 0.03                                // 3% fee (kCourierFactor)
minA = PriceByRate(1, amountCurrency)             // one-unit home value
fee  = (minA >= span) ? PriceByRate(1, amountCurrency) : span   // min-fee clamp
net  = trunc(base - fee)                          // delivered amount
```

`fee` is stored through a **float** (`r.fee = (i32)(float)fee;`, line 51) — the
narrowing is faithful to the original (`v26` is a float). On commit it emits two
`QueueRequest17` legs: charge `base` from treasury (`payer = -1`) to the player
account, then deliver `net`.

### 1.5 Goods-for-goods swap — `ExchangeGoodsTrade` (`0x51bb4c`)

Matches a player "give" slot against a counterparty "take" slot. When the
player is in a **foreign** city it charges a spread on the move
(`exchange.cpp:82-122`):

```text
value   = PriceByRate(takeQty, takeCurrency)          // counterparty value (v112)
feeBase = playerCash * 0.01                           // kExchangeSpread (dbl_622068)
if (sameCity) fee = 0
else {
    span = value * feeBase
    fee  = (PriceByRate(1, takeCurrency) >= span)
              ? PriceByRate(1, giveCurrency)          // min-fee clamp
              : span
}
// Affordability gate:
if (PriceByRate(giveQty, giveCurrency) < value) -> REJECT (cannot afford)
```

Note the fee base is **the player's own cash × 1%** — i.e. the spread scales
with the player's liquidity, not with the trade size directly; the trade size
enters via `span = value * feeBase`. `fee` is again truncated through a float
(`exchange.cpp:96`).

On accept + commit, **four** `QueueRequest17` legs are emitted (header
`exchange.h:85-89`, code `exchange.cpp:111-120`): take-good in to the player's
city account, give-good out of it (`value − fee`), pay (`takeValue`), and
receive (`value + fee`). The asymmetric `±fee` on the city legs is where the
counting house pockets the spread.

The deterministic outputs (`giveValue`, `fee`, `accept`) are surfaced so they
can be golden-checked.

### 1.6 Fee-edit dialog — `ExchangeApplyFees` (`0x51ca40`)

On confirm (widget `1210`) the dialog writes two edited fee fields back to the
object via a delta packet: `buyFee` → object field **+105**, `sellFee` → object
field **+109** (`exchange.cpp:128-133`). These are the building's own posted
buy/sell fees.

### 1.7 The exchange panel loop — `RunGoodsExchangeLoop` (`0x51ce1c`)

`exchange_loop.cpp` recovers the deterministic rule core of the goods-exchange
panel (the form/render plumbing is deferred). Mechanics:

* **Slot tables** — two 16-slot tables (give / take), 7 dwords per slot, each
  initialised to `{id = -1, count = 0}` (`ExchangeInitSlotTables`,
  `exchange_loop.cpp:6-16`).

* **Per-city currency seed** — for cities `1..3` that are *active*, the take
  table's slot `c-1` is seeded with that city's currency id and the city index
  as its flag (`ExchangeSeedCityCurrencies`, lines 19-35). This is what
  populates a foreign-city exchange desk with the neighbouring cities'
  currencies:

  ```cpp
  for (int c = 1; c < 4; ++c)
      if (cities[c].active) {
          take[c-1].currency = cities[c].currencyId;
          take[c-1].flag     = (u8)c;
      }
  ```

* **Active-slot count + overflow** — `ExchangeCountActiveSlots` counts slots
  with `id != -1`; `ExchangeListOverflows` is `activeCount > 4` (drives the
  scroll affordance / "+N more" label) (lines 38-48).

* **Courier trigger** — the courier dialog fires when there is no pending
  shipment, a take slot is selected, and a give slot is populated
  (`ExchangeShouldShowCourier`, lines 51-54):

  ```cpp
  return !shipmentPending && selectedSlot != -1 && populatedSlot != -1;
  ```

* **Button dispatch** — widget `1210` routes to the fees or exchange dialog;
  `1211`/`1212` scroll the give/take overflow lists
  (`ExchangeDispatchButton`, lines 57-73).

### 1.8 Bank contact router — `BankRouteContact` (`0x51da04`)

The `Geldleihe` building routes a clicked contact to a sub-dialog
(`exchange_loop.cpp:80-99`). Two gates:

* The **Meister/Vermögen** (master certificate / asset overview) contacts only
  exist when the player is **guild master** (`word_631758 & 0x200`).
* The **Exchange/Credit** pair routes by **home city** (`*(obj+39) ==
  word_63CC5C`): at home → the full goods-exchange loop + lender dialog;
  foreign → the plain goods-exchange dialog + take-loan dialog.

---

## 2. Economy statistics report

This is the periodic "state of the economy" broadcast — the trend lines players
receive ("trade is up", "the city's services sector is struggling", "the city
is booming"). It is **per-good supply/demand data reduced into four category
totals**, gated to fire once every fourth round.

### 2.1 The float accumulator reduction (`statistics.cpp`, `0x579ad0`)

The report reads a block of per-good float accumulators at `flt_1235090`,
modelled as a flat 20-float window (4 good-slots × 5 floats, stride `0x14`).
Four interleaved columns are summed across the four good-slots and scaled by
`kStatScale = 0.25f` (`flt_62570C`) (`statistics.h:21-32`, `statistics.cpp`):

```cpp
float StatisticsCategoryTotal(const float* accum, int k) {
    return (accum[k] + accum[k+5] + accum[k+10] + accum[k+15]) * kStatScale;
}
// i.e. total[k] = (a[k] + a[k+5] + a[k+10] + a[k+15]) * 0.25, k in 0..3
```

The four columns map to categories (`statistics_report.h:38-47`):

| `k` / local | Category |
|---|---|
| 0 / `v33` | goods |
| 1 / `v34` | services |
| 2 / `v35` | trade / production |
| 3 / `v26` | luxury |

A fifth input, the **law score** (`v32`), is *not* from this block — it is
supplied by the caller from `VIBE_Economy_ComputeInterpolatedLawScore` and is
**signed** (`EconomyReportFromAccum`, `statistics_report.cpp:13-21`).

### 2.2 Cadence gate (`0x579af5`)

The report runs only when (`statistics_report.cpp:24-26`):

```cpp
bool EconomyReportShouldRun(i32 round) {
    return round >= 8 && (round % 4) == 0;   // round counter qword_13CE852
}
```

### 2.3 Per-category trend ids (`0x579bac`)

Each non-law category compares its total against `kReportTrendThreshold = 0.15`
(`dbl_625714`); the law category keys on `< 0` since it is signed
(`statistics_report.cpp:28-39`):

```text
trade    (total2):  > 0.15 ? 6178 : 6174
goods    (total0):  > 0.15 ? 6179 : 6175
law      (lawScore): < 0   ? 6180 : 6176
services (total1):  > 0.15 ? 6181 : 6177
```

(6174–6181 are string-table ids for the up/down phrasings.)

### 2.4 Weakest sector (`0x579c1a`)

The report finds the weakest category via the binary's exact short-circuit
comparison chain over `(total2, total0, lawScore, total1)`
(`statistics_report.cpp:42-67`). The `.value` (`v24`) is the true minimum; the
`.index` (`v12`, 0..3) selects the "<sector> struggling" string. The header
flags (`statistics_report.h:82-83`) that **the index is NOT a strict argmin** —
a tie or late-chain comparison can leave it at 3; it is reproduced 1:1, not
"cleaned up". The chain:

```cpp
float v7  = (v35 >= v33) ? v33 : v35;
int   v9  = (v32 <= v7) ? (v33 > v35 ? 1 : 0) : 2;
float v10 = (v7  >= v32) ? v32 : v7;
int   v12 = (v34 <= v10) ? v9 : 3;          // the selected category index
float v13 = (v10 >= v34) ? v34 : v10;       // the minimum value
```

### 2.5 Weak-line gate + severity tiers

The "<sector> struggling" line fires only when the weak value clears
`kReportWeakGate = 0.20` (`dbl_62571C`) (`EconomyReportEmitsWeakLine`). Its
severity tier (`statistics_report.cpp:75-83`):

```text
value <  0.30          -> kMild
0.30 <= value < 0.50   -> kModerate     (kReportTier2Gate / kReportTier3Gate)
value >= 0.50          -> kSevere
```

### 2.6 Luxury summary branch (`0x579d5b`)

The trailing positive/negative summary keys on the luxury total
(`statistics_report.cpp:86-93`):

```text
luxury > 0.55  -> kBooming  (ids 6170/6171; kReportLuxuryHigh)
luxury < 0.45  -> kSlump    (ids 6172/6173; kReportLuxuryLow)
else           -> kNeutral  (only the weak line, if any)
```

### 2.7 Assembly + broadcast (`statistics_full.cpp`, `0x579ad0`)

`EconomyReportBuild` (`statistics_full.cpp:25-46`) combines the pieces into one
per-tick `EconomyReportOutcome`. A message is broadcast this tick iff:

```cpp
out.broadcasts = (out.luxury != kNeutral) || out.weakLine;
```

matching the original's three branches: luxury-high summary, luxury-low summary,
or the weak-line-only fall-through.

**Recipient walk** (`statistics_full.h:17-36`, `.cpp:14-23`): the report scans a
fixed table of **768 player-person slots** (`byte_12CE912`, stride 536 bytes /
134 dwords, bound `102912`). Only slots whose kind byte == **6**
(`kReportRecipientKind`) receive the `He`-entity message; the recoverable core
counts the eligible recipients.

### 2.8 Tax-history window (`statistics.cpp`, `0x57a900`)

A separate query: the tax window renders the last **17** rounds
(`kStatTaxRounds`) from the per-round tax ring `dword_12350E0`. For row `i`
(0..16) the absolute round number is (`statistics.cpp:35-37`):

```cpp
i32 StatisticsTaxRowRound(i32 currentRound, int row) {
    return currentRound - 16 + row;
}
```

---

## 3. Prosperity metric — `UpdateOfficeProsperity` (`0x57b718`)

Once per `Amt` (office) turn, every active business building gets a recomputed
**prosperity score** from the owner's wealth and the building's three
production-room values, relative to a city-wide reference maximum. The score is
stored at object field **+480**.

### 3.1 Tuning constants (`office_prosperity.h:24-27`)

```cpp
constexpr double kProsperityBlend   = 0.5;  // dbl_6258B4 — blend weight (twice)
constexpr double kProsperityAiDecay = 0.95; // dbl_6258BC — AI-method decay at +180
```

### 3.2 Average-wealth intermediate (`office_prosperity.cpp:24-33`)

```cpp
// v25 = (ownerWealth + sum(room[0..2])) / (count(room != 0) + 1)
i32 ProsperityAverageWealth(const ProsperityInput& in) {
    i32 roomSum = 0; int nonzero = 0;
    for (int i = 0; i < 3; ++i) { roomSum += in.room[i]; nonzero += (in.room[i] != 0); }
    return (in.ownerWealth + roomSum) / (nonzero + 1);   // integer divide
}
```

### 3.3 The score formula (`office_prosperity.cpp:40-60`)

```text
avg     = ProsperityAverageWealth(in)               // integer
ratioF  = (float)avg / (float)ownerWealth           // single-precision divide
ratio   = (ratioF >= 1.0) ? 1.0 : ratioF            // clamp to <= 1.0
score   = ratio * 0.5  +  0.5 * (1.0 - ownerWealth / cityMax)
```

i.e. the prosperity score is a 50/50 blend of:

* **how the building's averaged room/owner wealth compares to the owner's total
  wealth** (capped at 1.0), and
* **how far below the city's richest reference the owner sits**
  (`1 − ownerWealth/cityMax`).

`cityMax` (`v16`) is the city-wide reference max from the building-table walk
(`FindNextActiveBuilding`). The float→double mixing is preserved bit-for-bit
(the ratio is computed in float, widened for the blend — `office_prosperity.cpp:43-58`).

### 3.4 Per-building commit (`office_prosperity.cpp:62-90`)

Three deterministic commits in order:

1. **Wealth field** — write the averaged wealth `v25` to field **+476**.
2. **Prosperity delta** — `−(currentField480 − score)`; field +480 trends
   *toward* the new score (`QueueRequestArgs26(obj, 480, delta)`).
3. **AI decay delta** — `−(aiField180 − aiField180*0.95)`; the AI-method value
   at +180 decays by 5% each turn.

```cpp
r.prosperityDelta = -(currentField480 - r.score);
float aiTmp       = (float)((double)aiField180 * kProsperityAiDecay);
r.aiDecayDelta    = -(aiField180 - aiTmp);
```

> Note: the field-180 "AI method" decayed here is the *building's* AI value, a
> distinct field from the city `CityRecord.prunk` (splendour) array which also
> sits at record offset +180 (`types.h:71`) — do not conflate them; the +180 in
> the prosperity pass is the building object's AI-method slot.

---

## 4. City-level economic indicators

### 4.1 Economy parameter seed (`city.cpp`, `0x577a9c`)

`CityInitParameterTable` seeds the 28-good economy table (`g_goods`,
`kGoodCategoryCount = 28`). Each good gets three hand-tuned weights
(`city.cpp:80-107`):

* **`driftWeight`** (`word_1234750`) — price-drift multiplier per good.
* **`contribWeight`** (`word_1234752`) — how much the good contributes to the
  city-total money/goods aggregates.
* **`cap`** (`word_1234754`) — the per-good supply cap / equilibrium reference.

The literal seed arrays are in `city.cpp:84-97` (e.g. grain `driftWeight=300`,
luxury goods `cap=1000`, index 15 has the sentinel `65535`/`-1`). The city
totals `g_cityTotalMoney`/`g_cityTotalGoods` (`flt_641FD4`/`flt_641FD8`) and the
equilibrium divisor `g_capDivisor` (`flt_641DA8`) are the aggregates this
parameter table drives.

City records also carry two explicit economic-history series read from the
`.ini` (`city.cpp:203-218`, `types.h:70-71`):

* **`einwohner[10]`** — population growth pairs `(year, value)`.
* **`prunk[10]`** — *splendour* (Prunk) growth pairs — the scripted prosperity
  curve.

### 4.2 Population census — `AggregateDistrictStats` (`0x578abc`)

This is the per-round pass that derives the city's live economic indicators from
the **actual** residents and buildings on the map (distinct from the
AI-consumed population-trend score and the price EMA — see
`city_population_census.h:6-18`). It writes a 10-field broadcast `CityStats`
record (`city_population_census.h:107-117`):

| Field | Meaning |
|---|---|
| `density0` | interpolated law score (`out[0]`) |
| `ratioB1` | `1 − v46/count` — column-A resident presence ratio |
| `satScore2` | satisfaction × law fold |
| `ratioD3` | `1 − v49/count` — column-B presence ratio |
| `residents4` | live resident-slot count `v1` |
| `popDensity5` | population/occupancy density product |
| `v47sum7` | per-type `word_641DB4` sum |
| `popClamped8` | population sum `v3`, clamped `>= 2*word_641DB8` |
| `lawFold9` | final law/satisfaction composite |

The pass (`city_population_census.cpp:46-213`):

1. (Re)build the satisfaction grid.
2. **Loop 1** — bin residents into the 8×8 district grid; count live slots
   `v1`, and the two column presence counters `v46`/`v49`. Eligible residents
   add `1.0` to their district's resident bin (`GridSatCount`).
3. **Loop 2** — sum per-type population (`word_641DB0` → `v3`), occupancy
   (`word_641DB2` → `v48`), `word_641DB4` → `v47`, plus level bytes from the
   type record (`+583` security / `+584` max level).
4. **Population floor clamp**: `v3 = max(v3, 2*word_641DB8)`.
5. Density and ratio writes:

```text
popDensity5 = (v53/v50 * (v51/v52) + 0.5) * (v44/v3 * v48)   // v44 = (float)v1
ratioD3     = 1 - v49/v44
ratioB1     = 1 - v46/v44
```

6. **Satisfaction reduction** over the 8×8 grid (`city_population_census.cpp:153-182`):
   each cell contributes `SatWeight / SatDenom * SatCount`, where the count is
   normalised by `v1 * 2.0` and the denom is clamped `>= 1.0f`. The accumulator
   `v55` is the city-wide satisfaction product.

7. **Law fold** — `out[2]` (`satScore2`) and `out[9]` (`lawFold9`)
   (`city_population_census.cpp:184-211`):

```text
v39  = EconomyComputeWeightedLawScore()
v37  = GesetzGetRecord(0).threshold          // +24
v35  = GesetzGetRecord(1).threshold

satScore2 = (1 - (v39*0.7
                  + ((4-v37)*0.25*0.75 + (1-v35)*0.25) * 0.3)) * v55

lawFold9  = (0.25*( (1-ratioB1) + (1-ratioD3) + 1 - satScore2 + density0 )
             + (1-ratioD3)*2.0*(1-ratioB1)*density0*(1-satScore2)) * (1/3)
```

The constants (`0.7`, `0.25`, `0.75`, `0.3`, `1/3`) are the recovered
`popcensus::*` FP literals (`city_population_census.h:51-60`). `satScore2` is
the city's resident-satisfaction-weighted-by-law index — the headline
"satisfaction" indicator.

### 4.3 Per-district wealth grid — `ComputeWealthGrid` (`0x577e74`)

Builds the per-district **mean room-worth** grid: it accumulates each placed
resident's `roomWorth` into its district cell, counts occupants, then writes
back the truncated mean `worth/count` per cell (`city_population_census.cpp:223-257`).
Output is an 8×8 int grid (district `[x][y]` at `out[8*x+y]`). This is the
spatial wealth-distribution indicator.

### 4.4 Stats snapshot — `SnapshotStats` (`0x5783e4`)

Copies the 300-byte district stat block into the broadcast snapshot, copies four
scalar stat dwords, and truncates `g_capDivisor` (`flt_641DA8`) into the last
dword (`city_population_census.cpp:262-276`). This is what gets broadcast.

### 4.5 District satisfaction grid (`city_satisfaction_grid.cpp`)

The 8×8 grid (`g_cityGrid`, `0x1234988`) is the spatial backing store for both
**crime pressure** and **resident satisfaction** (the district "Ruf"/standing).
Economic-relevant pieces:

* **`BuildSatisfactionGrid` (`0x5788d4`)** — for each live, placed, eligible
  resident, adds `(scaled13/scale19 * weight18)` to its cell's satisfaction
  weight and spreads half (`kSatSpreadWeight = 0.5f`, `flt_62568C`) to the 3×3
  ring (`city_satisfaction_grid.cpp:185-230`). This is the per-resident
  satisfaction contribution that loop 5 of the census later reduces.

* **Crime accumulators** — `AddCrimeToGrid`/`RemoveCrimeFromGrid` (`0x577fc4` /
  `0x578110`) raise/lower a per-district crime weight at the centre cell plus a
  3×3 ring. Crime pressure depresses district satisfaction.

* The grid has an **intentional field overlap** (24-byte cell stride but fields
  reach +44) so cell (x,y)'s satisfaction fields alias cell (x,y+1)'s low
  fields — load-bearing and reproduced byte-exactly
  (`city_satisfaction_grid.h:34-42`); do not "fix".

* `SendSyncCommand`/`SendResetCommand` (`0x579460` / `0x5794e0`) broadcast the
  grid via opcode-86 network commands.

---

## 5. How these metrics feed back into prices / AI / events

The metrics are produced and consumed inside the per-round turn cycle. The
order is fixed by `VIBE_GameTick_BeginPlayerRound` (`amt.h:192-208`,
`sim/turn_driver.cpp:109-110`):

```text
Production -> Prosperity -> BuildingTax -> LoanRepayments -> OfficeWages -> UpdateOffices
```

### 5.1 Prices

* The **economy parameter table** (§4.1) directly sets price behaviour: per-good
  `driftWeight`, `contribWeight` and `cap` drive `EconomyComputePriceDeltas`.
  `economy.h:89-90`: for goods `g in [3,28)`, if `cap >= g_capDivisor` the delta
  is 0, else `raw = driftWeight * accum / g_capDivisor`. The city aggregates
  (`g_cityTotalMoney`, `g_capDivisor`) are the divisor/equilibrium.

* The **price-level EMA** (`EconomyTickPriceLevel`, `0x579098`,
  `economy_tick.h:106-123`) folds the demand into `g_capDivisor`:
  `target = (1 + s9) * g_cityTotalMoney`; on the first tick
  `g_capDivisor = target * 0.75`, thereafter an EMA with α=0.5 over
  `flt_641DAC`. The broadcast spread `flt_1235234 = (flt_641DAC −
  g_capDivisor)*1.03*…` is the price-trend signal. The driver
  (`play/turn_economy.cpp:38-44`) sums the resulting `g_goods[3..27].priceDelta`
  each turn.

* The **statistics report** (§2) reads the *same* per-good supply/demand
  accumulators that the price pass fills (`flt_1235090…`), so the report's four
  category totals are a downstream view of the price/demand state — not an
  independent feedback into prices.

### 5.2 AI

* **Prosperity** writes both the building's prosperity field (+480, the
  AI-visible "how well this office is doing") and a decayed **AI-method** value
  (+180, ×0.95/turn) — §3.4. The decay makes stale AI valuations fade. The
  `Amt` pass that drives this is `UpdateOfficeProsperity`
  (`turn_economy.cpp:72-84`).

* **`EconomyComputePopulationTrend` (`0x57a008`, `economy_tick.h:141-150`)** is
  the explicit AI-consumed scalar built from the same city indicators:

  ```text
  trend = clamp01_floor(growth + births + deaths)
  score = EconomyComputeWeightedLawScore()*0.30 + trend*0.65
  return EconomyComputeLawSatisfaction()*0.05 + score
  ```

  The demographic terms come from the `PopulationStats` / `g_capDivisor` the
  census (§4.2) maintains. The Meister AI's turn passes include
  `UpdateOfficeProsperity` among `ProcessLoanRepayments`, `RunBuildingTaxPass`,
  etc. (`ai/meisterai.cpp:24`).

### 5.3 Events / broadcast

* The statistics report (§2.7) broadcasts trend/weak-sector/luxury messages to
  all `kind==6` player slots — these surface as in-game economic news events.
* The census snapshot (§4.4) and the satisfaction grid (§4.5) are broadcast via
  the opcode-86 / `RequestBuildOp65` network commands
  (`CityTickStatsAndBroadcast`, `0x57919c`, `economy_tick.h:125-138`), pushing
  the city's economic indicators to all players each day-step.

---

## 6. Unknowns / flagged gaps

* **Rate-table seeding** — `dword_649A88` is runtime-seeded; the recovered code
  injects it via `ExchangeSetRateTable`. The seed source (which currency ids map
  to which multipliers, and where they are set) is not in these files.
* **Goods-trade fee semantics** — the spread base is `playerCash * 0.01`
  (player liquidity), which is unusual for a per-trade fee; reproduced verbatim
  but its design intent is inferred, not documented.
* **Accumulator block fill** — the report's `flt_1235090` accumulators and the
  exact mapping of the 4 good-slots to in-game goods are filled by the
  demand/supply pass elsewhere; only the *reduction* is recovered here.
* **Weakest-sector index** — explicitly *not* a strict argmin
  (`statistics_report.h:82-83`); the index ↔ string mapping for ties is the
  binary's exact chain, reproduced 1:1.
* **Trend/luxury string ids** (6170–6181) — known to be string-table ids; the
  actual rendered text is GUI-bound and not in scope.
* **Census injected inputs** — the two scan loops read live `Person`/`Object`
  BSS arrays surfaced as injected lists (`CityCensusInputs`); the production
  backend wiring is out of scope here.
* **`PlaceRandomCrime` world point** — the heightmap inverse (`TileToWorld`) is
  a render leaf; the recovered function returns the band tile, not true world
  coords (`city_satisfaction_grid.cpp:162-180`).
