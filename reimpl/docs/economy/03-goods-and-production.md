# Economy System — 03. Goods & Production

A reverse-engineering reference for the goods catalog and the production pipeline of
*The Guild / Europa 1400* (`gilde.exe`), as recovered in this C++ reimplementation.
Everything below is grounded in the translated source; original function addresses,
global symbol names, and recovered float constants are cited inline. Where the
binary's behaviour could not be fully recovered (cross-module joins routed through
hooks, multi-component aggregation, etc.) it is flagged explicitly as **UNKNOWN /
partial**.

---

## Source files

Core production / goods code analyzed for this document:

| File | Role |
|------|------|
| `src/world/production.cpp` / `.h` | Work-minute integration over game-time (the "how long did the workshop work" clock) |
| `src/sim/building_production.cpp` / `.h` | Production-slot table, market price, the per-building production tick |
| `src/sim/production_slots.cpp` / `.h` | Production work-slot inventory rules, slot-capacity table, the per-NPC production-timer countdown |
| `src/sim/building_value.cpp` / `.h` | Per-item base value, production rate, current/max output interpolation |
| `src/sim/building_stock.cpp` / `.h` | Stock projection/decay, efficiency, sale price, goods-to-customer distribution, production worth |
| `src/sim/building_storage.cpp` / `.h` | Storage-room item worth, stock value |
| `src/sim/trade_sell.cpp` / `.h` | The produce-and-sell recipe engine (`ExComputeSellableAmount`) |
| `src/world/market_price_model.h` | Standalone full market-price model (recipe recursion + cache) |
| `src/world/amt_goods.cpp` / `.h` | Per-turn business replenishment / goods-distribution pass |
| `src/world/economy_quality.cpp` | City-wide quality/ratio aggregates (peripheral to production) |
| `src/sim/types.h` | `ItemType` good-id enum, record layouts (Person/Object/SceneNode/Item) |
| `src/sim/building_types.h` | `BuildingTypeDef` (recipe factors), `BuildingRec` live fields |
| `src/world/data_load.cpp` / `.h` | Loader for the building/object type tables from `A_Geb.dat` / `A_Obj.dat` |

**Data sources** (binary tables shipped with the game):

- `data/A_Geb.dat` — the **building-type** table. Records are **589 bytes** each
  (`kBuildingTypeStride`). The shipped file is 113088 bytes = **192 records**, but
  the loader reads only the first **72** (`kBuildingTypeLoadCount`,
  `src/world/data_load.h:43`). Loaded into `g_buildingTypes` (orig `dword_13CE294`)
  via `WorldLoadBuildingAndObjectData`, `src/world/data_load.cpp:183`.
- `data/A_Obj.dat` — the **object / good (scene-type)** table. Records are **65
  bytes** each (`kSceneTypeStride`). The shipped file is 66560 bytes = **1024
  records**, of which the loader reads **731** (`kSceneTypeLoadCount`,
  `data_load.h:44`). Loaded into `g_sceneTypes` (orig `dword_13CE27C`),
  `data_load.cpp:187`.

> Note: in the live `gilde.exe` these tables are mounted out of the resource
> archives (`Objects.BIN`, etc.); the reimplementation reads the loose
> `A_Geb.dat` / `A_Obj.dat` extracts directly. The record strides and counts are
> byte-faithful to the original `VIBE_File_Read(..., 0x24D, 72)` /
> `VIBE_File_Read(..., 0x41, 731)` calls (`data_load.cpp:182,186`).

---

## 1. The goods catalog

### 1.1 How a good is identified

A good is identified by a small integer **prototype id** (variously called *prot*,
*type*, or *goodId* in the originals). The same id space indexes:

- the **scene/object type table** (`g_sceneTypes`, 65-byte `SceneTypeDef`,
  indexed `prot * 65`), and
- the inventory item header (`ItemRec.type` at node `+0`, `src/sim/types.h:179`).

There is no separate human-readable "goods enum" in the binary — goods are data
rows in `A_Obj.dat`. Only a handful of ids carry **special inventory semantics**,
and those are the only ones the code names. From `enum ItemType`
(`src/sim/types.h:189`):

```cpp
enum ItemType : i16 {
    kItemCurrency   = 9,    // money good (special storage handling)
    kItemReserveA   = 42,   // effective stock = count - 1
    kItemReserveB   = 278,  // effective stock = count - 1
    kItemReserveC   = 475,  // effective stock = count - 1
    kItemReserveD   = 476,  // effective stock = count - 1
    kItemHighCap    = 477,  // slot capacity = 5*count + 10
    kItemSpecialA   = 377,  // carry-capacity special (capacity 0)
    kItemSpecialB   = 378,  // carry-capacity special (capacity 0)
};
```

Semantics of the named ids:

- **Currency (9)** — the money good; gets special storage handling
  (`src/sim/types.h:177`).
- **Reserve goods (42, 278, 475, 476)** — their *effective* stock is `count - 1`,
  i.e. one unit is always held back and never consumable. This is applied in
  `IsReserveGood` (`src/sim/building_storage.cpp:19`) and again in the
  effective-stock rule used by the recipe/capacity engines
  (`src/sim/inventory_capacity.h:21`). Note `477` is *also* treated as a reserve
  good in the capacity path (`trade_sell.h:125`).
- **High-capacity good (477)** — overrides the per-slot capacity formula to
  `5*level + 10` (see §4.1).
- **Carry-capacity specials (377, 378)** — carried-item slots with capacity 0
  (`src/sim/types.h:196`).

### 1.2 Good categories (the `SceneTypeDef` "kind")

Each good/object row has a leading **kind** byte. The production engine branches
on these kinds (`SceneTypeDef.kind`, `src/sim/building_production.h:60`):

| kind | meaning (as used by production) | source |
|------|---------------------------------|--------|
| 2    | storage / room                  | `building_production.h:60` |
| 6    | storage / room (sale room)      | `building_production.h:60`, `building_storage.cpp:106` |
| 23   | "raw" / special good — recipe is skipped, price uses the `*2.2` gain | `building_production.cpp:149`, `market_price_model.h:25` |
| 37   | special — excluded from slot input/output (treated like 23) | `building_production.cpp:179` |

The `SceneTypeDef` row (`src/sim/building_production.h:59`) is the price-relevant
view; its named fields are:

```cpp
struct SceneTypeDef {
    u8  kind;            // +0   2/6 storage, 23/37 special
    u8  subtype;         // +33  ==3 -> *1.5 market multiplier
    i32 baseValue;       // +34  base value (numerator of the ratio)
    u16 priceField;      // +38  per-component price field
    i32 compType;        // +44  component type; high word (+46) = component prot id
    u16 divisor;         // +54  value divisor (0 treated as 1)
    i32 cachedPrice;     // +56  cached computed price (0 = recompute)
};   // stride 65
```

### 1.3 Building (workshop) types

Workshops are rows in the 589-byte `BuildingTypeDef` table. The production-relevant
fields (`src/sim/building_types.h:86`):

```cpp
struct BuildingTypeDef {
    u8  kind;              // +0    category enum (1..0x1A)
    u16 roomList[64];      // +35   room/object type ids, 0-terminated; bit 0x8000 = "present"/flagged
    u8  outputProf;        // +547  output-product profession code
    u8  inputFactor[2];    // +553  input-good factor [0..1]
    u8  inputProf;         // +559  input-product profession code
    u8  outputFactor[2];   // +563  output/production factor [0..1]
    u8  security;          // +583  security level (== current upgrade level)
    u8  maxUpgradeLevel;   // +584
    i32 roomWorthMul;      // +585  room-worth base multiplier
};   // stride 589
```

The **kind** byte classifies a building (`src/sim/building_types.h:59`):

```cpp
namespace BuildingTypeKind {
constexpr u8 kStorage     = 10;          // IsStorageType
// IsProductionType: kinds 11,12,13,16,28
constexpr u8 kProduction1 = 11;
constexpr u8 kProduction2 = 12;
constexpr u8 kProduction3 = 13;
constexpr u8 kProduction4 = 16;
constexpr u8 kProduction5 = 28;
}
```

`Building_IsStorageType` / `Building_IsProductionType` (`src/sim/building.h:89`)
test these. The **recipe** of a workshop is encoded by its `inputFactor[2]` and
`outputFactor[2]` bytes plus the `inputProf`/`outputProf` profession codes — see
§2.

---

## 2. Production recipes (inputs → outputs)

There are **two** distinct recipe representations in the code, used by different
parts of the engine:

### 2.1 Factor-based recipe (the building-type table)

The "macro" production model keys off the `BuildingTypeDef` factor bytes. A
workshop's *production rate* is derived purely from its two `outputFactor` bytes
(`Building_ComputeProductionRate`, gilde.exe `0x58f268`,
`src/sim/building_value.cpp:172`):

```cpp
// rate = (Σ_{i=0..1} outputFactor[i] * 28 * 32 * (1/12) * (1/60))
//        * scenePriceField / sceneDivisor
double v2 = 0.0;
for (int i = 0; i < 2; ++i)
    v2 += outputFactor[i] * 28.0f * 32.0f * (1.0/12.0) * (1.0/60.0);
return scenePriceField * (float)v2 / sceneDivisor;
```

Constants (recovered byte-for-byte, `building_value.cpp:27`):
`flt_6268E4 = 28`, `flt_6268E8 = 32`, `dbl_6268EC = 1/12`, `dbl_6268F4 = 1/60`.
Note `28 * 32 * (1/12) * (1/60) = 896/720 ≈ 1.2444` per unit of `outputFactor`.

The **per-item base value** of an input or output good is
`Building_ComputeItemBaseValue` (gilde.exe `0x58f328`,
`src/sim/building_value.cpp:139`):

```cpp
float v9 = 0.0f;
if (inIdx  != -1) v9 = 896 * inputFactor[inIdx];   // input good
else if (outIdx != -1) v9 = 896 * outputFactor[outIdx]; // output good
// sale/auction buildings (objectKind 6 or 7) apply a price discount:
if (kind == 6 || kind == 7)
    v9 = (1 - (2 - g_priceMode) * 0.25) * v9;   // dbl_6268FC = 0.25
return v9;
```

So each building type advertises **up to 2 input goods and up to 2 output goods**,
each weighted by a one-byte factor and scaled by the constant `896`. The
*identity* of those goods is not in the factor bytes themselves — it comes from the
`roomList` / scene rooms and the `inputProf`/`outputProf` profession codes
(`building_types.h:74-76`). **UNKNOWN / partial:** the exact mapping from
profession code → good prototype id is resolved by the entity/profession module and
is not reproduced in these files.

### 2.2 Ingredient-slot recipe (the produce-and-sell engine)

The "micro" recipe used when a player/NPC actually crafts a stack is the
**4-ingredient-slot** form in `TradeComputeSellableAmount`
(gilde.exe `0x497538`, `src/sim/trade_sell.cpp:156`). Each recipe has:

```cpp
struct RecipeSlot {
    i16 proto;     // ingredient prototype (0 == empty slot)
    int ratio;     // units consumed per produced unit  (slot[19])
    i32 effStock;  // effective stock of the ingredient in the container
};
struct SellableResolve {
    i32 startQty;              // *(a1+22) initial production cap
    RecipeSlot ingredients[4]; // up-to-4 ingredient slots
    i16 outProto;              // produced good prototype (a1+18 >> 16)
    int outputCount;           // units produced per craft (slot+54 word)
};
```

The producible quantity is the **min over ingredient slots** of
`effectiveStock_i / ratio_i`, then capped by the output container's free capacity
(`trade_sell.cpp:161`):

```cpp
int v4 = startQty;
for (slot in 0..3) {
    if (proto == 0) continue;          // empty slot, no gate
    int ratio = ratio ? ratio : 1;
    if (effStock <= 0) { v4 = 0; continue; }  // ingredient missing -> can't produce
    int crafts = effStock / ratio;
    if (v4 >= crafts) v4 = crafts;     // min
}
int outCount  = outputCount ? outputCount : 1;
int freeUnits = ComputeFreeCapacity(outProto, outCount * v4);
int v15       = min(freeUnits / outCount, v4);   // crafts we can actually make
```

On a nonzero `v15` the engine (`trade_sell.cpp:194`):

1. **consumes** `ratio_i * v15` of each ingredient (`kRemoveSource`),
2. **emits** `outCount * v15` of `outProto` (`kAddDest`),
3. **credits** the proceeds (`kCredit`):

```cpp
proceeds = trunc( marketPrice(outProto, 100) * (outCount * v15) );
```

This is the literal "raw materials → finished product" transform. The effective
stock for ingredients respects the reserve-good `count-1` rule (§1.1).

### 2.3 Component recipe (the market-price model)

A third recipe view exists purely for **pricing** a good from its constituents.
`market_price_model.h` (gilde.exe `0x58f3d0`) models a good as up to **4
`(goodId, qty)` component slots** (`GoodRecord.components`, `market_price_model.h:60`):

```cpp
struct GoodRecord {
    u8  category;      // +0   23 == raw/special: skip recipe
    u8  flag;          // +33  ==3 -> cached price gets *1.5
    u32 value;         // +34  numerator of the base ratio
    u16 divisor;       // +54  denominator (0 treated as 1)
    i32 cachedBase;    // +56  0 == not yet computed
    struct Comp { u16 good; u16 qty; } components[4];  // id 0 ends; 0xFFFF == named, no price
};
```

The price recurses into the component goods and writes the un-currency-scaled base
into `cachedBase` on first compute (see §3).

---

## 3. Market price (the `ComputeMarketPrice` core)

`Building_ComputeMarketPrice(prot, qty)` (gilde.exe `0x58f3d0`,
`src/sim/building_production.cpp:114`) is the canonical price function; production
quality and worth all flow through it. Recovered constants
(`building_production.cpp:22`):

```
flt_626948 = 0.01    (1/100, currency/qty scale)   kMP_Qty
flt_62694C = 1.5     (subtype==3 multiplier)        kMP_Sub3
flt_626950 = 28.0 / flt_626954 = 32.0
dbl_62695C = 1/12 / dbl_626964 = 1/60
dbl_62696C = 0.5
flt_626974 = 2.2     (kind==23 multiplier)          kMP_F74
flt_626978 = 0.03125 (1/32, cache-write scale)      kMP_F78
```

Cached path (price already computed, `building_production.cpp:120`):

```cpp
v26 = 32 * cachedPrice * qty * 0.01;
if (subtype == 3) v26 *= 1.5;
return v26;
```

Cold path (`building_production.cpp:128`):

```cpp
divisor = td->divisor ? td->divisor : 1;
v23 = (u32)baseValue / divisor;
// remap[prot] -> building type; sum its 2 output factors with the 28*32/12/60 weight,
// else v27 = 4.0
v28 = v27 * 0.5 * v23;
if (kind == 23) return (float)(v28 * 2.2);   // raw goods: 2.2x, no recipe
// + single component contribution (see note below)
v20 = v28 * 2.2 * 0.03125;
cachedPrice = trunc(v20);                      // cache the base
return (float)( 2.2 * (v28 * qty * 0.01) );
```

> **UNKNOWN / partial:** the original walks **up to 4** component rows when
> summing component prices; the reimplementation models only the **single-component
> leaf** path (`building_production.cpp:154`), because multi-component aggregation
> reads scene rows that are not modeled here. For goods with ≥2 priced components
> the recovered price is therefore approximate.

The standalone `world::BuildingComputeMarketPrice` (`market_price_model.h`) is the
fuller version that does recurse over all 4 component slots; both share the same
constants. `qty` is conventionally **100** at every call site
(`building_production.cpp:233,261`; `production_slots.cpp:18`).

---

## 4. The production process (per tick)

There are two clocks in the production engine: the **schedule/curve tick** that
updates a building's smoothed input/output/yield state, and the **per-NPC work-order
timer** that fires when a single craft completes.

### 4.1 The production-slot table

Production runtime state lives in a flat table, `g_prodStore` (orig
`dword_13C3B00`), of **256 buildings × 1988 dwords**, each building holding a
16-byte header plus **62 slots × 32 dwords** (`building_production.h:96-128`). Each
slot's recovered columns (`building_production.h:146`):

| accessor | record offset | meaning |
|----------|---------------|---------|
| `slotProtPacked` | +0x60 | prot id in high word (`>>16`) |
| `slotCust0..3`   | +0x70..+0x7C | per-tick customer counters (cleared each tick) |
| `slotSmoothIn`   | +0x80 | smoothed input level |
| `slotF24`        | +0x84 | output band (`slot+0x24`) |
| `slotOutBase`    | +0x90 | base output |
| `slotOutComp`    | +0x94 | computed output |
| `slotYield`      | +0x98 | yield/price the slot produces at |
| `slotActive`     | +0xA0 | active flag (low byte) |

Per-building tick header: `inValue`/`inScale` (+0x50/+0x54),
`outValue`/`outScale` (+0x58/+0x5C).

### 4.2 The schedule curves

Each building owns a **production-schedule curve table** (orig `dword_13CD6A0`,
stride 756 bytes, `building_production.h:163`): up to **9 `(time, value)`
keyframes** for an **input** curve and an **output** curve, plus a
`hasProduction` flag at +0.

### 4.3 `Building_RunProductionTick` (gilde.exe `0x5847a0`)

`src/sim/building_production.cpp:339`. Per tick, for a building at game-time
`(nowDay, nowMinute)`:

1. **Interpolate** the input curve → `inValue`, scaled per-mille by `inScale`:
   ```cpp
   inValue = InterpCurve(input, nowKey, dayByte);
   inValue = inScale * inValue / 1000;
   ```
   then the output curve the same way (`outValue = outScale * outValue / 1000`).
   `InterpCurve` (`building_production.cpp:303`) finds the keyframe segment
   bracketing the packed time and lerps within it (day-fraction inside an exact
   keyframe, full-minute cursor between keyframes).

2. **For each of the 62 slots**, unless its good kind is 23 or 37
   (`building_production.cpp:360`):
   - update the **smoothed input** with a half-step low-pass:
     ```cpp
     smoothIn += (computeSlotInput(b,slot) - smoothIn) * 0.5;   // kHalf = dbl_62649C
     ```
   - compute the slot **output**: for the *player's* building, live via
     `Building_ComputeSlotOutput`; for AI buildings reuse the stored `slotOutBase`
     (`building_production.cpp:367`). The result is written to `slotOutComp`.
   - recompute the slot **yield** via `Building_ComputeSlotYield` and store it.
   - if the slot is active, set `smoothIn = (float)slotOutComp`.
   - clear the four customer counters.

3. If standalone (`g_standaloneFlag == -1`), fire the net-sync / stock-transform
   hooks (`building_production.cpp:384`).

`Building_RecalcAllProduction` (gilde.exe `0x583c3c`,
`building_production.cpp:393`) runs the tick for **up to 4** buildings flagged
`hasProduction` in the schedule table.

### 4.4 Slot input / output / yield formulas

**Slot input** — `Building_ComputeSlotInput` (gilde.exe `0x584d34`,
`building_production.cpp:175`):

```cpp
if (kind == 23 || kind == 37) return 0;        // raw/special goods take no input
factor = slot float at +0x68;
if (building) v6 = inValue * factor * 0.0005;   // kInScale  = dbl_6264A4
else          v6 = 0 * factor * 0.0025 + 0.5;   // kInScale0 = dbl_6264AC, kInBias0 = 0.5
return trunc(v6);
```

**Slot output** — `Building_ComputeSlotOutput` (gilde.exe `0x584de8`,
`building_production.cpp:199`):

```cpp
if (!slotActive) return 0;
int v12 = SlotWorkerOutput(building, slot);    // Σ worker contributions (hook; see §5)
int v13 = trunc(v12 * 0.5);                    // kOutScale = dbl_6264BC
if (v13 != 0) {
    num = slotSmoothIn(slot);                  // slot+0x20
    den = slotF24(slot);                       // slot+0x24
    if (num == 0) return v13;
    return trunc(den / num * v13);             // scale by current output/input ratio
}
return v13;
```

**Slot yield** — `Building_ComputeSlotYield` (gilde.exe `0x584ec8`,
`building_production.cpp:225`). This is the **quality/price** the slot currently
produces at:

```cpp
stored = SlotStoredQuantity(building, slot);   // -1 == no work order
if (stored < 0 && building != playerBuilding)
    return marketPrice(prot, 100);             // idle AI slot: just the market price

v26 = (stored > 0) ? stored : 0;
cap   = slotSmoothIn(slot);   // slot+0x20  capacity
outv  = slotF24(slot);        // slot+0x24  output
price = slotYield(slot);      // slot+0x38  last price

// supply ratio v25: 1.0 if supply <= capacity (or raw kind 23), else cap/max(v26,1)
if (v26 <= cap || kind == 23) v25 = 1.0;
else                          v25 = cap / max(v26, 1.0);

v27 = (cap != 0) ? outv / cap : 0;
v12 = v27 * v25;
v20 = max(v12, 0.25);          // kYieldLo = dbl_6264C4
v18 = min(v20, 5.0);           // kYieldHi = dbl_6264CC  -> quality multiplier in [0.25, 5.0]
v21 = (float)v18;

mp  = marketPrice(prot, 100);
current = slotYield(slot);
// drift the stored yield toward (market price * quality):
v28 = (mp * v21 - price) * 0.4 + current;      // kYieldStep = flt_6264D4 = 0.4
if (current <= 0) {
    return (mp >= 512.0) ? (float)mp : 512.0;  // kYieldMin = flt_6264D8 = 512
}
return v28;
```

Key takeaways:
- **Quality multiplier** `v21` is the supply-vs-capacity ratio, **clamped to
  `[0.25, 5.0]`**. A workshop that is well-supplied relative to its capacity
  produces at higher quality; under-supply (`v26 > cap`) scales it down by
  `cap / supply`.
- The slot's stored yield **drifts** toward the target `market_price * quality`
  at rate `0.4` per tick (an EMA), with a **floor of 512** when first warming up.

### 4.5 The per-NPC work-order timer

`InventoryTickProductionOrder` (gilde.exe `VIBE_Inventory_TickProductionTimers`
`0x54f168`, `src/sim/production_slots.cpp:168`) steps a single in-progress craft:

```cpp
if (!order.active) return;                              // idle slot skipped
order.timerMinutes -= DiffMinutes(order.lastStamp, now);  // debit elapsed minutes
order.lastStamp = now;                                  // restamp
if (order.timerMinutes >= 0) return;                    // still producing
// --- complete ---
order.active = false;                                   // clear active flag (node+33)
if (order.notifyReady)        NotifyProductReady(...);  // person kind==6 sale building
if (personIsOwnerTurn)        EmitProductionFinished(...); // only the owner emits the cmd
```

The live game runs this over a 32-NPC window per call; the reimplementation exposes
the single-order step. Each work order carries `timerMinutes` (the remaining
production minutes), the last clock stamp, the product type and owning NPC id
(`production_slots.h:180`). When the timer crosses zero, the finished-good command
(`VIBE_Command_QueueRequest17`) is emitted — but only by the owner-turn NPC
(`VIBE_Character_IsObjectForTurn` gate).

### 4.6 Work-minute integration (the production clock)

The amount of *work time* a workshop accrues between two game timestamps is
`ProductionComputeOutputOverTime` (gilde.exe `0x59064c`,
`src/world/production.cpp:80`). It integrates **in-window work minutes** across a
per-weekday daily work window:

```
work-start hour per weekday  flt_6476FC = { 8, 7, 8, 9 }   (kWorkStartHour)
work-end   hour per weekday  flt_64770C = { 20, 21, 20, 19 } (kWorkEndHour)
minute scale                 flt_626A04 = 60.0  (kMinuteScale)
weekday = start.day % 4
```

The integration (`integrateWindow`, `production.cpp:36`) sums: a partial first day
(from the start time-of-day up to the window close), the full window for each
middle day, and a partial last day (from the window open up to the end
time-of-day). Two special cases:
- if **pause mode** is active (orig `word_63C740 & 0x80`), the plain minute
  difference is returned (windows ignored), `production.cpp:86`;
- if `start` is strictly after `end`, the result is **0**.

`ProductionComputeDailyHourOutput` (gilde.exe `0x590a3c`, `production.cpp:95`) is
the same integration with a **fixed `[6, 23]` hour window** and no weekday/pause
branches.

---

## 5. Worker / labor effects

Worker effort enters production through **`SlotWorkerOutput`**
(`IProductionHooks::SlotWorkerOutput`, `building_production.h:202`): in the original
`Building_ComputeSlotOutput` iterates the Person array and sums the output
contributed by workers assigned to that slot. The reimplementation routes this
through a hook (default 0) because the Person↔slot join is owned by the entity
module. The summed worker output is then halved (`* 0.5`) and scaled by the slot's
output/input ratio (§4.4).

Worker effort also feeds the **production-worth** aggregator
`BuildingValue_ComputeProductionWorth` (gilde.exe `0x58fe68`,
`src/sim/building_stock.cpp:342`), which sums `ComputeItemBaseValue * quality*0.01`
over the building's output slots (0..5) and input slots (0..1):

```cpp
qScale = quality(+61) * 0.01;                   // kQualityScale = dbl_6269CC = 0.01
for (o in 0..5)  v5 += trunc(ComputeItemBaseValue(b, type, o, -1) * qScale);
for (in in 0..1) v5 += trunc(ComputeItemBaseValue(b, type, -1, in) * qScale);
```

> **UNKNOWN / partial:** the real function joins worker records through the parallel
> `dword_12CEA7C` column and the worker's `+353` packed profession; that join is
> entity-module-owned and the default hook reproduces only a single self-worker so
> the leaf arithmetic stays testable (`building_stock.cpp:357`).

The building's **production rating** per stat (staff/equipment quality) is
`Building_EvalProductionRating` (gilde.exe `0x58a794`,
`building_value.h:84`): `base = statLevel[stat] * (1/252)`, minus a
handler-weighted penalty, plus an inventory-slot bonus (stat 3 only), minus a
staff/equipment term decoded from the `staffBits` bitfield (`+44`), clamped to
`[0,1]`. This rating feeds the production-gauge UI rather than the throughput math
directly.

---

## 6. Stock / storage interplay (raw-material gating)

### 6.1 Stock decay and projection

A live production object carries a stock recurrence (`BuildingStockRec`,
`building_stock.h:52`): current stock at +10, decay rate at +16/+20, output at
zero/full fill at +20/+28, fill capacity at +32. The decay integrates
`s = s - rate + rate/s` while `s > 1.0`:

`Building_ComputeProjectedStock` (gilde.exe `0x57d1c8`,
`building_stock.cpp:69`):

```cpp
int steps = 0;
for (float i = fullFill; i > 1.0f; ++steps)
    i = i - zeroFill + zeroFill / i;            // decay step (rate at +20)
float headroom = max(fillCap - stock, 0);
return trunc(steps + stock + headroom);
```

`Building_SyncStockLevel` (gilde.exe `0x57d0f8`, `building_stock.cpp:100`) projects
a new stock forward (`ProjectForward`, `building_stock.cpp:85`) and queues a
price-update delta. `Building_AdjustStockAndNotify` (gilde.exe `0x57d5b4`,
`building_stock.cpp:122`) adds/removes stock and, when the result drops to zero,
clears the active flag and resets the slot — i.e. **a workshop that runs out of
stock goes inactive**. It refuses to act on empty (`marker==0xFFFF`), storage
(`kind==10`), or inactive buildings (`building_stock.cpp:124`).

### 6.2 Current / max output interpolation

`Building_ComputeCurrentOutput` (gilde.exe `0x57d26c`,
`building_value.cpp:185`) — output as a function of fill level:

```cpp
if (typeIndex == 0xFFFF || !activeFlag) return -1;       // empty/inactive
if (fillLevel <= fillCap)
    out = (outFullFill - outZeroFill) * fillLevel / fillCap + outZeroFill;  // lerp
else
    out = outFullFill;                                    // saturated
total = outBonus + out;
return max(total, 0);
```

`Building_ComputeMaxOutput` (`0x57d310`) is the same lerp **without** the
`+outBonus` and floor. `Building_ComputeOutputRatio` (`0x57d384`) = current / max.
These are combined into the **efficiency score**
`Building_ComputeEfficiencyScore` (gilde.exe `0x57d3d0`,
`building_stock.cpp:111`):

```cpp
if (activeFlag <= 1) return 0;
return maxOut * 0.001 * 0.2 + (curOut / maxOut) * 0.8;
//      kEffMaxBase   kEffMaxW            kEffRatioW
```

So efficiency blends an absolute capacity term (weight 0.2) with the current/max
fill ratio (weight 0.8).

### 6.3 Storage worth

Goods sitting in storage are valued by `BuildingValue_ComputeStockValue` (gilde.exe
`0x590360`, `building_storage.cpp:52`) and `_SumStorageItemWorth`
(`0x591658`, `building_storage.cpp:39`). The stock-worth loop applies the
reserve-good rule and a `0.3` stock multiplier:

```cpp
for (item in storage) {
    qty  = IsReserveGood(prot) ? count - 1 : count;
    v14  = max(trunc(qty * 0.3), 1);              // kSV_StockM = dbl_6269EC = 0.3
    worth += marketPrice(prot, 100) * v14;
}
```

`BuildingValue_ComputeRoomWorth` (gilde.exe `0x59116c`, `building_storage.cpp:89`)
walks the `roomList`, and for storage rooms (good kind 2 or 6) accumulates each
room's item market price into a base of `3840 * roomWorthMul`, finally scaling by
`mul * 0.01 * roomWorthMul-base` (`kRW_Mul = flt_626A10 = 0.01`).

### 6.4 Sale price (output → revenue)

`Building_ComputeSalePrice` (gilde.exe `0x591480`, `building_stock.cpp:219`)
converts a building's flagged-slot worth into a sale price, applying a tax-tier
slope and a quality discount:

```cpp
worth   = SumFlaggedSlotsWorth(type);                       // 0x5913e0
qmul    = (qualityCode == 14) ? 0.85 : 1.0;
taxTier = SaleTaxTier();                                     // Gesetz_GetRecord(6)
price   = ( (4 - taxTier) * 0.10 + 0.80 ) * worth * qmul;   // flt_626A14 / flt_626A18
return trunc(price);
```

`Building_SumFlaggedSlotsWorth` (gilde.exe `0x5913e0`, `building_stock.cpp:193`)
sums `marketPrice(roomKind, 100)` over the rooms flagged with bit `0x8000` in
`roomList`, on top of a `3840 * roomWorthMul` base.

### 6.5 Goods distribution to customers

`Building_DistributeGoodsToCustomers` (gilde.exe `0x57d83c`,
`building_stock.cpp:276`) is the per-building customer pass: it drifts the sale
price (a random nudge weighted by the customer count via `flt_641FEC[6]`
= `{0.2400…, 0.2222…, 0.125, 0.0556…, 0.0139…, 0}`), and schedules goods transfers
to customers. The supplier match that gates it is
`Building_FindMatchingSupplier` (gilde.exe `0x57dcb4`, `building_stock.cpp:150`):
a non-sale building (kind ≠ 6/7) finds a sale/auction building (kind 6/7) that has a
free customer slot (`< 5` customers) and is not already serving it. **Raw-material
availability is therefore gated at the supplier level**: a workshop with no matching
supplier cannot be replenished.

### 6.6 City-level replenishment

`GoodsRunDistributionPass` (gilde.exe `0x57dd84`, `src/world/amt_goods.cpp:90`) is
the per-turn business heartbeat. It counts active production buildings, **drains
over-stocked** type-3 and type-8 firms, and — when the active count falls at/below
the threshold `dbl_62598C = 652.8` (`kGoodsThreshold`, `amt_goods.h:36`) — spawns
replacement businesses toward a target of 40 (`GoodsSpawnCount`,
`amt_goods.cpp:78`):

```
firms >= 40            -> 0
firms >= 30 (< 40)     -> RandomModulo(2) + 1   (1 or 2)
firms <  30            -> (40 - firms) / 2
```

The type-3 drain predicate (`GoodsType3ShouldDrain`, `amt_goods.cpp:57`) requires
the building to be present, occupied, have a supplier, all destroy-guards clear, and
**either** its worth word ≥ `RandomModulo(4) + 44` **or** the city already over
threshold. This keeps the citywide supply of goods roughly balanced.

---

## 7. Production quality summary (exact formula)

For reference, the **production quality** that a workshop slot produces at is the
clamped supply/capacity ratio from `Building_ComputeSlotYield` (§4.4):

```
supplyRatio v25 = (supply <= capacity || good.kind == 23)
                    ? 1.0
                    : capacity / max(supply, 1.0)
capUtil    v27  = (capacity != 0) ? output / capacity : 0
quality    v21  = clamp( v27 * v25, 0.25, 5.0 )       // ∈ [0.25, 5.0]

yield_next      = (marketPrice(prot,100) * quality - lastPrice) * 0.4 + lastYield
                  (with a 512.0 floor on cold start)
```

And the **per-item worth** contributed to building value is
`ComputeItemBaseValue * (buildingQuality * 0.01)` (§5).

---

## 8. Open questions / unknowns

- **Profession → good-id mapping.** `inputProf`/`outputProf` (`BuildingTypeDef
  +547/+559`) select which goods a workshop consumes/produces, but the resolution
  table is in the entity/profession module and not reproduced here.
- **Multi-component pricing.** `Building_ComputeMarketPrice` models only the
  single-component leaf; goods with ≥2 priced components are priced approximately
  (`building_production.cpp:154`). The standalone `market_price_model.h` recurses
  over all 4 slots but is a separate, not-yet-wired implementation.
- **Worker aggregation.** `SlotWorkerOutput` and the production-worth worker join
  are hook-stubbed; the real Person-array walk (parallel column `dword_12CEA7C`,
  worker `+353` profession) is entity-owned.
- **Scene-room worth columns** in `ComputeProductionWorth` (kind-7 buildings) are
  zeroed pending the scene-walk hook (`building_stock.cpp:396`).
- **`A_Geb.dat` / `A_Obj.dat` record counts.** The shipped files hold 192 / 1024
  records but the loader reads 72 / 731; the tail records' role is not established
  here.
