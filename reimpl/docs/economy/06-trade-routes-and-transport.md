# Trade, Trade Routes & Transport

This document describes the inter-city trade and transport subsystem of the
reimplementation: the player buy/sell trade dialogs, the trade-route /
caravan-cart panel and its route-assignment math, the caravan cargo grids and
their valuation/load math, the transport cost model, and the AI fleet-management
decisions that drive it.

Everything below is grounded in the recovered C++ ports of the original
`gilde.exe` functions (VIBE_* names and absolute addresses are preserved in the
source as comments). Where a behaviour is not modeled in the code, it is flagged
explicitly.

## Source files

Primary (trade / transport core):

- `src/world/trade_player.cpp`, `src/world/trade_player.h` — player money/coord
  rules, the BUY resolution core (`VIBE_WineCellar_ShowBuyDialog`), the Einkauf
  buy-contact router, currency-rate model.
- `src/world/trade_route.cpp`, `src/world/trade_route.h` — trade-route panel FSM,
  the cart "handler" record layout, `RouteAssign` / `RouteResetCart` /
  `RouteFindOwnerChain`, cart-cost formula.
- `src/world/tradetransport.cpp`, `src/world/tradetransport.h` — cart-cost +
  cargo-valuation rules core (`ComputeCartCost`, `ComputeCargoValue`,
  `AssignRoute`).
- `src/world/caravan_cargo.cpp`, `src/world/caravan_cargo.h` — caravan cargo
  slot-grid model, faithful cargo valuation, `LoadFromStorage` load math.

AI decision-making (noted here; deep AI behaviour belongs to the AI doc):

- `src/ai/meister_general_trade.cpp`, `src/ai/meister_general_trade.h` —
  transporter-fleet orchestration (`CollectTransporters`, `CartIsLost`).
- `src/ai/meister_trade2.cpp`, `src/ai/meister_trade2.h` —
  `TransporterBuyDecision`, `BalanceCityGoods`, tavern refill.
- `src/ai/meister_trade.cpp`, `src/ai/meister_trade.h` — stock capacity/deficit,
  stock sort.

Supporting:

- `src/world/amt.h` — the recovered transport tuning constants
  (`kCartRateMode*`, `kCartCostBase`, `kCartValueFloor/Ceil`) live here.
- `src/world/exchange.cpp`, `src/world/exchange.h` — the courier fee rule
  (`ExchangeCourier`), invoked by the trade-route panel's `Courier` button.

---

## 1. Player trading mechanics (`trade_player`)

The player-facing trade dialogs (wine cellar / market stall / contor "Einkauf"
contacts) are split across modules. `trade_player` owns the money/coordinate
primitives and the **BUY** resolution core; the SELL side lives in `sim/trade_sell`
(covered in another document) and the bank/exchange legs in `world/exchange`.

### 1.1 Currency-rate model

Foreign cities have a per-city currency, and prices are scaled by a per-currency
rate. In the live image the rate is read as
(`src/world/trade_player.h:43-46`):

```
rate = dword_649A88[ dword_13CD6F2[189*city] >> 16 ]
```

i.e. the per-city currency object id (city record `+82`, high word) indexes a
per-currency rate table `dword_649A88[]`. The port surfaces both as settable
tables and resolves them in `TradeCityRate` (`src/world/trade_player.cpp:20-29`):

```cpp
i32 TradeCityRate(u8 city) {
    i32 currencyId = 0;
    if (city < g_cityCurrency.size())
        currencyId = g_cityCurrency[city];
    if (currencyId >= 0 && currencyId < g_rateByCurrency.size()) {
        i32 r = g_rateByCurrency[currencyId];
        return r != 0 ? r : 1;
    }
    return 1;  // identity when unset (home-currency case)
}
```

When neither table is set the rate is **1** (identity) — the home-currency case
the dialogs assume.

Two money primitives use this rate:

- `MoneyConvertToDisplayCoord(amount, city)` — `VIBE_Money_ConvertToDisplayCoord`
  `0x58f14c` (`trade_player.cpp:34-38`): divides a foreign amount into the
  displayed home value with round-to-nearest:

  ```cpp
  v = (double)amount / (double)TradeCityRate(city) + 0.5;  // kMoneyRoundBias
  return trunc(v);
  ```

  The `+0.5` bias is `kMoneyRoundBias = 0.5` (`dbl_6268DC`,
  `trade_player.h:39`); the trunc is round-toward-zero
  (`VIBE_Coord_ConvertX`).

- `MoneyDivideByRate(amount, currencyId)` — `VIBE_Money_DivideByRate`
  `0x58f1dc` (`trade_player.cpp:41-48`): integer-truncating
  `amount / rateTable[currencyId]` (falls back to `amount` when the rate is 1/0).

Note the **asymmetry**: the BUY core (below) *multiplies* by the rate, while the
display helper *divides*. The displayed Gulden value of a foreign price is
`value / rate`; the actual charge in foreign currency is `value * rate`.

### 1.2 The BUY resolution core

`WineCellarBuy` ports `VIBE_WineCellar_ShowBuyDialog` `0x519b14`
(`trade_player.cpp:80-102`). It is the generic player BUY rule used by the
wine-cellar / stall buy dialogs. Given the item's raw value, the city, and the
player's held cash:

```cpp
BuyResult r;
r.cost   = in.itemValue * TradeCityRate(in.city);  // MultiplyByRate
r.afford = in.playerCash >= r.cost;
```

The cost is the item value scaled by the **city rate** (so foreign goods cost
more in foreign currency). The affordability gate is a plain `>=`.

On commit the dialog emits exactly one balance-transfer command
(`VIBE_Command_EnqueueCmd15(payer, recipient, amount, currency)`), and which leg
it emits encodes the *delta* of the player's cash, not the cost
(`trade_player.cpp:87-100`):

```cpp
if (r.afford)  // can pay
    EnqueueCmd15(playerAccount, sellerAccount, playerCash - cost, city);
else           // overdraft path
    EnqueueCmd15(sellerAccount, playerAccount, cost - playerCash, city);
```

Both legs encode the *same net effect* (the player ends up paying `cost`); the
original always passes the post-trade delta of the player's cash so the lockstep
network reconciles to the new balance. `currency` is the city byte.

### 1.3 The Einkauf buy-contact router

`TradeRegisterEinkaufContact` ports `VIBE_Trade_RegisterEinkaufContact`
`0x50c5d4` (`trade_player.cpp:105-128`). This wires the contor "Einkauf"
(purchasing) contacts that let the player import goods from a foreign city. It is
**gated on being in a foreign city** (`trade_player.cpp:109-110`):

```cpp
if (in.homeCity == in.currentCity)   // word_63CC5C == *(obj+39)
    return r;                        // no buy-contact at home
```

It then probes for one of five contact names in a fixed order and matches the
first present one (`trade_player.cpp:114-119`), mapping each to a good-class:

| Order | `EinkaufContact` | Contact object name      | Good class |
|-------|------------------|--------------------------|------------|
| 0     | `General`        | `contact_EINKAUF`        | general    |
| 1     | `Metal`          | `contact_EINKAUF_METALL` | metal      |
| 2     | `Scripts`        | `contact_EINKAUF_SKRIPTE`| scripts    |
| 3     | `Wood`           | `contact_EINKAUF_HOLZ`   | wood       |
| 4     | `Stone`          | `contact_EINKAUF_STEIN`  | stone      |

If the matched contact is the clicked widget, it opens the trade panel in **mode
1** (the "buy"/import variant) via the optional `openHook`
(`trade_player.cpp:122-126`).

---

## 2. Trade routes (`trade_route`)

The trade-route system lets the player (and AI) load goods onto transport carts
and assign them a destination building; the cart then carries the cargo to the
target over a fixed travel time. The whole flow lives behind the
**trade-transport panel** state machine.

### 2.1 The cart "handler" record

A cart is an entry in the engine's He (handler/entity) table. Only the
route-relevant fields are recovered (`trade_route.h:42-58`):

```cpp
struct TradeCartRecord {       // byte offsets as the original addresses them
    u8  pad0[40];
    u8  transportMode;   // +40  None/Slow/Med/Fast (the cost-mode byte)
    u8  pad41[41];
    u64 arriveTimeLo;    // +82  arrival-time snapshot (game clock day/hour/min)
    u32 arriveTimeMid;   // +90
    u16 arriveTimeHi;    // +94
    u8  pad60[16];
    i32 routeSlot;       // +112 route slot marker; -1 == cleared/no route
};
```

Carts are queried out of the He table by filter:
`VIBE_He_FindFirstHandlerByFilter(2,3,goodId,0,17)` (carts carrying a given good)
or `(1,0,15)` (route handlers). The `+40` transport-mode byte and the
`arriveTime*` snapshot are the load-bearing fields for routing.

### 2.2 Route definition & assignment — `RouteAssign`

`RouteAssign` ports `VIBE_TradeTransport_AssignRoute` `0x53f404`
(`trade_route.cpp:67-96`; mirrored in `tradetransport.cpp:89-103`). Assigning a
route does three things:

1. **Clear pending routes** for every cart handler matching the route good:
   stamp `arriveTime = current clock` and set `routeSlot = -1`
   (`trade_route.cpp:58-59` comment / engine-side walk).
2. **Emit the route packet** — snapshot the clock, advance it **+1 game day**
   (`VIBE_GameTime_Advance(.., 0, 1, 0)`), which becomes the cart's arrival time,
   and emit the route-assign command (`RouteCmd::RouteAssign`,
   `trade_route.cpp:72-78`).
3. **Charge the cart cost** — only when the cart has a transport mode
   (`+40 != 0`): compute the cost from the cargo value and charge it from the
   treasury sink (`payer = -1`) to the owner account
   (`trade_route.cpp:80-94`):

   ```cpp
   if (mode == RouteMode::None) { r.cost = 0; return r; }
   r.cost = RouteComputeCartCost(cargoValue, mode);
   if (commit && r.cost != 0)
       Emit(ChargeCost, payer=-1, recipient=ownerAccount, amount=r.cost);
   ```

The result is `{ cost, travelDays = 1, charged }`. **Travel time is a fixed one
game day, independent of distance, cargo, or mode** (`kRouteTravelDays = 1`,
`trade_route.h:72`). There is no per-distance travel-time model.

### 2.3 Route reset — `RouteResetCart`

`RouteResetCart` ports `VIBE_TradeTransport_ResetCartHandler` `0x53f5c0`
(`trade_route.cpp:102-111`). It finds the cart handler by id, re-stamps its
arrival-time to the current clock, and emits a reset packet
(`RouteCmd::ResetCart`, `payer = -1`). It is invoked before each re-assignment in
the panel's `AssignCart` path.

### 2.4 Route-target reachability — `RouteFindOwnerChain`

`RouteFindOwnerChain` ports `VIBE_TradeTransport_FindOwnerChain` `0x53f610`
(`trade_route.cpp:118-129`). This is the predicate that decides whether a chosen
target building is a valid route destination. A target is reachable when:

- it is within a spatial tolerance of the source (the original uses a **1350**
  distance tolerance and a not-occluded check), OR
- the source building id appears anywhere on the target's owner chain.

```cpp
bool RouteFindOwnerChain(i32 sourceId, const vector<i32>& chain,
                         bool targetWithinTolerance) {
    if (targetWithinTolerance) return true;       // within ~1350 & not occluded
    for (i32 nodeId : chain) {
        if (nodeId == -1) break;                  // chain terminator
        if (nodeId == sourceId) return true;      // source on the chain
    }
    return false;
}
```

This gates the `AssignCart` transition (an unreachable / self target produces no
state change — see below).

### 2.5 The trade-transport panel FSM — `RoutePanelStep`

`RoutePanelStep` is the reduced state-machine skeleton of
`VIBE_TradeTransport_PanelDispatcher` `0x54014c` (`trade_route.cpp:161-215`). The
deeply GUI-coupled widget layout / drag / rendering body is deferred; only the
route-management transitions are recovered.

**Open modes** (`RouteDecodeOpen`, `trade_route.cpp:134-147`): a mode byte
decodes into one of three source modes plus a route-edit flag:

| `PanelMode` | byte bits      | source object        |
|-------------|----------------|----------------------|
| `Market`   | bit0 set       | local market (own production), id 255 |
| `Import`   | `==2`          | contor import, id 475 |
| `Export`   | `==4`          | contor export, id 476 |

Bit-3 (`& 8`) of the byte selects the **route-edit** variant (binds the active
route context `dword_63174C`).

**Per-frame button dispatch** — the transitions (`trade_route.cpp:165-214`):

| `PanelButton` | Action |
|---------------|--------|
| `Cancel`      | close panel → `Closed`. |
| `Confirm`     | walk the goods slots; `qty>0` → player buys (`GoodBuy`, `QueueRequest17`), `qty<0` → player gives (`GoodGive`, `QueueRequest20`) → `GoodsAdded`. Empty / zero-qty slots skipped. |
| `AssignCart`  | if `routeTargetValid` (FindOwnerChain) and non-self: for each loaded cart `RouteResetCart` then `RouteAssign` → `CartAssigned`. **Invalid target = no transition.** |
| `AddGoods`    | buy a transport cart + repopulate → `GoodsAdded`. |
| `SellCarts`   | confirm-sell selected carts → `CartsSold`. |
| `Courier`     | compute courier fee + enqueue (handled by the exchange module — see §6); here a no-op staying `Open`. |

The `Confirm` per-good emit (`trade_route.cpp:169-191`):

```cpp
for (const GoodLine& g : step.goods) {
    if (g.goodId == -1 || g.quantity == 0) continue;
    if (g.quantity > 0)  // QueueRequest17(-1, account, qty, good, cur, 0)
        Emit(GoodBuy,  payer=-1, recipient=g.account, amount=g.quantity, good);
    else                 // QueueRequest20(account, good)
        Emit(GoodGive, payer=g.account,             amount=-g.quantity, good);
}
```

The `AssignCart` per-cart loop (`trade_route.cpp:193-202`):

```cpp
if (!step.routeTargetValid) return cur;   // unreachable/self: no-op
for (i32 value : step.cargoValues) {
    RouteResetCart(step.cart);
    RouteAssign(value, step.mode, step.ownerAccount, step.cart, /*commit=*/true);
}
return PanelState::CartAssigned;
```

---

## 3. Transport cost model — `ComputeCartCost`

This is the central transport-pricing formula, recovered byte-exact in two
identical copies: `RouteComputeCartCost` (`trade_route.cpp:30-55`) and
`TradeTransportComputeCartCost` (`tradetransport.cpp:26-52`), both porting
`VIBE_TradeTransport_ComputeCartCost` `0x592220`.

### 3.1 Transport modes

```cpp
enum class TransportMode { None = 0, Slow = 1, Medium = 2, Fast = 3 };
```

(`tradetransport.h:22`; `RouteMode` is the same enum, `trade_route.h:62`.)

### 3.2 The formula

```cpp
i32 ComputeCartCost(i32 goodValue, mode) {
    i32 v = goodValue;
    if ((double)goodValue > 0.0) {        // clamp ONLY when strictly positive
        if (goodValue < 32000) v = 32000;        // kCartValueFloor
        if (v > 256000)        v = 256000;       // kCartValueCeil
    }
    i32 fee;
    switch (mode) {
        case Slow:   fee = trunc(v * 0.05); break;   // kCartRateMode1
        case Medium: fee = trunc(v * 0.10); break;   // kCartRateMode2
        case Fast:   fee = trunc(v * 0.15); break;   // kCartRateMode3
        default:     fee = 0;               break;   // None / out-of-range
    }
    return trunc((double)fee + 0.5);                 // kCartCostBase
}
```

Recovered constants (`src/world/amt.h:44-49`, also `trade_route.h:66-71`):

| Constant | Value | Image symbol |
|----------|-------|--------------|
| `kCartRateMode1` (Slow)   | `0.05f` | `flt_626A7C` (0x3D4CCCCD) |
| `kCartRateMode2` (Medium) | `0.10f` | `flt_626A78` (0x3DCCCCCD) |
| `kCartRateMode3` (Fast)   | `0.15f` | `flt_626A74` (0x3E19999A) |
| `kCartCostBase`           | `0.5`   | `dbl_626A84` (0x3FE0000000000000) |
| `kCartValueFloor`         | `32000` | clamp lower bound |
| `kCartValueCeil`          | `256000`| clamp upper bound |

**Behavioural notes:**

- The clamp `[32000, 256000]` is applied **only when `goodValue > 0`** (the
  original guards it with `(double)a1 > 0.0`). A non-positive cargo value skips
  the clamp and (with mode ≠ None) yields `fee = trunc(v * rate)` directly.
- Cost is **proportional to cargo value, not distance.** A faster mode costs 3×
  a slow one (`0.15` vs `0.05`), but takes the **same one game day** to arrive.
  Speed in this model buys nothing in travel time — see §5.
- For mode `None` the per-good fee is 0 and the result is `trunc(0 + 0.5) == 0`.
- Worked examples (Medium, rate 0.10):
  - `goodValue = 20000` → clamp to 32000 → `trunc(3200) = 3200` → `trunc(3200.5) = 3200`.
  - `goodValue = 100000` → `trunc(10000) = 10000` → `10000`.
  - `goodValue = 500000` → clamp to 256000 → `trunc(25600) = 25600`.

So the effective cost band per Medium route is **3200 .. 25600**.

The cost is charged from the city/treasury sink (`payer = -1`) to the cart
owner's account (`RouteAssign` step 3; `tradetransport.cpp:101`
`AmtCommitTransfer(-1, ownerAccount, cost, 0)`).

---

## 4. Cargo valuation & caravan grids (`caravan_cargo`, `tradetransport`)

### 4.1 Cargo grids

The transport panel maintains two parallel cargo grids (`caravan_cargo.h:42-59`):
an **8-slot sell grid** and a **16-slot buy grid**. The originals address them as
families of parallel arrays striding by a dword stride of 14 (slot `k` at index
`14*k`). The port flattens each grid into a `std::vector<CaravanSlot>`:

```cpp
struct CaravanSlot {
    i32 goodIdPacked = 0;  // goodId == goodIdPacked >> 16
    i32 objectId     = -1; // -1 == empty slot (skipped)
    i32 storageSlot  = -1; // <0 == no committed destination
    i32 dataPtr      = 0;  // VIBE_Object_GetDataPtr result; doubles as on-hand qty
    i32 goodId() const { return goodIdPacked >> 16; }
};
```

`CaravanInitSlotTables` (`VIBE_TradePanel_InitSlotTables` `0x50854c`,
`caravan_cargo.cpp:28-46`) resets both grids: packed dword `-1`, object id `-1`,
storage slot `-1`, data ptr `0`.

### 4.2 Cargo valuation — `ComputeCargoValue`

`CaravanComputeCargoValue` (`caravan_cargo.cpp:49-94`) is the faithful two-array
port of `VIBE_TradeTransport_ComputeCargoValue` `0x53ff3c`;
`TradeTransportComputeCargoValue` (`tradetransport.cpp:63-83`) is the flattened
equivalent. For each non-empty slot with non-zero `dataPtr` (on-hand quantity):

```cpp
ctx = sourceKind71 ? sourceCtx101 : 0;     // byte_6477A1 default (=0)
price = LookupCachedMarketPrice(goodId, ctx);          // 0x58f6b8
unit  = ownerIsMarket ? price * 1.1            // dbl_623F30 (market premium)
                      : price * priceMul;      // object+73 per-cart scalar
if (mode == 2 || mode == 4)                    // "sell at remote contor"
    unit = LookupCachedMarketPrice(goodId, sourceCtx101);  // re-lookup, NO factor
value += dataPtr * unit;
```

Key facts:

- **Market premium**: when the owning building is a market (`ownerKind == 10`)
  the unit price gets a ×1.1 premium (`kCaravanMarketPriceFactor = 1.1`,
  `dbl_623F30`, `caravan_cargo.h:71`). Otherwise it is scaled by the per-cart
  `priceMul` from `object+73`.
- **Remote-contor sell modes (2 / 4)**: the unit price is re-looked-up under a
  different context byte (`sourceCtx101`, the cart's `+101` sell-context) with
  **no** market/`priceMul` factor. This is the mechanism by which goods are
  valued at the *destination's* prices rather than the source's — i.e. the
  closest thing to an inter-city price model (see §7).
- The original stores the **negated** total (`dword_63170C = -v13`, a
  cost/credit); the port returns the positive total and lets the caller negate.

> Caveat: in the flattened `tradetransport.cpp` copy, `sellAtContor` is set only
> for `mode == Medium` (mode 2); mode 4 is noted as "not modeled here"
> (`tradetransport.cpp:68`). The faithful `caravan_cargo.cpp` copy handles both
> `mode == 2 || mode == 4` (`caravan_cargo.cpp:69, 86`). Prefer the
> `caravan_cargo` copy as the reference.

### 4.3 Load math — `LoadFromStorage`

`CaravanLoadFromStorage` ports `VIBE_TradeTransport_LoadFromStorage` `0x53f6bc`
(`caravan_cargo.cpp:96-152`). For each cargo slot (sell grid then buy grid) it
decides whether to move cargo and at what unit price, producing a
`CaravanLoadLine { goodId, quantity, toSlot, unitPrice }`.

The per-slot decision (`caravan_cargo.cpp:104-138`):

```cpp
if (objectId == -1)        continue;   // empty slot
if (dataPtr == 0)          continue;   // no object data
amount = free[k];                      // capacity (free space / carry capacity)
if (amount <= 0)           continue;   // no room
if (storageSlot < 0)       continue;   // no committed destination
unit = applyPricing ? <same pricing as §4.2> : 0.0;
emit line { goodId, quantity = amount, toSlot = storageSlot, unit };
```

`free[k]` is the **carry capacity / free space** for that slot, supplied by the
already-translated inventory-capacity leaves (`sim/inventory_capacity.cpp`):

```
free = priceContext ? ComputeCarryCapacity(carrier, good, data)   // load path
                    : ComputeFreeSpaceForItem(dest, good, data)    // unload path
```

(`caravan_cargo.h:118-124`). The quantity moved per slot is exactly this
capacity value. **Cargo capacity is therefore not a single fixed cart number in
this module** — it is the free space / carry capacity computed by the inventory
leaves per good and per carrier/destination, injected here.

---

## 5. Travel time & risk/loss mechanics

- **Travel time is a fixed one game day** for every route assignment, regardless
  of distance, cargo, or transport mode: `VIBE_GameTime_Advance(.., 0, 1, 0)`
  (`kRouteTravelDays = 1` / `RouteResult.travelDays = 1`,
  `trade_route.h:72`, `tradetransport.h:71`). The arrival time is the clock
  snapshot + 1 day, stamped into the cart record's `arriveTime*` fields.

- **No banditry / en-route loss is modeled.** A grep across the tree for
  bandit/robber/ambush/raid/highwayman terms finds only the
  `RobberCampStandard` / `RobberCampRaid` *location dialog* actions
  (`src/world/location3.{h,cpp}`, action codes 98 / 117) — these are
  player-initiated camp interactions, **not** a probabilistic loss applied to
  caravans on a route. There is no transport-loss roll, no per-route risk
  parameter, and no cargo-shrinkage on arrival anywhere in the transport code
  path. **(Flag: if the original game had route banditry, it is not present in
  the recovered transport functions.)**

- The only "loss"-like cart mechanic is the AI's **reroute of lost carts** (§6.1)
  — a cart whose route building has been destroyed or captured is sent home. This
  is a navigation/ownership concern, not cargo loss.

---

## 6. AI trade decision-making (summary — defer deep AI to the AI doc)

The MeisterAi drives the transport fleet. Only the rules that bear on
trade/transport economics are summarized here.

### 6.1 Transporter audit & reroute — `CollectTransporters`

`CollectTransporters` (`meister_general_trade.cpp:27-52`, `0x45e71c`) runs the
twice-daily (odd-hour) transporter audit (gate `TransporterAuditRuns`,
`meister_trade2.cpp:13-19`: runs on odd hours when the `+436` high bit is clear).
It walks the faction's carts and:

- **Reroutes lost carts** (`CartIsLost`, `meister_general_trade.cpp:12-24`): a
  cart is lost iff it has neither a production handler (He cat 11) nor a route
  handler (He cat 15) **and** its route building resolves to a still-living owner
  that is a *different faction*. Such carts are sent home (`FleetCmd::Reroute`,
  SlotReset28 op 16). A dead chain (owner 0) or own-faction building is left
  alone.
- **Tallies** total carts (`cartCount`) and top-tier id-310 carts
  (`highCartCount`), then feeds the buy decision.

### 6.2 Cart purchase decision — `TransporterBuyDecision`

`TransporterBuyDecision` (`meister_trade2.cpp:22-85`, `0x45e71c`) decides which
cart (if any) to buy. Cart item ids (`meister_trade2.h:33-35`):

| Id  | Const          | Tier |
|-----|----------------|------|
| 308 | `kCartIdBase`  | base cart (fallback restock) |
| 309 | `kCartIdMid`   | better (mule) |
| 310 | `kCartIdHigh`  | best (wagon/caravan) |

The shared affordability/RNG gate (`tryBuy`, `meister_trade2.cpp:28-39`):

```cpp
if (roll_750 >= kCartBuyRollHit)   return 0;   // RandomModulo(0x2EE) < 2 to fire
i32 p = (i32)(double)price_of(cartId);          // Coord_ConvertX(market price)
if (2 * p >= st.budget)            return 0;    // require 2*price < budget
*cost = p;
return cartId;
```

So a purchase requires `RandomModulo(750) < 2` (`kCartBuyRollMod = 0x2EE`,
`kCartBuyRollHit = 2`) **and** `2 * price < budget`. The price comes from the
market-price lookup (`ComputeMarketPrice(cartId, 100)`), so carts are bought at
the live market price like any other good.

Branch structure (`meister_trade2.cpp:41-84`): caravan-class AIs (`aiType == 9`)
prefer 309/310 up to a transporter `quota`; non-caravan AIs with carts buy 309;
the empty-fleet fallback buys a 308 (the 308 path skips the quota/roll gate but
still respects the price via `tryBuy`'s affordability check). Human-controlled
owner classes (6/7) are additionally gated behind the `+436 & 4` flag.

### 6.3 Stock planning — `meister_trade`

The per-item stock-deficit math (`meister_trade.cpp`, `0x58658c`):

- `StockCapacity(itemTypeCode, level)` (`:17-23`): `5*level+10` for code 477;
  `80` at level 3; else `20*level`.
- `StockDeficit(cap, count, itemTypeCode)` (`:26-38`):
  `used = (u32)(cap*count) >> 2`; `have = count` (minus 1 for codes 42 / 278 /
  475 / 476); `deficit = max(0, have - used)`.
- `SortStockByValue` (`:41-50`): selection sort descending by value (most
  valuable needs first).

These feed the (deferred) purchase/transport planners
(`TradeManageStorage`, `TradeGeneral`, `TradeRemotePurchaseA/B`,
`RefillTavernStock`, `BalanceCityGoods`) — see the AI document for the full
orchestration.

### 6.4 Courier fee — `ExchangeCourier`

The trade-route panel's `Courier` button (and the exchange dialog) charge a
courier fee via `VIBE_Exchange_ShowCourierDialog` `0x51cbdc`
(`exchange.cpp:42-69`). The fee is **3% of the base amount, with a one-home-unit
minimum**:

```cpp
base = PriceByRate(rate, currency);
span = base * 0.03;                     // kCourierFactor (dbl_6220D0)
minA = PriceByRate(1, currency);        // one home unit
fee  = (minA >= span) ? minA : span;    // min-fee clamp
net  = trunc(base - fee);               // amount actually delivered
```

The player is charged `base` and delivered `net = base - fee` (the fee is the
courier's cut). `kCourierFactor = 0.03` (`exchange.h:28`).

---

## 7. Inter-city price arbitrage

There is **no explicit arbitrage engine** (no module computes a buy-here /
sell-there price differential and acts on it). Arbitrage is, however, *implicitly*
modeled through three mechanisms:

1. **Per-city currency rates** (§1.1): the same item value maps to different
   foreign-currency charges via `TradeCityRate`. Buying in a cheap-rate city and
   selling in a dear-rate one yields a currency gain. The BUY core multiplies by
   the rate (`WineCellarBuy`); the SELL side (in `sim/trade_sell`) realizes the
   other leg.

2. **Source-vs-destination valuation in `ComputeCargoValue`** (§4.2): for the
   remote-contor sell modes (2 / 4) cargo is valued at the *destination* contor's
   prices (`LookupCachedMarketPrice(good, sourceCtx101)`), with no market premium
   or per-cart scalar. The price spread between the source market value and the
   destination contor value is the arbitrage margin the transport captures — but
   the code only *values* the cargo; it does not search for the best
   destination. Destination choice is the player's (or the AI's stock planner's).

3. **Market premium ×1.1** (`kMarketPriceFactor`, §4.2): selling into a market
   building yields a 10% premium over the raw cached price, a fixed (non-spatial)
   margin.

The cost side of any such arbitrage is the §3 transport cost (5/10/15% of clamped
cargo value) plus, for courier deliveries, the §6.4 3% courier fee. The recovered
code provides all the *primitives* for inter-city arbitrage (per-city rates,
destination valuation, transport cost) but **no automated optimizer** that picks
profitable routes; route/destination selection is a player or AI-planner
decision, and the deep AI planning (`TradeRemotePurchaseA/B`, etc.) is deferred
to the AI document.

---

## 8. Open questions / unknowns

- **Banditry / en-route cargo loss**: not present in the recovered transport
  functions. Flagged in §5. If the original had it, it is outside the translated
  code (possibly an event/location mechanic — see `world/event_*`).
- **Mode-4 ("sell at remote contor") valuation** is only partially modeled in the
  flattened `tradetransport.cpp` copy (`mode==2` only); the faithful
  `caravan_cargo.cpp` copy handles `2 || 4`. The semantic difference between
  modes 2 and 4 (both "remote contor") is not separately documented in the code.
- **Cargo capacity** is supplied externally by the inventory-capacity leaves
  (`sim/inventory_capacity.cpp`); a single fixed per-cart capacity number is not
  exposed in the transport modules (the cart-tier ids 308/309/310 presumably map
  to different capacities in those leaves — not verified here).
- **Travel time** is hard-coded to 1 game day with no distance dependence; if the
  original scaled it by route length or transport mode, that is not in the
  recovered `AssignRoute`.
- The `PanelDispatcher` widget-layout / drag / rendering body is deferred; only
  the route-management transitions are recovered, so some panel-driven side
  effects (e.g. exact slot repopulation on `AddGoods`) are abstracted.
