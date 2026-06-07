#pragma once
// MeisterAi trade/stock planning decision cores (gilde.exe).
//
// The full trade planners (TradeManageStorage 0x45f1e4, TradeGeneral 0x4614d0,
// TradeRemotePurchaseA/B, CollectTransporters) are 1000+ instructions each over
// the city goods arrays and cart routing — DEFERRED (LISTed in the report). What
// is faithfully recovered here are the self-contained DECISION cores they (and
// EvaluateStockNeeds 0x58658c) use:
//
//   * StockCapacity / StockDeficit — the per-workslot stock-need math: a storage
//     capacity derived from the slot's "level" field and item type, the consumed
//     amount (cap*count >> 2), and the resulting deficit (clamped >= 0).
//   * SortStockByValue — the priority sort that orders the collected stock-need
//     records by their priced value (descending) before purchasing.
#include <cstddef>
#include "guild/common/types.h"

namespace guild::ai {

// gilde.exe 0x58658c (core) — storage capacity for a workslot. `itemTypeCode` is
// the slot's item type word (*v15), `level` is the slot's level field (slot+14):
//   itemTypeCode == 477 -> 5*level + 10
//   else level == 3      -> 80
//   else                 -> 20*level
int StockCapacity(int itemTypeCode, int level);

// gilde.exe 0x58658c (core) — stock deficit for a workslot. `cap` = StockCapacity,
// `count` = the object's current count (obj[7]), `itemTypeCode` the slot type:
//   used = (cap * count) >> 2                  (unsigned >> 2)
//   have = (itemTypeCode in {42,278,475,476}) ? count - 1 : count
//   deficit = have - used;  if (deficit < 0) deficit = 0
// Returns the clamped deficit (0 if no shortfall).
int StockDeficit(int cap, int count, int itemTypeCode);

// A stock-need record the planner collects and sorts (24 bytes in the original:
// dword_12CD6D0[6*i] block). Only the priced value `value` (flt_12CD6E0, the 4th
// dword of each 24-byte block) drives the ordering; `payload` is the opaque rest.
struct StockNeed {
    float value = 0.0f;     // flt_12CD6E0 (priced value: marketPrice * slot[73])
    i32   payload[5] = {};  // the other 20 bytes of the 24-byte record (id/slot/obj)
};

// gilde.exe 0x58658c (core) — selection-sort the stock-need records by `value`
// DESCENDING (the original swaps whenever the later record's value exceeds the
// current). Faithful to the original's O(n^2) in-place pass. `n` = record count.
void SortStockByValue(StockNeed* records, std::size_t n);

} // namespace guild::ai
