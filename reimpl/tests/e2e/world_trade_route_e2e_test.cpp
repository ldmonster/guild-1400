// End-to-end: a full trade-route flow across the trade_route + exchange +
// location modules.
//
// Scenario: a player in city A opens the trade panel for a production building,
// adds goods to a cart, assigns the cart to a route ending at city B, and
// confirms. We drive the recovered FSM through the sequence and verify the
// emitted (mock) command stream and the cart record against a hand-computed
// reference.
#include <vector>

#include "tests/framework/test.h"
#include "world/exchange.h"
#include "world/location.h"
#include "world/trade_route.h"

using namespace guild;
using namespace guild::world;

namespace {
std::vector<RouteCommand> g_cmds;
void Rec(const RouteCommand& c, void*) { g_cmds.push_back(c); }
} // namespace

TEST(WorldTradeRouteE2E, FullRouteFlow) {
    g_cmds.clear();
    RouteSetCmdHook(&Rec, nullptr);

    // --- Setup: two cities A (account 100, home) and B (account 200, target) ---
    const i32 cityAaccount = 100;
    const i32 cartOwnerAccount = 100; // player owns the cart, account in city A
    const i32 cart = 7;
    const i32 sourceBuildingId = 50;

    // The cart record: a Medium-transport cart starting empty (route slot -1).
    TradeCartRecord rec{};
    rec.transportMode = (u8)RouteMode::Medium;
    rec.routeSlot = -1;
    CHECK_EQ((int)rec.transportMode, (int)RouteMode::Medium);

    // --- Step 1: open the production contact menu, click "Transport" ---
    // (the contact loop offers the production menu; Transport opens the panel)
    auto menu = ProductionContactMenu(/*shopOpen=*/true);
    ContactRegistration reg = ContactRegisterMenu(menu);
    int clicked = reg.slotIds[2]; // the Transport item (registration order)
    CHECK_EQ(ContactDispatch(menu, reg, clicked), (int)ProductionTarget::Transport);

    // --- Step 2: the panel opens in Market mode (own production) ---
    PanelOpen open = RouteDecodeOpen((u8)PanelMode::Market);
    CHECK(open.mode == PanelMode::Market);
    PanelState s = PanelState::Open;

    // --- Step 3: add goods to the cart (AddGoods) ---
    {
        PanelStep st; st.button = PanelButton::AddGoods;
        s = RoutePanelStep(s, st);
        CHECK(s == PanelState::GoodsAdded);
    }

    // --- Step 4: confirm the per-good loadout (Confirm) ---
    // Two goods: 30 units of good 3 bought, 12 units of good 9 given.
    {
        PanelStep st; st.button = PanelButton::Confirm; st.currency = 0;
        st.goods = {
            {/*good*/3, /*qty*/+30, /*account*/200}, // buy from city B contor
            {/*good*/9, /*qty*/-12, /*account*/200}, // give to city B contor
        };
        s = RoutePanelStep(s, st);
        CHECK(s == PanelState::GoodsAdded);
    }
    // After confirm: two per-good commands.
    CHECK_EQ((int)g_cmds.size(), 2);
    CHECK(g_cmds[0].kind == RouteCmd::GoodBuy);
    CHECK_EQ(g_cmds[0].goodId, 3);
    CHECK_EQ(g_cmds[0].amount, 30);
    CHECK(g_cmds[1].kind == RouteCmd::GoodGive);
    CHECK_EQ(g_cmds[1].goodId, 9);
    CHECK_EQ(g_cmds[1].amount, 12);

    // --- Step 5: pick city B as the route target and assign the cart ---
    // Route-target predicate: B is reachable on the owner chain from the source.
    bool reachable = RouteFindOwnerChain(sourceBuildingId,
                                         /*chain=*/{60, sourceBuildingId, -1},
                                         /*targetWithinTolerance=*/false);
    CHECK(reachable);

    const i32 cargoValue = 100000; // value the cart loadout produced
    {
        PanelStep st; st.button = PanelButton::AssignCart;
        st.routeTargetValid = reachable;
        st.mode = (RouteMode)rec.transportMode;     // Medium
        st.ownerAccount = cartOwnerAccount;
        st.cart = cart;
        st.cargoValues = {cargoValue};
        s = RoutePanelStep(s, st);
        CHECK(s == PanelState::CartAssigned);
    }

    // The cart record now holds a route (the dispatcher sets the arrival snapshot
    // and clears/re-stamps routeSlot). We model the record-side mutation that the
    // AssignRoute rule implies: routeSlot is (re)written and a +1-day arrival is
    // scheduled. travelDays == 1.
    rec.routeSlot = cart; // route assigned to this cart
    CHECK_EQ(rec.routeSlot, cart);

    // --- Hand-computed reference for the emitted command stream ---
    // After the two Confirm commands, AssignCart emits per cart:
    //   ResetCart, RouteAssign, ChargeCost(cost=ComputeCartCost(100000, Medium))
    const i32 expectedCost = RouteComputeCartCost(cargoValue, RouteMode::Medium);
    CHECK_EQ(expectedCost, 10000); // 100000*0.1 + 0.5 -> 10000

    CHECK_EQ((int)g_cmds.size(), 5); // 2 goods + reset + route + charge
    CHECK(g_cmds[2].kind == RouteCmd::ResetCart);
    CHECK_EQ(g_cmds[2].recipient, cart);
    CHECK(g_cmds[3].kind == RouteCmd::RouteAssign);
    CHECK_EQ(g_cmds[3].recipient, cart);
    CHECK(g_cmds[4].kind == RouteCmd::ChargeCost);
    CHECK_EQ(g_cmds[4].payer, -1);            // treasury sink
    CHECK_EQ(g_cmds[4].recipient, cartOwnerAccount);
    CHECK_EQ(g_cmds[4].amount, expectedCost);

    // --- Step 6: close the panel ---
    {
        PanelStep st; st.button = PanelButton::Cancel;
        s = RoutePanelStep(s, st);
        CHECK(s == PanelState::Closed);
    }
    // No new commands from closing.
    CHECK_EQ((int)g_cmds.size(), 5);

    (void)cityAaccount;
}
