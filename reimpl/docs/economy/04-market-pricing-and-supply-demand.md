# Market Pricing & Supply/Demand

This document describes how goods are priced in the C++ reimplementation of *The Guild / Europa 1400*: where base prices come from, the supply/demand price model, the per-currency price cache, market stalls, the NPC market-supervisor pricing loop, and the player selling path. Everything below is grounded in the actual ported code and the recovered `gilde.exe` constants; unknowns are flagged explicitly.

## Source files

Reimplementation:

- `src/sim/building_production.cpp` / `src/sim/building_production.h` — the authoritative `Building_ComputeMarketPrice` (gilde.exe `0x58f3d0`) and the `SceneTypeDef` (65‑byte) good/type record. This is the live engine used by the rest of the sim.
- `src/world/market_price_model.cpp` / `src/world/market_price_model.h` — a standalone, golden‑testable translation of the same `0x58f3d0` price model over a `GoodRecord` POD (same formula, same constants).
- `src/world/market_price.cpp` / `src/world/market_price.h` — the per‑currency cached price lookup (gilde.exe `0x58f6b8`, `VIBE_Building_LookupCachedMarketPrice`).
- `src/world/market_stall.cpp` / `src/world/market_stall.h` — market‑stall stock book‑keeping (`0x49d2e8`), stall‑contact routing (`0x519918`), and the trade item‑list sort (`0x51b26c`).
- `src/sim/npc_market.cpp` / `src/sim/npc_market.h` — the market‑supervisor NPC director (`0x4e8bdc`) and its per‑slot supply/demand price recompute (`MarketRecomputeSlot`, body `0x4e8ca3..0x4e91d3`).
- `src/sim/trade_sell.cpp` / `src/sim/trade_sell.h` — the player sell command builder (`0x46bff0`) and the sell / produce‑and‑sell APPLY engines (`0x496b90`, `0x497538`).
- `src/world/data_load.cpp` / `src/world/data_load.h` — loads `A_Obj.dat` (good/type records) and `A_Geb.dat` (building‑type records) and builds the prot→building‑type remap `byte_13CE862`.
- `src/sim/building_types.h` — `BuildingTypeDef` (589‑byte) including the `outputFactor` field the price model reads.
- `src/world/event5.h` — stall slot price multipliers used when pricing items into/out of a stall.

Original data:

- `europe_guild_1400_original/data/A_Obj.dat` — good/object type table (65‑byte records).
- `europe_guild_1400_original/data/A_Geb.dat` — building type table (589‑byte records).

> Note: `europe_guild_1400_original/Resources/gamedata/` contains only `ai/`, `Cities/`, `Network/`, `Saves/`, `Screenshots/` — no price tables. The price inputs live in `data/A_Obj.dat` + `data/A_Geb.dat`.

---

## 1. Where base prices come from (data files)

There is **no flat "base price per good" table**. A good's price is *computed* from a few fields of its 65‑byte type record plus a contribution from its associated building type and its component recipe. The two data files are loaded by `WorldLoadBuildingAndObjectData` (`src/world/data_load.cpp:175`):

```
A_Geb.dat -> g_buildingTypes  (stride 589, kBuildingTypeLoadCount = 72 records)   data_load.cpp:182-185
A_Obj.dat -> g_sceneTypes     (stride 65,  kSceneTypeLoadCount    = 731 records)  data_load.cpp:186-189
```

These are raw `fread` blobs (`ReadDatBlob`, `data_load.cpp:161`), read straight into the fixed sim tables at `dword_13CE294` (buildings) and `dword_13CE27C` (goods/objects).

> **Discrepancy flagged:** the on‑disk files are larger than the load counts. `A_Obj.dat` is 66560 bytes = 1024×65 (loader reads only the first **731**); `A_Geb.dat` is 113088 bytes = 192×589 (loader reads only the first **72**). The reimplementation faithfully reads `0x41×731` and `0x24D×72` exactly as the original `VIBE_File_Read` calls (`data_load.cpp:182-189`), so the tails of both files are intentionally ignored.

The 65‑byte good record (`SceneTypeDef`, `building_production.h:59`) supplies the pricing inputs:

| Field            | Offset | Meaning in pricing |
|------------------|--------|--------------------|
| `kind`           | +0     | `23` ⇒ "raw"/special good, skip recipe and apply ×2.2 |
| `subtype`        | +33    | `3` ⇒ cached repricing gets ×1.5 |
| `baseValue`      | +34    | numerator of the base ratio (read as `u32`) |
| `priceField`     | +38    | per‑component price weight |
| `compType`       | +44    | component packed dword; the component good id is its **high word** (`compType >> 16`); `+46` |
| `divisor`        | +54    | denominator (treated as 1 if zero) |
| `cachedPrice`    | +56    | `0` ⇒ recompute; otherwise the cached un‑currency‑scaled base |

After the raw read, a fixup loop counts "qualifying" rooms per building and adds that count into the building record's `+33` byte (`data_load.cpp:206-221`). `WorldInitBuildingTypeTable` (`data_load.cpp:61`) then builds `byte_13CE862[731]` — the per‑good → building‑type index (`g_sceneTypeRemap`) that the price model uses to fetch a "type need" contribution (Section 2).

---

## 2. The price model (`Building_ComputeMarketPrice`, gilde.exe 0x58f3d0)

Signature (`building_production.cpp:114`):

```cpp
double Building_ComputeMarketPrice(i16 prot, u8 qty);   // qty is the "currency"/scale, callers pass 100
```

The same model is mirrored field‑for‑field in `BuildingComputeMarketPrice` (`market_price_model.cpp:11`) over a `GoodRecord` POD for isolated testing. Both reproduce the exact float arithmetic and constant order. Below, line references are to the authoritative `building_production.cpp`; the `mp::k*` names are the parallel constants in `market_price_model.h`.

### Recovered constants (byte‑for‑byte)

From `building_production.cpp:22-30` (`flt_/dbl_` addresses are the original BSS):

```cpp
kMP_Qty   = 0.009999999776482582f  // flt_626948  (1/100 currency scale)
kMP_Sub3  = 1.5f                    // flt_62694C  (subtype-3 multiplier)
kMP_F50   = 28.0f                   // flt_626950  (type-need weight A)
kMP_F54   = 32.0f                   // flt_626954  (type-need weight B)
kMP_D5C   = 0.08333333333333333     // dbl_62695C  (1/12)
kMP_D64   = 0.016666666666666666    // dbl_626964  (1/60)
kMP_D6C   = 0.5                     // dbl_62696C  (base half-weight)
kMP_F74   = 2.200000047683716f      // flt_626974  (2.2 price gain)
kMP_F78   = 0.03125f                // flt_626978  (1/32 cache-write scale)
```

### Fast path — cached good (`cachedPrice != 0`)

`building_production.cpp:119-125`:

```cpp
if (cached) {                                  // td->cachedPrice (+56)
    double v26 = (double)(32 * cached) * (double)qty * kMP_Qty;   // 32*base * qty * 0.01
    if (td->subtype == 3)                       // +33 == 3
        v26 *= kMP_Sub3;                         // * 1.5
    return v26;
}
```

So once a good has a cached base `B`, its price at scale `qty` is:

```
price = 32 * B * qty * 0.01           ( * 1.5 if subtype == 3 )
```

With the canonical `qty = 100`, this is `price = 32 * B * (1.5 if subtype 3 else 1)`.

### Compute path (first time, `cachedPrice == 0`)

1. **Base ratio** (`building_production.cpp:128-129`):

   ```cpp
   int divisor = td->divisor + (td->divisor == 0 ? 1 : 0);   // min 1
   double v23 = (double)(u32)td->baseValue / (double)divisor; // value / divisor
   ```

2. **Type‑need contribution `v27`** (`building_production.cpp:131-146`). Look up the remap `byte_13CE862[prot]`; if nonzero, fetch the 589‑byte `BuildingTypeDef` and sum its two `outputFactor[i]` bytes (`+563`, `+564`) with the `28*32/12/60` weight; otherwise `v27 = 4.0`:

   ```cpp
   if (remap) {
       double acc = 0.0;
       for (int i = 0; i < 2; ++i)
           acc += (double)(i16)bt->outputFactor[i] * 28.0 * 32.0 * (1.0/12.0) * (1.0/60.0);
       v27 = acc;
   } else {
       v27 = 4.0;
   }
   ```

   Note `28*32/(12*60) = 896/720 = 1.2444…` per unit of `outputFactor[i]`.

3. **Half‑weighted base** (`building_production.cpp:148`):

   ```cpp
   double v28 = v27 * 0.5 * v23;
   ```

4. **Raw‑good shortcut** (`building_production.cpp:149-150`): if the good is `kind == 23`, there is no recipe — return immediately with the ×2.2 gain:

   ```cpp
   if (td->kind == 23)
       return (double)(float)(v28 * 2.2f);
   ```

5. **Component recipe** (`building_production.cpp:154-164`). The component good id is the high word of `compType` (`compType >> 16`). `0` = leaf (no components), `0xFFFF` = "named ingredient, no price contribution". Each present component's price (computed **recursively** at the same `qty`) is scaled by `priceField` (`+38`) and divided by the divisor, then added to `v28`:

   ```cpp
   u16 compProt = (u16)(td->compType >> 16);
   if (compProt != 0 && compProt != 0xFFFF) {
       float scaled = (float)td->priceField;                        // +38
       double comp  = Building_ComputeMarketPrice(compProt, qty) * scaled;
       int dv = td->divisor;
       v28 += comp / (double)(dv ? dv : 1);
   }
   ```

   > **Fidelity note flagged in the code:** the original walks **up to 4** component rows; `building_production.cpp:155-156` only models the single‑component leaf path (multi‑component aggregation reads scene rows that are not modeled here). The standalone `market_price_model.cpp:59-75` *does* loop all 4 `components[k]` slots (`0` ends the list, `0xFFFF` skips), accumulating `compPrice/divisor` each iteration — so the model file is the more complete reproduction of the recipe loop.

6. **Cache write‑back + final return** (`building_production.cpp:166-169`):

   ```cpp
   double v20 = v28 * 2.2f * 0.03125f;        // 2.2 * (1/32)
   td->cachedPrice = (int)v20;                 // trunc toward zero -> +56
   return (double)(float)( 2.2f * (v28 * (double)qty * 0.01) );
   ```

   So the stored base is `B = trunc(v28 * 2.2 / 32)` and the returned price is `2.2 * v28 * qty * 0.01`. Note the cached base deliberately omits the currency scale, which is why the fast path re‑applies `32 * qty * 0.01` (≈ the `1/32` is undone) on every subsequent call.

### Effective formula summary

For a leaf good with no recipe and no type‑need (`remap == 0`):

```
v23 = baseValue / max(divisor, 1)
v28 = 4.0 * 0.5 * v23 = 2.0 * v23
price(qty) = 2.2 * v28 * qty * 0.01 = 0.044 * qty * v28
cachedBase B = trunc(v28 * 2.2 / 32)
```

With type‑need present, `4.0` is replaced by `1.2444 * (outputFactor[0] + outputFactor[1])`.

---

## 3. Per‑currency price cache (`MarketLookupCachedPrice`, gilde.exe 0x58f6b8)

This is the **fast read path** the trade/market panels use to read a good's current price without re‑running the model every frame (`market_price.cpp:18`, `market_price.h`).

Cache geometry recovered from the original's raw‑offset scan (`market_price.h:21-30`):

```
per-currency block stride = 7952 bytes   (dword_13C3B5C indexed by 7952 * currency)
entry stride              = 128 bytes
entries scanned           = 62           (v3: 0,128,...,7808; stops at >= 7936)
key dword                 = entry + 2     (good id == key >> 16, the HIGH word)
price float               = entry + 60    (parallel flt_13C3B98 array)
```

Lookup logic (`market_price.cpp:21-37`):

```cpp
const MarketCacheEntry* block = cache.entries + currencyId * cache.entriesPerBlock;
for (int slot = 0; slot < n; ++slot)          // n <= 62
    if ((block[slot].key >> 16) == goodId)
        return block[slot].price;             // cache hit
// miss: fall back to the full model
return fallback ? fallback(goodId, ctx) : 0.0f;   // ComputeMarketPrice(goodId, 100)
```

On a **miss** (or no cache) the original calls `VIBE_Building_ComputeMarketPrice(goodId, 100)`; here that fallback is injected by the caller.

> **Faithful quirk (documented at `market_price.h:55-57`):** a zeroed cache entry has `key == 0`, so a query for `goodId == 0` matches the first empty slot and returns its (zero) price. Real good ids are 1‑based; id `0` means "none". This behavior is preserved deliberately.

---

## 4. Market stalls (`market_stall.cpp` / `.h`)

### 4.1 Stall types and contact routing (gilde.exe 0x519918)

`MarketStallRouteContact` (`market_stall.cpp:9`) maps a clicked contact object name to a stall id (the `OpenStall` second argument, ids **146..151**) or to a non‑stall board (`market_stall.h:38-48`):

| Contact name                       | StallType (id)              |
|------------------------------------|-----------------------------|
| `ob_MARKTSTAND_TISCHLER_SCHMIEDE`  | `TischlerSchmiede` (146) — carpenter/smith |
| `ob_MARKTSTAND_STEINMETZ_WIRT`     | `SteinmetzWirt` (147) — mason/innkeeper |
| `ob_MARKTSTAND_PARFUM_KRAEUTER`    | `ParfumKraeuter` (148) — perfume/herbs |
| `ob_MARKTSTAND_ROHSTOFFE`          | `Rohstoffe` (149) — raw materials |
| `ob_MARKTSTAND_IMPORTEUR`          | `Importeur` (150) — importer |
| `ob_MARKTSTAND_KIRCHE`             | `Kirche` (151) — church stall |
| `ob_SCHWARZES_BRETT` / `contact_…` | `PamphletBoard` (notice board) |
| `ob_TRIBUENE`                      | `Tribune` (poem tribune) |

The probe order matches the original's fixed if‑ladder. Empty / unknown names return `StallType::None`.

### 4.2 Stall stock book‑keeping (`MarketStallUpsertTradeEntry`, gilde.exe 0x49d2e8)

This is the trade‑entry (stall stock) APPLY engine (`market_stall.cpp:26`). It resolves the destination and source buildings (both must exist, else `return 1`), then finds the scene‑type‑202 entry whose match key (`+84`) equals the order key (`+38`); if none exists it creates a fresh entry. It copies the order fields verbatim, then **accumulates stock at `entry+55`, clamped to 100** (`market_stall.cpp:67-69`):

```cpp
unsigned sum = (unsigned)e->stock + order.stockDelta;   // u8 wrap as in orig
u8 wrapped = (u8)sum;
e->stock = (wrapped <= 0x64u) ? wrapped : (u8)100;       // clamp to 100
```

So `stock = min((stock + delta) mod 256, 100)` with the `0x64` (=100) cap.

### 4.3 Stall item‑list sort (`TradeBuildSortedItemList`, gilde.exe 0x51b26c)

A stall's item list is sorted by **localized currency name** (`market_stall.cpp:107`). `TradeSortableCount` (`market_stall.cpp:79`) scans backward from index 15 to find the leading non‑empty prefix (a slot is empty when `id == -1` **or** `currency == 0`), capped at 16 slots. The sort key (`SortKey`, `market_stall.cpp:94`):

```
currency == homeCurrency -> "AAAAAAAAA"   (sorts first)
currency == 0 (none)     -> "ZZZZZZZZZ"   (sorts last)
otherwise                -> localized currency name
```

The swap rule reproduces the original `StrCmp(name(j), name(i)) == 1` (swap whole records when `name(j)` sorts strictly after `name(i)`), `market_stall.cpp:115`.

### 4.4 Stall slot price multipliers (event5)

When goods are priced into/out of a stall slot, the cached market price is scaled by category multipliers (`event5.h:56-58`, used at `event5.cpp:1111/1119/1147`):

```
kSlotPriceMulIn   = 0.9   // dbl_6203E8 — take/buy-in at 90%
kSlotPriceMulRest = 0.8   // dbl_6203F0 — rest at 80%
kSlotPriceMulRaw  = 1.1   // dbl_6203E0 — raw goods at 110%
```

---

## 5. NPC market behavior — the supervisor pricing loop (`npc_market.cpp`)

`NpcMarket_RunMarktSupervisorStep` (`npc_market.cpp:164`, gilde.exe 0x4e8bdc) is the market‑stall pricing/restock director. Per round it:

1. **Gates**: returns/frees if the market is disabled (`marketEnabled`), or the clock hour `> 0x14` (20) — the daily market window is hours `0..20` (`npc_market.cpp:168-184`).
2. Computes `elapsed` minutes since the supervisor's last run; if `0`, does nothing (`npc_market.cpp:177-187`).
3. **Treasury top‑up**: if the market person's currency `< 5,120,000` (`kMktTreasuryTopUp`, `npc_market.h:68`) it enqueues a top‑up to that level (`npc_market.cpp:210-212`).
4. **Central‑market sweep** over the (fixed 62) stall slots, calling `MarketRecomputeSlot` per active slot and committing the result (`npc_market.cpp:214-227`).
5. **Re‑arms** the appointment by +1 hour if still within the window (the decompiler's `addDays` arg is actually an hour delta — documented at `npc_market.cpp:230-238`).

> **DEFERRED (flagged at `npc_market.cpp:154-162`):** the full original also sweeps up to 3 satellite markets × 62 slots × {sell, buy} categories with the same pipeline plus a capacity check. The ported sweep exercises the canonical central‑market path; the satellite‑specific capacity branch is not modeled.

### 5.1 Per‑slot supply/demand recompute (`MarketRecomputeSlot`, body 0x4e8ca3..0x4e91d3)

`MarketRecomputeSlot` (`npc_market.cpp:49`) is the core supply/demand RULE, translated 1:1 including the RNG draw count and order. A slot (`MarketSlot`, `npc_market.h:83`) carries `targetStk` (target stock), `refValue` (reference/base value), `price` (current unit price, mutated), and accumulated `buyQty`/`sellQty`.

**Recovered constants** (`npc_market.cpp:16-29`, `get_bytes @0x61F9A4..0x61FA18`):

```cpp
kMktDemandRate      = 0.0714285746f   // flt_61F9A4  (1/14, per-slot demand rate)
kMktMinFrac         = 0.016666666…    // dbl_61F9A8  (1/60, minutes -> fraction of hour)
kMktScatterBias     = 80.0f           // flt_61FA18  (RNG scatter bias)
kMktScatterScale    = 0.01f           // flt_61F9B0  (price-scatter scale)
kMktOverDecay       = 0.6f            // flt_61F9C8  (oversupply price decay)
kMktHalf            = 0.5             // dbl_61F9B8  (half-target threshold)
kMktThreeQuarter    = 0.75            // dbl_61F9C0  (3/4-target threshold)
kMktPriceFloorFrac  = 0.25            // dbl_61F9D0  (price floor = refValue*0.25)
kMktRiseFactor      = 0.8f            // flt_61F9D8  (understock rise factor)
kMktCeilFrac        = 1.5f            // flt_61F9DC  (price ceiling = refValue*1.5)
kMktResetLowFrac    = 0.4f            // flt_61F9E0  (reset-low = refValue*0.4)
kMktSellCeilFrac    = 3.0            // dbl_61F9E8
kMktSellPriceContrib= 0.33           // dbl_61F9F0
kMktSellResetHi     = 1.2f            // flt_61F9F8
```

**Demand magnitude** `v113` (`npc_market.cpp:59-75`). First an RNG draw `RandomModulo(0xA)` is **discarded** (demand jitter advance). Then:

```
v113 = targetStk * 0.0714285746 * (elapsedMin * 0.0166667)
v127 = RandomModulo(0x64)                          // 0..99 price scatter
v113 = (v127 + 80.0) * v113 * 0.01
if (v113 nonzero) v113 = max(v113, 1.0)            // clamp via float-bit test
```

**Branch on stock vs target** (`effectiveStock` = stock on hand, `targetStk` = target):

- **Oversupply** — `targetStk*0.5 <= stock` (`npc_market.cpp:80`):
  - **Heavily oversupplied** — `targetStk*0.75 < stock` (`npc_market.cpp:82-104`): decay the price, floor it, emit a SELL restock:
    ```
    v128  = targetStk * 0.0714285746 * (elapsedMin * 0.0166667)
    price = price - v128 * 0.6                       // oversupply decay
    price = max(price, refValue * 0.25)              // price floor
    price = max(price, 1.0)                          // absolute floor
    qty   = trunc(max(v128, 1.0))
    -> restock = kSell, QueueRequest17(-1, building, qty, itemType, market)
    sellQty = round(sellQty + max(v128,1.0))
    ```
  - **Mild oversupply** — `0.5*target <= stock <= 0.75*target`: **no price change**.

- **Understock** — `stock < targetStk*0.5` (`npc_market.cpp:106-122`): accumulate buy qty, maybe emit a BUY restock, raise the price:
  ```
  buyQty += trunc(v113)
  if (price > refValue * 0.5)                        // only restock if price elevated
      -> restock = kBuy, QueueRequest17(building, -1, trunc(v113), itemType, market)
  price += targetStk * 0.0714285746 * (elapsedMin * 0.0166667) * 0.8   // rise factor
  ```

**Price‑band clamp** — applied in **all** branches (`npc_market.cpp:124-141`). Ceiling = `refValue*1.5`, low = `refValue*0.4`:

```
if (price <= refValue*1.5):
    if (price < refValue*0.4):
        ratio = price / (refValue*0.4)
        if (RandomFloatScaled() > ratio)   price = refValue*1.5   // snap up to ceiling
else:                                       // price above ceiling
    ratio = (refValue*1.5) / price
    if (RandomFloatScaled() > ratio)        price = refValue*0.4   // snap down to reset-low
```

This is a **probabilistic** re‑anchor: the further price strays outside `[0.4·ref, 1.5·ref]`, the smaller `ratio` becomes and the more likely the price snaps to the opposite band edge.

**Yield recompute gate** (`npc_market.cpp:144-147`): if `RandomModulo(0x64) > 10` **or** `clockHour == 12`, recompute the slot yield (`computeYield(slotIndex)`).

RNG draw order is fixed: (1) `RandomModulo(0xA)` discarded, (2) `RandomModulo(0x64)` scatter, (3–4) `RandomFloatScaled()` clamp rolls, (5) `RandomModulo(0x64)` yield gate (`npc_market.h:156-165`). Truncation uses `ConvertX` (x87 round‑toward‑zero == `(int)`, `npc_market.cpp:43`).

---

## 6. Player selling mechanics (`trade_sell.cpp`)

### 6.1 Price the player gets

The player‑facing price uses **the same market price** — no extra haggling or markup is applied on the sell side. `TradeMarketPrice` (`trade_sell.cpp:19`) resolves via an injected hook; the host wires it to `Building_ComputeMarketPrice(prot, 100)` (the engines pass `qty = 100`, `trade_sell.h:46-53`). With no hook the price is `0` (no trade). The unit price is **truncated to int** (`trade_sell.cpp:38`, `TruncToInt`).

> There is **no haggling/markup multiplier in `trade_sell`** — the player receives `trunc(marketPrice(proto, player))` per unit. (Stall‑specific 0.9/0.8/1.1 multipliers in Section 4.4 apply to *stall slot* pricing, not this player sell path.)

### 6.2 Sell command builder (`TradeRequestSellObjekt`, gilde.exe 0x46bff0)

`trade_sell.cpp:51`:

```cpp
if (!req.buildingFound || req.buildingKind != 2)   // *building must be 2 (sellable contor)
    return 0;
i32 price = TruncToInt(TradeMarketPrice(req.proto, req.player));   // truncated unit price
// emit cm_RequestSellObjekt(buildingId, -1, qty, proto, player, price)
return 14;                                          // ack token
```

The seller building must be kind `2`; otherwise the command is rejected (returns `0`). On success it emits `QueueRequest17(seller, -1, qty, proto, player, price)` and returns the opcode token `14`. (The original computes the price twice — once for the log Sprintf, once for the command; the port computes once, `trade_sell.cpp:48-49`.)

### 6.3 Sell transfer engine (`TradeSellObjektResolve`, gilde.exe 0x496b90)

`trade_sell.cpp:83`. Deterministic transfer resolution — **no money math here**, only stock/capacity gating:

- Source must be resolved and hold the goods. For a **reserve good** (`srcType` ∈ {42,278,475,476,477}) it gates on `effectiveStock >= qty` (`trade_sell.cpp:94-96`); always gates on raw count `srcRawCount >= qty` (`trade_sell.cpp:98-99`).
- Destination capacity: storage goods gate on `ComputeFreeCapacity >= qty` (`trade_sell.cpp:102-106`); carried goods **clamp** `qty = ComputeCarryCapacity(...)`, rejecting if `0` (`trade_sell.cpp:107-113`).
- On commit: emit `kRemoveSource` then `kAddDest` for the (possibly clamped) `qty`. Returns the moved quantity (`0` == rejected).

### 6.4 Produce‑and‑sell engine (`TradeComputeSellableAmount`, gilde.exe 0x497538)

`trade_sell.cpp:156`. Walks up to 4 recipe ingredient slots; the producible amount is the min over present slots of `effStock / ratio`, with a missing ingredient forcing `0` (`trade_sell.cpp:161-175`). It is then capped by the output's free capacity / `outputCount` (`trade_sell.cpp:177-184`). On a positive amount `v15`:

```cpp
out.produced = v15;
double price  = TradeMarketPrice(r.outProto, r.player);
out.proceeds  = TruncToInt(price * (double)(outCount * v15));   // proceeds = trunc(price * units)
```

So the **proceeds the player is credited** = `trunc( marketPrice(outProto,100) * outputCount * producedCrafts )` (`trade_sell.cpp:190-191`). On commit it consumes ingredients (`ratio*v15` each), emits `outputCount*v15` of the output good, and credits the proceeds (`kCredit`, `trade_sell.cpp:194-218`). No markup/haggle factor is applied.

---

## 7. Clamps, min/max, and scaling constants — quick reference

| Quantity / clamp | Value | Where |
|------------------|-------|-------|
| Currency scale (price model) | `0.009999999776482582` (≈ 1/100) | `building_production.cpp:22` |
| Subtype‑3 multiplier | `1.5` | `building_production.cpp:23,122-123` |
| Type‑need weight | `28 * 32 / (12*60)` ≈ `1.2444` per `outputFactor` | `building_production.cpp:138-139` |
| Default type‑need (`remap==0`) | `4.0` | `building_production.cpp:145` |
| Base half‑weight | `0.5` | `building_production.cpp:148` |
| Price gain | `2.200000047683716` | `building_production.cpp:166,169` |
| Cache‑write scale | `0.03125` (1/32) | `building_production.cpp:166` |
| Cached base reconstruction | `32 * base * qty * 0.01` | `building_production.cpp:121` |
| Divisor floor | `max(divisor, 1)` | `building_production.cpp:128` |
| Cache scan width | 62 entries × 128‑byte stride, 7952‑byte block | `market_price.h:27-30` |
| Stall stock cap | `min(stock+delta, 100)` (0x64) | `market_stall.cpp:67-69` |
| Market demand rate | `0.0714285746` (1/14) | `npc_market.cpp:16` |
| Minutes→hour fraction | `0.0166667` (1/60) | `npc_market.cpp:17` |
| Price scatter bias / scale | `80.0` / `0.01` | `npc_market.cpp:18-19` |
| Demand magnitude floor | `max(v113, 1.0)` | `npc_market.cpp:71-74` |
| Oversupply decay | `0.6` | `npc_market.cpp:20` |
| Half / three‑quarter target | `0.5` / `0.75` | `npc_market.cpp:21-22` |
| Price floor (oversupply) | `refValue * 0.25`, then `>= 1.0` | `npc_market.cpp:90-95` |
| Understock rise factor | `0.8` | `npc_market.cpp:24` |
| Price band | `[refValue*0.4, refValue*1.5]` (probabilistic re‑anchor) | `npc_market.cpp:124-141` |
| Restock-on-buy gate | `price > refValue * 0.5` | `npc_market.cpp:110` |
| Yield recompute gate | `RandomModulo(100) > 10` or `hour == 12` | `npc_market.cpp:144` |
| Market window | clock hour `0..20` (`> 0x14` frees) | `npc_market.cpp:181` |
| Treasury top‑up threshold | `5,120,000` | `npc_market.h:68` |
| Sellable seller kind gate | `*building == 2` | `trade_sell.cpp:52` |
| Player unit price | `trunc(marketPrice(proto, 100))` (no markup) | `trade_sell.cpp:54` |
| Stall slot price multipliers | in 0.9 / rest 0.8 / raw 1.1 | `event5.h:56-58` |

---

## 8. Open questions / flagged unknowns

- **Multi‑component recipes in the live engine**: `building_production.cpp:155-156` models only the single (leaf) component path; the original aggregates up to 4 component rows from scene records not present in the port. The standalone `market_price_model.cpp:59-75` does loop all 4 slots and is the more faithful recipe reproduction. The two should be reconciled if exact multi‑ingredient prices matter.
- **Satellite market sweeps** in `NpcMarket_RunMarktSupervisorStep` (3 markets × 62 slots × {sell,buy} + the `Inventory_ComputeFreeCapacity` capacity branch) are DEFERRED — only the central‑market pricing path is exercised (`npc_market.cpp:154-162`).
- **`A_Obj.dat` / `A_Geb.dat` tail records** (records beyond 731 / 72 respectively) are present on disk but intentionally not loaded; their purpose is unknown from the load path alone.
- The constants `kMktSellCeilFrac (3.0)`, `kMktSellPriceContrib (0.33)`, `kMktSellResetHi (1.2)`, and the satellite constants `dbl_61FA00 (0.4)`, `flt_61FA08 (2.5)`, `dbl_61FA10 (0.1)` are recovered (`npc_market.h:44-50`) but are consumed in the **deferred** satellite/sell sub‑paths, not in the modeled central‑market `MarketRecomputeSlot`.
