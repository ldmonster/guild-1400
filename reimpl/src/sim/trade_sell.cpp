#include "sim/trade_sell.h"

#include <cmath>

// Faithful 1:1 port of the player sell command builder and the two inventory
// sell/produce APPLY engines from gilde.exe. The deterministic resolution
// (quantity / price / capacity) is translated verbatim from the pseudocode; the
// storage-room alloc/remove + HUD/voice render leaves are routed through the
// command hook (and listed DEFERRED in the module report). Provenance addresses
// on each function.

namespace guild::sim {

// ---------------------------------------------------------------------------
// Market-price + command hooks.
// ---------------------------------------------------------------------------
static MarketPriceFn g_priceHook = nullptr;
void TradeSetMarketPriceHook(MarketPriceFn fn) { g_priceHook = fn; }
double TradeMarketPrice(i16 prot, u8 player) {
    if (g_priceHook)
        return g_priceHook(prot, player);
    // No hook installed: the host wires a backend wrapping
    // Building_ComputeMarketPrice(prot, 100) (the engines pass qty 100 == 0x64);
    // see TradeSetMarketPriceHook. Without a backend the price is 0 (no trade).
    (void)prot;
    (void)player;
    return 0.0;
}

static TradeCmdHook g_cmdHook = nullptr;
void TradeSetCmdHook(TradeCmdHook hook) { g_cmdHook = hook; }
void TradeEmit(const TradeCommand& cmd) {
    if (g_cmdHook)
        g_cmdHook(cmd);
}

// ---------------------------------------------------------------------------
// Storage-node phase backend (0x496b90 node mutations).  Inert base instance by
// default; WireBuildingCallers() (buildingtype_callers.cpp) installs the 1:1
// phases (SellStoragePhase1to1).
// ---------------------------------------------------------------------------
static SellStoragePhase g_inertStoragePhase;
static SellStoragePhase* g_storagePhase = &g_inertStoragePhase;
void TradeSetStoragePhase(SellStoragePhase* phase) {
    g_storagePhase = phase ? phase : &g_inertStoragePhase;
}
SellStoragePhase* TradeStoragePhase() { return g_storagePhase; }

// trunc-to-zero (x86 cvttsd2si / (int)double): matches Coord_ConvertX's fixup.
static i32 TruncToInt(double v) { return static_cast<i32>(v); }

// ===========================================================================
// VIBE_Trade_RequestSellObjekt  0x46bff0
//   v3 = Building_FindById(req+4);
//   if (!v3 || *building != 2) return 0;
//   price = (int) LookupCachedMarketPrice(proto, player);     // truncated
//   Sprintf("cm_RequestSellObjekt(%i,%i,%i,%i,%i,%i)", id,-1,qty,proto,player,price);
//   QueueRequest17(id, -1, qty, proto, player, (int64)price);
//   return 14;
// (The original computes the price twice — once for the log Sprintf, once for the
// QueueRequest17 — both LookupCachedMarketPrice(proto, player). We compute once.)
// ===========================================================================
int TradeRequestSellObjekt(const SellRequest& req, bool commit) {
    if (!req.buildingFound || req.buildingKind != 2)
        return 0;
    i32 price = TruncToInt(TradeMarketPrice(req.proto, req.player));
    if (commit) {
        TradeCommand c{};
        c.cmd = TradeCmd::kRequestSell;
        c.seller = req.buildingId;
        c.buyer = -1;
        c.qty = req.qty;
        c.proto = req.proto;
        c.player = req.player;
        c.amount = price;
        TradeEmit(c);
    }
    return 14;
}

// ===========================================================================
// VIBE_Command_ExSellObjekt  0x496b90 (deterministic transfer core).
//
// Source gate (when src is a reserve/477 good, v4):
//     EffectiveStock(src) >= qty   else reject ("Not enough Objekts at source").
// Source gate (always, v56 == the resolved source stack):
//     *(src+14) (raw count) >= qty else reject.
// Destination gate:
//   storage good (v57 / *dst==278 / *dst==477):
//     ComputeFreeCapacity(dst, proto, dst, qty) >= qty else reject (return 1).
//   carried good (no building dst v50, no storage v57, no scene v49):
//     qty = ComputeCarryCapacity(dst, proto, qty); if (!qty) reject.
// Transfer: *(src+14) -= qty;  dest gains qty (AddObjekt / AddObjektToParent).
// ===========================================================================
i32 TradeSellObjektResolve(SellResolve& r, bool commit) {
    if (!r.srcResolved || !r.destResolved)
        return 0;

    i32 qty = r.qty;
    if (qty <= 0)
        return 0;

    // --- source validation --------------------------------------------------
    if (!r.srcHasStock)
        return 0;
    if (r.srcIsReserveGood) {
        if (r.srcEffectiveStock < qty)
            return 0;  // "Not enough Objekts at source"
    }
    if (qty > r.srcRawCount)
        return 0;      // "Not enough Objekts at source" (raw count)

    // --- destination capacity ----------------------------------------------
    if (r.destStorage) {
        int free = InventoryComputeFreeCapacity(r.destStockRec, r.destContainer,
                                                r.proto, qty, r.destRootResolved);
        if (free < qty)
            return 0;  // reject
    } else if (r.destCarried) {
        qty = InventoryComputeCarryCapacity(r.destContainer, r.proto, qty,
                                            r.personKind, r.hasAvatar,
                                            r.typeCategory, r.carriable);
        if (qty == 0)
            return 0;  // reject
    }

    // --- commit the transfer (record count move; leaves are hooked) ---------
    if (commit) {
        TradeCommand rem{};
        rem.cmd = TradeCmd::kRemoveSource;
        rem.qty = qty;
        rem.proto = r.proto;
        TradeEmit(rem);

        // 0x496ec2..0x496f47: source stock-node decrement + depleted-node
        // removal.  The original rejects (return 1) AFTER the source count has
        // already been decremented — no rollback; mirrored here by returning 0
        // after the kRemoveSource emit.
        if (!g_storagePhase->SourcePhase(r, qty))
            return 0;

        // 0x496f4b..0x4974ce + LABEL_58: dest stock-node ensure / increment
        // (storage-room alloc through Building_AllocStorageRoom when the dest
        // object type is 2/6) + the dword_631290 latch.
        if (!g_storagePhase->DestPhase(r, qty))
            return 0;

        TradeCommand add{};
        add.cmd = TradeCmd::kAddDest;
        add.qty = qty;
        add.proto = r.proto;
        TradeEmit(add);
    }
    return qty;
}

// ===========================================================================
// VIBE_Command_ExComputeSellableAmount  0x497538 (produce-and-sell core).
//
//   v4 = *(a1+22);                                  // start cap
//   for (slot in 0..3) {                            // ingredient gate loop
//       proto = slot.proto;
//       if (proto) {
//           v8 = QueryFind(source, proto);
//           if (v8) { ratio = slot.ratio;
//                     if (v4 >= effStock(v8)/ratio) v4 = effStock(v8)/ratio; }
//           else      v4 = 0;
//       }
//   }
//   v12 = ComputeFreeCapacity(source, outProto, ..., outCount * v4);
//   v14 = v12 / outCount;  if (v4 < v14) v14 = v4;  v15 = v14;
//   if (v15) {  consume ingredients (ratio*v15);
//               add outCount*v15 of outProto;
//               proceeds = trunc( marketPrice(outProto,100) * (outCount*v15) );
//               credit proceeds;  return 0; }
//   else return 1;                                  // nothing producible
//
// NB: the original's ComputeFreeCapacity 4th arg is outCount*v4; the result is
// then divided by outCount to convert "free units of output good" into "crafts".
// ===========================================================================
SellableResult TradeComputeSellableAmount(SellableResolve& r, bool commit) {
    SellableResult out{};
    if (!r.sourceResolved || !r.ownerResolved)
        return out;  // ok == false (reject)

    int v4 = r.startQty;
    for (int i = 0; i < 4; ++i) {
        const RecipeSlot& s = r.ingredients[i];
        if (s.proto == 0)
            continue;                          // empty slot: no gate
        int ratio = s.ratio ? s.ratio : 1;
        if (s.effStock <= 0 && s.proto != 0) {
            // QueryFind miss (v8 == 0) -> v4 = 0 in the original.
            v4 = 0;
            continue;
        }
        int crafts = s.effStock / ratio;
        if (v4 >= crafts)
            v4 = crafts;
    }

    int outCount = r.outputCount ? r.outputCount : 1;
    int freeUnits = InventoryComputeFreeCapacity(
        r.outStockRec, r.outContainer, r.outProto, outCount * v4,
        r.outRootResolved);
    int v14 = freeUnits / outCount;
    if (v4 < v14)
        v14 = v4;
    int v15 = v14;

    if (v15 <= 0)
        return out;  // ok == false (return 1)

    out.produced = v15;
    // 0x4976c8..0x4976e0: fild[v15]; fmulp; ConvertX; fistp.  The proceeds are
    // price * v15 (the CRAFT count), NOT price * outCount*v15 — the output goods
    // added are outCount*v15 but the credit is per craft.  (1:1 with the disasm.)
    double price = TradeMarketPrice(r.outProto, r.player);
    out.proceeds = TruncToInt(price * static_cast<double>(v15));
    out.ok = true;

    if (commit) {
        // consume ingredients
        for (int i = 0; i < 4; ++i) {
            const RecipeSlot& s = r.ingredients[i];
            if (s.proto == 0)
                continue;
            TradeCommand cons{};
            cons.cmd = TradeCmd::kRemoveSource;
            cons.proto = s.proto;
            cons.qty = (s.ratio ? s.ratio : 1) * v15;
            TradeEmit(cons);
        }
        // add output
        TradeCommand add{};
        add.cmd = TradeCmd::kAddDest;
        add.proto = r.outProto;
        add.qty = outCount * v15;
        TradeEmit(add);
        // credit proceeds
        TradeCommand cr{};
        cr.cmd = TradeCmd::kCredit;
        cr.amount = out.proceeds;
        cr.player = r.player;
        TradeEmit(cr);
    }
    return out;
}

}  // namespace guild::sim
