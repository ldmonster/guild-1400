# Wave-14 1:1 fidelity audit — world economy / trade / finance (W14-ECON)

IDA MCP DOWN → no live binary diff. This is an in-tree 1:1 audit: inventory every
reconstructed function with its gilde.exe provenance address, verify the source
constants/tables/control-flow match the provenance + progress docs, and confirm a
GOLDEN TEST pins every recovered 1:1 value. Added pins are sourced from the source's
own recovery (never invented). Build/ kept green.

Extends `harden-econ-wave12.md` (the ASAN/UBSAN hardening pass on the same cluster).

## Scope (owned source)
`src/world/`: amt*.{h,cpp}, economy*.{h,cpp}, market_price*.{h,cpp},
market_stall.{h,cpp}, exchange*.{h,cpp}, trade_player.{h,cpp}, trade_route.{h,cpp},
tradetransport.{h,cpp}, trade_recon*.{h,cpp}, production.{h,cpp}, tax.{h,cpp},
treasury.{h,cpp}, bank*.{h,cpp}, player_finance.{h,cpp}, caravan_cargo.{h,cpp},
world_economy*.{h,cpp}. (NOT building_production — sim cluster.)

## Headline result
The cluster is a previously-hardened (wave-12) target and is **comprehensively
golden-pinned**. Every public reconstructed function carries a `gilde.exe 0x…`
provenance address, and every recovered scalar table / price-model formula /
tax-treasury-loan math / production recipe window / amt office-role table / trade-route
cost is asserted by a golden test. **No address-less in-tree function found. No
constant/offset drift found.** One genuine coverage gap was found and filled.

## Change made (1 test addition; no source edits)
- **production weekday work-window selection** — `world_economy_test.cpp` new test
  `WorldProduction.OutputWeekdayWindowSelection` (+5 checks). The prior production
  goldens only exercised weekday 0 ([8,20]); they never drove the table-index path
  `kWorkStartHour[(day%4+4)%4]` / `kWorkEndHour[…]` for weekdays 1/2/3 (windows
  {7,21}/{8,20}/{9,19}). New goldens discriminate each entry through
  `ProductionComputeOutputOverTime` (gilde.exe 0x59064c). Values traced to the
  source's own `integrateWindow` same-day clamp. world_economy_test now 156 checks,
  0 failures. (The raw table BYTES {8,7,8,9}/{20,21,20,19} were already pinned in
  `sim_npc_daily_test.cpp`; this pins the *consumer's* index selection.)

## Build / test status (normal build/, GUILD_BACKEND=OFF)
All owned cluster test targets rebuilt and green:
world_economy 156 / world_economy2 21 / world_economy3 59 / world_economy_quality 22 /
world_market_price 11 / world_market_price_model 19 / world_trade_route 127 /
world_trade_player 111 / bank_treasury 55 / caravan_cargo 67 / economy_tick 42 — 0
failures across all.

## INVENTORY + CONFIDENCE MAP (GOLDEN-PINNED unless noted)

### Price/rate scalar tables (byte values)
| fn | addr | pin |
|---|---|---|
| EconomyLookupRateScalar | 0x579a24 | world_economy: ids 0/4→10/50, ≥5→126, neg→126 |
| MoneyConvertToDisplayCoord | 0x58f14c | world_trade_player: 100/1→100, 100/3+.5→33, neg |
| MoneyDivideByRate | 0x58f1dc | world_trade_player: 100/3→33, /7→14 |
| ExchangePriceByRate | 0x58f1d0 | world_trade_route: table[c]*amt, OOR→0 |
| TradeCityRate | (rate vector) | world_trade_player: rate[0]=1, rate[1]=3 |
| AmtMoneyMultiplyByRate | 0x57b346 (via bank) | app_session_init: 1000@100→1000, @50→500 |

### Market price model formula + constants
| fn | addr | pin |
|---|---|---|
| BuildingComputeMarketPrice | 0x58f3d0 | world_market_price_model (19 checks): cat-23 raw path 4*0.5*v23*2.2; compute path 2.2*(v28*cur*0.01); cache write trunc(v28*2.2/32); flag-3 *1.5; recipe recursion; OOR guards |
| MarketLookupCachedPrice | 0x58f6b8 | world_market_price: per-block key>>16 match, fallback |
Constants pinned by value through goldens: kCurrencyScale 0.01, kFlag3Mult 1.5,
kTypeWeightA/B 28/32, kTwelfth 1/12, kSixtieth 1/60, kHalf 0.5, kPriceGain 2.2,
kCacheScale 1/32, kNoTypeNeed 4.0.

### Economy supply/demand/price-delta model
| fn | addr | pin |
|---|---|---|
| ClassifyProfession | (branch chain) | world_economy: 3→Flat; 7/15/19/>22→Service; else Default |
| EconomyComputeGoodsDemand | 0x578438 | world_economy: per-class slope/bias, city money/goods totals |
| EconomyComputeGoodsSupply | 0x578634 | world_economy: single-index accumulate |
| EconomyComputePriceDeltas | 0x5787d4 | world_economy: normal +20.875 / -0.65; inverted goods 4&16 → -2.0; cap≥div → 0; driftWeight table 300/65535/800 |
Constants (eco::): slopeA/B 2.0/3.0, bias -1.0, class3 1.5, empScale 0.5, deltaTail
-1.0 — all exercised by the delta goldens.

### Economy tick / quality (EMA + law/population model)
| fn | addr | pin |
|---|---|---|
| EconomyTickPriceLevel | 0x579098 | economy_tick |
| CityTickStatsAndBroadcast | 0x57919c | economy_tick |
| EconomyComputePopulationTrend | 0x57a008 | economy_tick: 0.70 growth/clamp goldens (weights 0.65/0.05) |
| EconomyFillLawRangeRatios(/FromTable) | 0x57aa30 | economy_tick: (v-lo)/(hi-lo) golden vectors |
| CityCopyStateStruct | 0x579448 | economy_tick |
| EconomyComputeAverageQuality | 0x579a38 | world_economy_quality: 0.5806… golden |
| EconomyComputeIndustryRatio | 0x57a3c8 | world_economy_quality: 1.0/0.2/-1.0/-0.9 |
| EconomyComputeResidentialRatio | 0x57a474 | world_economy_quality: 0.25/1.0 |
| EconomyLoadDemandSnapshot | 0x57a5dc | world_economy_quality: slot-6 patch, slot-9 return |
| EconomyComputeLawSatisfaction | 0x57a520 | world_economy_quality: 0.625 (0.25/0.75 consts) |
| EconomyComputeWeightedLawScore | 0x57a580 | world_economy_quality |
| EconomyComputeInterpolatedLawScore | 0x57a990 | world_economy_quality |

### Tax / treasury math
| fn | addr | pin |
|---|---|---|
| TaxComputeTradeIncome | 0x57aa88 | world_economy: 10@5000→499, 7@1234→86, clamp 0; INT extremes no-trap |
| TaxCollectTradeIncome | 0x57aa88 | world_economy: ≥6 laws→0; flags 1/2 paths |
| CityTaxHalveOfficeLevel | 0x501245 | bank_treasury: signed /2 toward zero |
| CityTaxRateFor | 0x50123f | bank_treasury: per-cell golden vs seed table |
| ComputeCityTaxRates | 0x5011dc | bank_treasury: full-int return vs stored byte |
| kCityTaxSeedRates[16] | byte table | bank_treasury: all 16 bytes (0x18..0x4B) asserted |
| TaxComputeTradeIncome (end-to-end) | — | turn_economy: city-sweep aggregate golden |

### Bank / loan math
| fn | addr | pin |
|---|---|---|
| LoanGrantCapacity | 0x591990 | world_trade_player: 0.2*w, clamp 64000 |
| LoanOfferBase | 0x591990 | world_trade_player: min(w*factor, prev*0.5) |
| LoanComputeInterest | 0x591990 | world_trade_player: f*0.7 + term/6*(f*1.5-f*0.7); bankType fav adj |
| LoanRelationFactor | (tier table) | world_trade_player: High 0.6 / Mid 0.45 / Default 0.3 (via OfferBase) |
| LoanGenerateOffers | 0x591990 | world_trade_player: seed-1 golden (amts/terms/interests) |
| CreditConfirmLoanRequest | 0x51a100 | world_trade_player: grant cmd + family-debt += amount |
| BankInterestBase / BankApplyLoanStep | 0x57b346 / 0x57b304 | covered via amt loan path |

### Production recipe / work-window
| fn | addr | pin |
|---|---|---|
| ProductionComputeOutputOverTime | 0x59064c | world_economy: same-day/clamp/multi-day/pause; **+weekday selection (new W14)** |
| ProductionComputeDailyHourOutput | 0x590a3c | world_economy: [6,23] window goldens |
| kWorkStartHour/kWorkEndHour[4] | flt_6476FC/64770C | sim_npc_daily: all 8 entries; W14 pins consumer index path |

### AMT office-role / slot tables
| fn | addr | pin |
|---|---|---|
| kAmtRoleTable[5] | funcs_557292 @0x63d584 | amt_recon_office_window: collector/renderer addrs + listIndex 1..5 |
| kAmtOverviewSlotOfficeType[8] | dword_5526b0 | amt_recon_office_window: {00,06,04,05,01,02,03,07} |
| AmtFindOfficeTypeRecord / FindSlot* / FindFreePlacement | 0x56e850.. | world_amt_office, world_amt |
| TriggerOfficeNotice / ResetGuildSlots / HighlightGuildMembers / BuildOfficeInfoText / ComputeOfficeRenderOffset / FindNextActiveBuilding / ComputeBuildingRivalryScore / OpenOfficeWindow | 0x483570/0x480b50/0x48311c/0x483414/0x4834e4/0x57bb50/0x57bc60/0x5546a0 | amt_economy2 |
| ComputeOfficePaymentAmount / ComputeAppointmentPrice / ScanMaxCandidateRank / RollActionDirection / CountOfficeDependents / ComputeGridLabelOffset | 0x482299../0x481f1c../0x481e3e../0x4825c9../0x4822c6../0x5583c0 | world_economy2 |
| OfficePrepareCandidatePage / RatingBarRowMode / AmtFindFirstNonEmptyRole / AmtSelectRoleAtFrameTop / AmtComputeRoleButtonYs | 0x555eb4/0x556ba0/0x556c40/0x556f6d/0x556df1 | amt_recon_office_window |

### Trade-route cost + transport
| fn | addr | pin |
|---|---|---|
| RouteComputeCartCost | 0x592220 | world_trade_route: 0.05/0.1/0.15 rates, floor 32000, ceil 256000, +0.5 base, no-mode→0 |
| RouteAssign / RouteResetCart / RouteFindOwnerChain / RouteDecodeOpen / RoutePanelStep | 0x53f404/0x53f5c0/0x53f610/0x54011c/loop | world_trade_route |
| CaravanComputeCargoValue / CaravanInitSlotTables / Caravan LoadFromStorage | 0x53ff3c/0x50854c/0x53f6bc | caravan_cargo (67 checks: owner-market, sell-at-contor, price-mul, null-skip) |
| TradeTransport thunks (mode2/mode4) | 0x54012c/0x54013c | trade_recon_transport_thunks |
| LayoutDragSlotsVariant/Wide | 0x50b350/0x50c140 | trade_recon_dragslots (incl. wave-12 OOB-bound goldens) |

### Exchange / player-finance
| fn | addr | pin |
|---|---|---|
| ExchangeCourier | 0x51cbdc | world_trade_route: base/fee/net (0.03 factor + min-fee clamp) |
| ExchangeGoodsTrade | 0x51bb4c | world_trade_route: value/fee/accept/reject + 4 commit legs (0.01 spread) |
| ExchangeApplyFees | 0x51ca40 | world_trade_route: buy/sell fee fields 105/109 |
| Person_SumStoredMoney / ComputeTopWealthList / CheckExamFeeAffordable / ShowFineAmount | 0x591600/0x592b50/0x592c18/0x4c4b60 | player_finance |
| MarketStall_RouteContactByType / UpsertTradeEntry / sort | 0x519918/0x49d2e8/0x51b26c | covered via dialog_market / play_slice_market |

## NEEDS-LIVE-MCP queue (decompile targets for the binary diff when MCP returns)
These are behavioral/envelope items the in-tree audit cannot resolve without the live
binary. None are blocking; all are documented as faithful-to-envelope today.

1. **0x57aa88 VIBE_Tax_CollectTradeIncome** — confirm the final `(i32)` cast of an
   out-of-int-range float matches x87 `fistp` saturation (0x80000000). Our build does
   not trap; pinned by TradeIncomeIntExtremesNoTrap. (carried from wave-12)
2. **0x556df1 AmtComputeRoleButtonYs** — `span/(count-1)` & `span%(count-1)` divide by
   zero when count==1 (engine always count≥2). Confirm no extra guard. (wave-12)
3. **0x556d2b AmtFindFirstNonEmptyRole / 0x557970 AmtOverviewFindClickedSlot** —
   unbounded `rowCounts[5*current]` / pre-count `slotObjectIds[0]` read; confirm the
   original has no additional guard (caller-contract). (wave-12)
4. **0x58f3d0 BuildingComputeMarketPrice** — div-by-zero on `rec.divisor==0` in the
   recipe leg reproduces the original FP divide (inf/nan); confirm divisor envelope.
5. **0x579098/0x57919c economy tick** — `g_capDivisor==0` / `hi-lo==0` FP divides;
   confirm engine's first-tick seed envelope.
6. **0x592220 RouteComputeCartCost** — confirm the `default` (mode 0/None) leaves
   `fee=0` (no switch case) → returns trunc(0+0.5)=0, vs an unhandled-mode trap.
7. **0x591990 loan offer loop** — re-verify the per-offer index scaling order
   (offer0 *0.25, offer1 *0.5) and the `rand%30` / `rand%6+2` draw order against the
   decompile (golden currently pinned to our crt::rand stream at seed 1).
8. **0x57b304 BankApplyLoanStep / 0x51a100 CreditConfirmLoanRequest** — confirm the
   foreclosure/dun branches (held<0, marker, 2*base) we modelled as the charge step.

## Internal-consistency check
Verified source constants/offsets/control-flow against provenance comments + the
wave-12 doc + headers. No drift found: every `constexpr` carries its
`flt_/dbl_/dword_` symbol + hex bit-pattern and matches the value the goldens assert
(e.g. kRouteRateSlow 0.05 = 0x3D4CCCCD; kCityTaxSeedRates bytes; kWorkStartHour
{8,7,8,9}). No source edits were required.
