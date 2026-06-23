// Unit tests for the trade-route / exchange / location-contact rules cores.
//   src/world/trade_route.{h,cpp}  — route record, AssignRoute, panel FSM
//   src/world/exchange.{h,cpp}     — goods/currency exchange + courier fees
//   src/world/location.{h,cpp}     — contact-dispatch FSM + classifier
// Golden values computed with python3 (see the agent report).
#include <vector>

#include "tests/framework/test.h"
#include "world/exchange.h"
#include "world/location.h"
#include "world/trade_route.h"

using namespace guild;
using namespace guild::world;

// ---------------------------------------------------------------------------
// Command-recording harness shared by the route + exchange tests.
// ---------------------------------------------------------------------------
namespace {
std::vector<RouteCommand> g_routeCmds;
void RouteRec(const RouteCommand& c, void*) { g_routeCmds.push_back(c); }

std::vector<ExchangeCommand> g_exCmds;
void ExRec(const ExchangeCommand& c, void*) { g_exCmds.push_back(c); }

void ResetRoute() { g_routeCmds.clear(); RouteSetCmdHook(&RouteRec, nullptr); }
void ResetEx()    { g_exCmds.clear();    ExchangeSetCmdHook(&ExRec, nullptr); }
} // namespace

// ===========================================================================
// trade_route: cart-cost formula (golden)
// ===========================================================================
TEST(WorldTradeRoute, CartCostGolden) {
    CHECK_EQ(RouteComputeCartCost(100000, RouteMode::Slow), 5000);
    CHECK_EQ(RouteComputeCartCost(100000, RouteMode::Medium), 10000);
    CHECK_EQ(RouteComputeCartCost(100000, RouteMode::Fast), 15000);
    CHECK_EQ(RouteComputeCartCost(500000, RouteMode::Medium), 25600); // clamp ceil
    CHECK_EQ(RouteComputeCartCost(1000, RouteMode::Medium), 3200);    // clamp floor
    CHECK_EQ(RouteComputeCartCost(0, RouteMode::Medium), 0);          // not >0
    CHECK_EQ(RouteComputeCartCost(-5, RouteMode::Medium), 0);         // negative
    CHECK_EQ(RouteComputeCartCost(100000, RouteMode::None), 0);       // no mode
}

// ===========================================================================
// trade_route: record layout is byte-exact
// ===========================================================================
TEST(WorldTradeRoute, RecordLayout) {
    CHECK_EQ((int)offsetof(TradeCartRecord, transportMode), 40);
    CHECK_EQ((int)offsetof(TradeCartRecord, arriveTimeLo), 82);
    CHECK_EQ((int)offsetof(TradeCartRecord, arriveTimeMid), 90);
    CHECK_EQ((int)offsetof(TradeCartRecord, arriveTimeHi), 94);
    CHECK_EQ((int)offsetof(TradeCartRecord, routeSlot), 112);
}

// ===========================================================================
// trade_route: AssignRoute emits the route packet + cost charge
// ===========================================================================
TEST(WorldTradeRoute, AssignRouteEmitsCommands) {
    ResetRoute();
    RouteAssignResult r = RouteAssign(/*cargoValue=*/100000, RouteMode::Medium,
                                      /*ownerAccount=*/77, /*cart=*/5, true);
    CHECK_EQ(r.cost, 10000);
    CHECK_EQ(r.travelDays, 1);
    CHECK(r.charged);
    CHECK_EQ((int)g_routeCmds.size(), 2);
    CHECK(g_routeCmds[0].kind == RouteCmd::RouteAssign);
    CHECK_EQ(g_routeCmds[0].recipient, 5);
    CHECK(g_routeCmds[1].kind == RouteCmd::ChargeCost);
    CHECK_EQ(g_routeCmds[1].payer, -1);
    CHECK_EQ(g_routeCmds[1].recipient, 77);
    CHECK_EQ(g_routeCmds[1].amount, 10000);
}

TEST(WorldTradeRoute, AssignRouteNoModeNoCharge) {
    ResetRoute();
    RouteAssignResult r = RouteAssign(100000, RouteMode::None, 77, 5, true);
    CHECK_EQ(r.cost, 0);
    CHECK(!r.charged);
    // Only the route packet is emitted; no cost charge.
    CHECK_EQ((int)g_routeCmds.size(), 1);
    CHECK(g_routeCmds[0].kind == RouteCmd::RouteAssign);
}

// ===========================================================================
// trade_route: FindOwnerChain route-target predicate
// ===========================================================================
TEST(WorldTradeRoute, FindOwnerChain) {
    // within tolerance: reachable regardless of chain
    CHECK(RouteFindOwnerChain(9, {}, /*targetWithinTolerance=*/true));
    // source id appears on the chain -> reachable
    CHECK(RouteFindOwnerChain(9, {3, 9, 4}, false));
    // source not on chain, not in tolerance -> unreachable
    CHECK(!RouteFindOwnerChain(9, {3, 4, 5}, false));
    // terminator stops the walk before a later match
    CHECK(!RouteFindOwnerChain(9, {3, -1, 9}, false));
}

// ===========================================================================
// trade_route: panel open-mode decode
// ===========================================================================
TEST(WorldTradeRoute, DecodeOpenMode) {
    CHECK(RouteDecodeOpen(1).mode == PanelMode::Market);
    CHECK(!RouteDecodeOpen(1).routeEdit);
    CHECK(RouteDecodeOpen(2).mode == PanelMode::Import);
    CHECK(RouteDecodeOpen(4).mode == PanelMode::Export);
    // flag 8 selects the route-edit variant, keeps the mode bits
    CHECK(RouteDecodeOpen(1 | 8).mode == PanelMode::Market);
    CHECK(RouteDecodeOpen(1 | 8).routeEdit);
    CHECK(RouteDecodeOpen(2 | 8).mode == PanelMode::Import);
}

// ===========================================================================
// trade_route: FSM transitions for a sequence of inputs
// ===========================================================================
TEST(WorldTradeRoute, PanelFsmTransitions) {
    ResetRoute();
    PanelState s = PanelState::Open;

    // AddGoods -> GoodsAdded
    {
        PanelStep st; st.button = PanelButton::AddGoods;
        s = RoutePanelStep(s, st);
        CHECK(s == PanelState::GoodsAdded);
    }
    // AssignCart with INVALID target -> no transition (stays GoodsAdded)
    {
        PanelStep st; st.button = PanelButton::AssignCart;
        st.routeTargetValid = false; st.cargoValues = {100000};
        s = RoutePanelStep(s, st);
        CHECK(s == PanelState::GoodsAdded);
        CHECK_EQ((int)g_routeCmds.size(), 0); // nothing emitted
    }
    // AssignCart with VALID target -> CartAssigned, emits reset+route+charge
    {
        PanelStep st; st.button = PanelButton::AssignCart;
        st.routeTargetValid = true; st.mode = RouteMode::Medium;
        st.ownerAccount = 77; st.cart = 5; st.cargoValues = {100000};
        s = RoutePanelStep(s, st);
        CHECK(s == PanelState::CartAssigned);
        // per cart: ResetCart + RouteAssign + ChargeCost
        CHECK_EQ((int)g_routeCmds.size(), 3);
        CHECK(g_routeCmds[0].kind == RouteCmd::ResetCart);
        CHECK(g_routeCmds[1].kind == RouteCmd::RouteAssign);
        CHECK(g_routeCmds[2].kind == RouteCmd::ChargeCost);
        CHECK_EQ(g_routeCmds[2].amount, 10000);
    }
    // Cancel -> Closed (terminal)
    {
        PanelStep st; st.button = PanelButton::Cancel;
        s = RoutePanelStep(s, st);
        CHECK(s == PanelState::Closed);
        // Closed is terminal: any further input stays Closed
        PanelStep st2; st2.button = PanelButton::AddGoods;
        CHECK(RoutePanelStep(s, st2) == PanelState::Closed);
    }
}

TEST(WorldTradeRoute, ConfirmEmitsPerGood) {
    ResetRoute();
    PanelStep st; st.button = PanelButton::Confirm; st.currency = 0;
    st.goods = {
        {/*good*/3, /*qty*/+10, /*account*/40},  // buy
        {/*good*/7, /*qty*/-4,  /*account*/41},  // give
        {/*good*/-1, /*qty*/+9, /*account*/42},  // empty slot -> skipped
        {/*good*/8, /*qty*/0,   /*account*/43},  // zero qty -> skipped
    };
    PanelState s = RoutePanelStep(PanelState::Open, st);
    CHECK(s == PanelState::GoodsAdded);
    CHECK_EQ((int)g_routeCmds.size(), 2);
    CHECK(g_routeCmds[0].kind == RouteCmd::GoodBuy);
    CHECK_EQ(g_routeCmds[0].goodId, 3);
    CHECK_EQ(g_routeCmds[0].amount, 10);
    CHECK(g_routeCmds[1].kind == RouteCmd::GoodGive);
    CHECK_EQ(g_routeCmds[1].goodId, 7);
    CHECK_EQ(g_routeCmds[1].amount, 4);
}

// ===========================================================================
// exchange: PriceByRate + courier fee golden values
// ===========================================================================
TEST(WorldExchange, PriceByRate) {
    ExchangeSetRateTable({10, 7, 3});
    CHECK_EQ(ExchangePriceByRate(100, 2), 300); // table[2]*100
    CHECK_EQ(ExchangePriceByRate(5, 0), 50);    // table[0]*5
    CHECK_EQ(ExchangePriceByRate(5, 9), 0);     // out of range -> 0
}

TEST(WorldExchange, CourierFeeGolden) {
    ResetEx();
    ExchangeSetRateTable({10, 7, 3});
    // base=300, span=9, minA=3, 3>=9? no -> fee=9, net=trunc(300-9)=291
    CourierResult r = ExchangeCourier(/*rate=*/100, /*cur=*/2, /*acct=*/55, true);
    CHECK_EQ(r.base, 300);
    CHECK_EQ(r.fee, 9);
    CHECK_EQ(r.net, 291);
    CHECK_EQ((int)g_exCmds.size(), 2);
    CHECK_EQ(g_exCmds[0].amount, 300); // charge base
    CHECK_EQ(g_exCmds[1].amount, 291); // deliver net

    // rate=1: base=3, span=0.09, minA=3, 3>=0.09 yes -> fee=pbr(1,2)=3, net=0
    CourierResult r2 = ExchangeCourier(1, 2, 55, false);
    CHECK_EQ(r2.base, 3);
    CHECK_EQ(r2.fee, 3);
    CHECK_EQ(r2.net, 0);
}

// ===========================================================================
// exchange: goods-trade fee + affordability golden
// ===========================================================================
TEST(WorldExchange, GoodsTradeGolden) {
    ResetEx();
    ExchangeSetRateTable({10, 7, 3});
    GoodsTradeInput in;
    in.takeQty = 10; in.takeGood = 2; in.takeCurrency = 2;   // value=30
    in.giveQty = 100; in.giveGood = 5; in.giveCurrency = 0;  // afford=1000
    in.playerCash = 5000; in.playerAccount = 1; in.playerCity = 8;
    in.sameCity = false;
    GoodsTradeResult r = ExchangeGoodsTrade(in, true);
    // value=30, feeBase=50, span=1500, minA=3, 3>=1500? no -> fee=1500
    CHECK_EQ(r.giveValue, 30);
    CHECK_EQ(r.fee, 1500);
    CHECK(r.accept);
    CHECK_EQ((int)g_exCmds.size(), 4); // four legs

    // same city -> fee 0
    ResetEx();
    in.sameCity = true;
    GoodsTradeResult r2 = ExchangeGoodsTrade(in, false);
    CHECK_EQ(r2.fee, 0);
    CHECK(r2.accept);

    // cannot afford: value=300, afford=pbr(1,0)=10 < 300 -> reject, no commit
    ResetEx();
    GoodsTradeInput in2 = in;
    in2.sameCity = false;
    in2.takeQty = 100; in2.takeCurrency = 2; // value = 3*100 = 300
    in2.giveQty = 1;   in2.giveCurrency = 0;  // afford = 10
    GoodsTradeResult r3 = ExchangeGoodsTrade(in2, true);
    CHECK(!r3.accept);
    CHECK_EQ((int)g_exCmds.size(), 0);
}

TEST(WorldExchange, ApplyFees) {
    ResetEx();
    ExchangeApplyFees(/*objectId=*/12, {/*buy=*/40, /*sell=*/55}, true);
    CHECK_EQ((int)g_exCmds.size(), 2);
    CHECK_EQ(g_exCmds[0].field, 105);
    CHECK_EQ(g_exCmds[0].amount, 40);
    CHECK_EQ(g_exCmds[1].field, 109);
    CHECK_EQ(g_exCmds[1].amount, 55);
}

// ===========================================================================
// location: classifier + contact-dispatch resolution
// ===========================================================================
// Real object-type codes from gilde.exe 0x51defc VIBE_Building_EnterAndDispatch
// (binary-search switch on the object's 16-bit type word, `mov ax,[esi]`):
// Production=247, Church=229/230, ThiefGuild=84, Tavern=288; all else -> Idle.
TEST(WorldLocation, ClassifyKind) {
    CHECK(ClassifyLocationKind(247) == LocationKind::Production);
    CHECK(ClassifyLocationKind(229) == LocationKind::Church);
    CHECK(ClassifyLocationKind(230) == LocationKind::Church);
    CHECK(ClassifyLocationKind(84) == LocationKind::ThiefGuild);
    CHECK(ClassifyLocationKind(288) == LocationKind::Tavern);
    CHECK(ClassifyLocationKind(99) == LocationKind::Idle);
}

TEST(WorldLocation, ProductionDispatchResolves) {
    auto items = ProductionContactMenu(/*shopOpen=*/true);
    ContactRegistration reg = ContactRegisterMenu(items);
    // All 5 offered -> slot ids 1..5 in registration order.
    CHECK_EQ((int)reg.slotIds.size(), 5);
    CHECK_EQ(reg.slotIds[0], 1);
    CHECK_EQ(reg.slotIds[4], 5);
    // Clicking slot 3 -> Transport item.
    CHECK_EQ(ContactDispatch(items, reg, 3), (int)ProductionTarget::Transport);
    // Clicking slot 1 -> ProductionWindow.
    CHECK_EQ(ContactDispatch(items, reg, 1), (int)ProductionTarget::ProductionWindow);
    // Nothing clicked -> -1.
    CHECK_EQ(ContactDispatch(items, reg, 0), -1);
}

TEST(WorldLocation, ProductionShopClosedOffersNothing) {
    auto items = ProductionContactMenu(/*shopOpen=*/false);
    ContactRegistration reg = ContactRegisterMenu(items);
    for (int id : reg.slotIds)
        CHECK_EQ(id, 0); // nothing offered
    CHECK_EQ(ContactDispatch(items, reg, 1), -1);
}

TEST(WorldLocation, ThiefGuildGatedDispatch) {
    // Low rank, shop open, no back room: Attack item is NOT offered, so the
    // slot ids of the items after it shift down by one.
    auto items = ThiefGuildContactMenu(/*shop=*/true, /*back=*/false, /*rank=*/0);
    ContactRegistration reg = ContactRegisterMenu(items);
    // items: Burglary(1) Spy(2) Attack(0=not offered) Pickpocket(3) Info(4)
    //        StaffBook(0) MasterCert(0)
    CHECK_EQ(reg.slotIds[0], 1); // Burglary
    CHECK_EQ(reg.slotIds[1], 2); // Spy
    CHECK_EQ(reg.slotIds[2], 0); // Attack not offered (rank<2)
    CHECK_EQ(reg.slotIds[3], 3); // Pickpocket
    CHECK_EQ(reg.slotIds[4], 4); // Info
    CHECK_EQ(reg.slotIds[5], 0); // StaffBook (no back room)
    // slot 3 resolves to Pickpocket (NOT Attack), proving the shift.
    CHECK_EQ(ContactDispatch(items, reg, 3), (int)ThiefGuildTarget::Pickpocket);

    // High rank: Attack now offered as slot 3, Pickpocket shifts to 4.
    auto items2 = ThiefGuildContactMenu(true, false, /*rank=*/2);
    ContactRegistration reg2 = ContactRegisterMenu(items2);
    CHECK_EQ(reg2.slotIds[2], 3); // Attack offered
    CHECK_EQ(ContactDispatch(items2, reg2, 3), (int)ThiefGuildTarget::Attack);
    CHECK_EQ(ContactDispatch(items2, reg2, 4), (int)ThiefGuildTarget::Pickpocket);

    // Back room offers the staff/master items.
    auto items3 = ThiefGuildContactMenu(false, /*back=*/true, 0);
    ContactRegistration reg3 = ContactRegisterMenu(items3);
    CHECK_EQ(reg3.slotIds[5], 1); // StaffBook is the first offered (shop closed)
    CHECK_EQ(reg3.slotIds[6], 2); // MasterCert
    CHECK_EQ(ContactDispatch(items3, reg3, 2),
             (int)ThiefGuildTarget::MasterCertificate);
}

// ---------------------------------------------------------------------------
// Boundary: exchange rate-table index. ExchangePriceByRate guards
// `currency < table.size()` -> currencies past the (empty/short) table return 0.
// ---------------------------------------------------------------------------
TEST(WorldExchange, PriceByRateEmptyAndOversizeIndex) {
    ExchangeSetRateTable({});                 // empty order/rate book
    CHECK_EQ(ExchangePriceByRate(10, 0), 0);  // index past empty table -> 0
    CHECK_EQ(ExchangePriceByRate(10, 65535), 0);

    ExchangeSetRateTable({3, 5});             // two-entry book
    CHECK_EQ(ExchangePriceByRate(4, 0), 12);  // 3*4
    CHECK_EQ(ExchangePriceByRate(4, 1), 20);  // 5*4
    CHECK_EQ(ExchangePriceByRate(4, 2), 0);   // == size -> guard -> 0
    CHECK_EQ(ExchangePriceByRate(4, 9999), 0);
    ExchangeSetRateTable({});                 // restore
}

// ---------------------------------------------------------------------------
// Boundary: route panel with 0 stops and with many stops (the cargoValues list).
// RoutePanelStep iterates cargoValues; 0 entries must still transition, a large
// list must process every entry without OOB.
// ---------------------------------------------------------------------------
TEST(WorldTradeRoute, PanelAssignCartZeroAndManyStops) {
    g_routeCmds.clear();
    RouteSetCmdHook(RouteRec, nullptr);

    // Zero stops: valid target, empty cargoValues -> CartAssigned, no per-cart
    // commands (only the loop body emits, and the loop runs zero times).
    {
        PanelStep st; st.button = PanelButton::AssignCart;
        st.routeTargetValid = true; st.mode = RouteMode::Medium;
        st.ownerAccount = 1; st.cart = 9; st.cargoValues = {};
        PanelState s = RoutePanelStep(PanelState::Open, st);
        CHECK(s == PanelState::CartAssigned);
    }

    // Many stops: 64 carts, each emits a ResetCart + (route packet [+ charge]).
    g_routeCmds.clear();
    {
        PanelStep st; st.button = PanelButton::AssignCart;
        st.routeTargetValid = true; st.mode = RouteMode::Medium;
        st.ownerAccount = 1; st.cart = 9;
        st.cargoValues.assign(64, 100000);
        PanelState s = RoutePanelStep(PanelState::Open, st);
        CHECK(s == PanelState::CartAssigned);
        // Each iteration: ResetCart (1) + RouteAssign route packet (1) + charge
        // (1, since cost!=0 for Medium/100000) == 3 commands per cart.
        CHECK_EQ((int)g_routeCmds.size(), 64 * 3);
    }
    RouteSetCmdHook(nullptr, nullptr);
}

// FindOwnerChain over an empty chain and a chain terminated immediately by -1.
TEST(WorldTradeRoute, FindOwnerChainEmptyAndTerminator) {
    CHECK_EQ(RouteFindOwnerChain(7, {}, false), false);       // empty, no tolerance
    CHECK_EQ(RouteFindOwnerChain(7, {}, true), true);         // tolerance short-circuit
    CHECK_EQ(RouteFindOwnerChain(7, {-1, 7}, false), false);  // -1 terminates first
    CHECK_EQ(RouteFindOwnerChain(7, {3, 7, 9}, false), true); // 7 present
}
