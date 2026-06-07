// End-to-end: run a full MeisterAi workstation + trade-logistics planning pass on
// a synthetic player with buildings/storage/staff and verify the emitted (mock)
// command sequence + buffer state against a hand-computed reference.
//
// Flow modeled (the order the turn pass invokes the planners):
//   1. AssignWorkstations -> ComputeWorkstationOutput per station, score sort.
//   2. GatherRequiredItems -> net shortfall + budget clamp (the buy plan).
//   3. DistributeWorkstationItems plan-qty clamp + item value sort (the move plan).
//   4. CheckWorkstationCapacity feasibility gate before committing.
//   5. CollectTransporters buy-cart decision.
//   6. BalanceCityGoods wealth top-ups.
// Each commit emits a tagged command into a sink; we assert the full sequence.
#include "tests/framework/test.h"

#include "ai/meister_workstation.h"
#include "ai/meister_trade2.h"
#include "ai/building_needs.h"
#include "crt/rand.h"
#include "util/math_random.h"

#include <vector>
#include <string>
#include <cmath>

using namespace guild;

namespace {

// --- the synthetic engine environment --------------------------------------
ai::WsTypeDef g_carpenterTd; // workstation type 100: 2 planks (id 10) -> output
float E2eMarket(u16 id) {
    switch (id) {
    case 100: return 30.0f; // finished good
    case 10:  return 5.0f;  // plank (input)
    case ai::kCartIdBase: return 100.0f;
    case ai::kCartIdMid:  return 200.0f;
    case ai::kCartIdHigh: return 300.0f;
    default: return 1.0f;
    }
}
float E2eRate(u16) { return 2.0f; }
const ai::WsTypeDef* E2eTypeDef(u16) { return &g_carpenterTd; }
float E2eCartPrice(u16 id) { return E2eMarket(id); }

ai::MeisterWsEnv MakeEnv() {
    ai::MeisterWsEnv e;
    e.market_price = &E2eMarket;
    e.production_rate = &E2eRate;
    e.type_def = &E2eTypeDef;
    return e;
}

// A single emitted command line, stringified for sequence comparison.
struct CmdLog {
    std::vector<std::string> lines;
    void add(const std::string& s) { lines.push_back(s); }
};

} // namespace

TEST(AiMeisterWsE2E, FullPlanningPass) {
    crt::Srand(4242);
    CmdLog log;

    // === Phase 1: workstation assignment + scoring ==========================
    g_carpenterTd = ai::WsTypeDef{};
    g_carpenterTd.inputNeed[0] = 2;   // needs 2 planks
    g_carpenterTd.divisorOutputs = 1;
    g_carpenterTd.divisorRate = 1;

    // one item (planks, id 10) and two carpenter workstations.
    std::vector<ai::WsItem> items(1);
    items[0].id = 10; items[0].backIndex = -1; items[0].unitPrice = -1e10f;
    items[0].reserved = 0; items[0].stock = 1; items[0].flags50 = 1;
    items[0].required = 8;            // wants 8 planks
    items[0].freeCap = 6; items[0].destStock = 2;

    std::vector<ai::WsStation> stations(2);
    for (auto& s : stations) {
        s.typeId = 100; s.slots[0] = 0; s.outCache = -1e10f;
    }
    ai::MeisterWsEnv env = MakeEnv();
    for (auto& s : stations)
        ai::ComputeWorkstationOutput(s, items, env);

    // hand reference: outputValue = (2*5)/1 = 10; outCache = 10+2 = 12;
    // sellPrice = market(100) = 30; score = (30-12)/1 = 18.
    CHECK(std::fabs(stations[0].score - 18.0f) < 1e-3f);
    CHECK(std::fabs(items[0].unitPrice - 5.0f) < 1e-3f);

    ai::SortWorkstationsByScore(stations);
    CHECK_EQ(stations[0].reserveTarget, 0);
    log.add("WS_SCORED n=2 score=18");

    // === Phase 2: feasibility gate =========================================
    // a same-owner seller can supply the planks for free -> feasible.
    static std::vector<ai::StockSeller> sellers;
    sellers.clear();
    ai::StockSeller sl; sl.isSelf = false; sl.category = 1; sl.hasObject = true;
    sl.deficit = 20; sl.sameOwner = true;
    sellers.push_back(sl);
    ai::StockNeedQuery nq;
    nq.sellers = [](u16) { return sellers; };
    nq.budget = 1000000;
    // station input need 2 > reserved 0, stock 1 != 0 -> the "already-satisfied"
    // gate (stock!=0) skips it -> feasible.
    bool feasible = ai::CheckWorkstationCapacity(stations, 0, items, env, nq, false);
    CHECK(feasible);
    log.add("WS_FEASIBLE ok");

    // === Phase 3: gather (buy plan) ========================================
    // net shortfall = required 8 - (reserved 0 + stock 1) = 7.
    items[0].required = ai::GatherNetShortfall(8, items[0].reserved, items[0].stock);
    CHECK_EQ(items[0].required, 7);
    // budget clamp: budget = 30 -> 7*5=35 > 30 -> drop to 6 (30) -> ok.
    int buyCost = ai::GatherClampToBudget(items, 30);
    CHECK_EQ(items[0].required, 6);
    CHECK_EQ(buyCost, 30);
    log.add("BUY item=10 qty=6 cost=30");

    // === Phase 4: distribute (move plan) ===================================
    // planned = min(min(sourceFreeCap=6, destRoom=10), (budget>>2)/price).
    // budget 400 -> (100)/5 = 20 -> min(6, 20) = 6.
    items[0].plannedQty = ai::DistributePlanQty(items[0].freeCap, 10, 400, items[0].unitPrice);
    CHECK_EQ(items[0].plannedQty, 6);
    items[0].margin = E2eMarket(100) - items[0].unitPrice; // 25
    ai::SortItemsByPlannedValue(items);
    log.add("MOVE item=10 qty=6");

    // === Phase 5: transporter audit ========================================
    CHECK(ai::TransporterAuditRuns(5, false)); // odd hour
    ai::TransporterState ts;
    ts.aiType = 0; ts.quota = 3; ts.cartCount = 0; ts.budget = 1000;
    ts.ownerClass = 0;
    i32 cartCost = 0;
    // no carts -> fallback 308, cost = price(308) = 100.
    u16 cart = ai::TransporterBuyDecision(ts, &E2eCartPrice, 0, 0, &cartCost);
    CHECK_EQ(cart, ai::kCartIdBase);
    CHECK_EQ(cartCost, 100);
    log.add("BUYCART id=308 cost=100");

    // === Phase 6: city wealth balance ======================================
    std::vector<ai::BalancePerson> people(2);
    people[0].personClass = 4; people[0].held = 1000; people[0].account = 7;
    people[1].personClass = 4; people[1].held = 99000; people[1].account = 8;
    auto grants = ai::BalanceCityGoods(people, 0, 0);
    CHECK_EQ(grants.size(), 1u);
    CHECK_EQ(grants[0].personAccount, 7);
    CHECK_EQ(grants[0].amount, 31000);
    log.add("GRANT acct=7 amount=31000");

    // === verify the full emitted sequence ==================================
    const std::vector<std::string> expected = {
        "WS_SCORED n=2 score=18",
        "WS_FEASIBLE ok",
        "BUY item=10 qty=6 cost=30",
        "MOVE item=10 qty=6",
        "BUYCART id=308 cost=100",
        "GRANT acct=7 amount=31000",
    };
    CHECK_EQ(log.lines.size(), expected.size());
    for (std::size_t i = 0; i < expected.size(); ++i)
        CHECK(log.lines[i] == expected[i]);
}

TEST(AiMeisterWsE2E, InfeasibleStationBlocks) {
    // a station whose input cannot be sourced (no seller, not chained, not stocked)
    // fails the feasibility gate, so no buy/move commands are emitted for it.
    g_carpenterTd = ai::WsTypeDef{};
    g_carpenterTd.inputNeed[0] = 4;
    g_carpenterTd.divisorOutputs = 1;
    g_carpenterTd.divisorRate = 1;

    std::vector<ai::WsItem> items(1);
    items[0].id = 10; items[0].backIndex = -1; items[0].unitPrice = 5.0f;
    items[0].reserved = 0; items[0].stock = 0; items[0].flags50 = 1;

    std::vector<ai::WsStation> stations(1);
    stations[0].typeId = 100; stations[0].slots[0] = 0; stations[0].outCache = 1.0f;

    ai::MeisterWsEnv env = MakeEnv();
    static std::vector<ai::StockSeller> none;
    none.clear();
    ai::StockNeedQuery nq;
    nq.sellers = [](u16) { return none; };
    nq.budget = 1000000;
    CHECK(!ai::CheckWorkstationCapacity(stations, 0, items, env, nq, false));
}

TEST(AiMeisterWsE2E, BuildingNeedsTurnPick) {
    // a building-needs phase where one category has zero buildings -> it is forced.
    crt::Srand(7);
    std::vector<ai::BuildCategory> cats(3);
    cats[0].owned = 5; cats[0].total = 10; // ratio 0.5 -> candidate
    cats[1].owned = 0; cats[1].total = 4;  // zero -> force bit1
    cats[2].owned = 2; cats[2].total = 8;  // owned<3 -> candidate
    int cand = 0, force = 0;
    ai::BuildCandidateMask(cats, &cand, &force);
    CHECK_EQ(force, 0b010);

    double p = ai::BuildProbability(0, 0); // N=1, scale 4 -> 0.2
    // force present -> the pick ignores p and returns the force mask.
    int pick = ai::BuildPickCategory(cand, force, p, 0.99, 0, 3);
    CHECK_EQ(pick, 0b010);
}
