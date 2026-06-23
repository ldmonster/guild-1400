#pragma once
// MeisterAi management / production-planner DATA/RULES cores (gilde.exe).
//
// This unit reconstructs the *pure decision logic* of a cluster of MeisterAi
// (Guild-Master AI) per-building daily-tick planners. The original functions are
// giant orchestrators wired directly into:
//   * the per-building work-order tables (dword_B56464 / dword_B56468 stride 88B
//     == 44 words == 22 dwords; flag column word_B564BC stride 44B; quantity
//     columns dword_B564A4/A8; cost columns dword_B56484/88),
//   * the resource-stock tables (dword_B5444E stride 64B / word_B5448C flags /
//     dword_B54458/64/74/78/7C quantity+price columns),
//   * the entity/person array (word_12CE910 stride 536B == 268 words == 134
//     dwords; parallel id column dword_12CE914),
//   * the scene-tree query (VIBE_GameObject_QueryFind), market-price lookups
//     (VIBE_Building_ComputeMarketPrice / LookupCachedMarketPrice), the He
//     handler tables (VIBE_He_FindFirstHandlerByFilter), the command emitters
//     (VIBE_Command_*), and the RNG (VIBE_Math_RandomModulo).
//
// Per the project rules (3-5 tech swaps only; 8 no cheap analogues; 13 wire up),
// the coupled leaves are NOT faked here. Instead the *math* of each planner is
// lifted into a pure function that takes record snapshots + an injected RNG, so
// the decisions are deterministic and golden-testable. The orchestration that
// drives the He tables / command queue / entity sweep stays DEFERRED in the
// caller VIBE_Ai_EvaluateMeister (0x4533a8) family until those leaves land.
//
// Reconstructed (pure cores):
//   * ResourceRestockTarget    — 0x453638 VIBE_Ai_ManageResourceStock inner math
//   * IdleWorkerShouldQueue     — 0x45379c VIBE_Ai_AssignIdleWorkers predicate
//   * CancelTaskFlagMask        — 0x45d4c4 VIBE_MeisterAi_CancelMatchingTasks map
//   * TaskMatchesOrder          — 0x45d4c4 inner work-order match predicate
//   * AiSlotDeficit             — 0x45cfac VIBE_MeisterAi_FillAiSlots deficit math
//   * AiSlotCost                — 0x45cfac the 8000*count cost + 24000 gate
//   * DispatchOrderCount        — 0x45d618 VIBE_MeisterAi_DispatchOrders count math
//   * RenovateRoomBudget        — 0x45c9ac VIBE_MeisterAi_RenovateBuilding worth math
//   * RenovateUpgradeAffordable — 0x45c9ac the 3*price < cash upgrade gate
//   * FreeStaffSlotCount        — 0x45e12c VIBE_MeisterAi_FindFreeStaffSlot tally
//   * ProductionSlotShuffle     — 0x4537e8 / 0x4542b8 Fisher-Yates permute (32 ids)
//
// DEFERRED (reason): the full planner bodies (0x4537e8 CalcMeisterProduction,
// 0x4542b8 CalcMeisterCraftProduction, 0x454f50 CalcMeisterFarming, 0x455cd8
// CalcMeisterWache, 0x4569a8, 0x457440 CalcMeisterDiebe, 0x4588d0
// CalcMeisterAmbush, plus EquipStaffWeapon 0x45e350) cannot run without the He
// handler tables, the command-emission cluster (Command_QueueRequest17/20/
// SlotReset28/EnqueueCmd15/...), GameObject_QueryFind, the market-price leaves
// and the live entity arrays — all out-of-tree leaves. Their decision cores are
// reconstructed; the loops that drive the coupled leaves are left to the caller.
#include "guild/common/types.h"

namespace guild::sim {

// Injected RNG: faithful image of VIBE_Math_RandomModulo (gilde.exe 0x58b89c),
// returning a value in [0, modulo). Passed in so the planners are deterministic
// and golden-testable; the real backend wires the engine RNG.
using MeisterRng = u16 (*)(u32 modulo);

// ---------------------------------------------------------------------------
// 0x453638 VIBE_Ai_ManageResourceStock — inner restock-target computation.
//
// Per stock-table entry, when (flags & 0x20)==0 && (flags & 0x200)==0 && the
// "blocked" dword is 0, and (capacity/4) > currentReserve, the planner values
// the stock by the ratio of the cached market price to the fresh market price:
//     ratio = cachedPrice / freshPrice
// and, when ratio < 0.8 (SLODWORD threshold 1061158093 == 0.8f), the new
// target is  target = (int)(baseQty * (1.0 - ratio))  clamped to baseQty.
//
// Returns true when the entry is *eligible* (the four predicate gates pass and
// ratio<0.8) and writes the clamped target into *outTarget. The 0.8f cutoff is
// the bit pattern 0x3F4CCCCD compared as a signed dword (decompile: SLODWORD).
// (Byte-verified at disasm 0x4536db `cmp ..., 3F4CCCCDh`; the earlier 0x3F59999D
//  / 0.85f value was wrong — corrected in sim_07 harden pass.)
//   gilde.exe 0x453638
constexpr u16  kStockFlagSkipA  = 0x20;   // word_B5448C[..] & 0x20
constexpr u16  kStockFlagSkipB  = 0x200;  // word_B5448C[..] & 0x200
constexpr u32  kRatioCutoffBits = 1061997773u; // 0x3F4CCCCD == 0.8f (SLODWORD)
struct StockRestock {
    bool eligible;   // all gates + ratio cutoff passed
    int  target;     // clamped restock target (only meaningful if eligible)
};
StockRestock ResourceRestockTarget(u16 flags, int blocked, int slotCapacity,
                                    int currentReserve, int baseQty,
                                    float cachedPrice, float freshPrice);

// ---------------------------------------------------------------------------
// 0x45379c VIBE_Ai_AssignIdleWorkers — per-worker queue predicate.
// The loop queues a request for a worker iff its "blocked" dword is 0 AND
// (flagWord & 8)==0. Returns that predicate.
//   gilde.exe 0x4537c1
bool IdleWorkerShouldQueue(int workerBlocked, u16 workerFlags);

// ---------------------------------------------------------------------------
// 0x45d4c4 VIBE_MeisterAi_CancelMatchingTasks — filter-type -> flag-mask map.
// The cancel pass only handles filter types 3, 4, 8. For 4 and 3 the match flag
// is bit 8 (0x8); for 8 it is bit 0x10. CancelTaskFlagMask returns that mask, or
// 0 when the filter type is not one the pass handles.
//   gilde.exe 0x45d509 / 0x45d54f / 0x45d58f
u16 CancelTaskFlagMask(int filterType);

// The inner work-order match: a task matches a handler when the order's building
// id (the HIWORD of the order key, i.e. orderKey>>16) equals the handler's
// building id (handlerKey>>16) AND the order's flag bit (per CancelTaskFlagMask)
// is clear. Returns true on a match (sets the "found" flag in the original).
//   gilde.exe 0x45d533 / 0x45d573 / 0x45d5b3
bool TaskMatchesOrder(int orderKey, int handlerKey, u16 orderFlags, u16 flagMask);

// ---------------------------------------------------------------------------
// 0x45cfac VIBE_MeisterAi_FillAiSlots — staff-slot deficit + cost gate.
//
// usedWorkers/usedGuards are the running sums over the He handler list (worker =
// handler+44*4, guard = handler+45*4). The building type record holds the slot
// caps at +576 (worker cap) and +577 (guard cap). The deficit is:
//     workerShort = cap_worker(+576) - usedWorkers
//     guardShort  = cap_guard (+577) - usedGuards
// The pass fires only when (workerShort>0 || guardShort>0). The count of slots to
// fill is (workerShort!=0) + (guardShort!=0), each filled for 8000 cash; it is
// gated on the AiPlayer having >= 24000 cash (the *((int*)v32+110) >= 24000 test).
//   gilde.exe 0x45d076 / 0x45d091 / 0x45d14c
struct AiSlotPlan {
    bool fire;        // workerShort>0 || guardShort>0
    int  workerShort; // cap_worker - usedWorkers
    int  guardShort;  // cap_guard  - usedGuards
    int  slotCount;   // (workerShort!=0)+(guardShort!=0)
    int  cost;        // 8000 * slotCount
    bool affordable;  // cash >= 24000  (the spend gate)
};
AiSlotPlan AiSlotDeficit(int usedWorkers, int usedGuards, int capWorker,
                         int capGuard, int cash);

// ---------------------------------------------------------------------------
// 0x45d618 VIBE_MeisterAi_DispatchOrders — per-pass order-count computation.
// Every branch computes the same count distribution:
//     if (divisor <= 1 || passIndex)  count = total/divisor + already
//     else                            count = total%divisor + total/divisor + already
// where a1[5]=divisor, a1[6]=total, a1[7]=passIndex (nonzero after first pass),
// a1[8]=already. The remainder is folded into the first pass only.
//   gilde.exe 0x45d644 / 0x45daaa / 0x45de5f / 0x45d812
int DispatchOrderCount(int divisor, int total, int passIndex, int already);

// ---------------------------------------------------------------------------
// 0x45c9ac VIBE_MeisterAi_RenovateBuilding — room-renovation budget math.
//
// roomWorth = VIBE_BuildingValue_ComputeRoomWorth(...).  The original computes:
//     scaled = flt_619940 * (roomWorth * flt_619940)   // flt_619940 = 0.005f
//     budget = (scaled >= flt_619944) ? scaled : 128.0  // flt_619944 = 128.0f
//     trigger = (cash * flt_619948) > budget            // flt_619948 = 0.75f
// (Constants byte-verified via get_bytes @0x619940/44/48 = 0x3BA3D70A / 0x43000000
//  / 0x3F400000; the earlier 0.5/0.0/0.1 comment was wrong — corrected wave-19.)
// Renovation enqueues iff trigger. RenovateRoomBudget returns the budget value
// and whether the spend triggers, given the room worth and current cash.
//   gilde.exe 0x45cb1b..0x45cb7f
constexpr float kRenovWorthScale = 0.005f; // flt_619940 (0x3BA3D70A) — byte-exact
constexpr float kRenovWorthFloor = 128.0f; // flt_619944 (0x43000000) — byte-exact
constexpr float kRenovCashScale  = 0.75f;  // flt_619948 (0x3F400000) — byte-exact
constexpr float kRenovMinBudget  = 128.0f; // the (scaled<floor) fallback == 128.0
struct RenovBudget {
    float budget;   // the clamped renovation budget
    bool  trigger;  // (cash * 0.1) > budget
};
RenovBudget RenovateRoomBudget(int roomWorth, int cash);

// The upgrade-item affordability gate (the second half of RenovateBuilding):
// once a missing input item is found, its unit price is computed and the upgrade
// fires iff  3*price < cash.  Returns true when affordable (and writes the unit
// price into *outPrice).  gilde.exe 0x45ce91
bool RenovateUpgradeAffordable(int unitPrice, int cash);

// ---------------------------------------------------------------------------
// 0x45e12c VIBE_MeisterAi_FindFreeStaffSlot — free guard-slot tally.
// The pass starts a budget of 2 and decrements it once per assigned worker whose
// action object's kind byte (*v4) == 22 (a guard post). A free slot exists iff
// the remaining budget is > 0 after the sweep. FreeStaffSlotCount returns the
// remaining budget given the number of workers currently holding a kind-22 post.
//   gilde.exe 0x45e13a / 0x45e171 / 0x45e180
constexpr int kStaffSlotBudget = 2;
int FreeStaffSlotCount(int guardPostHolders);

// ---------------------------------------------------------------------------
// 0x4537e8 / 0x4542b8 — production-slot Fisher-Yates shuffle.
// Both CalcMeister*Production planners build the index array 0..31, then do 32
// in-place swaps:  for i in 0..31: j = rng(32); swap(order[i], order[j]).
// (The original walks a 128-byte window in 4-byte steps == 32 dwords.)
// The order[] buffer must hold 32 ints; it is filled with the identity then
// permuted in place using the injected RNG. Faithful to the loop order.
//   gilde.exe 0x4538e8 / 0x4538f8
constexpr int kProductionSlots = 32;
void ProductionSlotShuffle(int order[kProductionSlots], MeisterRng rng);

} // namespace guild::sim
