#pragma once
// MeisterAi transporter-fleet ORCHESTRATION — the deferred CollectTransporters
// reroute scan + buy orchestration (gilde.exe 0x45e71c, ~593 insns).
//
// CollectTransporters runs twice a day (odd hours only). It:
//   1. Walks the whole scene tree (dword_13CE290, stride 67, 0x2000 nodes) looking
//      for transporter cart nodes (type-def kind 29) owned by this faction.
//   2. For each cart, checks whether a production handler (He cat 11) OR a route
//      handler (He cat 15) already references it. If NEITHER does, the cart is
//      "lost": it resolves the cart's route-building chain via ResolveEntityById
//      and, if the route building exists and belongs to a DIFFERENT faction (or is
//      gone), reroutes the cart home (QueueRequestSlotReset28 op 16).
//   3. Tallies the total cart count `v49` and the count of top-tier (id 310) carts
//      `v48`, then feeds those to the buy decision (TransporterBuyDecision, already
//      recovered in meister_trade2.h) to decide whether to purchase a new cart.
//
// The scene-tree walk + He-handler matching + ResolveEntityById chain-walk are the
// orchestration translated here (on a synthetic cart list + handler/route model);
// the buy decision core itself is REUSED from meister_trade2.h. Commands route
// through a hook.
#include <vector>

#include "guild/common/types.h"
#include "ai/meister_trade2.h"

namespace guild::ai {

// scene node type-def kind for a transporter cart.
constexpr int kTransporterNodeKind = 29;
// the reroute SlotReset28 op the original stamps onto a lost cart.
constexpr int kRerouteOp = 16;

// One transporter cart node the scene scan yields.
struct CartNode {
    u16  goodId = 0;          // *v3 (cart item id; 310 = top tier)
    i32  nodeId = 0;          // *(v3+1) cart entity id
    bool hasProdHandler = false; // an He cat-11 handler references this cart
    bool hasRouteHandler = false;// an He cat-15 handler references this cart
    // the resolved route-building owner: 0 = chain dead / no building. A non-zero
    // owner that differs from the faction's own building means "lost" → reroute.
    i32  routeBuildingOwner = 0; // *v47 (resolved root owner)
    i32  routeBuildingId = 0;    // *(v47+1) (for the reroute command target)
};

// A reroute / buy command CollectTransporters emits.
enum class FleetCmd {
    Reroute, // a lost cart is sent home (SlotReset28 op16)
    BuyCart, // a new cart of `goodId` is purchased for `cost`
};
struct FleetCommand {
    FleetCmd kind;
    u16 goodId = 0;
    i32 cost = 0;
    i32 cartNodeId = 0;
    i32 fromOwner = 0;
};

// gilde.exe 0x45e71c (reroute decision, per cart) — decide whether a cart is lost
// and must be rerouted home. A cart is lost iff it has NO production handler AND no
// route handler AND its route building resolves to a different, still-living owner
// (routeBuildingOwner != 0 && != ownBuildingOwner). Returns true → reroute.
bool CartIsLost(const CartNode& cart, i32 ownBuildingOwner);

// gilde.exe 0x45e71c (orchestration) — the full audit. `ownBuildingOwner` is the
// faction's home-building owner id. Walks `carts`, emitting a Reroute for each lost
// cart, tallying total + top-tier (id 310) counts, then runs the buy decision
// (`st` carries the faction transporter state with cartCount/highCartCount filled
// from the tally). `roll_750`/`roll_8`/`price_of` feed TransporterBuyDecision.
// Appends commands to `out`; returns the total cart count seen.
int CollectTransporters(const std::vector<CartNode>& carts, i32 ownBuildingOwner,
                        TransporterState st, float (*price_of)(u16 cartId),
                        int roll_750, int roll_8, std::vector<FleetCommand>& out);

} // namespace guild::ai
