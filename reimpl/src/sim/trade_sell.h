#pragma once
// ===========================================================================
// trade_sell.{h,cpp} — the player SELL command + the sell/produce inventory
// engines (gilde.exe). MODULE: the player Trade command side (guild::sim).
// ===========================================================================
//
// This file ports the player-driven "sell an object" path and the two inventory
// engines the lockstep dispatcher runs to apply it:
//
//   * VIBE_Trade_RequestSellObjekt    0x46bff0 — the player command BUILDER. It
//     looks up the per-unit market price (cached price table -> ComputeMarketPrice
//     fallback) for the object's prototype and emits cm_RequestSellObjekt via
//     VIBE_Command_QueueRequest17(seller, -1, qty, proto, player, unitPrice).
//
//   * VIBE_Command_ExSellObjekt       0x496b90 — the sell APPLY engine: resolve
//     source + destination containers, validate source stock (effective-stock /
//     raw-count), validate destination capacity (ComputeFreeCapacity for storage
//     goods, ComputeCarryCapacity for carried goods — which CLAMPS the qty),
//     then move the goods (remove from source, add to destination).
//
//   * VIBE_Command_ExComputeSellableAmount 0x497538 — the produce-and-sell APPLY
//     engine: walk a recipe's up-to-4 ingredient slots, compute the producible
//     amount qty = min over slots of effectiveStock/ratio, clamp by the output
//     slot's free capacity, then consume ingredients, emit the output stack, and
//     credit the proceeds (qty * outputCount * marketPrice).
//
// The engines' deep leaves — storage-room alloc/remove (Building_AllocStorageRoom
// / RemoveStorageRoom), the HUD status banner + voice sample, and the scene-tree
// AddObjekt/RemoveObjekt mutations — are routed through a mockable command hook
// (the originals route through the lockstep + render clusters). The DETERMINISTIC
// resolution (qty / price / capacity) is translated 1:1 and golden-testable.
//
// Reuses: inventory_capacity (ComputeFreeCapacity / ComputeCarryCapacity / the
// capacity table + reserve rule) and building_production (Building_ComputeMarketPrice).
#include <vector>

#include "guild/common/types.h"
#include "sim/inventory_capacity.h"
#include "sim/types.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// Market-price resolver. The builder reads VIBE_Building_LookupCachedMarketPrice
// (a runtime cache table, dword_13C3B5C) which falls back to ComputeMarketPrice.
// We inject the unit price so the arithmetic is host/test-controlled; the host
// wires a backend wrapping Building_ComputeMarketPrice(prot, 100) (the engines
// pass qty 100 == 0x64). With no hook the price is 0 (no trade).
//   unitPrice = trunc( marketPrice(prot, player) )
// ---------------------------------------------------------------------------
using MarketPriceFn = double (*)(i16 prot, u8 player);
void TradeSetMarketPriceHook(MarketPriceFn fn);
double TradeMarketPrice(i16 prot, u8 player);  // resolves via hook or default

// ---------------------------------------------------------------------------
// Command hook: every committed transfer / credit / sell-request is emitted as a
// TradeCommand (the originals call VIBE_Command_QueueRequest17 / AddObjekt /
// RemoveObjekt). Tests capture them; nullptr applies the modeled mutation only.
// ---------------------------------------------------------------------------
enum class TradeCmd {
    kRequestSell,   // cm_RequestSellObjekt(seller, -1, qty, proto, player, price)
    kRemoveSource,  // remove qty of proto from the source container
    kAddDest,       // add qty of proto to the destination container
    kCredit,        // credit `amount` currency to the seller
};
struct TradeCommand {
    TradeCmd cmd;
    i32 seller = 0;
    i32 buyer = 0;     // -1 == market sink
    i32 qty = 0;
    i16 proto = 0;
    u8  player = 0;
    i32 amount = 0;    // price (kRequestSell) / credited currency (kCredit)
};
using TradeCmdHook = void (*)(const TradeCommand& cmd);
void TradeSetCmdHook(TradeCmdHook hook);
void TradeEmit(const TradeCommand& cmd);

// ===========================================================================
// gilde.exe 0x46bff0 — VIBE_Trade_RequestSellObjekt (the player command BUILDER).
//   building = Building_FindById(req+4); if (!building || *building != 2) return 0;
//   price = trunc( LookupCachedMarketPrice(proto, player) );
//   QueueRequest17(building.id, -1, qty, proto, player, price);  return 14;
// `buildingKind` is *building (must be 2 to sell); `buildingId` is building+1.
// Returns 14 (the opcode/ack token) on success, 0 if the building is not a
// sellable contor (kind 2) / not found.
struct SellRequest {
    i32 buildingId = 0;   // building+1
    u8  buildingKind = 0; // *building (2 == sellable contor)
    bool buildingFound = true;
    i16 proto = 0;        // HIWORD(slot+2): the good prototype
    i32 qty = 0;          // slot+8: amount to sell
    u8  player = 0;       // byte_6477A1
};
int TradeRequestSellObjekt(const SellRequest& req, bool commit);

// ===========================================================================
// gilde.exe 0x496b90 — VIBE_Command_ExSellObjekt (the sell APPLY engine, core).
//
// The deterministic transfer resolution: validate the source has the goods,
// validate the destination has room, and (for carried goods) clamp the moved
// quantity by the carry capacity. Containers are modeled as ContainerView
// (inventory_capacity.h); the engine reads:
//   srcStock     : the source stock child for `proto` (its effective stock +
//                  raw count) — both must be >= qty.
//   isReserveGood: *src in {42,278,475,476,477} (drives the effective-stock gate)
//   destCarried  : true when the destination is a carried-good slot on a person
//                  (no storage building / no scene container) — capacity comes
//                  from ComputeCarryCapacity and CLAMPS the moved qty.
//   destStorage  : true when the destination is a storage good (42/278/477) — the
//                  free capacity (ComputeFreeCapacity) must be >= qty else reject.
//
// Returns the moved quantity (0 == rejected). On a nonzero result with commit it
// emits kRemoveSource + kAddDest. The storage-room alloc/remove + HUD/voice
// leaves are DEFERRED (the engine's record mutations beyond the count transfer).
struct SellResolve {
    i16 proto = 0;
    i32 qty = 0;           // requested amount (a1+31)
    // Source side
    bool srcResolved = true;
    bool srcHasStock = true;  // a source stock child exists
    i16  srcType = 0;         // *srcStock (reserve-good detection)
    i32  srcEffectiveStock = 0;  // GetEffectiveStock(src) — gate for reserve goods
    i32  srcRawCount = 0;        // *(src+14) — raw count gate (always checked)
    bool srcIsReserveGood = false;  // *src in {42,278,475,476,477}
    // Destination side
    bool destResolved = true;
    bool destCarried = false;     // person carried-good slot (carry-capacity path)
    bool destStorage = false;     // storage good (free-capacity path)
    // Destination capacity inputs (used per path):
    ContainerView destContainer;  // dest container view (children + fill bytes)
    bool destRootResolved = true; // ComputeFreeCapacity root guard
    StockChild destStockRec;      // the dest capacity record (type/level)
    // Carry-capacity inputs (destCarried path):
    u8   personKind = 0; bool hasAvatar = false;
    int  typeCategory = 0; bool carriable = true;
    // --- storage-node phase context (0x496b90 node mutations; consumed by the
    //     installed SellStoragePhase — see buildingtype_callers.h, which owns
    //     the 1:1 phases).  Null/default values keep each phase step inert. ---
    bool srcIdPresent  = true;        // *(a1+20) != -1 (source-phase gate)
    bool destIdPresent = true;        // *(a1+16) != -1 (dest-phase gate)
    i32  destId = -1;                 // *(a1+16) (AddObjekt parent id)
    u8*  srcStockNodePtr  = nullptr;  // v56 — source stock node (count dword @+14)
    u8*  destStockNodePtr = nullptr;  // v55 — existing dest stock node (null => ensure)
    u8*  srcBuildingRec   = nullptr;  // v52 — source building record (RemoveStorageRoom)
    u8*  destBuildingRec  = nullptr;  // v50 — dest building record (AllocStorageRoom)
    u8*  srcStorageRec    = nullptr;  // v4  — source storage container (parent arg)
    u8*  destStorageRec   = nullptr;  // v57 — dest storage container (count-bump gate)
    u8*  srcChildList     = nullptr;  // v54 — source child-list field (RemoveByProt)
    const u8* destOwnerRec = nullptr; // v3  — dest owner record (+2 kind byte)
    i32  lastDestNodeId = 0;          // out: *(destNode+2) (the dword_631290 latch)
};

// ---------------------------------------------------------------------------
// Storage-node phase backend — the 0x496b90 source/dest stock-node mutations
// trade_sell had deferred (storage-room alloc/remove, dest node ensure).  The
// 1:1 phases live in buildingtype_callers.{h,cpp} (SellStoragePhase1to1) and
// are installed by WireBuildingCallers().  The default base instance is inert
// (both phases succeed without touching anything — the pre-wiring behavior).
// ---------------------------------------------------------------------------
struct SellStoragePhase {
    virtual ~SellStoragePhase() = default;
    // 0x496ec2..0x496f47: source node count decrement + depleted-node removal.
    // Returns false on the original's `return 1` reject paths.
    virtual bool SourcePhase(SellResolve& r, i32 qty) { (void)r; (void)qty; return true; }
    // 0x496f4b..0x4974ce + LABEL_58: dest node ensure / count increment + latch.
    virtual bool DestPhase(SellResolve& r, i32 qty) { (void)r; (void)qty; return true; }
};
void TradeSetStoragePhase(SellStoragePhase* phase);   // nullptr => inert default
SellStoragePhase* TradeStoragePhase();

// Resolves and (optionally) commits the transfer; returns the moved quantity.
i32 TradeSellObjektResolve(SellResolve& r, bool commit);

// ===========================================================================
// gilde.exe 0x497538 — VIBE_Command_ExComputeSellableAmount (produce-and-sell).
//
// A recipe has up-to-4 ingredient slots; each slot i has (proto_i, ratio_i). The
// producible amount is qty = min over present slots of effectiveStock_i/ratio_i
// (a slot with proto present but stock 0 forces qty 0). The output slot then
// caps qty by ComputeFreeCapacity(outProto)/outputCount. On a nonzero qty:
//   - consume ratio_i*qty of each ingredient,
//   - add outputCount*qty of the output good to the container,
//   - credit trunc(marketPrice(outProto,100) * (outputCount*qty)).
struct RecipeSlot {
    i16 proto = 0;       // ingredient prototype (0 == empty slot)
    int ratio = 1;       // units consumed per produced unit (slot[19])
    i32 effStock = 0;    // effectiveStock of the ingredient in the container
};
struct SellableResolve {
    bool sourceResolved = true;   // ResolveEntityById + type in {42,278}
    bool ownerResolved = true;    // ResolveOwnerOrParentB != 0
    i32 startQty = 0;             // *(a1+22): the initial cap (v4)
    RecipeSlot ingredients[4];    // the up-to-4 ingredient slots
    i16 outProto = 0;             // the produced good prototype (a1+18 >> 16)
    int outputCount = 1;          // units produced per craft (slot+54 word)
    // Output capacity:
    ContainerView outContainer;   // the container the output lands in
    bool outRootResolved = true;
    StockChild outStockRec;       // dest capacity record (type/level)
    u8 player = 0;                // market-price player arg (byte_6477A1)
};
struct SellableResult {
    i32 produced = 0;   // v15: the produced/sellable amount
    i32 proceeds = 0;   // trunc(marketPrice * produced*outputCount)
    bool ok = false;    // false == rejected (return 1 in the original)
};
SellableResult TradeComputeSellableAmount(SellableResolve& r, bool commit);

}  // namespace guild::sim
