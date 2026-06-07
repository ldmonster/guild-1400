#pragma once
// Wave 27 PLAY P5 — the MARKET / TRADE vertical slice: a faithful
// click -> market dialog/HUD -> REAL trade command -> sim effect -> render
// slice for the buy/sell-goods system (namespace guild::play).
//
// It mirrors the playable_slice / interact_building pattern: resolve a market
// interaction, classify it, emit the REAL trade command packet, apply it
// (mutating treasury + a building's goods stock + the folded market-price cache),
// then advance ONE game-day and prove price/stock/treasury evolved while the
// whole run stays deterministic (HashFullWorld differs pre/post and is
// byte-identical on rerun).
//
// GROUNDING (decompiled this wave — the live player buy/sell-goods path):
//
//   * VIBE_Trade_RequestSellObjekt @0x46bff0 is the player SELL command BUILDER:
//        v3 = Building_FindById(req+4);
//        if (!v3 || *building != 2) return 0;            // must be a kind-2 contor
//        price = (int)LookupCachedMarketPrice(proto, player);  // truncated
//        QueueRequest17(building.id, -1, qty, proto, player, price);  return 14;
//     Wire opcode = 17 (VIBE_Command_QueueRequest17 @0x49465c): packet bytes[0]=17,
//     then +0x10 seller, +0x14 buyer(-1), +0x18(word) proto, +0x1E(byte) player,
//     +0x1F(dword) qty, +0x23(dword) price.
//
//   * VIBE_Building_ComputeMarketPrice @0x58f3d0 is the price oracle the builder
//     reads (via the LookupCachedMarketPrice cache + this fallback). It prices a
//     good off the 65-byte scene-type record (g_sceneTypes, dword_13CE27C) and
//     WRITES the un-currency-scaled base into record+56 (cachedPrice) on first
//     compute — a HashFullWorld-folded mutation. Reconstructed as
//     sim::Building_ComputeMarketPrice(prot, qty) (building_production.cpp).
//
//   * VIBE_Command_ExSellObjekt @0x496b90 / VIBE_Command_ExComputeSellableAmount
//     @0x497538 are the sell APPLY engines: resolve source/destination containers,
//     validate stock + capacity, move the goods, and CREDIT the proceeds
//     (qty * marketPrice) into the seller's money field (object record +77) and
//     the family wealth (+9). Reconstructed (deterministic core, leaves hooked) as
//     sim::TradeSellObjektResolve / sim::TradeComputeSellableAmount (trade_sell.cpp).
//
// WIRED REAL siblings: sim::Building_ComputeMarketPrice (price), the opcode-17
// QueueRequest17 packet layout + the REAL sim::CommandQueue codec
// (EnqueuePacket/FlushSendQueue/ExecCommands), sim::BuildingFindById (the live
// record the apply mutates), play::RunEconomyTurn (the real game-day cascade),
// play::HashFullWorld (the determinism oracle).
//
// INERT-default GAPS (declared honestly): the BUY-from-market apply (treasury
// DEBIT + stock GAIN) is modeled as the inverse of the reconstructed SELL credit
// — there is no single reconstructed buy-from-market command in the binary (the
// buy paths are GUI dialog loops, WineCellar_ShowBuyDialog @0x519b14 etc., that
// dispatch the same QueueRequest channel). And the seller "treasury" is stored in
// a folded object-record pad dword (object+77, the field ExComputeSellableAmount
// credits) so HashFullWorld observes it without the live Person/family tree. Both
// are exposed as installable MarketApplyHooks with inert-by-default reconstructions
// DEFINED IN slice_market.cpp (the build model), overridable by tests.
//
// Additive: no edits to trade_sell.cpp / building_production.cpp / command.cpp /
// turn_economy.cpp / playable_slice.cpp / wiring.cpp.
#include <cstdint>

#include "guild/common/types.h"
#include "sim/command.h"

namespace guild::play {

// ===========================================================================
// The market interaction the slice replays: buy or sell `qty` of ware `ware`
// at the market building `buildingId` for `player`.
// ===========================================================================
enum class MarketSide {
    kNone = 0,
    kBuy  = 1,   // buy goods FROM the market: stock += qty, treasury -= price*qty
    kSell = 2,   // sell goods TO the market:   stock -= qty, treasury += price*qty
};

struct MarketInteraction {
    MarketSide side       = MarketSide::kNone;
    i16        ware       = 0;     // the good prototype (the scene-type id priced)
    i32        qty        = 0;     // amount to trade
    i32        buildingId = 0;     // the market/contor building (live g_objects id)
    u8         buildingKind = 2;   // *building (must be 2 to trade — the contor gate)
    u8         player     = 0;     // byte_6477A1 (the acting player slot)
};

// ===========================================================================
// The classified trade COMMAND (the golden: interaction -> wire command). 1:1
// with VIBE_Trade_RequestSellObjekt's QueueRequest17 emission.
// ===========================================================================
inline constexpr u8 kTradeCmdOpcode = 17;        // VIBE_Command_QueueRequest17

// QueueRequest17 packet-staging offsets (recovered @0x49465c):
inline constexpr u32 kTradeSellerOff = 0x10;     // v8  = seller building id (a1)
inline constexpr u32 kTradeBuyerOff  = 0x14;     // v9  = buyer / -1         (a2)
inline constexpr u32 kTradeProtoOff  = 0x18;     // v10 = proto (word)       (a4)
inline constexpr u32 kTradePlayerOff = 0x1E;     // v11 = player (byte)      (a5)
inline constexpr u32 kTradeQtyOff    = 0x1F;     // v12 = qty (dword)        (a3)
inline constexpr u32 kTradePriceOff  = 0x23;     // v13 = unit price (dword) (a6)

// Object-record field ExComputeSellableAmount credits the proceeds into
// (*(_DWORD *)(v24 + 77) += proceeds): the seller's money/treasury field. Lives in
// the 169-byte object record's pad span, folded by HashFullWorld.
inline constexpr u32 kTreasuryFieldOff = 77;     // object+77 (the credited money)

struct MarketCommand {
    bool       issued    = false;  // a valid trade results (contor kind 2, qty > 0)
    u8         opcode     = 0;      // kTradeCmdOpcode (17) when issued
    MarketSide side       = MarketSide::kNone;
    i32        seller     = 0;      // building id (a1)
    i32        buyer      = -1;     // -1 == market sink (a2)
    i16        proto      = 0;      // good prototype
    i32        qty        = 0;
    u8         player     = 0;
    i32        unitPrice  = 0;      // trunc(Building_ComputeMarketPrice(proto,100))
    i64        totalValue = 0;      // unitPrice * qty (the treasury delta magnitude)
};

// 1:1 classifier: resolve a MarketInteraction into its MarketCommand. The unit
// price comes from the REAL price oracle sim::Building_ComputeMarketPrice(ware,100)
// (the builder's LookupCachedMarketPrice fallback). A non-trade (no side / kind != 2
// / qty <= 0) yields {issued=false}.
MarketCommand ClassifyMarketInteraction(const MarketInteraction& mi);

// ===========================================================================
// Market apply hooks — the leaves not covered by a single reconstructed sibling.
// Installable with inert-by-default reconstructions defined in slice_market.cpp.
// ===========================================================================
struct MarketApplyHooks {
    // The treasury credit/debit: signed money delta into the building's record
    // (object+77, the field ExComputeSellableAmount credits). Default mutates the
    // live record via sim::BuildingFindById.
    void (*applyTreasury)(i32 buildingId, i64 delta) = nullptr;
    // The stock move: signed stock delta into the building's fill level (object+10,
    // BuildingRec::fillLevel). Default mutates the live record (clamped at 0).
    void (*applyStock)(i32 buildingId, i32 deltaQty) = nullptr;
};
void SetMarketApplyHooks(const MarketApplyHooks* hooks);

// Install the opcode-17 trade-command handler on `q` (decodes the QueueRequest17
// staging and applies the treasury + stock mutation). Idempotent per queue.
void InstallMarketCommandHandler(sim::CommandQueue& q);

// Read the current treasury (object+77) of a live building (0 if not found).
i64 ReadBuildingTreasury(i32 buildingId);
// Read the current stock (object+10 fillLevel) of a live building (0 if not found).
i32 ReadBuildingStock(i32 buildingId);

// ===========================================================================
// The result of one market click -> command -> apply -> game-day cycle.
// ===========================================================================
struct MarketSliceResult {
    // --- the classified + emitted command ---
    MarketCommand command{};
    bool   enqueued = false;       // a packet hit the send ring
    i32    ringSlot = -1;          // EnqueuePacket ring slot
    bool   applied  = false;       // FlushSendQueue + ExecCommands ran

    // --- before / after the trade (the sim effect) ---
    i64    treasuryBefore = 0;
    i64    treasuryAfter  = 0;     // after the trade applied
    i32    stockBefore    = 0;
    i32    stockAfter     = 0;     // after the trade applied
    i32    priceBefore    = 0;     // Building_ComputeMarketPrice(ware,100) pre-trade
    i32    priceAfter     = 0;     // ... after the game-day repriced the cache

    // --- the game-day ---
    int    economyPasses  = 0;

    // --- determinism oracle (the three world hashes, fold order = loop order) ---
    std::uint64_t hashBefore       = 0;  // HashFullWorld() before the trade
    std::uint64_t hashAfterCommand = 0;  // ... after the trade applied
    std::uint64_t hashAfterDay     = 0;  // ... after the game-day

    bool tradeChangedWorld() const { return hashBefore != hashAfterCommand; }
    bool dayChangedWorld()   const { return hashAfterCommand != hashAfterDay; }
    bool treasuryMoved()     const { return treasuryBefore != treasuryAfter; }
    bool stockMoved()        const { return stockBefore != stockAfter; }
};

// ===========================================================================
// RunMarketSlice — the trade slice over the ALREADY-LOADED live world.
//
//   1. snapshot treasury/stock/price + HashFullWorld() (before),
//   2. classify the interaction -> MarketCommand (real price oracle),
//   3. build the opcode-17 packet, enqueue through the REAL sim::CommandQueue,
//      flush + exec so the apply handler mutates treasury + stock + price cache,
//   4. HashFullWorld() (after command),
//   5. advance ONE game-day via play::RunEconomyTurn (seeded by `econSeed`) +
//      reprice the ware (Building_ComputeMarketPrice repopulates / drifts the cache),
//   6. HashFullWorld() (after day), snapshot treasury/stock/price (after).
//
// The caller has populated the live sim arrays (synthetic seed or io::LoadWorld)
// and seeded g_sceneTypes for the traded ware (so the price oracle returns a real
// value). `q` is a standalone CommandQueue (Init()'d). Returns the run result.
MarketSliceResult RunMarketSlice(const MarketInteraction& mi,
                                 std::uint32_t econSeed, sim::CommandQueue& q);

} // namespace guild::play
