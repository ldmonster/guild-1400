#include "sim/meister_mgmt_recon.h"

// MeisterAi management / production-planner DATA/RULES cores (gilde.exe).
// See meister_mgmt_recon.h for the provenance + deferral notes. Each function
// below is a 1:1 translation of the decision math from the named decompile, with
// the coupled leaves (He tables, command queue, entity arrays, market lookups)
// supplied as inputs so the logic is deterministic and golden-testable.

namespace guild::sim {

// gilde.exe 0x453638 VIBE_Ai_ManageResourceStock — inner restock-target math.
//   if ((flags & 0x20)==0 && (flags & 0x200)==0 && !blocked) {
//     if ((slotCapacity / 4) > currentReserve) {           // signed /4
//       ratio = cachedPrice / freshPrice;
//       if (SLODWORD(ratio) < 0x3F4CCCCD) {                // ratio < 0.8f
//         target = (int)(baseQty * (1.0 - ratio));
//         if (target >= baseQty) target = baseQty;          // clamp up to base
//       }
//     }
//   }
// The decompile clears the restock dword (dword_B54458)=0 unconditionally inside
// the ratio branch and only re-arms it later from a transporter check; the
// quantity write (dword_B54464) is the (int)(baseQty*(1-ratio)) value clamped to
// baseQty. We return that target + the eligibility predicate.
StockRestock ResourceRestockTarget(u16 flags, int blocked, int slotCapacity,
                                   int currentReserve, int baseQty,
                                   float cachedPrice, float freshPrice) {
    StockRestock out{false, 0};

    if ((flags & kStockFlagSkipA) != 0)
        return out;
    if ((flags & kStockFlagSkipB) != 0)
        return out;
    if (blocked != 0)
        return out;

    // (SlotCapacity - (((SlotCapacity>>31) ... ) )) >> 2  == arithmetic /4 toward
    // -inf folded by the compiler; for the non-negative capacities the AI uses
    // this is the signed division by 4 of the slot capacity.
    if ((slotCapacity / 4) <= currentReserve)
        return out;

    float ratio = cachedPrice / freshPrice;
    // SLODWORD(ratio) < 1061158093 : reinterpret the float bits as a signed
    // dword and compare. For the positive ratios produced here this is exactly
    // ratio < 0.8f, but we reproduce the bit-compare to stay byte-faithful.
    i32 ratioBits;
    static_assert(sizeof(ratioBits) == sizeof(ratio), "float/dword size");
    __builtin_memcpy(&ratioBits, &ratio, sizeof(ratio));
    if (ratioBits >= static_cast<i32>(kRatioCutoffBits))
        return out;

    int target = static_cast<int>(static_cast<double>(baseQty) *
                                  (1.0 - static_cast<double>(ratio)));
    if (target >= baseQty)
        target = baseQty;

    out.eligible = true;
    out.target = target;
    return out;
}

// gilde.exe 0x45379c VIBE_Ai_AssignIdleWorkers — queue predicate.
//   if (!dword_B564A4[..] && (word_B564BC[..] & 8) == 0) QueueRequest20(...)
bool IdleWorkerShouldQueue(int workerBlocked, u16 workerFlags) {
    return workerBlocked == 0 && (workerFlags & 8) == 0;
}

// gilde.exe 0x45d4c4 VIBE_MeisterAi_CancelMatchingTasks — filter -> flag mask.
//   a2==4 : (word_B564BC[..] & 8)  == 0  -> match     (mask 0x8)
//   a2==3 : (word_B564BC[..] & 8)  == 0  -> match     (mask 0x8)
//   a2==8 : (word_B564BC[..] & 0x10) == 0 -> match     (mask 0x10)
u16 CancelTaskFlagMask(int filterType) {
    switch (filterType) {
        case 4:
        case 3:
            return 0x8;
        case 8:
            return 0x10;
        default:
            return 0;
    }
}

// gilde.exe 0x45d4c4 inner predicate (e.g. 0x45d533):
//   if (orderKey>>16 == handlerKey>>16 && (orderFlags & mask)==0) found=1;
bool TaskMatchesOrder(int orderKey, int handlerKey, u16 orderFlags, u16 flagMask) {
    return (orderKey >> 16) == (handlerKey >> 16) && (orderFlags & flagMask) == 0;
}

// gilde.exe 0x45cfac VIBE_MeisterAi_FillAiSlots — deficit + cost gate.
//   v11 = capGuard(+577) - usedGuards;  v12 = v11;        // guardShort
//   result = capWorker(+576) - usedWorkers;               // workerShort
//   fire = (result > 0 || v11 > 0);
//   ... if (cash >= 24000) {
//         v14 = (workerShort != 0) + (guardShort != 0);    // v29 + v30
//         cost = 8000 * v14;  cash -= cost;
//       }
AiSlotPlan AiSlotDeficit(int usedWorkers, int usedGuards, int capWorker,
                         int capGuard, int cash) {
    AiSlotPlan p{};
    p.workerShort = capWorker - usedWorkers;
    p.guardShort = capGuard - usedGuards;
    p.fire = (p.workerShort > 0 || p.guardShort > 0);
    p.slotCount = (p.workerShort != 0 ? 1 : 0) + (p.guardShort != 0 ? 1 : 0);
    p.cost = 8000 * p.slotCount;
    p.affordable = (cash >= 24000);
    return p;
}

// gilde.exe 0x45d618 VIBE_MeisterAi_DispatchOrders — count distribution.
//   if (divisor <= 1 || passIndex) count = total/divisor + already;
//   else                           count = total%divisor + total/divisor + already;
int DispatchOrderCount(int divisor, int total, int passIndex, int already) {
    if (divisor <= 1 || passIndex)
        return total / divisor + already;
    return total % divisor + total / divisor + already;
}

// gilde.exe 0x45c9ac VIBE_MeisterAi_RenovateBuilding — room budget math.
//   v57 = 0.005f * (roomWorth * 0.005f);     // flt_619940 = 0.005f
//   v55 = (128.0f <= v57) ? v57 : 128.0;     // flt_619944 = 128.0f
//   trigger = ((double)cash * 0.75f) > v55;  // flt_619948 = 0.75f
// (Constants corrected wave-19; byte-verified at 0x619940/44/48.)
RenovBudget RenovateRoomBudget(int roomWorth, int cash) {
    RenovBudget r{};
    float scaled = kRenovWorthScale *
                   (static_cast<float>(roomWorth) * kRenovWorthScale);
    r.budget = (kRenovWorthFloor <= scaled) ? scaled : kRenovMinBudget;
    r.trigger = (static_cast<double>(cash) *
                 static_cast<double>(kRenovCashScale)) >
                static_cast<double>(r.budget);
    return r;
}

// gilde.exe 0x45c9ac upgrade affordability (0x45ce91):
//   v54 = (int)ComputeMarketPrice(item, 100);
//   result = 3 * v54;
//   if (result < cash) { ...enqueue upgrade...; cash -= v54; }
bool RenovateUpgradeAffordable(int unitPrice, int cash) {
    return 3 * unitPrice < cash;
}

// gilde.exe 0x45e12c VIBE_MeisterAi_FindFreeStaffSlot — budget tally.
//   v2 = 2; for each assigned worker with action-obj kind==22: --v2;
//   if (v2 > 0) ... a free slot exists.
int FreeStaffSlotCount(int guardPostHolders) {
    return kStaffSlotBudget - guardPostHolders;
}

// gilde.exe 0x4537e8 / 0x4542b8 — production-slot Fisher-Yates shuffle.
//   for (v8=0; v8<32; ++v8) order[v8] = v8;           // identity fill
//   for (i=0; i!=128; i+=4) {                          // 32 iterations
//       v11 = RandomModulo(0x20);                       // [0,32)
//       swap(order[i/4], order[v11]);
//   }
void ProductionSlotShuffle(int order[kProductionSlots], MeisterRng rng) {
    for (int i = 0; i < kProductionSlots; ++i)
        order[i] = i;
    for (int i = 0; i < kProductionSlots; ++i) {
        int j = static_cast<int>(rng(static_cast<u32>(kProductionSlots)));
        int tmp = order[i];
        order[i] = order[j];
        order[j] = tmp;
    }
}

} // namespace guild::sim
