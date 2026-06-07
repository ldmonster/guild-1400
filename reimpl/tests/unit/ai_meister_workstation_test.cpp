// Unit tests for the MeisterAi workstation / trade-logistics / building-needs
// planning RULES (guild::ai):
//   * workstation output value/rate/margin + score sort (ComputeWorkstationOutput,
//     SortWorkstationsByScore, SortItemsByPlannedValue)
//   * item shortfall netting + budget clamp (GatherNetShortfall/ClampToBudget)
//   * distribute plan-quantity clamp (DistributePlanQty)
//   * recursive capacity feasibility (CheckWorkstationCapacity)
//   * transporter buy decision, tavern refill, city wealth balance
//   * building-needs probability / candidate-mask / category pick
#include "tests/framework/test.h"

#include "ai/meister_workstation.h"
#include "ai/meister_trade2.h"
#include "ai/building_needs.h"
#include "crt/rand.h"
#include "util/math_random.h"

#include <vector>
#include <cmath>

using namespace guild;

// ---------------------------------------------------------------------------
// ComputeWorkstationOutput — golden vs. hand-computed.
// ---------------------------------------------------------------------------
namespace {
ai::WsTypeDef g_td;
float FakeMarket(u16 id) {
    // distinct prices per id so the sum is deterministic.
    switch (id) {
    case 100: return 10.0f; // workstation output good
    case 10:  return 2.0f;  // input A
    case 11:  return 4.0f;  // input B
    default:  return 1.0f;
    }
}
float FakeRate(u16) { return 1.0f; }
const ai::WsTypeDef* FakeTypeDef(u16) { return &g_td; }
ai::MeisterWsEnv MakeWsEnv() {
    ai::MeisterWsEnv e;
    e.market_price = &FakeMarket;
    e.production_rate = &FakeRate;
    e.type_def = &FakeTypeDef;
    return e;
}
} // namespace

TEST(AiMeisterWs, ComputeOutputGolden) {
    g_td = ai::WsTypeDef{};
    g_td.inputNeed[0] = 3; // need 3 of input A (price 2)
    g_td.inputNeed[1] = 2; // need 2 of input B (price 4)
    g_td.divisorOutputs = 5;
    g_td.divisorRate = 4;

    std::vector<ai::WsItem> items(2);
    items[0].id = 10; items[0].backIndex = -1; items[0].unitPrice = -1e10f;
    items[1].id = 11; items[1].backIndex = -1; items[1].unitPrice = -1e10f;

    ai::WsStation s;
    s.typeId = 100;
    s.slots[0] = 0; s.slots[1] = 1; s.slots[2] = -1; s.slots[3] = -1;
    s.outCache = -1e10f;

    ai::MeisterWsEnv env = MakeWsEnv();
    ai::ComputeWorkstationOutput(s, items, env);

    // outputValue = (3*2 + 2*4) / 5 = 14/5 = 2.8.
    CHECK(std::fabs(s.outputValue - 2.8f) < 1e-4f);
    // rate = 1.0; outCache = 2.8 + 1.0 = 3.8.
    CHECK(std::fabs(s.outCache - 3.8f) < 1e-4f);
    // sellPrice = market(100) = 10; margin = (10 - 3.8) / 4 = 1.55.
    CHECK(std::fabs(s.sellPrice - 10.0f) < 1e-4f);
    CHECK(std::fabs(s.score - 1.55f) < 1e-4f);

    // item prices were filled in.
    CHECK(std::fabs(items[0].unitPrice - 2.0f) < 1e-4f);
    CHECK(std::fabs(items[1].unitPrice - 4.0f) < 1e-4f);
}

TEST(AiMeisterWs, ComputeOutputChainDiscount) {
    // a chained input (backIndex != -1) is priced at 0.9 * market.
    g_td = ai::WsTypeDef{};
    g_td.inputNeed[0] = 1;
    g_td.divisorOutputs = 1;
    g_td.divisorRate = 1;

    std::vector<ai::WsItem> items(1);
    items[0].id = 10; items[0].backIndex = 0; items[0].unitPrice = -1e10f;

    ai::WsStation s;
    s.typeId = 100; s.slots[0] = 0; s.outCache = -1e10f;
    ai::MeisterWsEnv env = MakeWsEnv();
    ai::ComputeWorkstationOutput(s, items, env);
    // input price 2.0 * 0.9 = 1.8.
    CHECK(std::fabs(items[0].unitPrice - 1.8f) < 1e-4f);
    CHECK(std::fabs(s.outputValue - 1.8f) < 1e-4f);
}

TEST(AiMeisterWs, ComputeOutputCacheGate) {
    // outCache above the sentinel -> no recompute.
    std::vector<ai::WsItem> items(1);
    ai::WsStation s;
    s.outCache = 5.0f;
    s.outputValue = 99.0f;
    ai::MeisterWsEnv env = MakeWsEnv();
    ai::ComputeWorkstationOutput(s, items, env);
    CHECK_EQ(s.outputValue, 99.0f); // unchanged
}

// ---------------------------------------------------------------------------
// Sorting.
// ---------------------------------------------------------------------------
TEST(AiMeisterWs, SortWorkstationsAscending) {
    std::vector<ai::WsStation> v(3);
    v[0].score = 5.0f; v[0].typeId = 1;
    v[1].score = 1.0f; v[1].typeId = 2;
    v[2].score = 3.0f; v[2].typeId = 3;
    ai::SortWorkstationsByScore(v);
    CHECK_EQ(v[0].score, 1.0f);
    CHECK_EQ(v[1].score, 3.0f);
    CHECK_EQ(v[2].score, 5.0f);
    // reserveTarget stamped to final index.
    CHECK_EQ(v[0].reserveTarget, 0);
    CHECK_EQ(v[1].reserveTarget, 1);
    CHECK_EQ(v[2].reserveTarget, 2);
}

TEST(AiMeisterWs, SortItemsByPlannedValueDesc) {
    std::vector<ai::WsItem> v(3);
    v[0].plannedQty = 2; v[0].margin = 1.0f; v[0].id = 1; // 2.0
    v[1].plannedQty = 5; v[1].margin = 2.0f; v[1].id = 2; // 10.0
    v[2].plannedQty = 1; v[2].margin = 3.0f; v[2].id = 3; // 3.0
    ai::SortItemsByPlannedValue(v);
    CHECK_EQ(v[0].id, 2); // 10
    CHECK_EQ(v[1].id, 3); // 3
    CHECK_EQ(v[2].id, 1); // 2
    CHECK_EQ(v[0].flags50, 0);
    CHECK_EQ(v[2].flags50, 2);
}

// ---------------------------------------------------------------------------
// Gather shortfall + budget clamp.
// ---------------------------------------------------------------------------
TEST(AiMeisterWs, GatherNetShortfall) {
    CHECK_EQ(ai::GatherNetShortfall(10, 3, 2), 5);   // 10-(3+2)
    CHECK_EQ(ai::GatherNetShortfall(4, 3, 2), 0);    // 4-5 -> clamp 0
    CHECK_EQ(ai::GatherNetShortfall(0, 0, 0), 0);
}

TEST(AiMeisterWs, GatherClampToBudget) {
    std::vector<ai::WsItem> v(2);
    v[0].required = 5; v[0].unitPrice = 10.0f; // wants 50
    v[1].required = 4; v[1].unitPrice = 20.0f; // wants 80
    // budget 100: item0 5*10=50 fits (accum 50); item1 4*20=80 -> 50+80=130>100
    //   drop to 3 (60) -> 50+60=110>100 drop to 2 (40) -> 50+40=90<=100 ok.
    int cost = ai::GatherClampToBudget(v, 100);
    CHECK_EQ(v[0].required, 5);
    CHECK_EQ(v[1].required, 2);
    CHECK_EQ(cost, 90);
}

TEST(AiMeisterWs, GatherClampZeroBudget) {
    std::vector<ai::WsItem> v(1);
    v[0].required = 5; v[0].unitPrice = 10.0f;
    int cost = ai::GatherClampToBudget(v, 0);
    CHECK_EQ(v[0].required, 0);
    CHECK_EQ(cost, 0);
}

// ---------------------------------------------------------------------------
// Distribute plan-quantity clamp.
// ---------------------------------------------------------------------------
TEST(AiMeisterWs, DistributePlanQty) {
    // raw = min(sourceFreeCap=20, destRoom=15) = 15.
    // budgetQty = (budget>>2)/price = (400>>2=100)/10 = 10.
    // planned = min(15, 10) = 10.
    CHECK_EQ(ai::DistributePlanQty(20, 15, 400, 10.0f), 10);
    // raw smaller: min(5, 15)=5; budgetQty 10 -> 5.
    CHECK_EQ(ai::DistributePlanQty(5, 15, 400, 10.0f), 5);
    // budget tiny: (4>>2=1)/10 = 0.1 -> trunc 0.
    CHECK_EQ(ai::DistributePlanQty(20, 15, 4, 10.0f), 0);
}

// ---------------------------------------------------------------------------
// CheckWorkstationCapacity recursion.
// ---------------------------------------------------------------------------
namespace {
std::vector<ai::StockSeller> g_sellers;
std::vector<ai::StockSeller> FakeSellers(u16) { return g_sellers; }
} // namespace

TEST(AiMeisterWs, CapacityTopLevelInfeasible) {
    g_td = ai::WsTypeDef{};
    g_td.inputNeed[0] = 5;
    std::vector<ai::WsItem> items(1);
    items[0].id = 10; items[0].backIndex = -1; items[0].reserved = 0; items[0].flags50 = 1;
    std::vector<ai::WsStation> st(1);
    st[0].typeId = 100; st[0].slots[0] = 0;
    ai::MeisterWsEnv env = MakeWsEnv();
    ai::StockNeedQuery nq; nq.sellers = &FakeSellers; nq.budget = 1000000;
    // top level: need 5 > reserved 0 -> infeasible immediately.
    CHECK(!ai::CheckWorkstationCapacity(st, 0, items, env, nq, true));
}

TEST(AiMeisterWs, CapacitySellerFeasible) {
    g_td = ai::WsTypeDef{};
    g_td.inputNeed[0] = 5;
    std::vector<ai::WsItem> items(1);
    items[0].id = 10; items[0].backIndex = -1; items[0].reserved = 0;
    items[0].flags50 = 1; items[0].stock = 0; items[0].unitPrice = 2.0f;
    std::vector<ai::WsStation> st(1);
    st[0].typeId = 100; st[0].slots[0] = 0;
    ai::MeisterWsEnv env = MakeWsEnv();

    // a non-self market seller with deficit 10, same owner -> free transfer -> ok.
    g_sellers.clear();
    ai::StockSeller sl; sl.isSelf = false; sl.category = 1; sl.hasObject = true;
    sl.deficit = 10; sl.sameOwner = true;
    g_sellers.push_back(sl);
    ai::StockNeedQuery nq; nq.sellers = &FakeSellers; nq.budget = 1000000;
    CHECK(ai::CheckWorkstationCapacity(st, 0, items, env, nq, false));

    // no qualifying seller (category 2 = market, skipped) -> infeasible.
    g_sellers.clear();
    ai::StockSeller m; m.category = 2; m.hasObject = true; m.deficit = 10;
    g_sellers.push_back(m);
    CHECK(!ai::CheckWorkstationCapacity(st, 0, items, env, nq, false));
}

TEST(AiMeisterWs, CapacitySpecialItemAlwaysOk) {
    g_td = ai::WsTypeDef{};
    g_td.inputNeed[0] = 5;
    std::vector<ai::WsItem> items(1);
    items[0].id = 452; items[0].backIndex = -1; items[0].reserved = 0;
    items[0].flags50 = 1; items[0].stock = 0;
    std::vector<ai::WsStation> st(1);
    st[0].typeId = 100; st[0].slots[0] = 0;
    ai::MeisterWsEnv env = MakeWsEnv();
    ai::StockNeedQuery nq; nq.sellers = &FakeSellers; nq.budget = 0;
    // id 452 is a "service" id -> always feasible regardless of sellers.
    CHECK(ai::CheckWorkstationCapacity(st, 0, items, env, nq, false));
}

// ---------------------------------------------------------------------------
// Transporter buy decision.
// ---------------------------------------------------------------------------
namespace {
float FakeCartPrice(u16 id) {
    switch (id) {
    case ai::kCartIdBase: return 100.0f;
    case ai::kCartIdMid:  return 200.0f;
    case ai::kCartIdHigh: return 300.0f;
    default: return 50.0f;
    }
}
} // namespace

TEST(AiMeisterWs, TransporterAuditGate) {
    CHECK(!ai::TransporterAuditRuns(4, false)); // even hour -> no
    CHECK(ai::TransporterAuditRuns(5, false));  // odd, flag clear -> yes
    CHECK(!ai::TransporterAuditRuns(5, true));  // odd, flag set -> no
}

TEST(AiMeisterWs, TransporterBuyCaravan) {
    ai::TransporterState st;
    st.aiType = 9;
    st.quota = 5;
    st.cartCount = 2;
    st.highCartCount = 1; // != cartCount, and >0
    st.busy = false;
    st.budget = 1000;
    i32 cost = 0;
    // roll_750 = 1 (<2 ok), roll_8 = 5 (>=4 -> 310). price(310)=300, 2*300=600<1000.
    u16 buy = ai::TransporterBuyDecision(st, &FakeCartPrice, 1, 5, &cost);
    CHECK_EQ(buy, ai::kCartIdHigh);
    CHECK_EQ(cost, 300);

    // roll_8 = 0 -> 309. price 200, 2*200=400<1000.
    buy = ai::TransporterBuyDecision(st, &FakeCartPrice, 1, 0, &cost);
    CHECK_EQ(buy, ai::kCartIdMid);

    // roll_750 = 2 -> gate fails -> no buy.
    buy = ai::TransporterBuyDecision(st, &FakeCartPrice, 2, 5, &cost);
    CHECK_EQ(buy, 0);

    // affordability: budget 500, 2*300=600 >= 500 -> no buy.
    st.budget = 500;
    buy = ai::TransporterBuyDecision(st, &FakeCartPrice, 1, 5, &cost);
    CHECK_EQ(buy, 0);
}

TEST(AiMeisterWs, TransporterBuyCaravanBalanced) {
    ai::TransporterState st;
    st.aiType = 9; st.quota = 5; st.cartCount = 2; st.highCartCount = 2;
    st.budget = 100000;
    i32 cost = 0;
    // cartCount == highCartCount -> balanced -> no buy in this branch.
    CHECK_EQ(ai::TransporterBuyDecision(st, &FakeCartPrice, 0, 0, &cost), 0);
}

TEST(AiMeisterWs, TransporterBuyNonCaravan) {
    ai::TransporterState st;
    st.aiType = 0; st.quota = 5; st.cartCount = 1; st.budget = 1000;
    st.ownerClass = 0; st.busy = false;
    i32 cost = 0;
    // non-caravan, cartCount>0 -> buy 309 (price 200, 2*200<1000).
    CHECK_EQ(ai::TransporterBuyDecision(st, &FakeCartPrice, 0, 0, &cost), ai::kCartIdMid);

    // human class 6 without flag -> blocked.
    st.ownerClass = 6;
    CHECK_EQ(ai::TransporterBuyDecision(st, &FakeCartPrice, 0, 0, &cost), 0);
    st.flag436_4 = true;
    CHECK_EQ(ai::TransporterBuyDecision(st, &FakeCartPrice, 0, 0, &cost), ai::kCartIdMid);
}

TEST(AiMeisterWs, TransporterBuyFallbackBase) {
    ai::TransporterState st;
    st.aiType = 0; st.quota = 5; st.cartCount = 0; st.budget = 1000;
    i32 cost = 0;
    // no carts -> fallback 308.
    CHECK_EQ(ai::TransporterBuyDecision(st, &FakeCartPrice, 0, 0, &cost), ai::kCartIdBase);
    CHECK_EQ(cost, 100);
}

// ---------------------------------------------------------------------------
// Tavern refill.
// ---------------------------------------------------------------------------
TEST(AiMeisterWs, TavernRefill) {
    // fill 30, roll 5: 30 < 40+5=45 -> refill 100-30=70.
    CHECK_EQ(ai::TavernRefillAmount(30, 5), 70);
    // fill 60, roll 5: 60 >= 45 -> no.
    CHECK_EQ(ai::TavernRefillAmount(60, 5), 0);
    // fill 44, roll 5: 44 < 45 -> refill 56.
    CHECK_EQ(ai::TavernRefillAmount(44, 5), 56);
}

// ---------------------------------------------------------------------------
// City wealth balance.
// ---------------------------------------------------------------------------
TEST(AiMeisterWs, CityWealthFloor) {
    CHECK_EQ(ai::CityWealthFloor(0), 32000);
    CHECK_EQ(ai::CityWealthFloor(3), 32000 + 24000);
}

TEST(AiMeisterWs, BalancePersonGrant) {
    bool stop = false;
    int floor = ai::CityWealthFloor(0); // 32000
    // class 4 always: held 10000 -> grant 22000.
    ai::BalancePerson p; p.personClass = 4; p.held = 10000; p.account = 1;
    CHECK_EQ(ai::BalancePersonGrant(p, 0, floor, &stop), 22000);
    CHECK(!stop);
    // class 4 wealthy -> no grant.
    p.held = 40000;
    CHECK_EQ(ai::BalancePersonGrant(p, 0, floor, &stop), 0);
    // class 5 -> stop.
    p.personClass = 5; p.held = 0;
    CHECK_EQ(ai::BalancePersonGrant(p, 0, floor, &stop), 0);
    CHECK(stop);
    // class 2 staggered: tick%4 == id&3.
    p.personClass = 2; p.held = 0; p.personId = 5; // 5&3 = 1
    CHECK_EQ(ai::BalancePersonGrant(p, 1, floor, &stop), 32000); // tick 1 matches
    CHECK_EQ(ai::BalancePersonGrant(p, 2, floor, &stop), 0);     // tick 2 no
}

TEST(AiMeisterWs, BalanceCityGoodsScan) {
    std::vector<ai::BalancePerson> people(4);
    people[0].personClass = 4; people[0].held = 1000; people[0].account = 10;
    people[1].personClass = 4; people[1].held = 50000; people[1].account = 11; // rich
    people[2].personClass = 5; people[2].account = 12; // STOP
    people[3].personClass = 4; people[3].held = 0; people[3].account = 13; // never reached
    auto grants = ai::BalanceCityGoods(people, 0, 0);
    CHECK_EQ(grants.size(), 1u);
    CHECK_EQ(grants[0].personAccount, 10);
    CHECK_EQ(grants[0].amount, 31000);
}

// ---------------------------------------------------------------------------
// Building needs.
// ---------------------------------------------------------------------------
TEST(AiMeisterWs, BuildProbability) {
    // gameTick 0 -> N = (0>>2)+1 = 1; difficulty 4 -> scale 1.0; p = 1/(1+1)=0.5.
    double p = ai::BuildProbability(0, 4);
    CHECK(std::fabs(p - 0.5) < 1e-9);
    // gameTick 12 -> N = 3+1 = 4; difficulty 0 -> scale 4.0; p = 4/(4+4)=0.5.
    CHECK(std::fabs(ai::BuildProbability(12, 0) - 0.5) < 1e-9);
}

TEST(AiMeisterWs, BuildCandidateMask) {
    std::vector<ai::BuildCategory> cats(3);
    cats[0].owned = 0; cats[0].total = 10; // owned<3 & owned==0 -> cand+force bit0
    cats[1].owned = 5; cats[1].total = 10; // owned>=3, ratio 0.5>=0.5 -> cand bit1
    cats[2].owned = 5; cats[2].total = 20; // owned>=3, ratio 0.25<0.5 -> no bit2
    int cand = 0, force = 0;
    ai::BuildCandidateMask(cats, &cand, &force);
    CHECK_EQ(cand, 0b011);
    CHECK_EQ(force, 0b001);
}

TEST(AiMeisterWs, BuildPickCategory) {
    // force present -> return force outright (RNG/p ignored).
    CHECK_EQ(ai::BuildPickCategory(0b111, 0b010, 0.0, 0.99, 0, 3), 0b010);
    // no force, roll_float > p -> no build.
    CHECK_EQ(ai::BuildPickCategory(0b101, 0, 0.5, 0.9, 0, 3), 0);
    // no force, roll passes: candidate 0b100 (bit2), start at bit0 -> rotate to 2.
    CHECK_EQ(ai::BuildPickCategory(0b100, 0, 0.9, 0.1, 0, 3), 0b100);
    // start exactly on a set bit.
    CHECK_EQ(ai::BuildPickCategory(0b010, 0, 0.9, 0.1, 1, 3), 0b010);
    // empty candidate -> 0.
    CHECK_EQ(ai::BuildPickCategory(0, 0, 0.9, 0.1, 0, 3), 0);
}

TEST(AiMeisterWs, RunBuildingTasksOrder) {
    auto order = ai::RunBuildingTasksOrder();
    CHECK_EQ(order.size(), 6u);
    CHECK(order[0] == ai::BuildingTask::RequestBuildingCmd43);
    CHECK(order[5] == ai::BuildingTask::UpdateBuildingHealthState);
}
