#pragma once
// MeisterAi workstation assignment + item-distribution planner (gilde.exe).
//
// This is the deferred-from-meister_economy second half of the Guild-Master AI's
// production planning: the passes that build the two parallel scratch tables the
// AI uses to decide which production WORKSTATIONS to run and which ITEMS to buy /
// move / reserve for them, then sort them by value/margin so the highest-value
// work is funded first.
//
//   * AssignWorkstations         0x4599f0  — collect the building's workstations,
//       expand their input-item requirements into the item table, accumulate
//       incoming production/handler deliveries, score each workstation (output
//       value via ComputeWorkstationOutput), and selection-sort by score.
//   * ComputeWorkstationOutput   0x45b948  — per-workstation output value, rate,
//       and price margin (the float[9..13] fields of a workstation record).
//   * CheckWorkstationCapacity   0x45ba84  — recursive feasibility test: can a
//       workstation's input chain be satisfied (own stock or a reachable seller)?
//   * ReserveWorkstationItems    0x45bd68  — recursive reservation: stamp the
//       per-item reserve target + capacity flags down the input chain.
//   * GatherRequiredItems        0x45c10c  — net per-item shortfall and clamp the
//       purchase quantity to the AI budget.
//   * DistributeWorkstationItems 0x45aa78  — the storage<->workstation transfer
//       planner: clamp planned move quantity to budget/4 and free capacity, then
//       selection-sort items by (qty*margin) and workstations by (qty*margin).
//
// The two scratch tables (`dword_B5444E[]` item table, `dword_B56464[]`
// workstation table) and the stock-need scratch (`dword_12CD6D0[]`) are recovered
// BYTE-FOR-BYTE below. The deeply engine-coupled leaf reads (GameObject_QueryFind,
// Inventory_*, He_* handler iteration, Building_* market price/rate, Coord_ConvertX
// FP truncation) are injected through MeisterWsEnv so the planning RULES run on a
// synthetic faction. Commands (QueueRequest20 for a zero-stock notify) go through
// the same env hook so the sequence is verifiable.
//
// Recovered float constants (gilde.exe, byte-exact):
//   flt_6198FC = 0.01      (handler delivery scale)
//   flt_619900 = 7.5       (handler delivery bias step)
//   flt_619904 = 0.01      (storage-source margin scale, DistributeWorkstationItems)
//   dbl_619908 = -1e10     (ComputeWorkstationOutput recompute gate)
//   dbl_619910 = 0.9       (input-slot price discount when chained)
//   dbl_619918 = -1e10     (per-slot price recompute gate)
//   marker     = -1e10     (flt_B5446C / dword_B5648C "not computed" sentinel)
#include <vector>
#include <cstddef>

#include "guild/common/types.h"

namespace guild::ai {

// ===========================================================================
// Item table record  (gilde.exe `dword_B5444E[]` family — stride 64, capacity
// 128, count `dword_B56FE0`). Byte offsets are taken relative to the id field
// (the table base `dword_B5444E` points at offset +2 of the record; the original
// addresses it as `&dword_B5444E[16*i] + 2` so +2 == record start "v5").
// Recovered field map (offset from the id word):
//   +0  id (word)                     itemId, HIWORD(dword_B5444E[16*i])
//   +4  source-storage ptr            dword_B5445C (Distribute) — the seller obj
//   +6  workstation back-index (-1)   dword_B54454
//   +10 source building ptr           dword_B54458 (Gather/Distribute)
//   +14 free capacity (storage)       dword_B5447C — used by CollectStorage/Dist
//   +18 ?                             dword_B54460
//   +22 required count                dword_B54464 (Gather)
//   +26 ?                             dword_B54468
//   +30 unit price (market)           flt_B5446C
//   +34 price margin                  flt_B54470 (Distribute: sell-buy spread)
//   +38 reserved/incoming             dword_B54474 (handler deliveries)
//   +42 current stock                 dword_B54478
//   +46 free capacity (dup)           dword_B5447C  (same addr as +14? see note)
//   +50 flags byte (33/129)           dword_B54480 — also reused as final index
//   +54 planned move qty              dword_B54484 (Distribute)
//   +58 dest stock                    dword_B54488 (Distribute)
//   +62 bit flags (word)              word_B5448C  (0x02 reserve, 0x40 matched, 0x80 storage)
// NOTE: the original uses overlapping dword views; we model the distinct logical
// fields. For the planning RULES the load-bearing fields are id, backIndex,
// requiredCount, unitPrice, margin, reserved, stock, freeCap, plannedQty, flags.
// ===========================================================================
struct WsItem {
    u16   id = 0;             // +0
    void* sourceStorage = nullptr; // +4  (seller object, Distribute)
    i32   backIndex = -1;     // +6  workstation index that consumes this (-1 none)
    void* sourceBuilding = nullptr; // +10 (Gather: chosen seller building)
    i32   freeCap = 0;        // +14 free capacity in the destination storage
    i32   field18 = 0;        // +18
    i32   required = 0;       // +22 required count this turn
    i32   field26 = 0;        // +26
    float unitPrice = -1e10f; // +30 cached market buy price
    float margin = 0.0f;      // +34 sell - buy spread (Distribute)
    i32   reserved = 0;       // +38 incoming/reserved (handler deliveries)
    i32   stock = 0;          // +42 current stock at destination
    i32   plannedQty = 0;     // +54 planned transfer quantity (Distribute)
    i32   destStock = 0;      // +58
    i32   flags50 = 129;      // +50
    u16   bits = 0;           // +62 bit flags
};

// ===========================================================================
// Workstation table record (gilde.exe `dword_B56464[]` family — stride 88,
// capacity 32, count `dword_B56FDC`). The original addresses the record body via
// `dword_B56468` (= record +4); the float views in ComputeWorkstationOutput index
// `((float*)a1 + N)` where a1 = &record+4. Recovered field map (offset from base
// `dword_B56464`):
//   +0  type id (word)               LOWORD(dword_B56468[22*i]) — scene type
//   +4..+20  4 input-slot item idx   dword_B5646C.. (4 dwords, -1 = empty)
//   +14 source-storage ptr (Dist)    dword_B5445C-equiv (Distribute reuses +14)
//   +32 ?                            dword_B56484
//   +36 ?                            dword_B56488
//   +40 output-value cache           dword_B5648C (float idx 9; -1e10 = recompute)
//   +44 output value (float idx 10)  — accumulated input-price * count
//   +48 production rate (float 11)
//   +52 sell price (float 12)
//   +56 margin (float 13)            flt_B5649C — score for the sort
//   +60 incoming production          dword_B564A0
//   +64 current stock                dword_B564A4
//   +68 free capacity                dword_B564A8
//   +72 ?                            dword_B564AC
//   +76 reserve target / final idx   dword_B564B0 (set to running index at end)
//   +88 bit flags (word)             word_B564BC
// ===========================================================================
struct WsStation {
    u16   typeId = 0;          // +0
    i32   slots[4] = {-1, -1, -1, -1}; // +4..+20 input item indices
    void* sourceStorage = nullptr;     // +14 (Distribute)
    float outputValue = 0.0f;  // float idx 10 (+44)
    float rate = 0.0f;         // float idx 11 (+48)
    float sellPrice = 0.0f;    // float idx 12 (+52)
    float score = 0.0f;        // float idx 13 (+56) flt_B5649C
    float outCache = -1e10f;   // float idx 9  (+40) recompute gate
    i32   incoming = 0;        // +60 dword_B564A0
    i32   stock = 0;           // +64 dword_B564A4
    i32   freeCap = 0;         // +68 dword_B564A8
    i32   reserveTarget = 33;  // +76 dword_B564B0 (init 33, final = index)
    u16   bits = 0;            // +88 word_B564BC
};

// Recovered constants.
constexpr float kHandlerScale   = 0.01f;  // flt_6198FC
constexpr float kHandlerBias    = 7.5f;   // flt_619900
constexpr float kStorageMargin  = 0.01f;  // flt_619904
constexpr double kRecomputeGate = -1e10;  // dbl_619908 / dbl_619918
constexpr double kChainDiscount = 0.9;    // dbl_619910
constexpr float kNotComputed    = -1e10f; // flt_B5446C / dword_B5648C sentinel

// ===========================================================================
// gilde.exe 0x45b948 — ComputeWorkstationOutput.
// For a workstation `s` with a scene-type descriptor (input-slot need counts and
// a divisor), compute its output value, production rate, sell price, and margin.
// `inputNeed[k]` is the per-slot required-count from the scene type-def table
// (word at typedef +38+2k); `divisorOutputs` (typedef +54) and `divisorRate`
// (typedef +34) are the type-def scalars. Items are looked up by `slots[k]`.
// Mirrors: only recomputes when outCache <= -1e10. Each input price is the cached
// market price, *0.9 if the item is itself produced by a chained workstation
// (item.backIndex != -1). Output value = Σ(need*price) / divisorOutputs; the
// margin = (sellPrice - (outputValue+rate)) / divisorRate.
// ===========================================================================
struct WsTypeDef {
    int  inputNeed[4] = {0, 0, 0, 0}; // typedef +38 + 2k (word)
    int  divisorOutputs = 1;          // typedef +54 (word)
    int  divisorRate = 1;             // typedef +34 (dword)
};

// Engine leaf reads, injectable.
struct MeisterWsEnv {
    // market buy price for an item id (VIBE_Building_LookupCachedMarketPrice).
    float (*market_price)(u16 itemId) = nullptr;
    // production rate scalar for a workstation type (Building_ComputeProductionRate).
    float (*production_rate)(u16 typeId) = nullptr;
    // scene type descriptor for a workstation type id.
    const WsTypeDef* (*type_def)(u16 typeId) = nullptr;
};

void ComputeWorkstationOutput(WsStation& s, std::vector<WsItem>& items,
                              const MeisterWsEnv& env);

// gilde.exe 0x4599f0 (sort tail) — selection-sort workstations by `score`
// ASCENDING (the original swaps when flt_B5649C[later] < flt_B5649C[current], so
// the smallest score bubbles to the front). Faithful O(n^2) in-place pass; then
// stamps each workstation's reserveTarget to its final index (the +76 write).
void SortWorkstationsByScore(std::vector<WsStation>& stations);

// gilde.exe 0x45aa78 (item sort tail) — selection-sort items by (plannedQty *
// margin) DESCENDING (swap when later product > current product). Then stamps
// each item's final index (flags50 = index).
void SortItemsByPlannedValue(std::vector<WsItem>& items);

// ===========================================================================
// gilde.exe 0x45c10c — GatherRequiredItems budget clamp.
// Given the per-item `required` shortfall (already net of reserved+stock, then
// clamped >= 0), reduce each item's purchase quantity so the running purchase
// cost stays within `budget`: while qty>0 && budget < qty*unitPrice + accumCost,
// drop one unit; accumulate qty*unitPrice into the running cost (truncated to int
// via Coord_ConvertX). Mutates `items[*].required` to the affordable quantity and
// returns the total cost accumulated.
// ===========================================================================
int GatherClampToBudget(std::vector<WsItem>& items, int budget);

// gilde.exe 0x45c10c (net step) — net a single item's shortfall:
//   required = required - (reserved + stock); clamp(<0)->0.
// Returns the clamped shortfall.
int GatherNetShortfall(int required, int reserved, int stock);

// ===========================================================================
// gilde.exe 0x45aa78 (plan-quantity step) — DistributeWorkstationItems clamp.
// planned move quantity from a storage source to a workstation:
//   raw = min(sourceFreeCap, destStockRoom)              [the B5447C/B564B8 min]
//   budgetQty = floor((budget>>2) / unitPrice)           [budget/4 affordable]
//   planned = min(raw, budgetQty)                         [Coord_ConvertX trunc]
// `budget` is AiPlayer+440; the `>>2` is an arithmetic shift of a possibly-negative
// budget (faithful: ((budget>>31)<<2 + 4*(budget>>31)) correction, i.e. exact
// signed /4 toward zero is NOT what the original does — it uses >>2). We reproduce
// the original's signed-shift-by-2.
// ===========================================================================
int DistributePlanQty(int sourceFreeCap, int destRoom, int budget, float unitPrice);

// ===========================================================================
// gilde.exe 0x45ba84 — CheckWorkstationCapacity (recursive feasibility).
// Walks the 4 input slots of station index `stationIdx`. For each non-empty slot
// whose required count (typedef inputNeed) exceeds the item's incoming+reserved:
//   - if `topLevel` (a3) -> infeasible (return false) immediately.
//   - else if the item has no free-capacity headroom (flags50==0) -> false.
//   - else if the item is a chained product (backIndex != -1) -> recurse into the
//     producing workstation (must be feasible).
//   - else (a raw material, not one of the special ids 449..454) -> consult the
//     stock-need env: feasible iff some seller building (not self, category != 2,
//     within the deficit) can supply at least the needed amount.
// Returns true if every slot is satisfiable. `env`/`needs` inject the leaf reads.
// ===========================================================================
struct StockSeller {
    bool   isSelf = false;       // dword_12CD6D0[i] == own building
    int    category = 0;         // Building_MapTypeToCategory(*building) (2 = market)
    bool   hasObject = false;    // dword_12CD6D8[i] (the seller's stock object)
    int    deficit = 0;          // dword_12CD6DC[i] (seller's available surplus)
    bool   sameOwner = false;    // owner matches AI player (free transfer)
    void*  building = nullptr;   // dword_12CD6D0[i] (seller building; ReserveWs sets
                                 // item.sourceBuilding = this)
};
struct StockNeedQuery {
    // For an item id, the list of candidate sellers (the EvaluateStockNeeds scan).
    std::vector<StockSeller> (*sellers)(u16 itemId) = nullptr;
    int budget = 0;              // AiPlayer +440
};
bool CheckWorkstationCapacity(std::vector<WsStation>& stations, std::size_t stationIdx,
                              std::vector<WsItem>& items, const MeisterWsEnv& env,
                              const StockNeedQuery& needs, bool topLevel);

} // namespace guild::ai
