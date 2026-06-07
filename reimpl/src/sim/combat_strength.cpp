#include "sim/combat_strength.h"

namespace guild::sim {

// gilde.exe 0x48d160 — VIBE_Combat_ComputeAttackerStrength (raid-loot).
LootResult ComputeAttackerStrength(int commanderCash, const std::vector<LootWare>& wares,
                                   i32 targetSellerId, i32 attackerBuyerId,
                                   bool hasFamilyRecord, int familyMoneyBefore,
                                   const MarketPriceFn& marketPrice, CutsceneRng& rng,
                                   ILootCommandSink* sink) {
    LootResult r;
    // Base cash: (RandInt(50) * 0.001 + 0.07) * commanderCash, truncated.
    //   v19 = RandInt(50);  v8 = (v19*0.001 + 0.07) * commanderCash;
    u32 baseRoll = rng.RandInt(50);
    double baseCash = (static_cast<double>(baseRoll) * kLootBaseCashScale + kLootBaseCashBias)
                      * static_cast<double>(commanderCash);
    int cashTotal = static_cast<int>(baseCash);
    if (sink)
        sink->OnCashCredit(cashTotal);    // EnqueueCmd15(v8)

    // Per-ware market sell + value accumulation.
    for (const LootWare& w : wares) {
        // v14 = (RandInt(20) + 80) * 0.01 * ware.quantity;  qty = max(1, (int)v14)
        u32 qtyRoll = rng.RandInt(20);
        double v14 = static_cast<double>(qtyRoll + 80) * kLootWareQtyScale
                     * static_cast<double>(w.quantity);
        int qty = static_cast<int>(v14);
        if (qty < 1)
            qty = 1;
        if (sink)
            sink->OnSellWare(targetSellerId, attackerBuyerId, qty, w.wareTypeId);
        // cashTotal += marketPrice(ware) * qty;
        double v16 = (marketPrice ? marketPrice(w.wareTypeId) : 0.0)
                     * static_cast<double>(qty) + static_cast<double>(cashTotal);
        cashTotal = static_cast<int>(v16);
    }

    r.cashTotal = cashTotal;
    if (hasFamilyRecord) {
        r.familyCredited = familyMoneyBefore + cashTotal;   // *(family+7) += cashTotal
    }
    return r;
}

// gilde.exe 0x48d318 — VIBE_Combat_ComputeDefenderStrength (raid-loot).
LootResult ComputeDefenderStrength(const std::vector<std::vector<LootWare>>& stockpiles,
                                   float rate, i32 commanderSellerId, bool hasCommander,
                                   bool hasFamilyRecord, int familyMoneyBefore,
                                   const MarketPriceFn& marketPrice,
                                   ILootCommandSink* sink) {
    LootResult r;
    int cashTotal = 0;                    // v18

    // The original loops over squad.productionObjects[*] (a1+256), skipping -1
    // entries; here each populated entry is one stockpile in `stockpiles`.
    for (const std::vector<LootWare>& stock : stockpiles) {
        for (const LootWare& w : stock) {
            // v19 = ware.quantity * rate;  qty = (v19 >= 1.0) ? (int)v19 : 1
            double v19 = static_cast<double>(w.quantity) * static_cast<double>(rate);
            int qty = (v19 >= 1.0) ? static_cast<int>(v19) : 1;
            if (sink) {
                i32 seller = hasCommander ? commanderSellerId : -1;
                sink->OnSellWare(seller, w.ownerId, qty, w.wareTypeId);
            }
            double v11 = (marketPrice ? marketPrice(w.wareTypeId) : 0.0)
                         * static_cast<double>(qty) + static_cast<double>(cashTotal);
            cashTotal = static_cast<int>(v11);
        }
    }

    // No commander present -> halve the haul + emit the bulk cash command.
    if (!hasCommander && !stockpiles.empty()) {
        double v13 = static_cast<double>(cashTotal) * kLootDefenderFrac;
        cashTotal = static_cast<int>(v13);
        if (sink)
            sink->OnCashCredit(cashTotal);
    }

    r.cashTotal = cashTotal;
    if (hasFamilyRecord && !stockpiles.empty()) {
        r.familyCredited = familyMoneyBefore + cashTotal;   // *(family+7) += cashTotal
    }
    return r;
}

} // namespace guild::sim
