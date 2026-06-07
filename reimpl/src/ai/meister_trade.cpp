#include "ai/meister_trade.h"

#include <utility>

// MeisterAi trade/stock planning decision cores (gilde.exe 0x58658c et al.).
//
// DEFERRED (LISTed in the module report — deep coupling to the city goods arrays
// word_1234750.., cart routing, He handler arrays, and 100+ string-formatted log
// lines): TradeManageStorage (0x45f1e4), TradeGeneral (0x4614d0),
// TradeRemotePurchaseA/B (0x46329c/0x463c4c), CollectTransporters (0x45e71c),
// RefillTavernStock (0x45f0a0), BalanceCityGoods (0x4c763c). These orchestrate the
// purchase/transport plan; the per-item DECISION math they share is recovered here.

namespace guild::ai {

// gilde.exe 0x58658c — storage capacity for a workslot.
int StockCapacity(int itemTypeCode, int level) {
    if (itemTypeCode == 477)
        return 5 * level + 10;
    if (level == 3)
        return 80;
    return 20 * level;
}

// gilde.exe 0x58658c — stock deficit for a workslot.
int StockDeficit(int cap, int count, int itemTypeCode) {
    // used = (unsigned)(cap*count) >> 2 — the original's logical shift.
    int used = static_cast<int>(static_cast<u32>(cap * count) >> 2);
    int have = count;
    if (itemTypeCode == 42 || itemTypeCode == 278 ||
        itemTypeCode == 475 || itemTypeCode == 476) {
        have = count - 1;
    }
    int deficit = have - used;
    if (deficit < 0)
        deficit = 0;
    return deficit;
}

// gilde.exe 0x58658c — selection-sort by value descending.
void SortStockByValue(StockNeed* records, std::size_t n) {
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n; ++j) {
            // swap whenever the later record's value exceeds the current one,
            // so the maximum bubbles into position i (descending order).
            if (records[j].value > records[i].value)
                std::swap(records[i], records[j]);
        }
    }
}

} // namespace guild::ai
