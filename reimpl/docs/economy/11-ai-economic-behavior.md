# AI Economic Behavior — Meister AI, Needs & Desires

This document describes the **demand and supply side of the AI economy** in the
C++ reimplementation of *The Guild / Europa 1400* (Die Gilde): how an AI
"Meister" (master craftsman) runs a business, what it buys and sells, how it
plans production and staffing, how it builds new premises, and — on the demand
side — how NPC *needs* and *desires* drive consumption and thus the goods market.

All behavior is reconstructed from `gilde.exe`. A recurring architectural fact
worth stating up front: **the AI never mutates game state directly.** Every
decision is emitted as a *network command* (`Command_Queue*` / `Enqueue*`)
applied deterministically on every peer (lockstep). The reimplementation routes
each command through an injectable hook so the *sequence* and *math* are
verifiable, while the entity/scene/query leaf reads are injected through small
env structs. Where a decision consumes the RNG, the draw order is preserved
exactly (determinism-critical).

---

## Source files

| File | What it covers |
|------|----------------|
| `src/ai/meister_economy.{h,cpp}` | The per-faction economy turn (`ProcessPlayerTurn`, gilde.exe `0x5321ec`) and the staffing/production decision cores (`HireStaffDecision 0x45c670`, `TrainStaffDecision 0x45d2ac`, `PlanProductionRatio 0x4596e4`). |
| `src/ai/meisterai.{h,cpp}` | The per-worker rule cores the economy turn uses: `SecurityHeatDecrement`, `MoodRelationDelta`, `TaxPayout`, `MoodDecayRoll`, `ConfrontationDecision`, `RunWorkerMoodPass`. |
| `src/ai/meister_trade.{h,cpp}` | Per-workslot stock-need math: `StockCapacity`, `StockDeficit`, `SortStockByValue` (all from `EvaluateStockNeeds 0x58658c`). |
| `src/ai/meister_trade2.{h,cpp}` | Transporter buy decision (`0x45e71c`), tavern refill (`0x45f0a0`), city wealth balancing (`0x4c763c`). |
| `src/ai/meister_general_trade.{h,cpp}` | Transporter-fleet orchestration (`CollectTransporters 0x45e71c`). |
| `src/ai/meister_storage.{h,cpp}` | Storage sell decisions (`TradeManageStorage 0x45f1e4`), workstation/item table builds (`AssignWorkstations 0x4599f0`, `CollectStorageItems 0x45a62c`, `GatherRequiredItems 0x45c10c`, `ReserveWorkstationItems 0x45bd68`). |
| `src/ai/meister_workstation.{h,cpp}` | Workstation scoring + item-distribution cores (`ComputeWorkstationOutput 0x45b948`, `CheckWorkstationCapacity 0x45ba84`, `GatherClampToBudget`/`GatherNetShortfall`, `DistributePlanQty 0x45aa78`, sorts). |
| `src/ai/meister_buildtasks.{h,cpp}` | Construction-selection orchestration (`ProcessBuildingNeeds 0x4c7774`). |
| `src/ai/meister_supervision.{h,cpp}` | Per-turn building task dispatch (`RunBuildingTasks 0x4c930c`) + staffing supervision (`FlagIdleStaff 0x45df7c`, `CountStaffByType 0x45d3ec`, etc.). |
| `src/ai/needs.{h,cpp}` | Character need decay / random-need pickers (`0x58adc0`, `0x58aea8`, `0x58b0cc`, `0x58b2f4`, `0x58b4e8`, `0x58b614`) — the **demand generator**. |
| `src/ai/desire_table.{h,cpp}` | Desire/attribute name table + AI buy-plan price-ratio weighting (`ComputeWeights 0x47936c`, `LookupAttributeIndex 0x4794e4`). |
| `src/ai/building_needs.{h,cpp}` | Construction decision cores (`BuildProbability`, `BuildCandidateMask`, `BuildPickCategory`) + the building-task dispatch order. |
| `src/ai/aiplayer.{h,cpp}` | The `AiPlayer` per-building-type behavior descriptor and the `Ai_EvaluateMeister` dispatch classifier (`ClassifyMeisterRoutine 0x4533a8`). |
| `src/ai/types.h` | Need decay params (`kDecayScale`, `kDecayBound`) and the need-pick eligibility table layouts. |

> **Scope note / unknowns.** Several of the *largest* original bodies (the cart
> unload/reload tail of `TradeManageStorage`, the `TradeRemotePurchaseA/B`
> remote-purchase planners `0x46329c`/`0x463c4c`, `TradeGeneral 0x4614d0`'s full
> sweep, build phases 0 and 1) are **deferred** as entity-array/scene-tree/string
> plumbing — their *decision math* is recovered, their orchestration is not.
> These are flagged inline throughout. Anything not grounded in a cited
> `file:line` below should be treated as not-yet-recovered.

---

## 1. Who runs which business: the Meister dispatch

The `AiPlayer` table (`dword_13CE294`, stride **589**) is indexed by the
building/person **kind byte**, not by faction — each 589-byte entry is the
behavior descriptor for one *building type*
(`src/ai/aiplayer.h:33-67`). The two load-bearing fields:

- `+0` — the **AI-class id** (dispatch key): `4=Diebe` (thieves), `5=Bank`,
  `8`/`14`=craft production, `9`=generic production, `16=Ambush`, `19=Wache`
  (guard) — `src/ai/aiplayer.h:36-45`.
- `+583` — the **production/income multiplier** ("Vermehrungsfaktor"), tier
  `1/2/3`, which scales both production output and banker tax
  (`src/ai/aiplayer.h:58`).

`Ai_EvaluateMeister` (`0x4533a8`) classifies which per-meister routine drives a
building. The pure decision core is `ClassifyMeisterRoutine`
(`src/ai/aiplayer.cpp:28-47`):

```cpp
// category 1 or 4 -> production family; class 8/14 -> craft, else generic.
if (category == 1 || category == 4) {
    if (aiClass == 8 || aiClass == 14) return kCraftProduction;
    return kProduction;
}
if (category == 2) return kFarming;
switch (aiClass) {
    case 19: return kWache;   case 4: return kDiebe;
    case 16: return kAmbush;  case 5: return kBank;
    case 9:  return kPlanProduction;
    default: return kNone;
}
```

So a building's *category* (from `Building_MapTypeToCategory`) and its AI-class
byte together select the economic behavior: production shops, farms, banks,
guards, or intrigue specialists.

---

## 2. The per-faction economy turn — `ProcessPlayerTurn` (0x5321ec)

`ProcessPlayerTurn` (`src/ai/meister_economy.cpp:49-181`) is the Guild-Master
AI's whole economy turn for one faction, in three ordered phases. It returns a
running command count (`v3`).

### Phase 1 — object/building sweep
Iterates the faction's owned buildings (`src/ai/meister_economy.cpp:54-120`):

1. **Needs request** — a building with flag bit `0x800` set emits a `cmd25`
   "needs request" (`kRequestArgs25`, operand `2048`) — line 59-62.
2. **Security sweep** (`kind == 4/16/19`) — for each object with `heat > 5`, the
   heat is decremented by the guarding building's security level
   (`src/ai/meisterai.cpp:31-37`):
   ```cpp
   newHeat = clamp_lo0( heat - 2*securityLevel );   // signed-byte truncation
   ```
   If there is no guarding building, emit `Cmd14`; if `heat <= 5`, emit a
   `Quad43` request instead (lines 65-81).
3. **Banker** (`kind == 5`) — reset stats (`State23`), then pay the building tax
   `16000 * multiplier` (`TaxPayout`, `src/ai/meisterai.cpp:79-81`). The original
   gates the tax on the faction not being the guild head and on
   `Person_SumCurrencyHeld(player) < 160000` (lines 82-92).
4. **Guard target** (`kind == 11/12/13`) — emit a `GuardTarget61` request
   (line 93-97).
5. **Market supervision** — every owned building emits a `Quad56` supervision
   request; non-guard buildings additionally emit a `SlotReset28 op27`
   ("supervise begin") and, if market-supervised, an `op49` ("stammtisch")
   (lines 99-112).
6. **Mood-decay roll** — `MoodDecayRoll` (`src/ai/meisterai.cpp:84-91`): a
   `RandomModulo(100) > 30` roll, excluded for kinds `11/12/13/16` and the
   `flagBit0` case, applies a mood penalty `-(RandomModulo(3)+2)` (lines 114-119).

### Phase 2 — worker stocking + mood pass
For each production worker (`kind 6`) — `src/ai/meister_economy.cpp:126-166`:

1. **Stock request** — `QueueRequest16` carries a precomputed per-worker item
   value. That value (per `src/ai/meister_economy.h:86-88`) is
   `Coord_ConvertX(ComputeItemBaseValue * attitudeA * 0.01)` — i.e. the worker's
   *attitude* scales how much stock/wage it is granted (`kAttitudePriceScale =
   0.01`, `src/ai/meisterai.h:32`).
2. **Mood/relation delta** — `MoodRelationDelta` (`src/ai/meisterai.cpp:40-76`)
   computes a signed delta from two attitude scalars (`worker+61`, `worker+65`)
   and the player↔worker relation, with `/3` and `/5` integer divisions and
   min-1 clamps. A non-zero delta emits a `Coord27` mood command.
3. **Relation packet** — a `State22` relation delta is always emitted.
4. **Confrontation** — when post-delta relation `< -26`, a gated RNG roll
   (`-RandomModulo(0x4A) < newRelation`) may spawn a challenge (variant `52`) or
   duel (variant `51`), with a `RandomModulo(100) <= 50` split
   (`ConfrontationDecision`, `src/ai/meisterai.cpp:94-105`).
5. **Profession coord** — craft professions (`2/1/19/20/16/15`) get a profession
   coord update (lines 159-164).

### Phase 3 — production-worth pass
For each craft building (`kind 4`) — `src/ai/meister_economy.cpp:169-178`: emits
up to two `Cmd15` "production-worth payout" commands, one per produced product
(`productSumA`/`productSumB`, gated on `hasWorthA`/`hasWorthB`). These are the
income the AI books for goods it has produced.

> The deeply scene-coupled leaf reads (the `GameObject_QueryFind` object walk,
> `Building_ComputeItemBaseValue`, `GameObject_SumValuesAtLocation`) are injected
> through the `MeisterFaction` synthetic model — `src/ai/meister_economy.h:101-106`.

---

## 3. Staffing decisions — hire, train, production scaling

These cores live alongside the turn (`src/ai/meister_economy.cpp:183-226`).

### Hire (`HireStaffDecision`, 0x45c670)
```cpp
// src/ai/meister_economy.cpp:184-197
if (staffCount >= staffCap) return false;
gate = (staffCount == 0)
     ? true
     : (!busyFlag && wage <= budget &&
        RandomModulo(0x48) >= 36 - 2*staffCount);
return gate && !hasHandler;
```
The cap is `AiPlayer+561 + AiPlayer+562`. The first worker is always hireable
(when under cap and no handler pending); each additional worker faces a rising
RNG bar `36 - 2*staffCount` (so the more staff already present, the easier the
roll passes — a *diminishing-resistance* curve) **and** must be affordable.

### Train (`TrainStaffDecision`, 0x45d2ac)
```cpp
// src/ai/meister_economy.cpp:200-208
if (busyFlag) return false;
if (RandomModulo(100) < threshold) return false;
return !hasTrainer && trainerCount < 3 && budget >= 38400;
```
Training is capped at `< 3` trainers, requires a free budget floor of `38400`
(`kTrainStaffMinBudget`), and costs `12800` (`kTrainStaffCost`,
`src/ai/meister_economy.h:138-139`).

### Production scaling (`PlanProductionRatio`, 0x4596e4)
The output a building plans is the stock count scaled by the multiplier tier,
then biased (`src/ai/meister_economy.cpp:211-226`):
```cpp
scaled = stockCount * { tier1: 3.906250e-06, tier2: 7.812500e-06, tier3: 1.5625e-05 };
return scaled * 0.5 + 0.25;     // kProdRatioScale / kProdRatioBias
```
(Constants `flt_6198E8..F8`, `src/ai/meister_economy.cpp:36-40`.) Note the
default branch is left undefined in the original; the reimplementation treats it
as a `0` contribution since production buildings always have tier `1/2/3`
(`src/ai/meister_economy.cpp:217-223`).

---

## 4. What to restock — per-workslot stock-need math (`EvaluateStockNeeds`, 0x58658c)

Before buying, the AI computes a stock **deficit** per workslot
(`src/ai/meister_trade.cpp:17-50`):

```cpp
// StockCapacity: storage size depends on item type + slot "level".
StockCapacity(itemTypeCode, level):
    if itemTypeCode == 477:  return 5*level + 10;
    if level == 3:           return 80;
    else:                    return 20*level;

// StockDeficit: how short the slot is.
used = (unsigned)(cap*count) >> 2;          // logical shift /4
have = (itemTypeCode in {42,278,475,476}) ? count-1 : count;
deficit = max(0, have - used);
```

The collected stock-need records are then **selection-sorted by priced value,
descending** (`SortStockByValue`, `src/ai/meister_trade.cpp:41-50`), so the
highest-value shortfalls are funded first. The priced value is
`marketPrice * slot[73]` (`src/ai/meister_trade.h:38-40`).

---

## 5. Workstation planning — what to produce, what to buy for it

The AI maintains **two parallel scratch tables** (recovered byte-for-byte):

- the **item table** `dword_B5444E[]` (stride 64, capacity 128) — `WsItem`,
  `src/ai/meister_workstation.h:76-93`.
- the **workstation table** `dword_B56464[]` (stride 88, capacity 32) —
  `WsStation`, `src/ai/meister_workstation.h:118-132`.

### 5.1 Building the tables — `AssignWorkstations` (0x4599f0)
`src/ai/meister_storage.cpp:219-330`:

1. Collect each workstation with production `count > 1` into a `WsStation`
   (capped at 32).
2. Expand each station's 4 input slots into the item table (capped at 128),
   linking slot → item index.
3. **Special-output duplication** (ids `449..454`) — when the AI type allows it
   (`AiPlayer+0` is `14` or `8`), the produced "service" item is added as its own
   station and stamped with storage/matched bits (`0x24` / `0x204`) — lines
   261-280. This is how service outputs feed back as inputs.
4. Back-link each item to its producing station (so chained inputs get a price
   discount in scoring).
5. Snapshot stock + free capacity; emit a `Notify20` for any item with **zero
   stock** (lines 294-300).
6. Fold handler deliveries into `station.incoming` + `item.reserved`.
7. Score each station (`ComputeWorkstationOutput`), sort, mark matched items.

### 5.2 Scoring a workstation — `ComputeWorkstationOutput` (0x45b948)
`src/ai/meister_workstation.cpp:18-54`:

```cpp
for each input slot k:
    price = market_price(item.id);
    item.unitPrice = (item.backIndex == -1) ? price        // raw material
                                            : price * 0.9;  // chained (kChainDiscount)
    outputValue += inputNeed[k] * item.unitPrice;
outputValue /= divisorOutputs;                 // typedef +54
rate        = production_rate(typeId);
outCache    = outputValue + rate;
sellPrice   = market_price(typeId);
score       = (sellPrice - outCache) / divisorRate;   // typedef +34  -> the margin
```

So `score` is the **profit margin**: sell price minus (input cost + rate),
normalized. Stations are then selection-sorted by score **ascending**
(`SortWorkstationsByScore`, `src/ai/meister_workstation.cpp:57-75`) — the
original bubbles the *smallest* score to the front; the final index is stamped
into `reserveTarget`. (The chained-input `0.9` discount means goods the AI makes
itself are valued as cheaper inputs, favoring vertical integration.)

### 5.3 Feasibility & reservation
- `CheckWorkstationCapacity` (`0x45ba84`, `src/ai/meister_workstation.cpp:161-213`)
  recursively tests whether every input slot can be satisfied — from own stock,
  from a chained producer (recurse), or from a reachable **seller** (not self,
  category ≠ 2 (market), within the seller's deficit). Top-level slots that fall
  short fail immediately.
- `ReserveWorkstationItems` (`0x45bd68`, `src/ai/meister_storage.cpp:413-483`)
  recursively stamps reserve targets down the input chain, with a key economic
  rule: **same-owner transfers are free** (reserve the full chain quantity,
  `it.field18 = it.stock`, line 459), while cross-faction supply is a *priced*
  reservation clamped to the seller's deficit (lines 461-465).

### 5.4 Buying inputs — `GatherRequiredItems` (0x45c10c)
`src/ai/meister_storage.cpp:376-410`:

1. Bump `item.required` for each consumer need that is a tradeable good (in the
   city import/export lists) and set the reserve bit `0x2`.
2. Net each item's shortfall: `required - (reserved + stock)`, clamped ≥ 0
   (`GatherNetShortfall`, `src/ai/meister_workstation.cpp:95-100`).
3. Pick a seller; items with **no seller** drop their demand to zero
   (`src/ai/meister_storage.cpp:404-405`).
4. **Clamp purchases to budget** (`GatherClampToBudget`,
   `src/ai/meister_workstation.cpp:103-120`): walk items, dropping one unit at a
   time while `budget < required*unitPrice + accumCost`, accumulating the running
   (truncated) cost. This is the hard budget constraint on the AI's buying.

### 5.5 Moving stock storage↔workstation — `DistributePlanQty` (0x45aa78)
`src/ai/meister_workstation.cpp:123-138`:
```cpp
raw       = min(sourceFreeCap, destRoom);
budgetQty = (budget >> 2) / unitPrice;     // budget/4 affordable (signed shift)
planned   = trunc( min(raw, budgetQty) );
```
A move is clamped to physical room **and** to a *quarter* of the AI's budget per
item. Items are then sorted by `plannedQty * margin` descending
(`SortItemsByPlannedValue`, `src/ai/meister_workstation.cpp:78-92`) — the
highest-value moves go first.

---

## 6. Selling — storage management (`TradeManageStorage`, 0x45f1e4)

`RunStorageSellPass` (`src/ai/meister_storage.cpp:97-174`) runs four sell
decisions over the built tables. The `storageVariant` flag distinguishes
`TradeManageStorage` (cart cap 3, `+0.25` bias) from `TradeGeneral 0x4614d0`
(slotCap/3, no bias).

1. **Demand bump** (`StorageDemandBump`, lines 28-36) — for a flagged but
   unmatched item whose stock exceeds `slotCapacity/3`, stamp its `required` to
   the stock (or `stock/2` for AI type **22**, the "warehouse" faction):
   ```cpp
   if ((itemBits & 6) != 0) return 0;        // already matched/reserved
   if ((itemBits & 8) == 0) return 0;        // not flagged
   if (slotCapacity/3 >= stock) return 0;
   return aiTypeIs22 ? stock/2 : stock;
   ```

2. **Profit-margin sell** (`ProfitMarginSell`, lines 39-62, *odd hours only*,
   guarded by `+437 & 2` to run once per odd hour) — a workstation dumps its
   *whole stock* when its price ratio is good:
   ```cpp
   ratio = sellPrice / buyPrice;            // ComputeMarketPrice / CachedMarketPrice
   threshold = roll()*1.25 + bias;          // bias = 0.25 (storage) / 0 (general)
   if (ratio < 1.0 && ratio <= threshold) return stock;   // sell everything
   ```
   Selling when `ratio < 1.0` means the current *offer* price (ComputeMarketPrice)
   is below the *cached cost basis* — i.e. the AI offloads goods whose market has
   softened, scaled by a random willingness `roll*1.25+0.25`.

3. **Emergency sell** (`EmergencySell`, lines 65-81) — when cash-strapped
   (`funds < 3200` **or** `held < 8000`) and projected proceeds are still below
   `32000`, sell `max(1, 32000/price)` units per workstation until proceeds hit
   the `32000` target (`kEmergencyTarget`). Proceeds start at `min(held, funds)`
   (line 142).

4. **Overstock sell** (`OverstockSell`, lines 84-95) — a workstation above
   `3*slotCapacity/4` sells `max(5, stock/4)` units (`kOverstockMin = 5`).

Constants: `kProfitSellSlope = 1.25`, `kProfitSellBias = 0.25`,
`kEmergencyFundsFloor = 3200`, `kEmergencyHeldFloor = 8000`,
`kEmergencyTarget = 32000` (`src/ai/meister_storage.h:52-57`).

> The cart unload/reconcile/reload tail and the `cm_RequestSellObjekt` log lines
> are **deferred** (`src/ai/meister_storage.cpp:6-12`).

---

## 7. Trading logistics — transporters, taverns, city wealth

### 7.1 Transporter fleet (`CollectTransporters`, 0x45e71c)
Runs **twice a day, on odd hours only** (`TransporterAuditRuns`,
`src/ai/meister_trade2.cpp:13-19`). It reroutes "lost" carts home then decides
whether to buy.

A cart is **lost** (`CartIsLost`, `src/ai/meister_general_trade.cpp:12-24`) when
it has no production handler **and** no route handler **and** its route building
resolves to a *different, still-living* owner — then it is sent home
(`SlotReset28 op16`).

The buy decision (`TransporterBuyDecision`, `src/ai/meister_trade2.cpp:22-85`)
branches on the AI's transporter class:

- **Caravan class** (`aiType == 9`): if the fleet is unbalanced and quota
  exceeds cart count, buy `309` or `310` (310 iff `roll_8 >= 4`), else `310`.
- **Non-caravan with carts**: buy `309` (`kCartIdMid`), gated for human-controlled
  classes `6/7` on the `+436 & 4` flag.
- **No carts at all**: fall back to a base cart `308` (`kCartIdBase`).

Every buy passes affordability (`2*price < budget`) and an RNG gate
(`RandomModulo(750) < 2`, `kCartBuyRollHit = 2`, lines 30-38). Cart ids
`308/309/310` map to base/better/best tiers (`src/ai/meister_trade2.h:33-37`).

### 7.2 Tavern refill (`RefillTavernStock`, 0x45f0a0)
`src/ai/meister_trade2.cpp:88-92`: a slot whose fill level is below
`40 + RandomModulo(0x20)` is topped up by `100 - fillLevel`
(`kTavernRefillBase = 40`, `kTavernRefillTarget = 100`).

### 7.3 City wealth top-up (`BalanceCityGoods`, 0x4c763c)
`src/ai/meister_trade2.cpp:95-136`. This is the engine **injecting money into
NPCs so they can keep buying** — the macro-economic feedback that keeps demand
alive:
```cpp
floor = 32000 + 8000 * difficulty;                 // CityWealthFloor
```
A class-4 person always qualifies; a class-2 person qualifies on a staggered
1-in-4 schedule (`(tick % 4) == (personId & 3)`); a class-5 person **stops the
scan** (lines 100-120). Each qualifying person below the floor is granted the
shortfall `floor - held`. Constants `kWealthFloorBase = 32000`,
`kWealthFloorPerDifficulty = 8000` (`src/ai/meister_trade2.h:111-112`).

---

## 8. Building new premises — `ProcessBuildingNeeds` (0x4c7774)

The "what should the AI build next" pass. Decision cores in
`src/ai/building_needs.cpp`:

### Build probability (`BuildProbability`, lines 13-23)
```cpp
N = (gameTick >> 2) + 1;
p = N / (diffScale[difficulty] + N);     // diffScale = {4, 3, 2.5, 1.5, 1.0}
```
`kBuildDiffScale` (`flt_4C5140`) makes higher difficulties build *faster* (the
denominator shrinks), and `p` rises over time as `N` grows. This is the global
gate on whether any construction happens this turn.

### Candidate / force masks (`BuildCandidateMask`, lines 26-46)
Per category in the phase's set:
```cpp
ratio = (total > 0) ? owned/total : 0;
candidate if (owned < 3 || ratio >= 0.5);    // kBuildRatioThreshold
force     if (owned == 0);                    // zero-count -> must build
```

### Category pick (`BuildPickCategory`, lines 49-71)
```cpp
if (forceMask != 0) return forceMask;        // zero-count categories win outright
if (candidateMask == 0) return 0;
if (roll_float > p) return 0;                 // probability gate
// else rotate from RandomModulo(k) to a set candidate bit.
```

The **tally sweep** (`BuildTallySweep`, `src/ai/meister_buildtasks.cpp:32-61`)
counts the player's buildings by AI type (`0..22`) into city-wide `totals` and
`owned` counts, splitting type-7 (guild) into leaders vs members. The
phase→type-set mapping is fixed:
`phase 2 = {18,21,20,14}`, `phase 3 = {8,22}`
(`src/ai/meister_buildtasks.cpp:18-19`). **Phases 0 and 1 are deferred**
(`src/ai/meister_buildtasks.h:23-28`). Funding is `2 * worth` via `Cmd15`
(`src/ai/meister_buildtasks.cpp:99-104`).

---

## 9. Per-turn building supervision — `RunBuildingTasks` (0x4c930c)

`src/ai/meister_supervision.cpp:299-336` dispatches six sub-tasks in a fixed
order (`RunBuildingTasksOrder`, `src/ai/building_needs.cpp:74-83`):
`RequestBuildingCmd43 → RequestCmd134 → AssignWorkersToBuilding (deferred) →
ClearDarkCorner → SuperviseStammtisch → UpdateBuildingHealthState`, plus the
`FlagIdleStaff` pass.

Economically relevant pieces:

- **`FlagIdleStaff`** (`0x45df7c`, lines 159-207) — flags employed staff that are
  idle (no action object, gauges `gaugeA`/`gaugeB` below `168.0` =
  `dbl_619970`). A reprieve roll (`rng_mod(128) < 32`) can abort; fresh staff
  (gauge `< 252.0` = `dbl_619968`) get a skip. Flagged slots get bit `0x10` so
  they can be reassigned to productive work.
- **`CountStaffByType`** (`0x45d3ec`, lines 249-265) — recursively counts staff,
  classifying trades `23`/`37` as **masters** and everything else as **others**
  (feeds hire/train caps).
- **`UpdateBuildingHealthState`** (`0x4c9104`, lines 270-290) — for trade-6/7
  employees, sends a low-health (`5297`) or critical (`5296`) quickjump message
  when `curHealth/maxHealth < 0.5` (`dbl_61E884`). Unhealthy workers can't
  produce, so this drives intervention.

---

## 10. The demand side — character needs (`needs.cpp`)

This is **the engine that creates demand**: NPCs accrue *needs*, consume stock
to satisfy them, and thereby draw down goods the AI must restock. Like the AI
side, needs emit commands (a delta packet + a `Building_AdjustStockAndNotify`
stock change) through a `NeedsCommandHook` (`src/ai/needs.h:43-49`).

Each character record holds a packed **32-bit need word** at `+44`
(`NeedAgent.needWord`, `src/ai/needs.h:33-37`).

### 10.1 Need decay (`ApplyRandomDecayField`, 0x58adc0)
`src/ai/needs.cpp:14-40`:
```cpp
if ((needWord & 0xF) != 0) return 0;              // low nibble already set
mag = word_64773C ? RandNext() % 16 : 0;          // kDecayBound = 16
if (!mag) return 0;
needWord = (needWord & ~0xF) | (mag & 0xF);       // store magnitude in low nibble
amount   = (int)(mag * 40.0f);                     // kDecayScale = 40.0
hook->EmitNeedDelta(...); hook->AdjustStock(type, -amount);   // CONSUME stock
```
So each decay tick **removes `mag*40` units of stock** (negative `AdjustStock`)
from the building, where `mag ∈ [1,15]`. Decay params are `kDecayScale = 40.0`
(`unk_647738`) and `kDecayBound = 16` (`word_64773C`) — `src/ai/types.h:99-100`.

### 10.2 Random need selection
A family of seeded-random pickers builds an N-slot eligibility array (N = 4 or 8)
from the need word's bit groups, picks a start slot `RandNext() % N`, and scans
forward for the first eligible slot (`PickEligibleSlot`,
`src/ai/needs.cpp:46-58`). Two kinds:

- **"Clear" pickers** (`PickRandomFlagFromFourA/B`, `...FromEight`,
  `src/ai/needs.cpp:71-145`) — a *satisfied* need is selected and its bit group
  is cleared. Slot→need-id maps are fixed, e.g. FromFourA → `{2,3,4,5}` over
  groups `0xF0, 0xF00, 0x3000, 0x1C000`.

- **"Consume + restock" pickers** (`PickRandomNeedAndClearGroup{,B}`,
  `0x58aea8` / `0x58b0cc`, `src/ai/needs.cpp:222-270`) — these *raise* a need and
  consume stock. They pick a slot whose need bits are **clear** (a need to newly
  raise), look up the per-need restock row, draw a fresh magnitude, write it into
  the need word, and consume `mag * scale` stock.

The per-need restock table (`unk_647728`, recovered byte-for-byte at
`src/ai/needs.cpp:160-171`) maps need-id → `{scale, cap}`:

| need-id | scale | cap | | need-id | scale | cap |
|--------:|------:|----:|-|--------:|------:|----:|
| 0 | 0.0 | 1 | | 5 | 0.0 | 8 |
| 1 | 40.0 | 16 | | 6 | 8.0 | 8 |
| 2 | 15.0 | 16 | | 7 | 5.0 | 8 |
| 3 | 20.0 | 16 | | 8 | 3.0 | 4 |
| 4 | 10.0 | 4 | | 9 | 12.0 | 16 |

The magnitude bound is `(u16)(int)(cap * 0.5)`, then `mag = RandNext() % bound`,
and the **stock consumed = `(int)(mag * scale)`** (`src/ai/needs.cpp:195-206`).
Each successful "consume + restock" call consumes exactly **two** RNG draws (slot
+ magnitude); an early bail consumes one. This is the precise quantity of demand
each need event puts on the market — e.g. a need-id 1 event removes up to
`7 * 40 = 280` units (cap 16 → bound 8 → mag ≤ 7).

> Note: the eligibility seed tables (`dword_5830E0`/`5830F4`) are all-zero in the
> shipped data, so eligibility reduces to the need bits alone
> (`src/ai/needs.h:96-99`, `src/ai/types.h:83-91`).

---

## 11. The desire model — what NPCs want to buy (`desire_table.cpp`)

Needs decay drives *consumption*; **desires** drive *purchasing preference*. The
14 desire/attribute names map to fixed indices (`LookupAttributeIndex`,
`0x4794e4`, `src/ai/desire_table.cpp:55-63`):

```
APS=0 UNVERSEHRTHEIT=1 WOHNUNG=2 GELD=3 BERUF=4 VERGNUEGEN=5 ANSEHEN=6
AMT=7 BILDUNG=8 RECHTSCHAFFENHEIT=9 GEMEINHEIT=10 SICHERHEIT=11
FORTPFLANZUNG=12 TRAEGHEIT=13
```
(Geld = money, Vergnuegen = pleasure, Ansehen = reputation, Wohnung = housing,
Sicherheit = security, etc.)

### Buy-plan weighting (`ComputeWeights`, 0x47936c)
`src/ai/desire_table.cpp:107-167`. Given a buy-plan record's behavior-category
id, it looks up the **23-row goods table** (`dword_6496A9`, recovered byte-for-
byte at `src/ai/desire_table.cpp:78-102`), copies that row's non-zero good-ids
into the plan, then for each good computes a **price ratio**:

```cpp
cachedPrice = (int)CachedMarketPrice(good);   // LookupCachedMarketPrice
basePrice   = (int)BaseMarketPrice(good);     // ComputeMarketPrice(good, 100)
ratio       = cachedPrice / basePrice;
sum += ratio;
track maxRatioSlot / minRatioSlot;
avgRatio = sum / goodCount;
```

The result gives the AI, per behavior category, the set of goods that satisfy a
desire and how *over- or under-priced* each currently is (`ratio > 1` = above
base price). The `maxRatioSlot`/`minRatioSlot` identify the most expensive and
cheapest goods in the basket — the basis for picking *what to buy* to satisfy a
desire economically. The category→goods mapping ties an NPC's desire profile to
concrete market goods (e.g. each `0x29`/`0x2a`/`0x2b` row escalates the basket
of goods for one desire tier).

> The goods-table good-ids are recovered as raw values (`0x155`, `0x1d4`, …);
> their human-readable good names are **not** resolved in these files.

---

## 12. Closing the loop — how AI behavior and demand meet the market

Putting the pieces together, the economic loop is:

1. **NPCs generate demand.** Need decay (§10.1) and "consume + restock" pickers
   (§10.2) draw stock out of buildings via negative `AdjustStock`; desires (§11)
   shape *which goods* an NPC prefers and at what price. `BalanceCityGoods` (§7.3)
   keeps NPCs solvent so they can keep buying.

2. **The Meister sees the drawdown.** `EvaluateStockNeeds` (§4) computes
   per-workslot deficits; `AssignWorkstations` (§5.1) emits `Notify20` on
   zero-stock items and scores production by margin (§5.2).

3. **The Meister restocks and produces.** `GatherRequiredItems` (§5.4) nets
   shortfalls and buys inputs *within budget*; `ReserveWorkstationItems` (§5.3)
   reserves the chain (free for same-faction, priced cross-faction);
   `ProcessPlayerTurn` Phase 2/3 (§2) stocks workers and books production worth.

4. **The Meister sells surplus.** `TradeManageStorage` (§6) dumps overstock,
   sells on good margins, and force-sells in cash emergencies — feeding finished
   goods back to the market the NPCs buy from.

5. **The Meister expands.** Staffing (§3), transporters (§7.1), and construction
   (§8) scale capacity as `p = N/(diffScale+N)` rises over the game.

Prices are the shared signal throughout: `ComputeMarketPrice` (offer) vs
`LookupCachedMarketPrice` (cost basis) appear in profit-margin sells (§6),
workstation scoring (§5.2), and desire weighting (§11) — so an NPC's price-ratio
desire and the AI's sell/buy decisions are reading the *same* market state, which
is what closes the loop.

---

## Appendix — recovered economic constants

| Constant | Value | Where |
|----------|-------|-------|
| Production scale tier1/2/3 | `3.906e-06` / `7.8125e-06` / `1.5625e-05` | `meister_economy.cpp:36-38` |
| Production ratio scale / bias | `0.5` / `0.25` | `meister_economy.cpp:39-40` |
| Train cost / min budget | `12800` / `38400` | `meister_economy.h:138-139` |
| Profit-sell slope / bias | `1.25` / `0.25` | `meister_storage.h:52-53` |
| Emergency funds / held / target | `3200` / `8000` / `32000` | `meister_storage.h:54-56` |
| Overstock min | `5` | `meister_storage.h:57` |
| Chained-input discount | `0.9` | `meister_workstation.h:139` |
| City wealth floor | `32000 + 8000*difficulty` | `meister_trade2.h:111-112` |
| Cart buy RNG gate | `RandomModulo(750) < 2` | `meister_trade2.h:36-37` |
| Tavern refill base / target | `40` / `100` | `meister_trade2.h:94-95` |
| Build difficulty scale | `{4, 3, 2.5, 1.5, 1.0}` | `building_needs.cpp:10` |
| Build ratio threshold | `0.5` | `building_needs.h:36` |
| Need decay scale / bound | `40.0` / `16` | `types.h:99-100` |
| Banker tax | `16000 * multiplier` | `meisterai.cpp:79-81` |
| Idle-staff gauge / fresh-staff gates | `168.0` / `252.0` | `meister_supervision.cpp:11-12` |
| Building-health critical ratio | `0.5` | `meister_supervision.cpp:13` |
