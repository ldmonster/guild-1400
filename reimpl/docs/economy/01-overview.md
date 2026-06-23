# The Guild / Europa 1400 — Economy Documentation

This directory documents the **economy simulation** of the *Guild / Europa 1400* (Die Gilde)
reimplementation, reconstructed entirely from the C++ source in `src/` (which is itself a
faithful re-derivation of the original `gilde.exe`). Every claim in these documents is grounded
in real source: `file:line` references, real function/struct/field names, recovered constants,
and byte-faithful formulas. Where the original binary's behavior is genuinely unclear, the docs
flag it as unknown rather than inventing it.

## How to read these docs

Each economy subsystem lives in its own file. Start here, then jump to whichever subsystem you
need. The numbering is a suggested reading order (foundations first, then the systems that build
on them, then the actors that drive it all).

| # | Document | What it covers |
|---|----------|----------------|
| 01 | [Overview](01-overview.md) | This file — map of the economy and how the pieces connect |
| 02 | [Economy Core & Tick](02-economy-core-and-tick.md) | Data model, the per-day economy tick, quality model, city/world health aggregation |
| 03 | [Goods & Production](03-goods-and-production.md) | Goods catalog, recipes (inputs→outputs), the per-tick production pipeline, quality/quantity formulas |
| 04 | [Market Pricing & Supply/Demand](04-market-pricing-and-supply-demand.md) | Price computation, the supply/demand price walk, market stalls, NPC and player selling |
| 05 | [Buildings & Workshops](05-buildings-and-workshops.md) | Building valuation, construction/upgrade cost, storage capacity, ownership |
| 06 | [Trade Routes & Transport](06-trade-routes-and-transport.md) | Player trading, trade-route automation, cart/caravan cargo and transport cost |
| 07 | [Banking & Finance](07-banking-and-finance.md) | Currency model, loans, interest, player net-worth tracking |
| 08 | [Taxes, Treasury & Public Office](08-taxes-treasury-and-public-office.md) | Tax formulas, city treasury, the "Amt" public-office economy, office wages |
| 09 | [Inventory & Wealth](09-inventory-and-wealth.md) | Inventory data structure, slot capacity, total-wealth accounting |
| 10 | [Personnel & Labor](10-personnel-and-labor.md) | Recruitment cost, personnel roster, wages, payment cadence |
| 11 | [AI Economic Behavior](11-ai-economic-behavior.md) | Meister-AI business management, AI trading, the needs/desire demand model |
| 12 | [Exchange, Statistics & Prosperity](12-exchange-statistics-and-prosperity.md) | The currency/goods exchange desk, economic statistics, prosperity metrics |

## Source layout

Economy code is concentrated in three source trees:

- **`src/world/`** — the macro economy: `economy*`, `market_price*`, `bank*`, `tax`, `treasury`,
  `trade_*`, `tradetransport`, `caravan_cargo`, `production`, `exchange*`, `amt*`, `office*`,
  `statistics*`, `player_finance`, `money_format`.
- **`src/sim/`** — the micro/per-entity economy: `building_production`, `building_stock`,
  `building_storage`, `building_value`, `inventory*`, `trade_sell`, `npc_market`,
  `production_slots`, `recruit*`, `personnel*`.
- **`src/ai/`** — the actors that drive the economy: `meister_economy`, `meister_trade*`,
  `meister_storage`, `meister_workstation`, `needs`, `desire_table`, `building_needs`,
  `aiplayer`.

Static game data is loaded from the original assets under
`europe_guild_1400_original/Resources/gamedata/` and `…/data/` — notably `data/A_Geb.dat`
(building types, 589-byte records) and `data/A_Obj.dat` (object/good types, 65-byte records).
See [Goods & Production](03-goods-and-production.md) and
[Market Pricing](04-market-pricing-and-supply-demand.md) for the on-disk layouts.

## The economy at a glance

The economy is a per-day, lockstep-deterministic simulation. Money is plain signed integer
Gulden moved through a command channel rather than mutated in place, which keeps multiplayer
clients in sync. The loop, roughly:

```
                ┌─────────────────────────────────────────────────────────┐
                │                  PER-DAY ECONOMY TICK                     │
                │            (see 02-economy-core-and-tick.md)              │
                └─────────────────────────────────────────────────────────┘
                                          │
   NEEDS/DESIRES ──────────────┐          │          ┌────────────── PRODUCTION
   (NPCs consume stock,        │          ▼          │   (workshops turn raw
    creating demand)           ▼     ┌─────────┐     ▼    materials into goods)
   11-ai-economic-behavior  ┌─►│ MARKET  │◄─┐   03-goods-and-production
                            │  │ PRICES  │  │
   PLAYER & AI TRADE ───────┘  └─────────┘  └──── BUILDINGS & WORKSHOPS
   (buy low / sell high,        ▲    │            (assets, storage, value)
    move goods between cities)   │    │            05-buildings-and-workshops
   06-trade-routes-and-transport │    │
   04-market-pricing            │    ▼
                          STATISTICS / PROSPERITY
                          (city health feeds back into prices, AI, events)
                          12-exchange-statistics-and-prosperity
```

Cross-cutting flows of money:

- **Income** to a business comes from selling produced goods at market
  ([04](04-market-pricing-and-supply-demand.md)) and from trade arbitrage
  ([06](06-trade-routes-and-transport.md)).
- **Costs** are construction/upgrades ([05](05-buildings-and-workshops.md)), recruitment and
  wages ([10](10-personnel-and-labor.md)), taxes ([08](08-taxes-treasury-and-public-office.md)),
  and loan interest ([07](07-banking-and-finance.md)).
- **Wealth** is tracked as cash plus the market value of owned goods and buildings
  ([09](09-inventory-and-wealth.md)), and is the input to office eligibility, loan limits, and
  prosperity metrics.

## Notable findings & faithful quirks

These documents preserve several behaviors that look like bugs but are deliberate reproductions
of the original binary:

- **Double-counted loop tail.** `EconomyComputeInterpolatedLawScore` returns `sum + term`,
  intentionally double-counting its last term to reproduce an unrolled-loop tail quirk in the
  original. See [02](02-economy-core-and-tick.md).
- **No deposit interest, interest-only loans.** The bank pays no interest on deposits and there
  is no amortization schedule — loan repayment is interest-only with overdraft foreclosure when
  debt exceeds `2× base`. See [07](07-banking-and-finance.md).
- **No banditry / cargo loss.** Despite the medieval setting, there is no en-route cargo-loss
  mechanic in the recovered transport code, and travel time is hard-coded to 1 game day with no
  distance dependence. See [06](06-trade-routes-and-transport.md).
- **No rent/upkeep constant on buildings.** Running costs are driven entirely by the
  wage/tax subsystems, not a per-building maintenance fee. See
  [05](05-buildings-and-workshops.md).
- **Money is stored two ways.** A character's cash exists both as a currency-type stack in the
  scene-tree inventory and as a separate `u16` word on the Person record; total-wealth uses the
  currency-stack path. See [09](09-inventory-and-wealth.md).

## Conventions used in these docs

- **Addresses** like `@0x533188` are offsets into the original `gilde.exe`, preserved in the
  reimplementation as the canonical reference for the reconstructed routine.
- **Formulas** are reproduced byte-faithfully — including float precision (e.g. `0.01f` =
  `0x3C23D70A`) where it matters for determinism.
- **"Amt"**, **"Gesetz"**, **"Einkauf"**, **"Geldleihe/Wechsel"** etc. are the original German
  terms kept in the source: respectively public office/authority, law, purchasing, and the
  money-lending/exchange desk.
- Open questions are collected in each document, usually in a final "Unknowns" section.

---

*Generated from source analysis of the `src/` reimplementation. If a formula or symbol here
disagrees with the code, the code is authoritative — please update the doc.*
