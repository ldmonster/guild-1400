// Unit tests for the MeisterAi trade + workstation + build-tasks ORCHESTRATION
// bodies (gilde.exe 0x45f1e4 / 0x4599f0 / 0x45a62c / 0x45c10c / 0x45bd68 /
// 0x45e71c / 0x4c7774). Golden vectors are hand-computed from the recovered
// decision rules; the table-build sweeps are checked against a synthetic faction.
#include "tests/framework/test.h"

#include "ai/meister_storage.h"
#include "ai/meister_general_trade.h"
#include "ai/meister_buildtasks.h"
#include "ai/meister_workstation.h"

using namespace guild;
using namespace guild::ai;

// ---------------------------------------------------------------------------
// Storage sell-decision cores
// ---------------------------------------------------------------------------
TEST(MeisterTrade, StorageDemandBump) {
    // not flagged (bits&8 clear) -> no bump.
    CHECK_EQ(StorageDemandBump(/*bits*/0, /*stock*/100, /*slotCap*/30, false), 0);
    // matched (bits&6) -> no bump even if flagged.
    CHECK_EQ(StorageDemandBump(/*bits*/0x8 | 0x2, 100, 30, false), 0);
    // flagged, stock 100 > slotCap/3 (10) -> bump to stock.
    CHECK_EQ(StorageDemandBump(0x8, 100, 30, false), 100);
    // AiType 22 -> halve.
    CHECK_EQ(StorageDemandBump(0x8, 100, 30, true), 50);
    // stock below slotCap/3 -> no bump.
    CHECK_EQ(StorageDemandBump(0x8, 5, 30, false), 0);
}

TEST(MeisterTrade, ProfitMarginSell) {
    // ratio sell/buy = 50/100 = 0.5 < 1.0, threshold 0.6 -> 0.5 <= 0.6 -> sell all.
    CHECK_EQ(ProfitMarginSell(/*bits*/0, /*stock*/40, /*sell*/50.f, /*buy*/100.f,
                              /*thr*/0.6, /*storage*/true, /*cap*/3), 40);
    // ratio 0.5 but threshold 0.4 -> 0.5 > 0.4 -> no sell.
    CHECK_EQ(ProfitMarginSell(0, 40, 50.f, 100.f, 0.4, true, 3), 0);
    // ratio >= 1.0 -> never sells.
    CHECK_EQ(ProfitMarginSell(0, 40, 120.f, 100.f, 0.99, true, 3), 0);
    // matched (bits&6) -> no sell.
    CHECK_EQ(ProfitMarginSell(0x2, 40, 50.f, 100.f, 0.9, true, 3), 0);
    // storage variant, bits&8 set, stock 3 == cartCap -> gated out.
    CHECK_EQ(ProfitMarginSell(0x8, 3, 50.f, 100.f, 0.9, true, 3), 0);
    // storage variant, bits&8 set, stock 4 > cartCap -> proceeds.
    CHECK_EQ(ProfitMarginSell(0x8, 4, 50.f, 100.f, 0.9, true, 3), 4);
    // general variant, bits&8 set, stock 4 > slotCap/3 (3) -> proceeds.
    CHECK_EQ(ProfitMarginSell(0x8, 4, 50.f, 100.f, 0.9, false, 9), 4);
}

TEST(MeisterTrade, EmergencySell) {
    int proceeds = 0;
    // price 100, qty = 32000/100 = 320, clamped to stock 10 -> sell 10, proceeds
    // accrue the un-clamped 320*100 = 32000.
    CHECK_EQ(EmergencySell(0, 10, 100, &proceeds), 10);
    CHECK_EQ(proceeds, 32000);
    // proceeds now >= 32000 -> next call sells 0.
    CHECK_EQ(EmergencySell(0, 10, 100, &proceeds), 0);
    // matched -> 0.
    int p2 = 0;
    CHECK_EQ(EmergencySell(0x4, 10, 100, &p2), 0);
    // price 0 -> guarded out.
    int p3 = 0;
    CHECK_EQ(EmergencySell(0, 10, 0, &p3), 0);
}

TEST(MeisterTrade, OverstockSell) {
    // 3*slotCap/4 = 3*40/4 = 30; stock 100 > 30 -> sell max(5, 100/4=25) = 25.
    CHECK_EQ(OverstockSell(0, 100, 40), 25);
    // small overstock: stock 31 > 30, 31/4=7 -> 7.
    CHECK_EQ(OverstockSell(0, 31, 40), 7);
    // floor: stock 32 (>30) but 32/4=8 -> 8; stock 31->7; check floor with stock
    // just over line but /4 < 5: slotCap 8 -> 3*8/4=6, stock 7 -> 7/4=1 -> floor 5,
    // clamp to stock 7 -> 5.
    CHECK_EQ(OverstockSell(0, 7, 8), 5);
    // below line -> 0.
    CHECK_EQ(OverstockSell(0, 20, 40), 0);
    // matched -> 0.
    CHECK_EQ(OverstockSell(0x2, 100, 40), 0);
}

// ---------------------------------------------------------------------------
// AssignWorkstations table build
// ---------------------------------------------------------------------------
namespace {
// a tiny deterministic env: two workstation types.
static const WsTypeDef* g_typedef(u16 typeId) {
    static WsTypeDef tdA; // station 700: needs item 50 (count 2) in slot 0.
    static WsTypeDef tdB; // station 701: no inputs.
    static bool init = false;
    if (!init) {
        tdA.inputNeed[0] = 2; tdA.inputNeed[1] = 0;
        tdA.divisorOutputs = 2; tdA.divisorRate = 4;
        tdB.divisorOutputs = 1; tdB.divisorRate = 1;
        init = true;
    }
    if (typeId == 700) return &tdA;
    return &tdB;
}
static float g_market(u16 id) {
    if (id == 50) return 10.0f;   // input price
    if (id == 700) return 40.0f;  // station 700 output sells for 40
    if (id == 701) return 20.0f;
    return 5.0f;
}
static float g_rate(u16) { return 1.0f; }
static int g_stock(u16 id) { return id == 50 ? 0 : 8; }
static int g_free(u16) { return 1000; }
} // namespace

TEST(MeisterTrade, AssignWorkstationsBuildsBuffers) {
    StorageEnv env{};
    env.ws.market_price = g_market;
    env.ws.production_rate = g_rate;
    env.ws.type_def = g_typedef;
    env.stock_of = g_stock;
    env.free_cap = g_free;

    std::vector<WsCandidate> cands;
    WsCandidate c0; c0.typeId = 700; c0.count = 5; c0.inputId[0] = 50; cands.push_back(c0);
    WsCandidate c1; c1.typeId = 701; c1.count = 3; cands.push_back(c1);
    WsCandidate c2; c2.typeId = 702; c2.count = 1; cands.push_back(c2); // count<=1 -> skipped

    std::vector<WsDelivery> deliv;
    deliv.push_back(WsDelivery{700, 3}); // 3 units of station-700 output incoming.

    std::vector<WsStation> stations;
    std::vector<WsItem> items;
    std::vector<StorageCommand> out;
    AssignWorkstations(cands, deliv, /*special*/false, stations, items, env, out);

    // two stations (700, 701); the count<=1 candidate is dropped.
    CHECK_EQ(stations.size(), (size_t)2);
    // one input item (id 50) was expanded from station 700's slot 0.
    CHECK_EQ(items.size(), (size_t)1);
    CHECK_EQ(items[0].id, (u16)50);
    // item 50 has zero stock -> a Notify20 command was emitted.
    bool sawNotify = false;
    for (auto& cmd : out)
        if (cmd.kind == StorageCmd::Notify20 && cmd.itemId == 50) sawNotify = true;
    CHECK(sawNotify);
    // station 700 incoming folded the delivery.
    int inc700 = -1;
    for (auto& s : stations) if (s.typeId == 700) inc700 = s.incoming;
    CHECK_EQ(inc700, 3);
    // station 700 output value = need(2)*price(10)/divisorOutputs(2) = 10; rate 1;
    // outCache 11; sell 40; score = (40-11)/divisorRate(4) = 7.25.
    for (auto& s : stations) {
        if (s.typeId == 700) {
            CHECK(s.outputValue > 9.99f && s.outputValue < 10.01f);
            CHECK(s.score > 7.24f && s.score < 7.26f);
        }
    }
}

// ---------------------------------------------------------------------------
// CollectStorageItems
// ---------------------------------------------------------------------------
TEST(MeisterTrade, CollectStorageItems) {
    StorageEnv env{};
    env.ws.market_price = g_market;
    env.ws.production_rate = g_rate;
    env.ws.type_def = g_typedef;
    env.stock_of = g_stock;
    env.free_cap = g_free;

    std::vector<u16> imports = {50, 700, 0};
    std::vector<u16> exports = {701, 50, 0}; // 50 duplicated -> skipped
    std::vector<WsDelivery> deliv = {WsDelivery{700, 4}};
    std::vector<WsItem> items;
    CollectStorageItems(imports, exports, deliv, items, env);

    // 3 distinct items: 50, 700, 701.
    CHECK_EQ(items.size(), (size_t)3);
    // item 700 has the delivery folded into reserved.
    for (auto& it : items)
        if (it.id == 700) CHECK_EQ(it.reserved, 4);
    // item 50 buy price recovered.
    for (auto& it : items)
        if (it.id == 50) CHECK(it.unitPrice > 9.9f && it.unitPrice < 10.1f);
}

// ---------------------------------------------------------------------------
// GatherRequiredItems
// ---------------------------------------------------------------------------
namespace {
static void* g_seller_yes(u16) { static int dummy; return &dummy; }
static void* g_seller_no(u16) { return nullptr; }
} // namespace

TEST(MeisterTrade, GatherRequiredItems) {
    std::vector<u16> imports = {50, 0};
    std::vector<u16> exports = {0};
    std::vector<WsItem> items;
    WsItem it; it.id = 50; it.unitPrice = 10.0f; it.stock = 1; it.reserved = 0;
    items.push_back(it);

    // three consumers need item 50 -> required becomes 3; net 3-(0+1)=2; seller ok;
    // budget 25 affords 2 units (2*10=20 <= 25) -> cost 20.
    std::vector<GatherNeed> needs = {{50}, {50}, {50}};
    int cost = GatherRequiredItems(needs, imports, exports, items, /*budget*/25,
                                   g_seller_yes);
    CHECK_EQ(items[0].required, 2);
    CHECK_EQ(cost, 20);

    // no seller -> required dropped to 0.
    std::vector<WsItem> items2;
    WsItem it2; it2.id = 50; it2.unitPrice = 10.0f; it2.stock = 0; items2.push_back(it2);
    int cost2 = GatherRequiredItems(needs, imports, exports, items2, 25, g_seller_no);
    CHECK_EQ(items2[0].required, 0);
    CHECK_EQ(cost2, 0);
}

// ---------------------------------------------------------------------------
// CollectTransporters reroute + buy
// ---------------------------------------------------------------------------
namespace {
static float g_cart_price(u16 id) {
    if (id == kCartIdBase) return 1000.0f;
    if (id == kCartIdMid)  return 1500.0f;
    if (id == kCartIdHigh) return 2000.0f;
    return 0.0f;
}
} // namespace

TEST(MeisterTrade, CartIsLostRule) {
    CartNode c{};
    c.routeBuildingOwner = 99; // a different living owner
    CHECK(CartIsLost(c, /*own*/7));
    c.hasProdHandler = true; // active -> not lost
    CHECK(!CartIsLost(c, 7));
    c.hasProdHandler = false; c.routeBuildingOwner = 7; // owned by us -> not lost
    CHECK(!CartIsLost(c, 7));
    c.routeBuildingOwner = 0; // dead chain -> not rerouted
    CHECK(!CartIsLost(c, 7));
}

TEST(MeisterTrade, CollectTransportersReroutesAndBuys) {
    std::vector<CartNode> carts;
    CartNode lost{};   lost.goodId = 308; lost.nodeId = 11; lost.routeBuildingOwner = 99;
    CartNode active{}; active.goodId = 310; active.nodeId = 12; active.hasRouteHandler = true;
    carts.push_back(lost);
    carts.push_back(active);

    // caravan (type 9) faction wanting more carts: quota 5 > 2 carts; highCount will
    // be 1 (the id-310 cart); roll_750=1 (<2 hit), roll_8>=4 buys 310, price 2000,
    // 2*2000=4000 < budget 9000.
    TransporterState st{};
    st.aiType = 9; st.quota = 5; st.budget = 9000; st.ownerAccount = 77;

    std::vector<FleetCommand> out;
    int n = CollectTransporters(carts, /*own*/7, st, g_cart_price, /*roll750*/1,
                                /*roll8*/4, out);
    CHECK_EQ(n, 2); // two carts seen
    // a reroute for the lost cart + a buy.
    bool sawReroute = false, sawBuy = false;
    for (auto& cmd : out) {
        if (cmd.kind == FleetCmd::Reroute && cmd.cartNodeId == 11) sawReroute = true;
        if (cmd.kind == FleetCmd::BuyCart) { sawBuy = true; CHECK_EQ(cmd.goodId, (u16)310); }
    }
    CHECK(sawReroute);
    CHECK(sawBuy);
}

// ---------------------------------------------------------------------------
// ProcessBuildingNeeds tally + select
// ---------------------------------------------------------------------------
namespace {
static int g_worth(int, int) { return 4242; }
static int g_variant(int aiType, int) { return aiType * 10; }
static int g_group(int aiType) { return aiType; }
} // namespace

TEST(MeisterTrade, BuildTallySweep) {
    std::vector<BuildSlot> slots;
    BuildSlot a{}; a.alive = true; a.aiType = 18; a.ownedOrFree = true;  slots.push_back(a);
    BuildSlot b{}; b.alive = true; b.aiType = 18; b.ownedOrFree = false; slots.push_back(b);
    BuildSlot c{}; c.alive = true; c.aiType = 7;  c.ownedOrFree = false; c.guildLeader = true; slots.push_back(c);
    BuildSlot d{}; d.alive = true; d.aiType = 7;  d.ownedOrFree = false; d.guildLeader = false; slots.push_back(d);
    BuildSlot dead{}; dead.alive = false; dead.aiType = 18; slots.push_back(dead);

    std::vector<int> totals, owned;
    int members = -1, leaders = -1;
    BuildTallySweep(slots, totals, owned, &members, &leaders);
    CHECK_EQ(totals[18], 2); // two living type-18 (dead skipped)
    CHECK_EQ(owned[18], 1);  // one owned
    CHECK_EQ(totals[7], 2);
    CHECK_EQ(leaders, 1);
    CHECK_EQ(members, 1);
}

TEST(MeisterTrade, SelectBuildForcesZeroCount) {
    // phase 2 categories {18,21,20,14}. type 20 owned 0 -> force bit -> build 20.
    std::vector<int> totals(23, 5), owned(23, 5);
    owned[20] = 0; totals[20] = 5;
    BuildEnv env{}; env.worth_of = g_worth; env.variant_of = g_variant; env.group_of = g_group;
    std::vector<BuildCommand> out;
    int picked = SelectBuildToConstruct(/*phase*/2, /*tick*/40, /*diff*/2,
                                        totals, owned, /*rollf*/0.9, /*rollk*/0, env, out);
    CHECK_EQ(picked, 20);
    CHECK_EQ(out.size(), (size_t)1);
    CHECK_EQ(out[0].aiType, 20);
    CHECK_EQ(out[0].fundWorth, 4242);
    CHECK_EQ(out[0].variant, 200);
}

TEST(MeisterTrade, SelectBuildProbabilityGate) {
    // no force bits (all owned>=3, all candidate via owned<3? no -> candidate needs
    // owned<3 OR ratio>=0.5). Make all owned 5, total 5 -> ratio 1.0 >= 0.5 ->
    // every category is a candidate, no force. roll_float > p -> no build.
    std::vector<int> totals(23, 5), owned(23, 5);
    BuildEnv env{}; env.worth_of = g_worth; env.variant_of = g_variant; env.group_of = g_group;
    std::vector<BuildCommand> out;
    // tick 4 -> N=(4>>2)+1=2; diff 0 -> scale 4.0; p = 2/(4+2)=0.333. roll 0.9 > p.
    int picked = SelectBuildToConstruct(/*phase*/3, /*tick*/4, /*diff*/0,
                                        totals, owned, /*rollf*/0.9, /*rollk*/0, env, out);
    CHECK_EQ(picked, -1);
    CHECK_EQ(out.size(), (size_t)0);

    // deferred phase 0/1 -> -1.
    std::vector<BuildCommand> out2;
    CHECK_EQ(SelectBuildToConstruct(0, 4, 0, totals, owned, 0.0, 0, env, out2), -1);
    CHECK_EQ(SelectBuildToConstruct(1, 4, 0, totals, owned, 0.0, 0, env, out2), -1);
}
