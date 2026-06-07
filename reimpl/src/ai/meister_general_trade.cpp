#include "ai/meister_general_trade.h"

// MeisterAi transporter-fleet ORCHESTRATION (gilde.exe 0x45e71c). The scene-tree
// scan + He-handler match + ResolveEntityById route-chain walk are translated as a
// sweep over a synthetic cart list; the buy decision core (TransporterBuyDecision)
// is reused from meister_trade2.cpp. The full chain-walk via ResolveEntityById and
// the German Sprintf log lines are DEFERRED — entity-array plumbing.

namespace guild::ai {

// gilde.exe 0x45e71c (reroute decision, per cart).
bool CartIsLost(const CartNode& cart, i32 ownBuildingOwner) {
    // a cart referenced by a production (cat 11) or route (cat 15) handler is in
    // active use — not lost.
    if (cart.hasProdHandler || cart.hasRouteHandler)
        return false;
    // resolve the route building: a dead chain (owner 0) is not rerouted here (the
    // original requires v47 != 0). A live owner that is OURSELVES is also fine.
    if (cart.routeBuildingOwner == 0)
        return false;
    if (cart.routeBuildingOwner == ownBuildingOwner)
        return false;
    return true; // belongs to a different living faction -> send it home
}

// gilde.exe 0x45e71c (orchestration).
int CollectTransporters(const std::vector<CartNode>& carts, i32 ownBuildingOwner,
                        TransporterState st, float (*price_of)(u16 cartId),
                        int roll_750, int roll_8, std::vector<FleetCommand>& out) {
    int cartCount = 0;     // v49
    int highCount = 0;     // v48 (id 310)

    for (const CartNode& cart : carts) {
        ++cartCount;
        if (cart.goodId == kCartIdHigh)
            ++highCount;
        if (CartIsLost(cart, ownBuildingOwner)) {
            out.push_back(FleetCommand{FleetCmd::Reroute, cart.goodId, 0,
                                       cart.nodeId, cart.routeBuildingOwner});
        }
    }

    // feed the tally into the buy decision (the recovered core).
    st.cartCount = cartCount;
    st.highCartCount = highCount;
    i32 cost = 0;
    u16 buy = TransporterBuyDecision(st, price_of, roll_750, roll_8, &cost);
    if (buy != 0) {
        out.push_back(FleetCommand{FleetCmd::BuyCart, buy, cost, 0, st.ownerAccount});
    }
    return cartCount;
}

} // namespace guild::ai
