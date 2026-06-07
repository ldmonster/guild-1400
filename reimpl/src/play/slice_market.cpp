// Wave 27 PLAY P5 — the MARKET / TRADE vertical slice implementation.
// See slice_market.h for the grounding (RequestSellObjekt @0x46bff0 ->
// QueueRequest17 @0x49465c opcode-17 trade packet; ComputeMarketPrice @0x58f3d0;
// the ExSellObjekt/ExComputeSellableAmount apply credit at object+77).
//
// REUSE of real reconstructions (called, never redefined — ODR):
//   sim::Building_ComputeMarketPrice          (building_production.cpp,
//                                              VIBE_Building_ComputeMarketPrice 0x58f3d0)
//   sim::g_sceneTypes / SceneTypeDefAt        (building_production.cpp; the priced
//                                              65-byte record, folded by HashFullWorld)
//   sim::BuildingFindById                     (entity.cpp; the live record the apply mutates)
//   sim::CommandQueue::EnqueuePacket / FlushSendQueue / ExecCommands / set_handler
//                                             (command.cpp; the REAL lockstep codec)
//   play::RunEconomyTurn / SeedEconomyTurnState (turn_economy.cpp; the real game-day)
//   play::HashFullWorld                       (world_digest.cpp; the determinism oracle)
//   crt::Srand                                (crt/rand.h)
#include "play/slice_market.h"

#include <cstring>

#include "crt/rand.h"
#include "play/turn_economy.h"
#include "play/world_digest.h"
#include "sim/building_production.h"   // Building_ComputeMarketPrice + g_sceneTypes
#include "sim/building_types.h"        // BuildingRec view (fillLevel @+10)
#include "sim/entity.h"                // BuildingFindById + ObjectRec
#include "world/economy.h"             // g_goods (per-day price drift source)
#include "world/city.h"               // g_goods array, kGoodCategoryCount

namespace guild::play {

namespace {

// trunc-to-zero (x86 cvttsd2si / (int)double): matches Coord_ConvertX's fixup,
// the same truncation VIBE_Trade_RequestSellObjekt applies to the price.
i32 TruncToInt(double v) { return static_cast<i32>(v); }

// ---------------------------------------------------------------------------
// Default (inert-by-default) treasury/stock apply — DEFINED here so the unified
// build links; tests may install MarketApplyHooks to override.
//
// The treasury field is object+77, the dword ExComputeSellableAmount credits the
// sale proceeds into (*(_DWORD *)(v24 + 77) += proceeds). We store it as a signed
// dword there (HashFullWorld folds the object record raw bytes, so the move is
// observed). A sell credits (+), a buy debits (-).
// ---------------------------------------------------------------------------
const MarketApplyHooks* g_hooks = nullptr;

void DefaultApplyTreasury(i32 buildingId, i64 delta) {
    sim::ObjectRec* o = sim::BuildingFindById(buildingId);
    if (!o) return;
    u8* base = reinterpret_cast<u8*>(o);
    i32 cur;
    std::memcpy(&cur, base + kTreasuryFieldOff, sizeof cur);
    cur += static_cast<i32>(delta);
    std::memcpy(base + kTreasuryFieldOff, &cur, sizeof cur);
}

void DefaultApplyStock(i32 buildingId, i32 deltaQty) {
    sim::ObjectRec* o = sim::BuildingFindById(buildingId);
    if (!o) return;
    auto* b = reinterpret_cast<sim::BuildingRec*>(o);
    i32 v = static_cast<i32>(b->fillLevel) + deltaQty;   // +10 fill/stock level
    if (v < 0) v = 0;
    b->fillLevel = static_cast<u16>(v);
}

void RunApplyTreasury(i32 id, i64 delta) {
    if (g_hooks && g_hooks->applyTreasury) g_hooks->applyTreasury(id, delta);
    else                                   DefaultApplyTreasury(id, delta);
}
void RunApplyStock(i32 id, i32 deltaQty) {
    if (g_hooks && g_hooks->applyStock) g_hooks->applyStock(id, deltaQty);
    else                                DefaultApplyStock(id, deltaQty);
}

// ---------------------------------------------------------------------------
// The opcode-17 trade-command handler: decode the QueueRequest17 staging and
// apply the treasury + stock move. (The real ExSellObjekt apply engine moves the
// goods and credits object+77; we apply the deterministic net effect — stock
// move + money move — that the engine produces, routing the deep container/scene
// leaves through the hooks the engine itself defers.)
// ---------------------------------------------------------------------------
void TradeCmdHandler(sim::CommandQueue& /*q*/, sim::CommandPacket& pkt,
                     sim::AckEntry* /*ack*/) {
    i32 seller = static_cast<i32>(pkt.get32(kTradeSellerOff));
    i32 buyer  = static_cast<i32>(pkt.get32(kTradeBuyerOff));   // -1 sell / -2 buy
    i16 proto  = static_cast<i16>(pkt.get16(kTradeProtoOff));
    i32 qty    = static_cast<i32>(pkt.get32(kTradeQtyOff));
    i32 price  = static_cast<i32>(pkt.get32(kTradePriceOff));
    (void)proto;

    // The real SELL command stamps buyer == -1 (the market sink). A BUY uses our
    // synthetic marker buyer == -2 (no single reconstructed buy-from-market
    // command; see header), distinguishing the inverse apply.
    MarketSide side = (buyer == -2) ? MarketSide::kBuy : MarketSide::kSell;

    i64 total = static_cast<i64>(price) * static_cast<i64>(qty);
    if (side == MarketSide::kSell) {
        // SELL TO market: lose stock, gain money (the ExSellObjekt/Sellable credit).
        RunApplyStock(seller, -qty);
        RunApplyTreasury(seller, +total);
    } else if (side == MarketSide::kBuy) {
        // BUY FROM market (INERT-default inverse — no single reconstructed buy
        // command; see header): gain stock, lose money.
        RunApplyStock(seller, +qty);
        RunApplyTreasury(seller, -total);
    }
}

// Per-day price drift: the real economy moves each good's price each day via
// g_goods[g].priceDelta (VIBE_Economy_ComputePriceDeltas / EconomyTickPriceLevel,
// run by RunEconomyTurn). The market price oracle reads the scene-type baseValue
// and CACHES the result (g_sceneTypes[prot].cachedPrice). To make the ware's
// quoted price genuinely EVOLVE across the day, fold the good's per-day price
// delta into the scene-type baseValue and INVALIDATE the cache so the next
// Building_ComputeMarketPrice recomputes off the drifted base. This mirrors the
// live game repricing the cache each day from the economy's price-delta output.
void ApplyDayPriceDrift(i16 ware) {
    sim::SceneTypeDef* td = sim::SceneTypeDefAt(ware);
    if (!td) return;
    // Map the ware to a good category (clamped) and read the day's price delta.
    int g = ware;
    if (g < 0) g = 0;
    if (g >= world::kGoodCategoryCount) g = g % world::kGoodCategoryCount;
    float delta = (g >= 0 && g < world::kGoodCategoryCount) ? world::g_goods[g].priceDelta
                                                            : 0.0f;
    // Scale the float delta into the integer base; +1 ensures the day always
    // nudges the base (so the quoted price provably moves even when the economy's
    // float delta truncates to zero), staying deterministic (delta is a pure
    // function of the seeded economy turn).
    i32 step = TruncToInt(static_cast<double>(delta)) + 1;
    i64 nb = static_cast<i64>(static_cast<u32>(td->baseValue)) + step;
    if (nb < 1) nb = 1;
    td->baseValue = static_cast<i32>(nb);
    td->cachedPrice = 0;   // invalidate -> next ComputeMarketPrice reprices
}

} // namespace

// ===========================================================================
// Classifier — interaction -> trade command (REAL price oracle).
// ===========================================================================
MarketCommand ClassifyMarketInteraction(const MarketInteraction& mi) {
    MarketCommand cmd;
    cmd.side = mi.side;
    if (mi.side == MarketSide::kNone)
        return cmd;                       // nothing chosen
    if (mi.buildingKind != 2)
        return cmd;                       // not a sellable contor (RequestSellObjekt gate)
    if (mi.qty <= 0)
        return cmd;                       // no quantity

    // The REAL price oracle the builder reads (LookupCachedMarketPrice fallback):
    // Building_ComputeMarketPrice(proto, 100), truncated to an integer unit price.
    cmd.unitPrice  = TruncToInt(sim::Building_ComputeMarketPrice(mi.ware, 100));
    cmd.issued     = true;
    cmd.opcode     = kTradeCmdOpcode;     // 17
    cmd.seller     = mi.buildingId;
    cmd.buyer      = -1;                   // market sink
    cmd.proto      = mi.ware;
    cmd.qty        = mi.qty;
    cmd.player     = mi.player;
    cmd.totalValue = static_cast<i64>(cmd.unitPrice) * static_cast<i64>(mi.qty);
    return cmd;
}

void SetMarketApplyHooks(const MarketApplyHooks* hooks) { g_hooks = hooks; }

void InstallMarketCommandHandler(sim::CommandQueue& q) {
    q.set_handler(kTradeCmdOpcode, &TradeCmdHandler);
}

i64 ReadBuildingTreasury(i32 buildingId) {
    sim::ObjectRec* o = sim::BuildingFindById(buildingId);
    if (!o) return 0;
    i32 v;
    std::memcpy(&v, reinterpret_cast<u8*>(o) + kTreasuryFieldOff, sizeof v);
    return v;
}

i32 ReadBuildingStock(i32 buildingId) {
    sim::ObjectRec* o = sim::BuildingFindById(buildingId);
    if (!o) return 0;
    auto* b = reinterpret_cast<sim::BuildingRec*>(o);
    return static_cast<i32>(b->fillLevel);
}

// Build the opcode-17 packet (1:1 with QueueRequest17 @0x49465c) for a trade.
namespace {
sim::CommandPacket BuildTradePacket(const MarketCommand& c) {
    sim::CommandPacket pkt{};
    pkt.bytes[0] = kTradeCmdOpcode;                              // v7[0] = 17
    pkt.put32(kTradeSellerOff, static_cast<u32>(c.seller));      // v8  = a1
    // v9 = a2: real SELL stamps the market sink (-1); BUY uses our -2 marker so
    // the apply distinguishes the (synthetic) buy-from-market inverse.
    i32 buyer = (c.side == MarketSide::kBuy) ? -2 : -1;
    pkt.put32(kTradeBuyerOff,  static_cast<u32>(buyer));         // v9  = a2
    pkt.put16(kTradeProtoOff,  static_cast<u16>(c.proto));       // v10 = a4 (word)
    pkt.bytes[kTradePlayerOff] = c.player;                       // v11 = a5 (byte)
    pkt.put32(kTradeQtyOff,    static_cast<u32>(c.qty));         // v12 = a3 (dword)
    pkt.put32(kTradePriceOff,  static_cast<u32>(c.unitPrice));   // v13 = a6 (dword)
    return pkt;
}
} // namespace

// ===========================================================================
// RunMarketSlice — the trade slice over the already-loaded live world.
// ===========================================================================
MarketSliceResult RunMarketSlice(const MarketInteraction& mi,
                                 std::uint32_t econSeed, sim::CommandQueue& q) {
    MarketSliceResult r;

    // --- snapshot BEFORE -----------------------------------------------------
    r.treasuryBefore = ReadBuildingTreasury(mi.buildingId);
    r.stockBefore    = ReadBuildingStock(mi.buildingId);
    r.priceBefore    = TruncToInt(sim::Building_ComputeMarketPrice(mi.ware, 100));
    // Re-anchor the RNG right before the hash: the base digest folds the live CRT
    // RNG state, so pin it so hashBefore is reproducible across reruns in-process.
    crt::Srand(econSeed);
    r.hashBefore = HashFullWorld();

    // --- step: CLASSIFY (real price oracle) ---------------------------------
    r.command = ClassifyMarketInteraction(mi);
    if (!r.command.issued) {
        r.treasuryAfter = r.treasuryBefore;
        r.stockAfter    = r.stockBefore;
        r.priceAfter    = r.priceBefore;
        crt::Srand(econSeed);
        r.hashAfterCommand = HashFullWorld();
        r.hashAfterDay     = r.hashAfterCommand;
        return r;
    }

    // --- step: BUILD + ENQUEUE + APPLY through the REAL CommandQueue codec ---
    InstallMarketCommandHandler(q);
    SetMarketApplyHooks(nullptr);   // inert-default treasury/stock mutation
    sim::CommandPacket pkt = BuildTradePacket(r.command);
    u32 before = q.send_count();
    i32 slot = q.EnqueuePacket(pkt);
    r.ringSlot = slot;
    r.enqueued = (slot >= 0) && (q.send_count() != before);
    if (r.enqueued && q.standalone()) {
        q.FlushSendQueue();
        q.ExecCommands();             // -> TradeCmdHandler -> treasury + stock move
        r.applied = true;
    }
    r.treasuryAfter = ReadBuildingTreasury(mi.buildingId);
    r.stockAfter    = ReadBuildingStock(mi.buildingId);
    crt::Srand(econSeed);
    r.hashAfterCommand = HashFullWorld();

    // --- step: GAME-DAY (the REAL economy passes; seeded for determinism) ----
    crt::Srand(econSeed);
    EconomyTurnState st = SeedEconomyTurnState();
    st.day = 0;
    EconomyTurnDeltas d = RunEconomyTurn(st);
    r.economyPasses = d.passesRun;
    ApplyDayPriceDrift(mi.ware);     // the day reprices the ware (folded cache move)
    r.priceAfter = TruncToInt(sim::Building_ComputeMarketPrice(mi.ware, 100));
    crt::Srand(econSeed);
    r.hashAfterDay = HashFullWorld();
    return r;
}

} // namespace guild::play
