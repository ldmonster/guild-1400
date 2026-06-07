#include "world/trade_route.h"

#include <cmath>

namespace guild::world {

namespace {
RouteCmdHook g_cmdHook = nullptr;
void*        g_cmdCtx  = nullptr;

// trunc-toward-zero (VIBE_Coord_ConvertX 0x5c6b08 round-to-zero idiom).
i32 Trunc(double x) { return static_cast<i32>(std::trunc(x)); }
} // namespace

void RouteSetCmdHook(RouteCmdHook hook, void* ctx) {
    g_cmdHook = hook;
    g_cmdCtx = ctx;
}

void RouteEmit(const RouteCommand& cmd) {
    if (g_cmdHook)
        g_cmdHook(cmd, g_cmdCtx);
}

// gilde.exe 0x592220 — VIBE_TradeTransport_ComputeCartCost.
//   v2 = a1;
//   if ((double)a1 > 0.0) { if (a1 < 32000) v2 = 32000; if (v2 > 256000) v2 = 256000; }
//   switch (mode) { 1: trunc(v2*0.05); 2: trunc(v2*0.1); 3: trunc(v2*0.15); }
//   return trunc((double)fee + 0.5);
i32 RouteComputeCartCost(i32 goodValue, RouteMode mode) {
    i32 v2 = goodValue;
    if (static_cast<double>(goodValue) > 0.0) {
        if (goodValue < kRouteValueFloor)
            v2 = kRouteValueFloor;
        if (v2 > kRouteValueCeil)
            v2 = kRouteValueCeil;
    }
    i32 fee = 0;
    switch (mode) {
        case RouteMode::Slow:
            fee = Trunc(static_cast<double>(v2) * static_cast<double>(kRouteRateSlow));
            break;
        case RouteMode::Medium:
            fee = Trunc(static_cast<double>(v2) * static_cast<double>(kRouteRateMedium));
            break;
        case RouteMode::Fast:
            fee = Trunc(static_cast<double>(v2) * static_cast<double>(kRouteRateFast));
            break;
        case RouteMode::None:
        default:
            fee = 0; // no case matched: fee stays 0
            break;
    }
    return Trunc(static_cast<double>(fee) + kRouteCostBase);
}

// gilde.exe 0x53f404 — VIBE_TradeTransport_AssignRoute.
//   for (i = FindFirstHandlerByFilter(2,3,good,0,17); i; i = FindNext())
//       { stamp i+82..+94 = clock; *((_DWORD*)i+28) = -1; }     // clear routes
//   if (InvokeHandlerSlot60(36, ..) != 1) return 0;
//   ... snapshot clock, GameTime_Advance(&t, 0, 1, 0);          // +1 day arrive
//   QueueRequestSlotReset28(&packet);                            // RouteAssign
//   if (*(cart+40)) { CollectProductionSlots(cart);             // cargo value
//                     cost = ComputeCartCost(value, *(cart+40));
//                     QueueRequest16(-1, ownerAccount, cost, currency); }
//   return cost;
RouteAssignResult RouteAssign(i32 cargoValue, RouteMode mode, i32 ownerAccount,
                              i32 cart, bool commit) {
    RouteAssignResult r;
    r.travelDays = kRouteTravelDays;

    // (2) the route packet is always emitted (the +1-day arrival snapshot).
    if (commit) {
        RouteCommand pkt;
        pkt.kind = RouteCmd::RouteAssign;
        pkt.recipient = cart;
        RouteEmit(pkt);
    }

    // (3) charge only when the cart has a transport mode (+40 != 0).
    if (mode == RouteMode::None) {
        r.cost = 0;
        return r;
    }
    r.cost = RouteComputeCartCost(cargoValue, mode);
    if (commit && r.cost != 0) {
        RouteCommand chg;
        chg.kind = RouteCmd::ChargeCost;
        chg.payer = -1;            // treasury / city sink
        chg.recipient = ownerAccount;
        chg.amount = r.cost;
        RouteEmit(chg);
        r.charged = true;
    }
    return r;
}

// gilde.exe 0x53f5c0 — VIBE_TradeTransport_ResetCartHandler.
//   result = FindFirstHandlerByFilter(1, 0, 15);
//   while (result && *((_DWORD*)result+43) != cartId) result = FindNext();
//   if (result) { stamp result+82..+94 = clock; QueueRequestEntity29(-1, result); }
bool RouteResetCart(i32 cart) {
    // The "find by id" walk is engine-side; the rule is: when the cart handler is
    // found its arrival-time is re-stamped and a reset packet is emitted.
    RouteCommand rst;
    rst.kind = RouteCmd::ResetCart;
    rst.payer = -1;
    rst.recipient = cart;
    RouteEmit(rst);
    return true;
}

// gilde.exe 0x53f610 — VIBE_TradeTransport_FindOwnerChain.
//   if (!target.valid) return 0;
//   if (target within 1350 tolerance of source AND not occluded) return 1;
//   walk owner chain by id (*(node+6)); if a node id == source id return 1;
//   stop at -1; return 0.
bool RouteFindOwnerChain(i32 sourceId, const std::vector<i32>& chain,
                         bool targetWithinTolerance) {
    if (targetWithinTolerance)
        return true;
    for (i32 nodeId : chain) {
        if (nodeId == -1)
            break;          // chain terminator
        if (nodeId == sourceId)
            return true;    // a1 == v10 in the original
    }
    return false;
}

// gilde.exe 0x54011c/2c/3c thunks -> PanelDispatcher(a1, dl). dl is stored in
// HIBYTE(v369); the dispatcher tests (v369 & 0x1000000) (== HIBYTE bit0 == mode
// 1, market), HIBYTE==2 (import), HIBYTE==4 (export), and (a2 & 8) (route-edit).
PanelOpen RouteDecodeOpen(u8 modeByte) {
    PanelOpen o;
    o.routeEdit = (modeByte & 8) != 0;
    u8 m = modeByte & 0x07;
    if (m & 0x01)
        o.mode = PanelMode::Market;     // (v369 & 0x1000000): market/own-production
    else if (m == 2)
        o.mode = PanelMode::Import;     // HIBYTE(v369) == 2 -> contor 475
    else if (m == 4)
        o.mode = PanelMode::Export;     // HIBYTE(v369) == 4 -> contor 476
    else
        o.mode = PanelMode::Market;
    return o;
}

// The per-frame button dispatch from the dispatcher loop (LABEL_244..LABEL_251).
//   Cancel  (dword_672230 / 75BF38==1155): set close flag -> Closed.
//   Confirm (dword_75BF38==1228, after the "are you sure?" box): walk goods
//           slots; qty>0 -> QueueRequest17 (player buys = GoodBuy); qty<0 ->
//           QueueRequest20 (player gives = GoodGive).
//   AssignCart (dword_62D22C==v318): pick a route target via the map; if it is
//           valid (FindOwnerChain) and non-self, ResetCartHandler + AssignRoute
//           per loaded cart -> CartAssigned.
//   AddGoods (dword_62D22C==v320): BuyTransportCart + repopulate -> GoodsAdded.
//   SellCarts(dword_62D22C==v364): collect selected carts + ConfirmSellCarts.
//   Courier (dword_62D22C==v319): compute courier fee + enqueue (handled by the
//           exchange module; here it is a no-op transition staying Open).
PanelState RoutePanelStep(PanelState cur, const PanelStep& step) {
    if (cur == PanelState::Closed)
        return PanelState::Closed;

    switch (step.button) {
        case PanelButton::Cancel:
            return PanelState::Closed;

        case PanelButton::Confirm: {
            for (const GoodLine& g : step.goods) {
                if (g.goodId == -1 || g.quantity == 0)
                    continue;
                RouteCommand c;
                c.goodId = g.goodId;
                c.currency = step.currency;
                if (g.quantity > 0) {
                    // VIBE_Command_QueueRequest17(-1, account, qty, good, cur, 0)
                    c.kind = RouteCmd::GoodBuy;
                    c.payer = -1;
                    c.recipient = g.account;
                    c.amount = g.quantity;
                } else {
                    // VIBE_Command_QueueRequest20(account, good)
                    c.kind = RouteCmd::GoodGive;
                    c.payer = g.account;
                    c.amount = -g.quantity;
                }
                RouteEmit(c);
            }
            return PanelState::GoodsAdded;
        }

        case PanelButton::AssignCart: {
            if (!step.routeTargetValid)
                return cur; // target unreachable / self: no transition
            for (i32 value : step.cargoValues) {
                RouteResetCart(step.cart);
                RouteAssign(value, step.mode, step.ownerAccount, step.cart,
                            /*commit=*/true);
            }
            return PanelState::CartAssigned;
        }

        case PanelButton::AddGoods:
            return PanelState::GoodsAdded;

        case PanelButton::SellCarts:
            return PanelState::CartsSold;

        case PanelButton::Courier:
        case PanelButton::None:
        default:
            return cur;
    }
}

} // namespace guild::world
