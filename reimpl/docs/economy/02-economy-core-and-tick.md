# Economy Core & Simulation Tick

This document describes the **core economy model** of the C++ reimplementation of *The Guild / Europa 1400* (gilde.exe): the supply/demand/price-equilibrium kernel, the per-round economy tick, the quality and law-satisfaction scalars, and how the world/city aggregate state is computed and consumed. Everything below is grounded in the reconstructed source; original gilde.exe function addresses and global names are cited where the reimplementation records them.

## Source files analyzed

| File | Role |
|------|------|
| `src/world/types.h` | Record layouts: 756-byte `CityRecord`, 16-byte `GoodSlot` economy table |
| `src/world/economy.h` / `.cpp` | Supply/demand/price-delta kernel; profession classification; tuning constants |
| `src/world/economy_quality.cpp` (+ `economy_quality.h`) | Average-quality, industry/residential ratios, demand snapshot, law-satisfaction/weighted/interpolated law scores |
| `src/world/economy_tick.cpp` / `.h` | The per-round price-level EMA tick, the city day-step + broadcast, the population-trend score, law-range-ratio fill, state-struct copy |
| `src/world/city.cpp` / `city.h` | Owns the aggregate globals (`g_goods`, `g_capDivisor`, `g_cityTotalMoney`, `g_cityTotalGoods`) and `CityInitParameterTable` (seeds the 28-good table) |
| `src/world/world_economy2.cpp` / `.h` | **Not core economy** — office-appointment/dismissal/wage *dialog* kernels (payment, severance, appointment price, grid layout). Touched here only to disambiguate naming |
| `src/world/world_economy3.cpp` / `.h` | **Not core economy** — guild/office dialog decision kernels (candidate rating tiers, level-3 dispatch, guild-join fee). Disambiguated below |
| `src/play/turn_economy.cpp` / `.h` | The live glue: `RunEconomyTurn` — one game-day of the economy cascade, invokes `EconomyTickPriceLevel` |
| `src/play/game_day.h`, `src/play/game_day.cpp` | The recovered day order (`VIBE_GameTick_BeginPlayerRound @0x533188`) and cadence |

> **Naming caveat.** `world_economy2.*` and `world_economy3.*` are named "economy" but reconstruct the **office/guild dialog** family (`VIBE_Amt_*`, `VIBE_Guild_*`), not the supply/demand core. They are summarized in the [Adjacent "economy" modules](#adjacent-economy-modules-not-the-core-tick) section so they are not confused with the simulation tick. The genuine economy core lives in `economy.*`, `economy_quality.*`, and `economy_tick.*`.

---

## 1. High-level data model

### 1.1 The 28-good economy table (`GoodSlot`)

The heart of the economy is a fixed array of 28 interleaved good/profession slots. In gilde.exe this is four arrays sharing a 16-byte stride at `0x1234750`; the reimplementation models it as `GoodSlot[28]` (`src/world/types.h:139-151`):

```cpp
constexpr int kGoodCategoryCount = 28;

struct GoodSlot {            // 16-byte stride, gilde.exe 0x1234750
    u16   driftWeight;    // +0x00  word_1234750: price-drift multiplier
    u16   contribWeight;  // +0x02  word_1234752: city-total contribution weight
    u16   cap;            // +0x04  word_1234754: capacity / demand cap
    u16   pad6;           // +0x06  (high half of the +4 dword; unused)
    float accum;          // +0x08  flt_1234758: demand or supply accumulator
    float priceDelta;     // +0x0C  flt_123475C: price-delta output
};
```

This array is **owned by the world/city layer**, not by individual buildings. It is defined as a process-global in `src/world/city.cpp:14`:

```cpp
GoodSlot g_goods[kGoodCategoryCount];  // gilde.exe 0x1234750
```

Index `g` runs `0..27`. Slots `1` and `2` are the "money" categories; slots `3..27` are the "goods" categories (see §3.2). Slot `0` and slot `27` are effectively padding/sentinels in the various loops.

### 1.2 The aggregate economy globals

Four floats hold the per-city aggregate state (`src/world/city.cpp:15-17`, declared `extern` in `city.h:23-30`):

```cpp
float g_capDivisor    = 0.0f;  // gilde.exe flt_641DA8  — cap / equilibrium divisor
float g_cityTotalMoney = 0.0f; // gilde.exe flt_641FD4  — city "money" demand total
float g_cityTotalGoods = 0.0f; // gilde.exe flt_641FD8  — city "goods" demand total
```

- `g_capDivisor` (`flt_641DA8`) is the **equilibrium divisor** — the normalizing denominator for price deltas, ratios, and the population trend. It doubles as the price-tick EMA's "first-tick seen" sentinel (a bit value of `±0.0` means *uninitialized*).
- `g_cityTotalMoney` / `g_cityTotalGoods` are the two weighted demand totals, produced by `EconomyComputeGoodsDemand`.

### 1.3 The 756-byte City record (`CityRecord`)

Loaded from `gamedata/cities/<name>.ini` by `VIBE_City_LoadDefinitionIni @0x507144`. Stride 756, up to 9 records (self + 8 `NachbarStadt` neighbours) (`src/world/types.h:30-98`). The economy-relevant fields are the population/splendour growth tables and the law-id blocks:

- `einwohner[10]` (`+100`) and `prunk[10]` (`+180`) — `GrowthPair{year, value}` tables driving population/splendour growth by year.
- Law id blocks: `verfassung[12]` (+476, constitution), `finanz[12]` (+524, finance), `straf[12]` (+556, penal), `gilde[12]` (+604, guild), `kirche[12]` (+628, church), `diebeRaeuber[12]` (+656, thieves).
- `importGoods[16]` (+682) / `exportGoods[16]` (+714) — KONTOR trade-good object ids.

The City record is **not** mutated each tick; it is static configuration. The mutable per-day economy state is `g_goods` + the four aggregate floats + the broadcast/snapshot globals (§2.4).

### 1.4 Per-person inputs (`PersonEcoView`)

Demand/supply are accumulated from **per-person** inputs sourced from the canonical sim object array (gilde.exe iterates the 169-byte OBJECT/BUILDING array, *not* the Person array). Each contribution is reduced to a 2-field POD (`src/world/economy.h:35-38`):

```cpp
struct PersonEcoView {
    u8   need;        // TYPE_DESCRIPTOR+583 (dword_13CE294 + 589*objectType, +583)
    bool unemployed;  // OBJECT+39 == 0xFFFF in the original
};
```

The join (`OBJECT+39`, `typeDef+583`) is delegated to the sim adapter `guild::sim::PersonReadEcoInputs` via `EcoViewFromObject` (`economy.cpp:11-18`) so the byte-offset arithmetic lives in exactly one place.

---

## 2. The economy tick

### 2.1 Cadence — when does the tick run?

The economy advances **once per game day / player round**. The day order is recovered 1:1 from `VIBE_GameTick_BeginPlayerRound @0x533188`, with the pre-round clock advance from `VIBE_Command_ExAdvanceGameTick @0x498954` (`src/play/game_day.h:12-45`). The economy-owned steps within the day are:

| Step | Original | Owner |
|------|----------|-------|
| [3] `ComputeGoodsDemand` | `0x578438` | RunEconomyTurn (demand re-roll, pre-round) |
| [5] `ComputeWealthGrid` | `0x577e74` | preamble leaf |
| [10] `RecalcAllProduction` | `0x5333be` | preamble leaf |
| [12] `AmtRunProductionPass` | `0x533426` | production |
| [13] `AmtUpdateOfficeProsperity` | `0x533439` | prosperity |
| [14] `AmtRunBuildingTaxPass` | `0x533451` | treasury / tax |
| [15] `AmtProcessLoanRepayments` | `0x533456` | loan interest |
| [16] `AmtProcessAllOfficeWages` | `0x533469` | office wages |
| [17] `AmtUpdateOffices` | `0x53346e` | office commit |
| [21] `CityTickStatsAndBroadcast` | `0x5334bf` | **price/city EMA + broadcast** |

The whole day collapses to three sub-turn invocations; `RunEconomyTurn` owns steps `3, 5, 10, 12..17, 21` (`game_day.h:53-55`). It runs **after** the clock advances (so production integrates over the new calendar day) and **before** the AI turn (the AI reads the economy outputs).

The two terminal economy functions are siblings:
- `VIBE_Economy_TickPriceLevel @0x579098` — the per-round **price-level** EMA; in the live game it is driven from `ExAdvanceGameTick @0x498954`.
- `VIBE_City_TickStatsAndBroadcast @0x57919c` — the city **day-step** sibling, called as step [21].

### 2.2 Ordering within `RunEconomyTurn`

`RunEconomyTurn` (`src/play/turn_economy.cpp:166-220`) replays the recovered Amt pass order via `world::AmtRunTurnCycle` (the order table recovered from the same `0x533188` driver), then runs the final price tick. Per turn it:

1. Re-rolls each good's `need` from the seeded RNG (`turn_economy.cpp:185-187`) — demand drifts so the economy evolves rather than converging to a fixed point.
2. Replays the Amt passes in order: **Production → Prosperity → BuildingTax → LoanRepayments → OfficeWages → UpdateOffices** (`RunPass`, `turn_economy.cpp:55-128`).
3. Runs the final stat tick: `state.priceLevel = world::EconomyTickPriceLevel(state.demand.data())` (`turn_economy.cpp:203`).

(`turn_economy.cpp` is *glue*, not rule logic — it does not reconstruct any economy formula; it drives the real reconstructed siblings.)

### 2.3 The demand kernel — `EconomyComputeGoodsDemand` (`0x578438`)

`src/world/economy.cpp:51-91`. Steps:

1. Zero every slot's accumulator: `g_goods[g].accum = 0` for `g` in `0..27`.
2. For each profession/good index `g` in `[1, 27]`, classify the profession (§2.6) and accumulate one `term` per person:

```cpp
// Flat   (class 3):
term = need * kDemandClass3;                      // 1.5

// Service (class 7,15,19, or >22):
term = need * kDemandSlopeB + kBias;              // 3.0*need - 1.0
if (unemployed) term *= kEmployedScale;           // *0.5

// Default (everything else):
term = need * kDemandSlopeA + kBias;              // 2.0*need - 1.0
if (unemployed) term *= kEmployedScale;           // *0.5

g_goods[g].accum = (float)(term + g_goods[g].accum);
```

3. Compute the two **city demand totals** (weighted by each slot's `contribWeight`, gilde `word_1234752`):

```cpp
g_cityTotalMoney = contribWeight[1]*accum[1] + contribWeight[2]*accum[2];
g_cityTotalGoods = sum_{g=3..27} contribWeight[g]*accum[g];
```

So **money** = goods 1 & 2; **goods** = goods 3..27.

The demand tuning constants (`src/world/economy.h:42-54`, recovered byte-for-byte from gilde.exe `dbl_625634..dbl_62567C`):

```cpp
kEmployedScale  = 0.5;  // dbl_62563C  unemployed extra factor
kDemandSlopeA   = 2.0;  // dbl_625634  default-class slope
kDemandSlopeB   = 3.0;  // dbl_625644  service-class slope
kBias           = -1.0; // dbl_625654  additive bias
kDemandClass3   = 1.5;  // dbl_62564C  profession-class-3 (flat) weight
```

### 2.4 The supply kernel — `EconomyComputeGoodsSupply` (`0x578634`)

`src/world/economy.cpp:95-118`. Identical shape to demand but for **one** good index `g` at a time (the original advances an external index word `dword_641FCE` from 1..28). It resets `g_goods[g].accum` then accumulates with the *supply* constants (`economy.h:48-52`):

```cpp
kSupplySlopeA   = 2.0;  // dbl_62565C
kSupplyEmpScale = 0.5;  // dbl_625664
kSupplySlopeB   = 3.0;  // dbl_62566C
kSupplyBias     = -1.0; // dbl_62567C
kSupplyClass3   = 1.5;  // dbl_625674
```

These are numerically identical to the demand constants but recovered as a **separate** byte block in the binary, so they are kept distinct. Supply does **not** compute the city totals — it only fills one accumulator slot.

### 2.5 The price-delta kernel — `EconomyComputePriceDeltas` (`0x5787d4`)

`src/world/economy.cpp:124-156`. For each good `g` in `[3, 27]`:

```cpp
if ((double)g_goods[g].cap >= g_capDivisor) {     // cap meets/exceeds equilibrium
    g_goods[g].priceDelta = 0.0f;                  // no movement
    continue;
}
float raw = (float)((double)g_goods[g].driftWeight * g_goods[g].accum / g_capDivisor);

if (g == 4 || g == 16) {        // INVERTED goods
    delta = (raw >= 1.0) ? -(raw + kDeltaTail)     //  -(raw - 1)
                         :  (1.0 - raw);
} else {                        // NORMAL goods
    delta = (raw >= 1.0) ?  (raw + kDeltaTail)     //  (raw - 1)
                         : -(1.0 - raw);
}
g_goods[g].priceDelta = (float)delta;
```

`kDeltaTail = -1.0` (gilde `dbl_625684`, `economy.h:53`). So a normal good with `raw >= 1` moves price up by `raw - 1`; below equilibrium it moves down by `1 - raw`. Goods **4 and 16** invert this sign (these are the price-inverted categories). The walk is bounded to `[3,27]` (the original walks `v0=3..28`, where 28 would touch one slot past the table; behaviour is identical for every in-bounds slot — `economy.cpp:127-131`).

### 2.6 Profession classification — `ClassifyProfession`

Shared by demand and supply (`src/world/economy.cpp:42-48`):

```cpp
ProfClass ClassifyProfession(int profIndex) {
    if (profIndex == 3)                                          return Flat;     // class 3
    if (profIndex == 7 || 19 || 15 || profIndex > 22)           return Service;  // slope B
    return Default;                                                               // slope A
}
```

### 2.7 The price-level tick — `EconomyTickPriceLevel` (`0x579098`)

`src/world/economy_tick.cpp:76-115`. Advances the **smoothed price level** `g_smoothedPrice` (gilde `flt_641DAC`) one round:

```cpp
EconomyComputeGoodsDemand(persons);              // refreshes g_cityTotalMoney
double s9 = EconomyLoadDemandSnapshot(snap);     // 0x57a5dc; patches snap[6]=divisor, returns snap[9]
float target = (float)((1.0 + s9) * g_cityTotalMoney);

if ((bits(g_capDivisor) & 0x7FFFFFFF) != 0) {    // not first tick (capDivisor != ±0.0)
    g_smoothedPrice = (target - g_smoothedPrice) * kPriceEmaAlpha + g_smoothedPrice;   // EMA, alpha 0.5
} else {                                          // FIRST tick
    g_smoothedPrice = target;
    g_capDivisor    = (float)((double)target * kPriceInitFactor);                      // seed = target*0.75
}
EconomyComputePriceDeltas();                     // 0x5787d4
SnapshotBroadcastClock();
g_broadcastSpread = (float)((g_smoothedPrice - g_capDivisor) * kBroadcastScaleA * kBroadcastScaleB);
return (int)ConvertX(g_smoothedPrice);           // x87 round-toward-zero (truncate)
```

Constants (`economy_tick.h:51-58`, recovered bit patterns in `economy_tick.cpp:14-25`):

```cpp
kPriceEmaAlpha   = 0.5f;                  // flt_6256C8 — EMA blend factor
kPriceInitFactor = 0.75;                  // dbl_6256CC — first-tick divisor seed
kBroadcastScaleA = 1.03;                  // dbl_6256D4 / dbl_6256F4
kBroadcastScaleB = 0.0007122507122507123; // dbl_6256DC / dbl_6256FC
```

The EMA is a standard `new = (target - old)*0.5 + old`. The first tick seeds both the smoothed price (`= target`) **and** the equilibrium divisor (`= target * 0.75`). The `& 0x7FFFFFFF` test masks the sign bit, so the "first tick" branch fires only when `g_capDivisor` is exactly `+0.0` or `-0.0` (this is why `SeedEconomyTurnState` sets `g_capDivisor = 0.0f` to force the seed branch — `turn_economy.cpp:137`).

### 2.8 The city day-step + broadcast — `CityTickStatsAndBroadcast` (`0x57919c`)

`src/world/economy_tick.cpp:140-187`. The sibling of the price tick, called as the round's **last** economy step. It takes the 13-float district aggregate (`VIBE_City_AggregateDistrictStats`, owned by the city-stats agent, passed in as `stats[13]`) and:

1. Copies the first 10 floats into the demand snapshot.
2. EMAs the smoothed price toward `snap[5]` with `kCityEmaAlpha = 0.5` (gilde `flt_6256E4`); on the first tick seeds `g_capDivisor = snap[5] * kCityInitFactor` where `kCityInitFactor = 0.95` (gilde `dbl_6256EC`).
3. Assembles an 11-dword (44-byte) broadcast body: `body[0..4]=snap[0..4]`, `body[5]=g_smoothedPrice`, `body[6]=g_capDivisor`, `body[7..9]=snap[7..9]`, `body[10]=g_broadcastSpread` where the spread uses the same `(price - divisor)*1.03*0.000712...` formula.

The 44-byte body is emitted to an out-parameter; the actual network send (`VIBE_Command_RequestBuildOp65`) is the caller's responsibility (the shim boundary).

### 2.9 Mutated broadcast/display globals

Modeled as module-local settable state in `economy_tick.cpp:30-43`, mirroring the original's runtime globals:

| Symbol | gilde.exe | Meaning |
|--------|-----------|---------|
| `g_smoothedPrice` | `flt_641DAC` | the EMA-smoothed price level |
| `g_broadcastSpread` | `flt_1235234` | broadcast spread `(price-divisor)*1.03*0.000712...` |
| `g_clock` / `g_clockSnapshot` | `qword_13CE852` / `qword_1235262` | 22-byte game clock + broadcast copy |
| `g_stateStruct` | `dword_1235238` | 36-byte broadcast state struct |
| `g_demandSnapshot` | `dword_1234910` | 10-float (40-byte) demand snapshot (lives in `economy_quality.cpp:88`) |

---

## 3. The quality model (`economy_quality.cpp`)

"Quality" here means the **mean good/output quality** across the active object array, plus a cluster of normalized "economic-health" scalars (industry/residential pressure ratios and three law-derived scores).

### 3.1 Average quality — `EconomyComputeAverageQuality` (`0x579a38`)

`src/world/economy_quality.cpp:35-46`. The original walks the 169-byte object array (`dword_13CE298`) for object indices 1..27; for each **occupied** object it reads its type byte and indexes the 589-byte type table (`dword_13CE294`), summing two type bytes:

```cpp
numerator   += typeDef[type] + 583;   // the need/security byte
denominator += typeDef[type] + 584;   // the adjacent capacity byte
// ...
return (denominator == 0) ? 0.0
                          : (float)((double)numerator / (double)denominator);
```

So **quality = Σ(need bytes) / Σ(capacity bytes)** over occupied objects, with a divide-by-zero guard returning 0. The result is cast through `float` before return (mirroring the original `COERCE` to 32-bit). Each occupied object's contribution is the `(typeDef+583, typeDef+584)` pair, surfaced as `ObjectQualityView{qualityNeed, qualityCapacity}` (`economy_quality.h:73-76`); empty objects (type byte 0) contribute nothing.

### 3.2 Industry & residential pressure ratios

Both share a clamp shape (`ClampRatio`, `economy_quality.cpp:57-66`):

```cpp
double ClampRatio(float total, float divisor) {
    double ratio = (total - divisor) * (1.0f / divisor);  // == (total - divisor)/divisor
    if (ratio >= kRatioFloor && ratio > 1.0) return 1.0;   // upper clamp to +1
    if (ratio <  kRatioFloor)                return -1.0;   // lower clamp to -1
    return (float)ratio;                                   // pass-through
}
```

with `kRatioFloor = -1.0` (gilde `dbl_62577C`/`dbl_625784`). The two ratios:

```cpp
EconomyComputeIndustryRatio()     = ClampRatio(g_cityTotalGoods, g_capDivisor);  // 0x57a3c8
EconomyComputeResidentialRatio()  = ClampRatio(g_cityTotalMoney, g_capDivisor);  // 0x57a474
```

So **industry ratio** is the clamped pressure of the city *goods* total over the equilibrium divisor; **residential ratio** is the same over the *money* total. Both land in `[-1, +1]`.

### 3.3 Demand snapshot — `EconomyLoadDemandSnapshot` (`0x57a5dc`)

`src/world/economy_quality.cpp:95-102`. Copies the 10-float `g_demandSnapshot` block into `out`, patches `out[6] = g_capDivisor`, and returns `out[9]`. A null `out` returns 0.0. This `out[9]` is exactly the `s9` value the price tick multiplies into its `target = (1 + s9) * money` (§2.7).

### 3.4 Law-satisfaction — `EconomyComputeLawSatisfaction` (`0x57a520`)

`src/world/economy_quality.cpp:113-121`. A two-term score from law records 0 and 1 (their `+24` threshold field, read via `GesetzGetRecord`):

```cpp
return (4 - law0.threshold) * 0.25 * 0.75  +  (1 - law1.threshold) * 0.25;
//                            ^kLawSatScale ^kLawSatHiFactor          ^kLawSatLoFactor
```

Constants `kLawSatScale=0.25` (`flt_62578C`), `kLawSatHiFactor=0.75` (`dbl_625794`), `kLawSatLoFactor=0.25` (`dbl_62579C`).

### 3.5 Weighted law score — `EconomyComputeWeightedLawScore` (`0x57a580`)

`src/world/economy_quality.cpp:132-144`. A weighted sum of law thresholds for laws **16..25** (10 records):

```cpp
float sum = 0;
for (i in 0..9) sum = (float)(law[16+i].threshold * kWeightedLawWeights[i] + sum);
return sum;
```

Weights `kWeightedLawWeights` (gilde `dword_577A24`, `economy_quality.h:50-51`):
```
0.06 0.05 0.08 0.04 0.13 0.10 0.13 0.12 0.14 0.15
```
(The accumulator is a `float` in the original; each product is computed in `double` then narrowed back — preserved faithfully.)

### 3.6 Interpolated law score — `EconomyComputeInterpolatedLawScore` (`0x57a990`)

`src/world/economy_quality.cpp:158-178`. For laws **8..14** (7 records), interpolate each record's `value` within its `[lo, hi]` range and accumulate the weighted `(1 - t)`:

```cpp
float sum = 0, term = 0;
for (i in 0..6) {
    float t = (float)((double)(value - lo) / (hi - lo));     // record fields +4/+8/+24
    term = (float)(kInterpolatedLawWeights[i] * (1.0 - t));
    sum += term;
}
return (double)sum + (double)term;   // last term DOUBLE-COUNTED (faithful unrolled-tail quirk)
```

Weights `kInterpolatedLawWeights` (gilde `dword_577A4C`): `0.30 0.25 0.12 0.10 0.05 0.08 0.10`. The final return **double-counts the last term** — this is a faithful reproduction of an unrolled-loop tail quirk in the original, not a bug in the port (`economy_quality.cpp:176-177`).

> **Note the offset divergence.** `ComputeInterpolatedLawScore` reads the law record at `+4/+8/+24`, whereas `EconomyFillLawRangeRatios` (`0x57aa30`, §4.2) reads `+0/+4/+20`. Both are preserved exactly as the two distinct originals (`economy_tick.cpp:268`).

---

## 4. Prosperity / economic-health aggregation

### 4.1 Population trend — `EconomyComputePopulationTrend` (`0x57a008`)

`src/world/economy_tick.cpp:205-257`. This is the **single scalar the AI consumes** as the city's economic/demographic health. It combines a clamped population-growth trend with the weighted-law and law-satisfaction scores. Inputs come from `PopulationStats` (gilde `word/flt_1234xxx` triples filled by `VIBE_City_AggregateDistrictStats`, `economy_tick.h:73-83`) and the divisor `flt_641DA8`:

```cpp
double div = g_capDivisor;

// growth (v15): scaled prior count + current - div, normalized
growth = (prevCount*prevScale + curCount - div) / div;
t1 = max(growth, -1.0);
if (curCount > div && t1 < 0.0) t1 = 0.0;          // gate

// births (negative pressure, gated by birthsCmp)
births = (birthsCmp >= div) ? 0.0
       : max(-(birthsCount*birthsScale)/div, -1.0);

// deaths (negative pressure, gated by deathsCmp)
deaths = (deathsCmp >= div) ? 0.0
       : max(-(deathsCount*deathsScale)/div, -1.0);

float sum = (float)(t1 + births + deaths);
trend = (sum < 1 && sum <= -1.0) ? -1.0 : min(sum, 1.0);   // clamp to [-1, +1]

score  = EconomyComputeWeightedLawScore()*kLawWeight + trend*kPopWeight;   // 0.30 / 0.65
return   EconomyComputeLawSatisfaction()*kLawSatWeight + score;            // 0.05
```

Weights (`economy_tick.h:60-63`): `kTrendFloor=-1.0` (`dbl_62574C`), `kLawWeight=0.30` (`flt_625754`), `kPopWeight=0.65` (`flt_625758`), `kLawSatWeight=0.05` (`dbl_625774`). So the final health scalar is:

```
health = lawSatisfaction*0.05  +  weightedLawScore*0.30  +  populationTrend*0.65
```

This is where prosperity is aggregated at the **city/world level**: law state (constitution/finance/etc. thresholds) at 35% weight, clamped demographic trend at 65%.

> Note on naming: a *separate* per-**building** prosperity score (field +480, with a `*0.95` AI-method decay at field +180) is computed by `world/office_prosperity.h` `ProsperityUpdateBuilding` and driven by the `Prosperity` Amt pass (`turn_economy.cpp:72-85`). That building-level prosperity is a distinct subsystem from the city-level `EconomyComputePopulationTrend` health scalar; both feed the AI.

### 4.2 Law-range-ratio fill — `EconomyFillLawRangeRatios` (`0x57aa30`)

`src/world/economy_tick.cpp:270-291`. For laws 8..14 writes `out[8+i] = (value - lo) / (hi - lo)` using record fields `+0/+4/+20` (the `…FromTable` variant reads them live from `g_lawTable` via `GesetzGetRecord`). This fills a display/AI float array; it is a sibling of the interpolated law score but with a different field triple.

### 4.3 State-struct copy — `CityCopyStateStruct` (`0x579448`)

`src/world/economy_tick.cpp:297-300`. A 36-byte `memcpy` of the broadcast state struct (`dword_1235238`) into the caller's buffer — pure plumbing for the broadcast/UI path.

---

## 5. Global constants reference

All values are recovered byte-for-byte from gilde.exe; see the cited headers.

| Constant | Value | gilde.exe | Used by |
|----------|-------|-----------|---------|
| `kGoodCategoryCount` | 28 | (table size) | everywhere |
| `kEmployedScale` / `kDemandSlopeA` / `kDemandSlopeB` / `kBias` / `kDemandClass3` | 0.5 / 2.0 / 3.0 / -1.0 / 1.5 | `dbl_62563C/625634/625644/625654/62564C` | demand |
| `kSupplyEmpScale` / `kSupplySlopeA` / `kSupplySlopeB` / `kSupplyBias` / `kSupplyClass3` | 0.5 / 2.0 / 3.0 / -1.0 / 1.5 | `dbl_625664/62565C/62566C/62567C/625674` | supply |
| `kDeltaTail` | -1.0 | `dbl_625684` | price deltas |
| `kPriceEmaAlpha` / `kPriceInitFactor` | 0.5 / 0.75 | `flt_6256C8` / `dbl_6256CC` | price tick |
| `kCityEmaAlpha` / `kCityInitFactor` | 0.5 / 0.95 | `flt_6256E4` / `dbl_6256EC` | city day-step |
| `kBroadcastScaleA` / `kBroadcastScaleB` | 1.03 / 0.0007122507122507123 | `dbl_6256D4` / `dbl_6256DC` | broadcast spread |
| `kTrendFloor` / `kLawWeight` / `kPopWeight` / `kLawSatWeight` | -1.0 / 0.30 / 0.65 / 0.05 | `dbl_62574C` / `flt_625754` / `flt_625758` / `dbl_625774` | population trend |
| `kRatioFloor` | -1.0 | `dbl_62577C` / `dbl_625784` | industry/residential ratios |
| `kLawSatScale` / `kLawSatHiFactor` / `kLawSatLoFactor` | 0.25 / 0.75 / 0.25 | `flt_62578C` / `dbl_625794` / `dbl_62579C` | law satisfaction |
| `kWeightedLawWeights[10]` | 0.06,0.05,0.08,0.04,0.13,0.10,0.13,0.12,0.14,0.15 | `dword_577A24` | weighted law score |
| `kInterpolatedLawWeights[7]` | 0.30,0.25,0.12,0.10,0.05,0.08,0.10 | `dword_577A4C` | interpolated law score |

`CityInitParameterTable(capDivisor)` (`src/world/city.cpp:80-107`) seeds the 28-good table with per-good `driftWeight`, `contribWeight`, `cap` arrays (`kDriftWeight`/`kContribWeight`/`kCap`, `city.cpp:84-97`) and sets `g_capDivisor` from its argument. Note `driftWeight[10] == 65535` (`0xFFFF`, the original's `-1` slot) and `driftWeight[0..2] == 0` (the money/sentinel slots are not price-driven).

---

## 6. Interactions with other subsystems (touchpoints)

The economy core is a hub; named touchpoints only:

- **Production** (`world/production.h`): the `Production` Amt pass calls `ProductionComputeOutputOverTime` to integrate the day's work window; the credited work-minutes feed the treasury. `RecalcAllProduction (0x583c3c)` is a pre-round preamble.
- **Market** (`src/play/slice_market.cpp:112`): reads `g_goods[g].priceDelta` (the output of `EconomyComputePriceDeltas` / `EconomyTickPriceLevel`) to move market prices.
- **Sim object/person arrays** (`sim/person_record.h`): the demand/supply/quality kernels read per-object inputs through `PersonReadEcoInputs` / `EcoViewFromObject` (the `OBJECT+39`/`typeDef+583/+584` join).
- **Law** (`world/law.h`): `GesetzGetRecord` / `g_lawTable` supply the law thresholds for the satisfaction/weighted/interpolated/range-ratio scores.
- **AI**: consumes `EconomyComputePopulationTrend` (city health), the industry/residential ratios, and the per-building prosperity score (`office_prosperity.h`).
- **City config** (`world/city.cpp`): owns the aggregate globals and `CityInitParameterTable`; the 756-byte `CityRecord` provides static growth/law/trade configuration.
- **Office/guild dialogs** (`world_economy2.*`, `world_economy3.*`): the appointment/wage/severance and guild-join-fee kernels (see below) — these *price* office actions but are not part of the per-round simulation tick.
- **Broadcast/UI**: `CityTickStatsAndBroadcast` emits the 44-byte body to `VIBE_Command_RequestBuildOp65`; the game clock (`qword_13CE852`) is snapshotted into the broadcast block.

---

## Adjacent "economy" modules (NOT the core tick)

For completeness — these files carry the "economy" name but reconstruct **office/guild dialog kernels**, not the supply/demand tick:

**`world_economy2.*`** — office-action dialog kernels (`VIBE_Amt_*`):
- `ComputeOfficePaymentAmount` = `trunc(((rand+1)*(rating*15.0)+5.0)*2.55)` (`kPayRatingScale=15`, `kPayBase=5`, `kPayFinalScale=2.55`).
- `ComputeAppointmentPrice(rating, kind)`: kind==1 → `(rand+1)*(rating*0.4)+1.2`; else `1.0 - ((rand+1)*(rating*0.3)+0.2)`.
- `RollActionDirection`: `roll <= 0.3 ? -1 : 1`.
- `CountOfficeDependents`: 768-person dismissal-severance split (`totalAmount / dependentCount`).
- `ComputeGridLabelOffset`, `ScanMaxCandidateRank` (office-slot grid + candidate rank scan).

**`world_economy3.*`** — guild/office decision kernels (`VIBE_Guild_*`):
- `GuildCandidateOutputRatingId`: output tiers at 100/350/650 → text ids 1605/1604/1603/1602.
- `GuildLevel2JoinFee` = `trunc(max(160.0, wealth * 0.01))` (`kGuildJoinFeeRate=0.01`, `kGuildJoinFeeFloor=160`).
- `GuildLevel3RankAction` / `GuildLevel3DialogForDefKind` / `GuildLevel3ContactStatusId`: rank→dialog and def-kind (30/31/32/33)→dialog/contact-id dispatch.
- `GuildOfficeNameTextId`: promoted → `defKind+560`, else `defKind+525`.

These are summarized here only to prevent confusion with the genuine economy core; the per-round simulation tick is entirely contained in `economy.*`, `economy_quality.*`, and `economy_tick.*`.

---

## Open / unclear points

- **`EconomyComputeGoodsSupply` invocation.** Supply is computed one good at a time (`dword_641FCE` index walk in the original), but the reconstructed sources here do not show a per-round driver that walks all 28 supply slots the way `EconomyComputeGoodsDemand` is driven by the price tick. Supply appears to be invoked elsewhere (likely a market/trade path) not captured in the files analyzed; its caller is not pinned down here.
- **`PopulationStats` fill.** `VIBE_City_AggregateDistrictStats` (the 13-float district reduction that populates both the `stats[13]` broadcast input and the demographic triples) is referenced as owned by the "city-stats agent" but is not reconstructed in these files; the population-trend inputs are surfaced as a settable POD (all zero in the static image).
- **Supply/demand constant duplication.** The demand and supply tuning constants are numerically identical (2.0/3.0/0.5/-1.0/1.5) but stored as two separate byte blocks in gilde.exe; whether the originals ever diverge in other game versions is not determinable from this binary alone.
