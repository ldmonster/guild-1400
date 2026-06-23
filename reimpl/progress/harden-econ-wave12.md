# Wave-12 hardening — world economy / trade / finance cluster (W12-ECON)

MCP was DOWN: hardening only, no new 1:1 reconstruction. ASAN+UBSAN build of the
cluster's unit test targets, boundary/malformed tests added, memory-safety/UB bugs
fixed. Goldens kept byte-identical; valid-input paths unchanged.

## Scope (owned source)
`src/world/`: amt*.{h,cpp}, economy*.{h,cpp}, market_price*.{h,cpp},
market_stall.{h,cpp}, exchange*.{h,cpp}, trade_player.{h,cpp}, trade_route.{h,cpp},
tradetransport.{h,cpp}, trade_recon*.{h,cpp}, production.{h,cpp}, tax.{h,cpp},
treasury.{h,cpp}, bank*.{h,cpp}, player_finance.{h,cpp}, caravan_cargo.{h,cpp},
world_economy*.{h,cpp}. (NOT building_production — sim, wave-10/11.)

## Build
```
cmake -S . -B build-asan-econ -DCMAKE_BUILD_TYPE=Debug -DGUILD_BACKEND=OFF \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,undefined -fno-sanitize-recover=all -g"
```
22 owned unit test targets built + run clean under ASAN+UBSAN (baseline AND after
fixes). Normal `build/` also green for the touched targets (151/147/19/81/127 checks,
0 failures). build-asan-econ removed at the end.

## Bugs FIXED (genuine reconstruction OOB/UB — the original did not corrupt memory)

### 1. economy.cpp — EconomyLookupRateScalar negative-index OOB
`VIBE_Economy_LookupRateScalar` (gilde.exe 0x579a24). Signature `i8 id`; the guard
was `if (id < 5)`, so a NEGATIVE id passed the test and indexed `g_rateScalar[id]`
(a 5-byte global) before its start.
- UBSAN proof: `index -1 out of bounds for type 'signed char [5]'` at economy.cpp:34.
- Fix: `if (id >= 0 && id < 5)`. Faithful — the engine only passes ids 0..4 (its own
  envelope); negative ids now route to the same `126` the `>=5` path returns. Every
  valid id (0..4) and the `>=5` result stay byte-identical.
- Pinned by: `WorldEconomy.LookupRateScalarNegativeIdNoOOB` (world_economy_test.cpp).

### 2. trade_recon_dragslots.cpp — Block-4 column walk stack-buffer-overflow
`VIBE_TradePanel_LayoutDragSlots{Variant,Wide}` (gilde.exe 0x50b350 / 0x50c140).
The slider-row loop advances the working column with
`do { carry = in.colCarry[col]; ++col; } while (carry < 1);` and then reads
`st.prevCount[col] / st.xOff[col] / st.yOff[col]`. With malformed input where every
`colCarry < 1` (or every `prevCount` tiny so the advance keeps firing), `col` steps
past the fixed `std::array<i32, kDragColumns(=4)>` arrays and reads OOB.
- ASAN proof: `stack-buffer-overflow ... offset 208 overflows this variable 'in'`
  at trade_recon_dragslots.cpp:97 (reading `in.colCarry[4]`).
- Fix: bound the do/while with `&& col < kDragColumns`, and use a clamped `colIdx`
  for the subsequent offset reads. `p.column` still records the raw column counter
  (a returned int, not a memory access) so valid-input output is byte-identical (for
  in-range input `colIdx == col`). The balanced 16-row golden (VariantColumnOffsets)
  is unchanged.
- Pinned by: `TradeReconDragLayout.MalformedAllZeroCarryNoOOB`,
  `.MalformedZeroPrevCountNoOOB`, `.BoundedWalkBalancedColumnsUnchanged`
  (trade_recon_dragslots_test.cpp).

## Boundary / malformed tests ADDED (no bug; pin the existing guards)
- world_market_price_model_test.cpp: `GoodIdOutOfRangeReturnsZero` (negative / ==count
  / far-past goodId -> 0.0 via the `goodId<0 || goodId>=count` guard),
  `BadComponentIndexNoOOB` (recipe component id past the table is re-validated on the
  recursive call). [goods/price-table index out of range]
- world_amt_office_test.cpp: `FindOfficeTypeRecordEmptyTable` (0 records / null ptr),
  `FindOfficeTypeRecordSingleAndTerminator` (terminator stops walk, never reads next
  record), `FreePlacementAllOccupiedBlocked` (64/64 occupied -> blocked; all-free +
  size 0 -> 1). [market 0/max stalls, amt office-slot index]
- world_trade_route_test.cpp: `PriceByRateEmptyAndOversizeIndex` (empty/short rate
  table -> 0 for out-of-range currency), `PanelAssignCartZeroAndManyStops` (0 carts
  and 64 carts processed without OOB), `FindOwnerChainEmptyAndTerminator` (empty
  chain / immediate -1 terminator). [exchange empty order book, route 0/many stops]
- world_economy_test.cpp: `TradeIncomeIntExtremesNoTrap` (INT_MAX/INT_MIN tax inputs).

## Items reviewed and found already SAFE (no change needed)
- market_price.cpp: `currencyId < blockCount` + `n` capped at kMarketCacheEntries.
- amt_slot_table.cpp: fixed kAmtSlotCount(64) scans over caller-supplied arrays
  (callers own the 64-entry table); AmtFindOfficeTypeRecord capped at `count`.
- amt_economy2.cpp: every scan capped (slots30/37/768), `type<9` / `type<10` guards,
  `lawLevel & 3` for kRenderStretch[4].
- amt.cpp: `kEventProbTable[6]` index clamped to [0,5].
- bank_treasury.cpp: `kCityTaxSeedRates[16]` index is `((base+i)%16+16)%16` (0..15).
- production.cpp: `kWorkStartHour[4]` index is `((day%4)+4)%4` (0..3).
- player_finance.cpp / economy_quality.cpp / treasury.cpp / tax.cpp / caravan_cargo.cpp
  / exchange.cpp / trade_player.cpp / market_stall.cpp / amt_enforcement.cpp /
  world_economy2.cpp / economy_tick.cpp: all index/loop accesses bounded
  (vector iteration, `i < count`, null guards, size-checked subscripts).

## BEHAVIORAL — needs MCP (engine-envelope, NOT fixed)
These match the original's own faulting/saturating instruction on degenerate input;
"fixing" them would change observable output. Documented for the MCP-enabled pass.
- tax.cpp `TaxComputeTradeIncome` (0x57aa88): the final `(i32)` cast of an
  out-of-int-range float mirrors x87 `fistp` (saturates to 0x80000000). Under our
  `-fsanitize=undefined` flags it does NOT trap (returns the saturated value);
  exercised by TradeIncomeIntExtremesNoTrap. Engine envelope.
- amt_recon_office_window.cpp `AmtComputeRoleButtonYs` (0x556df1): `span / (count-1)`
  / `span % (count-1)` divide by zero when `count == 1`, matching the original's
  `idiv` by 0 (the engine always passes count >= 2). Degenerate; not "fixed".
- amt_recon_office_window.cpp `AmtFindFirstNonEmptyRole` (0x556d2b) reads
  `rowCounts[5*current]` with no bound, and `AmtOverviewFindClickedSlot` (0x557970)
  reads `slotObjectIds[0]` before the count check (do-while). Both are faithful to the
  binary's direct reads; the engine never passes an out-of-range `current` or a
  0-length slot list. Caller-contract / engine envelope — flagged for MCP to confirm
  the original had no additional guard.
- market_price_model.cpp / economy_quality.cpp / economy_tick.cpp div-by-zero on a
  zero divisor (`rec.divisor`, `g_capDivisor`, `hi-lo`) reproduce the original FP
  divide (inf/nan), the engine's envelope.
