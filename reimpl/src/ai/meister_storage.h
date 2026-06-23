#pragma once
// MeisterAi trade/storage ORCHESTRATION — the deferred big bodies that drive the
// Guild-Master AI's storage management and workstation item buffers (gilde.exe).
//
// This module translates the *orchestration* of:
//   * TradeManageStorage      0x45f1e4  (~1974 insns) — the per-faction storage
//       sell/restock decision pass. Runs once per turn over the item table
//       (dword_B5444E[]) and the workstation table (dword_B56464[]) the
//       AssignWorkstations sweep built, deciding what to sell out of storage:
//         1. demand bump  — flagged-but-unmatched items whose stock exceeds
//            slotCap/3 get their `required` field stamped (halved for AiType 22).
//         2. profit-margin sell (odd hours) — a workstation whose sell/buy price
//            ratio is < 1.0 AND <= RandomFloat()*1.25+0.25 dumps its whole stock.
//         3. emergency sell — when funds<3200 or held<8000 and projected proceeds
//            < 32000, sell max(1, 32000/price) per workstation until proceeds hit
//            32000.
//         4. overstock sell — a workstation over 3*slotCap/4 sells max(5, stock/4).
//       The cart unload / storage-reconcile / reload tail (which walks the scene
//       storage tree + emits cm_RequestSellObjekt logs) is DEFERRED plumbing; the
//       per-workstation/item DECISIONS above are translated 1:1 and tested.
//   * AssignWorkstations       0x4599f0  (~765 insns) — the table-build sweep that
//       POPULATES dword_B56464[] (workstations) + dword_B5444E[] (their input
//       items) from the scene tree, accumulates handler deliveries, computes each
//       workstation's output (ComputeWorkstationOutput), and sorts by score.
//   * CollectStorageItems      0x45a62c  — builds the item table from the city's
//       import/export good lists + accumulates handler deliveries.
//   * GatherRequiredItems      0x45c10c  — nets each item's shortfall against the
//       import/export lists, picks a seller, and clamps the buy qty to budget.
//   * ReserveWorkstationItems  0x45bd68  — recursive reservation stamp down the
//       workstation input chain.
//
// The scratch-table layouts (WsItem / WsStation) and the per-item decision cores
// (ComputeWorkstationOutput / CheckWorkstationCapacity / GatherClampToBudget /
// GatherNetShortfall / DistributePlanQty) live in meister_workstation.h and are
// REUSED here. Cross-cluster leaf reads (scene-tree query, inventory stock/free,
// He handler iteration, market price) are injected through StorageEnv so the
// sweeps run on a synthetic faction; commands route through a hook.
//
// Recovered float constants (byte-exact):
//   flt_619D2C = 1.25  flt_619E9C = 1.25  dbl_619D38 = 0.25  dbl_619D30 = 0.006
#include <vector>
#include <functional>

#include "guild/common/types.h"
#include "ai/meister_workstation.h"

namespace guild::ai {

// ---------------------------------------------------------------------------
// TradeManageStorage / TradeGeneral shared sell-decision core
// ---------------------------------------------------------------------------
constexpr float  kProfitSellSlope = 1.25f;   // flt_619D2C / flt_619E9C
constexpr double kProfitSellBias  = 0.25;    // dbl_619D38 (storage variant only)
constexpr int    kEmergencyFundsFloor = 3200;   // funds gate
constexpr int    kEmergencyHeldFloor  = 8000;   // currency-held gate
constexpr int    kEmergencyTarget     = 32000;  // proceeds target
constexpr int    kOverstockMin        = 5;      // overstock sell floor

// gilde.exe 0x45f1e4 (item demand-bump step). For an item that is flagged
// (bits & 8) but NOT already matched/reserved (bits & 6 == 0): if its stock
// exceeds slotCapacity/3, its `required` is set to the stock (or stock/2 when the
// AI player type is 22, the "warehouse" faction). Returns the new required value
// (0 = no bump). `slotCapacity` is Inventory_GetSlotCapacity of the storage.
int StorageDemandBump(int itemBits, int stock, int slotCapacity, bool aiTypeIs22);

// gilde.exe 0x45f1e4 / 0x4614d0 (profit-margin sell step). A workstation that is
// not matched (bits & 6 == 0) sells its whole stock when the price ratio
// sellPrice/buyPrice is < 1.0 AND <= rollThreshold. For TradeManageStorage the
// threshold is roll*1.25+0.25; for TradeGeneral it is roll*1.25 (no bias). The
// storage variant additionally gates the (bits & 8) case on stock > 3 (cart cap);
// the general variant on stock > slotCap/3.
//   sellPrice = Building_ComputeMarketPrice(typeId, 100)
//   buyPrice  = Building_LookupCachedMarketPrice(typeId)
// Returns the qty to sell (the whole stock) or 0.
int ProfitMarginSell(int workstationBits, int stock, float sellPrice,
                     float buyPrice, double rollThreshold, bool storageVariant,
                     int slotCapacityOrCartCap);

// gilde.exe 0x45f1e4 (emergency sell step, per workstation). When the faction is
// cash-strapped (funds<3200 OR held<8000) and the running projected proceeds are
// still below 32000, sell max(1, 32000/price) units, clamped to the stock. Returns
// the qty to sell and adds qty*price to *proceeds. `price` = Coord_ConvertX of
// Building_LookupCachedMarketPrice(typeId). Only sells when *proceeds < 32000 and
// the workstation is unmatched (bits & 6 == 0).
int EmergencySell(int workstationBits, int stock, int price, int* proceeds);

// gilde.exe 0x45f1e4 (overstock sell step, per workstation). A workstation whose
// stock exceeds 3*slotCapacity/4 sells max(5, stock/4) units, clamped to the
// stock. Returns the qty to sell (0 = below the overstock line). Only for
// unmatched (bits & 6 == 0) workstations.
int OverstockSell(int workstationBits, int stock, int slotCapacity);

// ---------------------------------------------------------------------------
// The synthetic faction + env the orchestration sweeps run against.
// ---------------------------------------------------------------------------
// A storage-sell command the pass emits (collapsed cm_RequestSellObjekt etc).
enum class StorageCmd {
    DemandBump,    // item.required stamped
    ProfitSell,    // workstation whole-stock sell (good margin)
    EmergencySell, // forced sell to raise cash
    OverstockSell, // trim excess stock
    Notify20,      // QueueRequest20 (zero-stock notify, from AssignWorkstations)
};
struct StorageCommand {
    StorageCmd kind;
    u16 itemId = 0;
    int qty = 0;
    i32 account = 0;
};

// Faction-level state the storage pass reads (the a1 AiPlayer / a2 storage args).
struct StoragePlayer {
    int  aiType = 0;          // AiPlayer +0 (22 = warehouse → demand-bump halves)
    bool oddHour = false;     // WORD2(qword_13CE852) % 2 (profit-sell runs odd)
    bool sellDoneFlag = false;// (+437 & 2) — profit/emergency block runs once/odd
    int  funds = 0;           // AiPlayer +440
    int  heldCurrency = 0;    // Person_SumCurrencyHeld
    int  slotCapacity = 0;    // Inventory_GetSlotCapacity(storage)
    i32  account = 0;         // dword_12CE914 command target
};

// gilde.exe 0x45f1e4 (orchestration) — run the storage sell decisions over the
// already-built item/workstation tables. `storageVariant`=true → TradeManageStorage
// (cart cap 3, +0.25 bias); false → TradeGeneral (slotCap/3, no bias). `roll()`
// returns one RandomFloatScaled() draw per profit-margin candidate.
//   sell_price(typeId) = Building_ComputeMarketPrice(typeId,100)   (the offer price)
//   buy_price(typeId)  = Building_LookupCachedMarketPrice(typeId)  (cost basis)
// Appends the emitted sell commands to `out`. Mutates item.required. Returns the
// number of commands emitted.
int RunStorageSellPass(StoragePlayer& player, std::vector<WsItem>& items,
                       std::vector<WsStation>& stations, bool storageVariant,
                       const std::function<double()>& roll,
                       float (*sell_price)(u16 typeId),
                       float (*buy_price)(u16 typeId),
                       std::vector<StorageCommand>& out);

// ---------------------------------------------------------------------------
// AssignWorkstations table-build sweep (the orchestration over the scratch tables)
// ---------------------------------------------------------------------------
// Injected leaf reads for the table builds. These mirror the scene-tree query,
// inventory, and He-handler iteration the original walks.
struct StorageEnv {
    MeisterWsEnv ws;  // market_price / production_rate / type_def (reused)
    // current stock for a (storage, itemId) — Inventory_GetEffectiveStock/FindItemStock.
    int (*stock_of)(u16 itemId) = nullptr;
    // free capacity for a (storage, itemId) — Inventory_ComputeFreeCapacity.
    int (*free_cap)(u16 itemId) = nullptr;
};

// A workstation candidate the scene query yields (GameObject type word + count>1).
// The 4 input-slot item ids come from the scene type-def's input-word column
// (dword_13CE27C + 65*type + 46 + 2k); 0 = empty slot. The matching need counts
// come from the WsTypeDef the env returns for `typeId`.
struct WsCandidate {
    u16 typeId = 0;        // *i (scene node type)
    int count  = 0;        // *(i+7) production count (must be > 1)
    u16 inputId[4] = {0, 0, 0, 0}; // type-def input item ids per slot (0 = empty)
};

// A handler delivery (He_FindFirstHandlerByFilter cat 11/20/21 → per-item incoming).
struct WsDelivery {
    u16 itemId = 0;     // the produced/delivered item id
    int amount = 0;     // units delivered (added to item.reserved + station.incoming)
};

// gilde.exe 0x4599f0 — build the workstation + item tables from the scene query.
// 1. each candidate with count>1 → a WsStation (slots[]=-1, init fields).
// 2. expand each station's input-slot needs (type_def inputNeed words at +46) into
//    WsItems, linking slot k → item index.
// 3. for special-output ids 449..454 / 452..454, add the produced item as its own
//    station and stamp the matched/storage bits (0x204 / 0x24).
// 4. back-link items → producing station (item.backIndex).
// 5. snapshot stock + free capacity for each station and item (Notify20 on zero
//    item stock).
// 6. fold handler deliveries into station.incoming + item.reserved.
// 7. ComputeWorkstationOutput per station; SortWorkstationsByScore; mark matched
//    items (0x40 bit) and stamp final indices.
// `aiTypeAllowsSpecial` = AiPlayer+0 is 14 or 8 (the special-item duplication gate).
void AssignWorkstations(const std::vector<WsCandidate>& candidates,
                        const std::vector<WsDelivery>& deliveries,
                        bool aiTypeAllowsSpecial,
                        std::vector<WsStation>& stations,
                        std::vector<WsItem>& items,
                        const StorageEnv& env,
                        std::vector<StorageCommand>& out);

// gilde.exe 0x45a62c — build the item table from two import/export good lists,
// snapshot stock + free capacity, and fold handler deliveries. Appends new items
// (skipping ids already present). `imports`/`exports` are the city's KONTOR lists.
void CollectStorageItems(const std::vector<u16>& imports,
                         const std::vector<u16>& exports,
                         const std::vector<WsDelivery>& deliveries,
                         std::vector<WsItem>& items,
                         const StorageEnv& env);

// ---------------------------------------------------------------------------
// GatherRequiredItems orchestration (gilde.exe 0x45c10c)
// ---------------------------------------------------------------------------
// A consumer person's required item (the 768-person sweep that bumps item.required
// for items appearing in the city import/export lists).
struct GatherNeed {
    u16 itemId = 0; // the person's needed good id
};

// gilde.exe 0x45c10c — the full gather sweep. For each `need` whose item id appears
// in the item table AND in either the imports or exports list, increment that
// item's `required` and stamp the 0x2 reserve bit. Then, per item with required>0,
// net the shortfall (GatherNetShortfall against reserved+stock), and if a viable
// seller exists pick it (item.sourceBuilding); items with no seller get required=0.
// Finally clamp the per-list purchase quantities to `budget` (GatherClampToBudget).
// `seller_for(itemId)` returns a non-null seller pointer when one is reachable.
// Returns the total budgeted purchase cost.
int GatherRequiredItems(const std::vector<GatherNeed>& needs,
                        const std::vector<u16>& imports,
                        const std::vector<u16>& exports,
                        std::vector<WsItem>& items, int budget,
                        void* (*seller_for)(u16 itemId));

// ---------------------------------------------------------------------------
// ReserveWorkstationItems orchestration (gilde.exe 0x45bd68)
// ---------------------------------------------------------------------------
// gilde.exe 0x45bd68 — recursively stamp the per-item reserve target + capacity
// flags down a station's input chain. For each of the 4 input slots of
// `stations[stationIdx]`:
//   * clamp the item's flags50 (+50 dword_B54480) to the station's reserveTarget;
//     set the item 0x2 reserve bit.
//   * special ids (449..454): set the 0x10 "headroom" flag when there is free
//     capacity and the chained production is below 4x the slot need.
//   * chained items (backIndex != -1): clamp the producer station's reserveTarget,
//     set its 0x2 bit, and recurse into it when it has capacity headroom.
//   * raw items (backIndex == -1) not yet reserved and short: consult the seller
//     query (reuse CheckWorkstationCapacity's AffordableSupply path) and stamp the
//     source. Mirrors the original's reserve walk; `needs`/`env` inject the leaf
//     reads. Returns 1 (the original's constant return).
int ReserveWorkstationItems(std::vector<WsStation>& stations, std::size_t stationIdx,
                            std::vector<WsItem>& items, const MeisterWsEnv& env,
                            const StockNeedQuery& needs);

} // namespace guild::ai
