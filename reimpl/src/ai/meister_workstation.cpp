#include "ai/meister_workstation.h"

#include <algorithm>
#include <cmath>
#include <utility>

// MeisterAi workstation assignment + item-distribution planning RULES
// (gilde.exe 0x4599f0/0x45aa78/0x45b948/0x45ba84/0x45bd68/0x45c10c). The scratch-
// table layouts are recovered byte-for-byte (see meister_workstation.h). The
// engine-coupled table-build sweeps (the GameObject_QueryFind / He_* handler
// iteration that POPULATE the tables) are DEFERRED — they are pure entity-array
// plumbing — but every per-record DECISION/MATH rule the planner applies once the
// tables are built is translated here 1:1 and exercised against a synthetic
// faction via MeisterWsEnv / StockNeedQuery.

namespace guild::ai {

// gilde.exe 0x45b948 — ComputeWorkstationOutput.
void ComputeWorkstationOutput(WsStation& s, std::vector<WsItem>& items,
                              const MeisterWsEnv& env) {
    // recompute gate: only when the cached output value is still the sentinel.
    if (static_cast<double>(s.outCache) > kRecomputeGate)
        return;

    const WsTypeDef* td = env.type_def(s.typeId);
    s.outputValue = 0.0f; // *((DWORD*)a1 + 10) = 0

    for (int k = 0; k < 4; ++k) {
        int idx = s.slots[k];
        if (idx < 0)
            continue;
        WsItem& it = items[static_cast<std::size_t>(idx)];
        // per-slot price recompute gate (item.unitPrice <= -1e10).
        if (static_cast<double>(it.unitPrice) <= kRecomputeGate) {
            float price = env.market_price(it.id);
            if (it.backIndex == -1)
                it.unitPrice = price;                 // raw material: full price
            else
                it.unitPrice = static_cast<float>(price * kChainDiscount); // chained
        }
        // outputValue += inputNeed[k] * price.
        s.outputValue += static_cast<float>(td->inputNeed[k]) * it.unitPrice;
    }
    // outputValue /= divisorOutputs (typedef +54).
    s.outputValue = static_cast<float>(static_cast<double>(s.outputValue) /
                                       static_cast<double>(td->divisorOutputs));
    s.rate = env.production_rate(s.typeId);
    s.outCache = s.outputValue + s.rate;              // float idx 9
    s.sellPrice = env.market_price(s.typeId);         // float idx 12
    // margin = (sellPrice - outCache) / divisorRate (typedef +34).
    s.score = static_cast<float>((static_cast<double>(s.sellPrice) -
                                  static_cast<double>(s.outCache)) /
                                 static_cast<double>(td->divisorRate));
}

// gilde.exe 0x4599f0 (sort + index tail) — workstations sorted by score ASCENDING.
void SortWorkstationsByScore(std::vector<WsStation>& stations) {
    const std::size_t n = stations.size();
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = 0; j < n; ++j) {
            // original: if flt_B5649C[i] < (double)flt_B5649C[outer], swap.
            // outer index is `i`, inner is `j` starting at 0 each time; the loop
            // structure bubbles the smallest score toward index i. Reproduce the
            // exact comparison: swap when stations[i].score < stations[j].score
            // for j scanning the whole array (faithful to the v53/v54 walk).
            if (static_cast<double>(stations[i].score) <
                static_cast<double>(stations[j].score)) {
                std::swap(stations[i], stations[j]);
            }
        }
    }
    // stamp final indices (dword_B564B0[i] = i).
    for (std::size_t i = 0; i < n; ++i)
        stations[i].reserveTarget = static_cast<i32>(i);
}

// gilde.exe 0x45aa78 (item sort + index tail) — items sorted by qty*margin desc.
void SortItemsByPlannedValue(std::vector<WsItem>& items) {
    const std::size_t n = items.size();
    for (std::size_t i = 0; i < n; ++i) {
        for (std::size_t j = i + 1; j < n; ++j) {
            double pj = static_cast<double>(items[j].plannedQty) *
                        static_cast<double>(items[j].margin);
            double pi = static_cast<double>(items[i].plannedQty) *
                        static_cast<double>(items[i].margin);
            if (pj > pi)
                std::swap(items[i], items[j]);
        }
    }
    for (std::size_t i = 0; i < n; ++i)
        items[i].flags50 = static_cast<i32>(i);
}

// gilde.exe 0x45c10c — net a single item's shortfall.
int GatherNetShortfall(int required, int reserved, int stock) {
    int v = required - (reserved + stock);
    if (v < 0)
        v = 0;
    return v;
}

// gilde.exe 0x45c10c — clamp purchase quantities to the budget.
int GatherClampToBudget(std::vector<WsItem>& items, int budget) {
    int accum = 0; // v39: running purchase cost (Coord_ConvertX truncated)
    for (WsItem& it : items) {
        if (it.required <= 0)
            continue;
        // while qty>0 && budget < qty*price + accum -> drop a unit.
        while (it.required > 0 &&
               static_cast<double>(budget) <
                   static_cast<double>(it.required) * it.unitPrice +
                       static_cast<double>(accum)) {
            --it.required;
        }
        double v = static_cast<double>(it.required) * it.unitPrice +
                   static_cast<double>(accum);
        accum = static_cast<int>(v); // Coord_ConvertX: truncate toward zero
    }
    return accum;
}

// gilde.exe 0x45aa78 (plan-quantity step) — DistributeWorkstationItems clamp.
int DistributePlanQty(int sourceFreeCap, int destRoom, int budget, float unitPrice) {
    // raw = min(sourceFreeCap, destRoom).
    int raw = sourceFreeCap;
    if (raw >= destRoom)
        raw = destRoom;
    // budgetQty = (budget >> 2) / unitPrice  (signed shift; FP divide; trunc).
    // The original computes the signed >>2 of the budget then multiplies by
    // 1/price; we reproduce the arithmetic-shift-right by 2.
    int budgetShifted = budget >> 2; // arithmetic shift (sign-propagating)
    float inv = 1.0f / unitPrice;
    float budgetQtyF = static_cast<float>(budgetShifted) * inv;
    // planned = min(raw, budgetQtyF) (Coord_ConvertX truncates the float result).
    float rawF = static_cast<float>(raw);
    float chosen = (rawF >= budgetQtyF) ? budgetQtyF : rawF;
    return static_cast<int>(static_cast<double>(chosen));
}

namespace {
// The per-slot seller affordability test shared by CheckCapacity / Reserve.
// need = the slot's required input count; returns the best affordable supply.
int AffordableSupply(const StockSeller& sl, int need, int budget, float unitPrice) {
    if (sl.sameOwner)
        return need; // free transfer within the faction: full need available
    // v23 = max(need>>1, 1); demandQty = v23*need - 0 (the +9/+10 terms are the
    // already-satisfied amounts; for a fresh feasibility check they are 0). Then
    // the affordable quantity is min(budget/price, need) clamped to deficit.
    float inv = 1.0f / unitPrice;
    float budgetQty = static_cast<float>(budget) * inv;
    float needF = static_cast<float>(need);
    float chosen = (budgetQty >= needF) ? needF : budgetQty;
    int qty = static_cast<int>(static_cast<double>(chosen));
    if (qty >= sl.deficit)
        qty = sl.deficit;
    return qty;
}
} // namespace

// gilde.exe 0x45ba84 — CheckWorkstationCapacity (recursive feasibility).
bool CheckWorkstationCapacity(std::vector<WsStation>& stations, std::size_t stationIdx,
                              std::vector<WsItem>& items, const MeisterWsEnv& env,
                              const StockNeedQuery& needs, bool topLevel) {
    WsStation& s = stations[stationIdx];
    const WsTypeDef* td = env.type_def(s.typeId);
    for (int k = 0; k < 4; ++k) {
        int idx = s.slots[k];
        if (idx < 0)
            continue;
        WsItem& it = items[static_cast<std::size_t>(idx)];
        int need = td->inputNeed[k];
        // only slots whose need exceeds what's already on hand (reserved+stock).
        if (need <= it.reserved)
            continue;
        if (topLevel)
            return false;          // top level cannot dig into the supply chain
        if (it.flags50 == 0)
            return false;          // no free-capacity headroom -> infeasible
        // already-satisfied? (the original's *((DWORD*)v6+9) == 0 gate).
        if (it.stock != 0)
            continue;
        // skip the special "service" item ids (449..454) — always feasible.
        u16 id = it.id;
        if (id == 449 || id == 450 || id == 451 ||
            id == 452 || id == 453 || id == 454)
            continue;
        if (it.backIndex == -1) {
            // raw material: consult the stock-need env for a viable seller.
            std::vector<StockSeller> sellers = needs.sellers(id);
            bool feasible = false;
            for (const StockSeller& sl : sellers) {
                if (sl.isSelf || sl.category == 2 || !sl.hasObject)
                    continue;
                if (need > sl.deficit)
                    continue;
                int supply = AffordableSupply(sl, need, needs.budget, it.unitPrice);
                if (sl.sameOwner || need <= supply) {
                    feasible = true;
                    break;
                }
            }
            if (!feasible)
                return false;
        } else {
            // chained product: recurse into the producing workstation.
            if (!CheckWorkstationCapacity(stations,
                                          static_cast<std::size_t>(it.backIndex),
                                          items, env, needs, false))
                return false;
        }
    }
    return true;
}

} // namespace guild::ai
