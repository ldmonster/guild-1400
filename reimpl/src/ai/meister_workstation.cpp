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
// The per-slot seller affordability test (0x45bc62..0x45bd3f). Returns the
// affordable supply v25 (= min(deficit, trunc(min(budgetQty, chainQty)))). The
// caller decides feasibility via `need <= v25`. sameOwner is handled by the
// caller (goto LABEL_25 = immediate feasible) before this is reached.
//   v23     = max(station.freeCap >> 1, 1)                 [ecx+40h sar 1]
//   chainQty= need*v23 - (item.reserved + item.stock)      [ebx+24h,+28h]
//   chainQty= max(chainQty, (item.freeCap>>1) - reserved)  [ebx+2Ch sar 1]
//   chainQty= min(chainQty, item.freeCap - reserved - 1)
//   budgetQty = budget * (1.0f/unitPrice)
//   chosen  = (budgetQty >= (float)chainQty) ? (float)chainQty : budgetQty
//   v25     = min(deficit, ConvertX(chosen))   (ConvertX truncates toward zero)
int AffordableSupply(const StockSeller& sl, int need, int stationFreeCap,
                     int itemReserved, int itemStock, int itemFreeCap,
                     int budget, float unitPrice) {
    int v23 = stationFreeCap >> 1;       // sar eax,1 (arithmetic)
    if (v23 < 1)
        v23 = 1;
    int chainQty = need * v23 - (itemReserved + itemStock);
    int floor1 = (itemFreeCap >> 1) - itemReserved;   // sar arithmetic
    if (chainQty <= floor1)
        chainQty = floor1;
    int cap1 = itemFreeCap - itemReserved - 1;
    if (chainQty > cap1)
        chainQty = cap1;
    float budgetQty = static_cast<float>(budget) * (1.0f / unitPrice);
    float chosen = (budgetQty >= static_cast<float>(chainQty))
                       ? static_cast<float>(chainQty)
                       : budgetQty;
    int v16 = static_cast<int>(static_cast<double>(chosen)); // ConvertX truncate
    int v25 = (sl.deficit >= v16) ? v16 : sl.deficit;
    return v25;
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
        // gate: need vs STOCK (0x45bace cmp eax,[ebx+28h]; +0x2A from id = +42 =
        // dword_B54478 = stock). Only slots whose need exceeds stock proceed.
        if (need <= it.stock)
            continue;
        if (topLevel)
            return false;          // top level cannot dig into the supply chain
        // freeCap == 0 -> infeasible (0x45badb mov edx,[ebx+2Ch]; +0x2E = +46 =
        // dword_B5447C = free capacity).
        if (it.freeCap == 0)
            return false;          // no free-capacity headroom -> infeasible
        // already-reserved? (0x45bae2 cmp [ebx+24h],0; +0x26 = +38 =
        // dword_B54474 = reserved/incoming). reserved != 0 -> already feasible.
        if (it.reserved != 0)
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
                if (need > sl.deficit)     // cmp eax,deficit; jg skip (0x45bbca)
                    continue;
                if (sl.sameOwner) {        // owner match -> LABEL_25 feasible
                    feasible = true;
                    break;
                }
                int v25 = AffordableSupply(sl, need, s.freeCap, it.reserved,
                                           it.stock, it.freeCap, needs.budget,
                                           it.unitPrice);
                if (need <= v25) {         // cmp need,v25; jle LABEL_25 (0x45bd3f)
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
