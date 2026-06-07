// End-to-end: run the full MeisterAi trade + workstation + build-tasks
// orchestration on a synthetic faction and verify the emitted (mock) command
// sequence + buffer state against a hand-computed reference. Mirrors the per-turn
// order: AssignWorkstations (build tables) -> RunStorageSellPass (sell decisions)
// -> CollectTransporters (fleet) -> ProcessBuildingNeeds select.
#include "tests/framework/test.h"

#include "ai/meister_storage.h"
#include "ai/meister_general_trade.h"
#include "ai/meister_buildtasks.h"
#include "ai/meister_workstation.h"

using namespace guild;
using namespace guild::ai;

namespace {
// deterministic synthetic city: one craft workstation (700) that consumes input 50.
const WsTypeDef* e2e_typedef(u16 /*typeId*/) {
    static WsTypeDef td;
    static bool init = false;
    if (!init) {
        td.inputNeed[0] = 2;
        td.divisorOutputs = 2;
        td.divisorRate = 4;
        init = true;
    }
    return &td;
}
float e2e_market(u16 id) {
    if (id == 50) return 10.0f;
    if (id == 700) return 40.0f;
    return 5.0f;
}
float e2e_rate(u16) { return 1.0f; }
int e2e_stock(u16 id) { return id == 700 ? 100 : 4; } // 700 overstocked
int e2e_free(u16) { return 1000; }

float e2e_sell(u16 id) { return id == 700 ? 40.0f : 5.0f; }  // ComputeMarketPrice
float e2e_buy(u16 id)  { return id == 700 ? 100.0f : 5.0f; } // cached cost basis

float e2e_cart_price(u16 id) {
    if (id == kCartIdBase) return 1000.0f;
    return 0.0f;
}
int e2e_worth(int, int) { return 1234; }
int e2e_variant(int aiType, int) { return aiType + 1; }
int e2e_group(int aiType) { return aiType; }
} // namespace

TEST(MeisterTradeE2E, FullTurnSequence) {
    StorageEnv env{};
    env.ws.market_price = e2e_market;
    env.ws.production_rate = e2e_rate;
    env.ws.type_def = e2e_typedef;
    env.stock_of = e2e_stock;
    env.free_cap = e2e_free;

    // ---- phase A: build the workstation/item tables -----------------------
    std::vector<WsCandidate> cands;
    WsCandidate c0; c0.typeId = 700; c0.count = 5; c0.inputId[0] = 50; cands.push_back(c0);
    std::vector<WsDelivery> deliv;
    std::vector<WsStation> stations;
    std::vector<WsItem> items;
    std::vector<StorageCommand> cmds;
    AssignWorkstations(cands, deliv, false, stations, items, env, cmds);

    CHECK_EQ(stations.size(), (size_t)1);
    CHECK_EQ(stations[0].typeId, (u16)700);
    CHECK_EQ(items.size(), (size_t)1);
    CHECK_EQ(items[0].id, (u16)50);
    // station 700 stock snapshot = 100 (overstocked).
    CHECK_EQ(stations[0].stock, 100);
    // no Notify20 (item 50 stock = 4, nonzero).
    for (auto& c : cmds) CHECK(c.kind != StorageCmd::Notify20);

    // ---- phase B: storage sell decisions (odd hour, storage variant) -------
    StoragePlayer player{};
    player.aiType = 4;
    player.oddHour = true;
    player.sellDoneFlag = false;
    player.funds = 100000;       // not cash-strapped -> no emergency sell
    player.heldCurrency = 100000;
    player.slotCapacity = 40;    // 3*40/4 = 30; stock 100 > 30 -> overstock sell
    player.account = 500;

    // a constant roll producing a low profit threshold so the 0.4 ratio (40/100)
    // sells: thr = 0.2*1.25 + 0.25 = 0.5 >= 0.4 -> profit sell fires.
    auto roll = []() -> double { return 0.2; };

    std::vector<StorageCommand> sellCmds;
    int emitted = RunStorageSellPass(player, items, stations, /*storage*/true, roll,
                                     e2e_sell, e2e_buy, sellCmds);
    // expected: profit sell (ratio 0.4 <= 0.5) of all 100, then overstock sell
    // max(5, 100/4=25)=25. Emergency sell skipped (not strapped). 2 commands.
    CHECK_EQ(emitted, 2);
    CHECK_EQ(sellCmds.size(), (size_t)2);
    CHECK_EQ(sellCmds[0].kind, StorageCmd::ProfitSell);
    CHECK_EQ(sellCmds[0].qty, 100);
    CHECK_EQ(sellCmds[1].kind, StorageCmd::OverstockSell);
    CHECK_EQ(sellCmds[1].qty, 25);
    CHECK(player.sellDoneFlag); // +437 |= 2

    // running it again (sellDoneFlag set) emits nothing.
    std::vector<StorageCommand> again;
    CHECK_EQ(RunStorageSellPass(player, items, stations, true, roll, e2e_sell, e2e_buy,
                                again), 0);
    CHECK_EQ(again.size(), (size_t)0);

    // even hour clears the flag.
    player.oddHour = false;
    std::vector<StorageCommand> evenCmds;
    RunStorageSellPass(player, items, stations, true, roll, e2e_sell, e2e_buy, evenCmds);
    CHECK(!player.sellDoneFlag);

    // ---- phase C: transporter fleet ---------------------------------------
    std::vector<CartNode> carts; // no carts -> fallback buys a base cart (308).
    TransporterState st{};
    st.aiType = 0;            // non-caravan
    st.quota = 3; st.budget = 5000; st.ownerAccount = 500;
    std::vector<FleetCommand> fleet;
    int cartCount = CollectTransporters(carts, /*own*/7, st, e2e_cart_price,
                                        /*roll750*/0, /*roll8*/0, fleet);
    CHECK_EQ(cartCount, 0);
    // no carts -> the buy decision restocks a base cart (308).
    CHECK_EQ(fleet.size(), (size_t)1);
    CHECK_EQ(fleet[0].kind, FleetCmd::BuyCart);
    CHECK_EQ(fleet[0].goodId, kCartIdBase);
    CHECK_EQ(fleet[0].cost, 1000);

    // ---- phase D: building-needs select (phase 3, force a zero-count build) -
    std::vector<int> totals(23, 5), owned(23, 5);
    owned[22] = 0; // type 22 owned 0 -> force.
    BuildEnv benv{}; benv.worth_of = e2e_worth; benv.variant_of = e2e_variant;
    benv.group_of = e2e_group;
    std::vector<BuildCommand> builds;
    int picked = SelectBuildToConstruct(/*phase*/3, /*tick*/40, /*diff*/1,
                                        totals, owned, 0.99, 0, benv, builds);
    CHECK_EQ(picked, 22);
    CHECK_EQ(builds.size(), (size_t)1);
    CHECK_EQ(builds[0].aiType, 22);
    CHECK_EQ(builds[0].fundWorth, 1234);
    CHECK_EQ(builds[0].variant, 23);
}

TEST(MeisterTradeE2E, EmergencySellWhenStrapped) {
    // a cash-strapped faction must dump stock to raise 32000.
    std::vector<WsStation> stations;
    WsStation s; s.typeId = 700; s.stock = 1000; s.bits = 0; stations.push_back(s);
    std::vector<WsItem> items;

    StoragePlayer player{};
    player.aiType = 4; player.oddHour = true; player.sellDoneFlag = false;
    player.funds = 1000;          // < 3200 -> strapped
    player.heldCurrency = 1000;   // proceeds start at min(1000,1000)=1000 < 32000
    player.slotCapacity = 4000;   // huge -> overstock line not crossed
    player.account = 9;
    auto roll = []() -> double { return 1.0; }; // high threshold -> no profit sell

    std::vector<StorageCommand> out;
    RunStorageSellPass(player, items, stations, true, roll, e2e_sell, e2e_buy, out);
    // buy price 100; emergency qty = 32000/100 = 320, clamped to stock 1000 -> 320.
    bool sawEmergency = false;
    for (auto& c : out)
        if (c.kind == StorageCmd::EmergencySell) { sawEmergency = true; CHECK_EQ(c.qty, 320); }
    CHECK(sawEmergency);
}
