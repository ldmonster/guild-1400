# Economy 05 — Buildings, Workshops & Business Assets

This document covers the **economic** side of buildings in the Guild / Europa 1400
(Die Gilde) C++ reimplementation: how a building's monetary value is computed,
what it costs to build / buy / upgrade, how stock storage scales, and how
ownership ties back into player wealth. Rendering, pathfinding, placement (Bauplatz)
and UI windows are explicitly out of scope — they are listed as DEFERRED in
`building.cpp` and are not discussed here except where they gate a money flow.

All formulas, constants and field offsets below are recovered byte-for-byte from
`gilde.exe` and cited against the reimplementation source.

---

## Source files

Primary (economic core):

- `src/sim/building.cpp`, `src/sim/building.h` — type table, id→record lookup, kind/security accessors, command sink.
- `src/sim/building_types.h` — `BuildingTypeDef` (589-byte type record) and `BuildingRec` (live building overlay) layouts.
- `src/sim/building_type.cpp`, `src/sim/building_type.h` — type-code classification / kind predicates.
- `src/sim/building_value.cpp`, `src/sim/building_value.h` — production rating, per-item base value, output interpolation.
- `src/sim/building_stock.cpp`, `src/sim/building_stock.h` — worth/sale-price model, workstation sums, production-worth aggregator, stock decay.
- `src/sim/building_storage.cpp`, `src/sim/building_storage.h` — storage-room item worth, stock value, room worth.
- `src/sim/building_upgrade.cpp`, `src/sim/building_upgrade.h` — upgrade cost (0.3 × worth) and level-up math.
- `src/sim/building_lifecycle.cpp`, `src/sim/building_lifecycle.h` — create/destroy slot accounting (build counter).
- `src/sim/building_create.cpp`, `src/sim/building_create.h`, `src/sim/building_create2.cpp` — new-building record initialisation + per-type defaults.

Secondary (referenced):

- `src/sim/building_production.cpp` — `Building_ComputeMarketPrice` (0x58f3d0), the price leaf every worth function calls.
- `src/sim/building4.cpp/.h` — AI "buy this building" evaluation and the default-objects (room contents) table.

---

## 1. Building / workshop types and their economic role

### 1.1 The type-definition table

Every building references a **building-type definition record**, the original's
`dword_13CE294` array, **stride 589 bytes**, indexed by the building's `+0` type
word (`building_types.h:55`, `kBuildingTypeStride = 589`). The accessor is
`BuildingTypeDefAt(typeIndex)` (`building.cpp:13`), which returns `nullptr` when the
table is unloaded (`g_buildingTypesLoaded == false`) — faithfully reproducing the
original's null-base bail.

The economically-load-bearing fields of `BuildingTypeDef` (`building_types.h:86-103`):

| Offset | Field | Economic role |
|--------|-------|---------------|
| +0   | `kind` | category/kind enum (1..0x1A); drives production/storage/sale classification |
| +35  | `roomList[64]` (word, 0-terminated) | room/object type ids; high bit (0x8000) = "flagged" worth slot |
| +547 | `outputProf` | output-product profession code |
| +553 | `inputFactor[2]` | input-good factors (used in item base value) |
| +559 | `inputProf` | input-product profession code |
| +563 | `outputFactor[2]` | output/production factors (drive production rate & item value) |
| +583 | `security` | security level — **also reused as the current upgrade level** (upgrade guard) |
| +584 | `maxUpgradeLevel` | highest upgrade level reachable for this type |
| +585 | `roomWorthMul` (dword) | **room-worth base multiplier** — the core of building valuation |

### 1.2 Kind classification (production vs. storage vs. sale)

The `kind` byte at `+0` of the type record selects the economic behaviour
(`building_type.cpp:253-261`):

- **Storage** — `Building_IsStorageKind(kind)` → `kind == 10` (`building_types.h:60`).
  Storage buildings are excluded from production and from the "nearest same type"
  search (`building_lifecycle.cpp:118`), and `Building_AdjustStockAndNotify` returns
  `-1` for them (`building_stock.cpp:124`).
- **Production / workshops** — `Building_IsProductionKind(kind)` → `kind ∈ {11,12,13,16,28}`
  (`building_type.cpp:259-261`). On creation a production building gets a default
  stock field (see §3.1).
- **Sale / auction** — the building object's `objectKind` byte (`BuildingRec +2`) being
  **6 or 7** marks it as a sale/auction price-mode building. This triggers the sale
  price discount in `Building_ComputeItemBaseValue` (`building_value.cpp:161-165`) and
  is the filter used by `Building_FindMatchingSupplier` (`building_stock.cpp:154`) and
  `DistributeGoodsToCustomers`.
- **Residences / civic** — kinds outside the above are mapped to UI categories by
  `Building_MapKindToCategory` (`building_type.cpp:238-251`) and otherwise carry no
  production; they contribute value only via their room worth (§2).

Type **codes** (the raw `+0` byte, 1..75) are grouped into 12 trade groups by
`BuildingType_GroupFromCode` (`building_type.cpp:17-34`); within a 6-code group the
highest code is rank 1 (`BuildingType_ComputeRankWithinGroup`, `building_type.cpp:56`).
This is the upgrade-tier ordering: **advancing one upgrade level increments the type
byte** (§4), moving the building to the next, higher-tier type record.

---

## 2. How a building's monetary VALUE is computed

Building value is built bottom-up from three layers: a per-item **market price**, a
per-type **flagged-slot worth**, and the **sale-price** / **room-worth** aggregations.

### 2.1 Market price (the price leaf)

`Building_ComputeMarketPrice(prot, qty)` (`building_production.cpp:114`, gilde 0x58f3d0)
is the price of a single good `prot` at quantity-byte `qty`. It reads a `SceneTypeDef`
(65-byte scene-object record):

- If a cached price exists (`+56`): `price = 32 · cached · qty · kMP_Qty`, ×`kMP_Sub3` if
  subtype==3 (`building_production.cpp:120-124`).
- Otherwise it composes from `baseValue / divisor`, the type's `outputFactor[]`, and a
  recursive sum over the good's component (`compType` high word), caching the result
  (`building_production.cpp:127-169`).

This is the unit every worth function below multiplies up.

### 2.2 Flagged-slot worth — the building "intrinsic" worth

`Building_SumFlaggedSlotsWorth(typeIndex)` (`building_stock.cpp:193`, gilde 0x5913e0) is
the canonical "how much is this building's structure worth" number that both the sale
price and the upgrade cost are derived from:

```
worth = 3840 * typeDef.roomWorthMul            // base term (roomWorthMul @ +585)
for each room in typeDef.roomList[0..63]:
    if room == 0: break
    kind    = room & 0x7FFF
    flagged = (room & 0x8000) != 0             // high-bit flag on the room word
    if flagged:
        worth = trunc( ComputeMarketPrice(kind, 100) + worth )
return worth
```

So a building's intrinsic worth = `3840 × roomWorthMul` plus the market value (at
qty 100) of each **flagged** room/equipment slot. `roomWorthMul` (`+585`) is the single
most important per-type valuation constant.

### 2.3 Sale price (what a building changes hands for)

`Building_ComputeSalePrice(typeIndex, qualityCode)` (`building_stock.cpp:219`, gilde 0x591480):

```
worth  = SumFlaggedSlotsWorth(typeIndex)
qmul   = (qualityCode == 14) ? 0.85 : 1.0          // condition-14 discount
taxTier = SaleTaxTier()                            // Gesetz law record 6, default 4
price  = trunc( ((4 - taxTier)*0.10 + 0.80) * worth * qmul )
```

Constants: `kSaleTaxSlope = 0.1` (`flt_626A14`), `kSaleTaxBase = 0.8` (`flt_626A18`)
(`building_stock.cpp:31-32`). With the default tax tier 4 the multiplier is exactly
`0.8`, i.e. a building sells for **80 % of its flagged-slot worth** (× 0.85 again when
its quality byte is 14). Lower tax tiers raise the multiplier (tier 0 → ×1.2).

### 2.4 Room worth — value of goods stored inside the building

`BuildingValue_ComputeRoomWorth(b, mul)` (`building_storage.cpp:89`, gilde 0x59116c):

```
v19 = 3840 * typeDef.roomWorthMul
walk typeDef.roomList; for storage rooms (scene kind 2 or 6):
    for each stored item:  v19 = trunc( ComputeMarketPrice(item.prot, item.qty) + v19 )
return trunc( mul * 0.01 * v19 )                   // kRW_Mul = 0.01 (flt_626A10)
```

So room worth = (structure base + value of goods in storage rooms) scaled by
`mul × 0.01`. Storage rooms are scene kinds 2 and 6 (`building_storage.cpp:106`).

### 2.5 Stock value (owner-share + goods)

`BuildingValue_ComputeStockValue(b, ownerId, buildingKind)` (`building_storage.cpp:52`,
gilde 0x590360) writes two numbers:

```
// owner share — a slice of the OWNER's liquid wealth, capped:
wealth     = OwnerWealth(ownerId)                  // 0 if ownerId == 0xFFFF
capped     = min(wealth, 2_560_000)                // kSV_Cap = flt_6269D4
ownerShare = trunc( capped * (buildingKind==2 ? 0.09 : 0.04) )   // kSV_M2b / kSV_M2

// stock worth — market value of goods at qty 100, scaled by count:
for each storage item:
    qty = isReserveGood ? count-1 : count
    n   = max(1, trunc(qty * 0.30))                // kSV_StockM = 0.3
    stockWorth += ComputeMarketPrice(item.prot, 100) * n
```

The **owner share** term (`building_storage.cpp:57-69`) directly couples a building's
booked value to the owner's wealth: 4 % of the owner's (capped) liquid wealth, or 9 %
for `buildingKind == 2`. The wealth cap is **2,560,000** (`flt_6269D4`,
`building_storage.cpp:8`). `SumStorageItemWorth` (`building_storage.cpp:39`, gilde
0x591658) is the simpler "sum of market price over the main storage room" used elsewhere.

### 2.6 Production worth (workshop output value)

`BuildingValue_ComputeProductionWorth(b, selfTypeIndex)` (`building_stock.cpp:342`, gilde
0x58fe68) produces a 21-column result frame (`ProductionWorth.col[21]`,
`building_stock.h:244`). The core loop values the building's output and input slots:

```
qScale = building.quality(+61) * 0.01              // kQualityScale = dbl_6269CC
for output slot o in 0..5:  contrib = trunc(ComputeItemBaseValue(b, type, o, -1) * qScale)
for input  slot i in 0..1:  contrib = trunc(ComputeItemBaseValue(b, type, -1, i) * qScale)
col[3] = total production value;  per-kind worth columns added for kinds 5/7/22
```

`Building_ComputeItemBaseValue(b, typeIndex, outIdx, inIdx)` (`building_value.cpp:139`,
gilde 0x58f328) is the per-good factor:

```
v9 = 896 * (inIdx != -1 ? inputFactor[inIdx] : outputFactor[outIdx])
if objectKind == 6 || 7 (sale/auction):
    v9 *= (1 - (2 - priceMode) * 0.25)             // sale discount; kPriceMode = 0.25
return v9
```

The constant **896** is the per-factor scale; the sale discount uses `g_priceMode`
(`dword_63C744`).

### 2.7 Production output & rate (capacity in goods/time)

Live output interpolates between a zero-fill and full-fill output by the building's
fill ratio (`building_value.cpp:185-218`):

```
ComputeCurrentOutput(b):  out = lerp(outZeroFill, outFullFill, fillLevel/fillCap) + outBonus, floored at 0
ComputeMaxOutput(b):      same lerp WITHOUT outBonus / floor
ComputeOutputRatio(b):    current / max
```

(`fillCap` @+32, `outZeroFill` @+16, `outFullFill` @+28, `outBonus` @+36 in `BuildingRec`,
`building_types.h:139-143`.) Empty (`typeIndex == 0xFFFF`) or inactive (`activeFlag == 0`)
buildings return -1/0.

The theoretical **production rate** (`Building_ComputeProductionRate`,
`building_value.cpp:172`, gilde 0x58f268):

```
rate = (Σ_{i=0..1} outputFactor[i] * 28 * 32 * (1/12) * (1/60)) * scenePriceField / sceneDivisor
```

Constants: `kRateF1=28`, `kRateF2=32`, `kRateD1=1/12`, `kRateD2=1/60`
(`building_value.cpp:27-29`). Note `28·32·(1/12)·(1/60) ≈ 1.244` per output factor unit.

### 2.8 Production rating (efficiency, 0..1)

`Building_EvalProductionRating(b, stat)` (`building_value.cpp:52`, gilde 0x58a794)
computes a per-stat rating in [0,1] from `statLevel[stat]/252`, minus a handler-weighted
penalty, plus an inventory-slot bonus (stat 3), minus a staff/equipment term decoded from
the packed `staffBits` (+44) bitfield. `kStatScale = 1/252` (`building_value.cpp:9`).
This feeds the efficiency score:

```
ComputeEfficiencyScore(b) = maxOut*0.001*0.2 + (curOut/maxOut)*0.8   // building_stock.cpp:111
```

(`kEffMaxBase=0.001`, `kEffMaxW=0.2`, `kEffRatioW=0.8`, `building_stock.cpp:27-29`),
gated to 0 when `activeFlag <= 1`.

---

## 3. Construction / purchase cost

### 3.1 Construction (build new building)

The full build pipeline (`VIBE_Building_CreateGebaeude` @0x586fb8, scene/name-table
coupled) is **DEFERRED** (`building.cpp:198`). The reimplementation models the observable
**record initialisation** in `building_create.cpp` and the per-type field defaults in
`building_create2.cpp`. No explicit monetary "build cost" constant appears in these files —
in the original the cost is charged through the command channel (`IBuildingCommandSink`,
`building.h:101`) at the request site (`RequestGebaeudeBauen` @0x5942bc), which is deferred.

What *is* recovered is the production-capacity / stock defaults stamped at creation
(`building_create.cpp:43-55`, `building_create2.cpp:64-156`):

| Type (prot) | Default written | Field | Meaning |
|-------------|-----------------|-------|---------|
| any production type | `5000` | +48 | initial stock/working capital field (`building_create2.cpp:72`) |
| prot 71 | `80000` | +48 | large stock field |
| prot 31, 32 | `100` | +48 | small stock field + quality 0.80 at +73 (`kQualityFloatBits`) |
| prot 30 | `100` + plant map (0x600 bytes) | +48/+113 | farm plot map (`building_create2.cpp:105-116`) |
| (all new) | `32000`/`100`/`2`/`100`/`1.0f`/cond `100` | +57/+61/+65/+69/+73/+92 | default scalar/quality fields (`building_create.cpp:43-49`) |

So a freshly-built **production workshop** starts with a stock/capital field of **5000**
(or 80000 for type 71, 100 for small types). The building condition byte (+92) starts at
**100** (fully new), and quality (+73) defaults to **1.0f** (or 0.80f for types 31/32).

The id is assigned from `g_buildingNextId` (`dword_649890`, `building_create.cpp:9,37`)
and the slot is found by linear scan for a dead slot (`FindFreeBuildingSlot`,
`building_create.cpp:16`). The global build counter `g_buildCounter` (`dword_647724`)
tracks live buildings and is decremented on demolition (`building_lifecycle.cpp:81-84`).

### 3.2 Purchase (buy existing building)

Buying a building goes through the command sink rather than a recovered formula. The
purchase branch is `Interaction_PerformBuildingUpgradeOnObject` with
`handlerKind==4 && targetObjKind==8` → `EnqueueBuyBuilding(buildingId, sellerId, buyerId)`
(`building_upgrade.cpp:132-137`), returning interaction code 19. The **price** for that
purchase is the sale price from §2.3 (`Building_ComputeSalePrice`). AI valuation of a buy
candidate is `Building4_EvalBuyBuilding` (`building4.h:255`, gilde 0x46c97c) which, on a
handler hit, enqueues the buy and returns 16.

---

## 4. Upgrade costs and their effects

### 4.1 Upgrade cost — 0.3 × worth

Identical across all four upgrade entry points (`building_upgrade.cpp:56-61`, gilde):

```
worth = Building_SumFlaggedSlotsWorth(typeIndex)   // §2.2
cost  = trunc( worth * 0.30 )                       // kUpgradeCostFactor = 0x3E99999A
```

The factor `kUpgradeCostFactor = 0.3` (`building_upgrade.h:46`); the four originals'
constants `flt_61A474 / _61A48C / _61A5B4 / _61A5D0` are all byte-identical
(`0x3E99999A`). An upgrade therefore costs **30 % of the building's current flagged-slot
worth**, and the charge is routed through `IUpgradeSink::EnqueueUpgradeCharge(payer,
target, cost, priceMode)` (gilde 0x494604, `building_upgrade.cpp:115`). The four entry
points (`building_upgrade.cpp`):

- `Interaction_PerformBuildingUpgrade` (0x46cfc4) → code 17.
- `Interaction_PerformBuildingUpgradeOnObject` (0x46d978) → code 19; note the upgrade
  branch always pays with **payer -1** (the AI/none payer, `building_upgrade.cpp:142`).
- `NpcAction_UpgradeTownHall` (0x472b90) → code 49.
- `NpcAction_UpgradeDungeon` (0x472fd0) → code 60.

### 4.2 Level-up effect

`Building_ApplyUpgradeLevel(typeIndex, packedQual)` (`building_upgrade.cpp:74`, gilde
0x49aff0):

```
guard: if typeDef.security(+583) >= typeDef.maxUpgradeLevel(+584): atMaxLevel, no change
apply:
    newTypeByte = typeIndex + 1                     // advance to next, higher type record
    cond        = HIBYTE(packedQual) + (100 - (packedQual >> 24)) / 2   // recompute condition (+92)
```

The key economic effect: **upgrading increments the building's type byte**, moving it to
the next type record in its group (§1.2). Because capacity, production factors,
`roomWorthMul` and security all live in the per-type record, the upgraded building
inherits the higher type's larger `roomWorthMul` (→ higher worth, sale price and future
upgrade cost) and larger output/input factors (→ higher production value). The condition
byte is recomputed toward 100 on each level-up. There is no separate "capacity" field
that is bumped — capacity scales because the building *becomes a different, better type*.

---

## 5. Upkeep / running costs / rent

**Flagged as unknown / not present in the recovered building modules.** No `rent`,
`upkeep`, per-tick maintenance fee, or wage field appears in any of the building source
files (verified by grep across `src/sim/building*`). Running costs in the original Guild
are driven by **worker wages and taxes**, which are handled outside the buildings module:

- The **tax tier** that scales the sale price comes from the law/Gesetz subsystem
  (`SaleTaxTier()` → `Gesetz_GetRecord(6)`, `building_stock.h:144-146`), not from the
  building record itself.
- Worker counts per building are available via `Building_SumWorkstationCount` /
  `_SumWorkstationByCategory` (`building_stock.cpp:229,248`), which sum per-room worker
  bytes (`+483+i`) for a category — these feed wage costs computed in the worker/person
  module, which is outside this topic.

If a recurring building upkeep exists in the live game, it is applied by the economy
tick / person-wage code, not the buildings module documented here. **Treat building
upkeep/rent as not modelled in these files.**

---

## 6. Storage capacity per building and how it scales

Storage is modelled by the live **stock record** `BuildingStockRec`
(`building_stock.h:52-66`) over the runtime building object:

| Field | Offset | Role |
|-------|--------|------|
| `stock`     | +10 | current stock / fill (word) |
| `decayRate` | +16 | per-step decay rate |
| `zeroFill`  | +20 | output at zero fill |
| `fullFill`  | +28 | output at full fill |
| `fillCap`   | +32 | **fill capacity** (the storage cap) |
| `delta`     | +36 | projected/added stock delta |

Capacity is the `fillCap` float at +32. Stock above `fillCap` does not raise output —
the output interpolation clamps fill to `fillCap` (`building_value.cpp:190`,
`building_stock.cpp:130`). The decay recurrence the game integrates while `s > 1.0` is
`s = s - rate + rate/s` (`building_stock.cpp:73,93`), used by `ComputeProjectedStock`
(0x57d1c8) and `SyncStockLevel` (0x57d0f8).

`Building_AdjustStockAndNotify(b, addAmount)` (`building_stock.cpp:122`, gilde 0x57d5b4)
adds goods and recomputes output; it returns **-1** for empty (`0xFFFF`), **storage-kind
(kind 10)**, or inactive buildings — i.e. only active production buildings hold a working
stock through this path. When the new total hits 0 the building is deactivated (active
byte +8 cleared) and a slot reset queued (`building_stock.cpp:143-146`).

Capacity **scales with type**: `fillCap`, `outZeroFill`, `outFullFill` are set from the
building's type record at creation/upgrade, so a higher-tier (upgraded) workshop has a
larger capacity and output band. The initial stock/capital field at +48 (5000 / 80000 /
100 per type, §3.1) is the starting working stock.

---

## 7. Building ownership and player wealth

Ownership is recorded directly on the building record as **owner words**:

- New buildings stamp `ownerWord` at **+37** and **+39** (`building_create.cpp:52-54`,
  `BuildingSaleRec.ownerPlayer @+37`, `ownerB @+39`, `building_stock.h:90-91`).
- For owner protocols 4..7 the owner's person id is also written at +101
  (`applyLabel97`, `building_create2.cpp:44-61`); `0xFFFF` owner → no owner (LABEL_89,
  `building_create2.cpp:42`).

Ownership feeds player wealth in two directions:

1. **Building as a booked asset.** `BuildingValue_ComputeStockValue` adds an **owner-wealth
   share** — `4 %` (or `9 %` for kind 2) of the owner's capped liquid wealth — to the
   building's value (`building_storage.cpp:65-69`). `OwnerWealth(ownerId)` resolves to
   `VIBE_Person_ComputeTotalWealth(owner)` (`building_stock.h:47-49`). Conversely the
   building's sale price (§2.3) is realised into the owner's cash when sold.

2. **Trade-route customer links.** Sale/auction buildings (kind 6/7) carry up to 5
   customer-id slots at +104+4k (`BPCustomerId`, `building_stock.cpp:45`).
   `Building_FindMatchingSupplier` (`building_stock.cpp:150`) and
   `DistributeGoodsToCustomers` (`building_stock.cpp:276`) use these to route goods and
   drift prices between owned buildings — the per-building income mechanism.

On **demolition** `Building_RemoveAndCleanup` (`building_lifecycle.cpp:47`, gilde 0x5894b0)
releases the occupant's holdings (`ReleaseOccupantHoldings`), clears trade routes pointing
at the building (notifying rival owners if any hit), destroys the character handle,
decrements the type's active count, and (if freeing the slot) decrements the global build
counter. No demolition refund/cost constant is present in the recovered code — any payout
is handled at the command/economy layer.

---

## 8. Constant quick-reference

| Constant | Value | Address | Used by |
|----------|-------|---------|---------|
| roomWorth base | `3840 × roomWorthMul` | type +585 | flagged-slot worth, room worth |
| item base factor | `896` | — | `ComputeItemBaseValue` |
| upgrade cost factor | `0.30` | 0x3E99999A | `ComputeUpgradeCost` |
| sale tax base / slope | `0.80` / `0.10` | flt_626A18 / flt_626A14 | `ComputeSalePrice` |
| condition-14 sale discount | `×0.85` | — | `ComputeSalePrice` |
| stock-value owner share | `0.04` (kind≠2) / `0.09` (kind==2) | dbl_6269DC / dbl_6269E4 | `ComputeStockValue` |
| owner-wealth cap | `2,560,000` | flt_6269D4 | `ComputeStockValue` |
| stock-count scale | `0.30` (min 1) | dbl_6269EC | `ComputeStockValue` |
| room-worth scale | `0.01` | flt_626A10 | `ComputeRoomWorth` |
| sale price-mode discount | `(1-(2-mode)·0.25)` | dbl_6268FC | `ComputeItemBaseValue` |
| stat scale | `1/252` | flt_62670C | `EvalProductionRating` |
| production rate term | `28·32·(1/12)·(1/60)` | — | `ComputeProductionRate` |
| efficiency weights | `0.001·0.2` + `0.8` | flt_62594C/dbl_625954/dbl_62595C | `ComputeEfficiencyScore` |
| default production stock | `5000` (80000/100 per type) | — | `ApplyTypeDefaults` +48 |
| default condition | `100` | — | create +92 |

---

## 9. Known unknowns / gaps

- **No upkeep / rent / maintenance fee** is present in the buildings module (§5). If it
  exists, it lives in the person-wage / economy-tick code (out of topic).
- **Explicit build cost** is not a recovered constant; construction money is charged via
  the deferred command path (`RequestGebaeudeBauen`, `building.cpp:215`). The recovered
  side only stamps the starting stock/capital fields (§3.1).
- **`ComputeProductionWorth`'s full Person-array walk** (768 records, He_* handlers) is
  modelled via hooks for a single self-worker (`building_stock.cpp:353-380`); the
  multi-worker accumulation is owned by the entity module.
- The **scene-room worth scene walks** inside `ComputeProductionWorth` (kind 7 columns
  12/15) default to 0 via the hook (`building_stock.cpp:395-397`) — exact values depend on
  the deferred `QueryFind` scene subsystem.
- `roomWorthMul` and the per-type production factors are **data-table values**
  (`dword_13CE294`) loaded at runtime; the table is empty in the cold IDB, so concrete
  per-building numbers depend on the loaded gamedata, not the code.
