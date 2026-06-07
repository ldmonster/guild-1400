// Unit tests for the combat order-tick non-attack state machine + strength(loot)
// + projectile-remainder modules (guild::sim).
#include "sim/combat_orders.h"
#include "sim/combat_strength.h"
#include "sim/combat_projectile2.h"
#include "crt/rand.h"

#include "test.h"

using namespace guild;
using namespace guild::sim;

// Build an OrderSlot in a given state with a target tile.
static OrderSlot MakeSlot(u8 state, i32 unitId = 7) {
    OrderSlot s{};
    s.unitId = unitId;
    s.state = state;
    s.packetId = -1;     // no command in flight
    s.phase = 0;
    s.tileX = 10;
    s.tileZ = 20;
    return s;
}

// A world context that reports the unit "not on tile" and "free tile found".
static OrderWorldContext FarContext() {
    OrderWorldContext c;
    c.unitOnTargetTile = [](const OrderSlot&, float) { return false; };
    c.findFreeTile = [](i32 x, i32 z, i32& ox, i32& oz) { ox = x; oz = z; return true; };
    c.unitBusy = []() { return false; };
    return c;
}

// A world context reporting the unit IS on the target tile.
static OrderWorldContext ArrivedContext() {
    OrderWorldContext c = FarContext();
    c.unitOnTargetTile = [](const OrderSlot&, float) { return true; };
    return c;
}

// ---------------------------------------------------------------------------
// Order-tick non-attack states.
// ---------------------------------------------------------------------------

TEST(CombatOrders, MoveIssuesPathThenArrives) {
    // Phase 0, far -> issues a path, sets phase=1.
    OrderSlot s = MakeSlot(kOrderMove);
    OrderCommand c = TickNonAttackOrder(s, FarContext());
    CHECK_EQ(c, OrderCommand::PathToTile);
    CHECK_EQ(s.phase, (u8)1);
    CHECK_EQ(s.packetId, 1);

    // Phase 1, arrived -> Captured (objective reached).
    s.packetId = 1;                  // command finished
    OrderCommand c2 = TickNonAttackOrder(s, ArrivedContext());
    CHECK_EQ(c2, OrderCommand::Captured);
}

TEST(CombatOrders, MovePhase1NotArrivedNoOp) {
    OrderSlot s = MakeSlot(kOrderMove);
    s.phase = 1;
    s.packetId = 1;
    OrderCommand c = TickNonAttackOrder(s, FarContext());
    CHECK_EQ(c, OrderCommand::None);
    CHECK_EQ(s.packetId, -1);        // packet cleared, still walking
}

TEST(CombatOrders, MoveGatedWhilePacketInFlight) {
    OrderSlot s = MakeSlot(kOrderMove);
    s.packetId = 42;                 // a real command id still pending
    OrderCommand c = TickNonAttackOrder(s, ArrivedContext());
    CHECK_EQ(c, OrderCommand::None);
    CHECK_EQ(s.packetId, 42);        // untouched
}

TEST(CombatOrders, MarchArrivedSyncs) {
    OrderSlot s = MakeSlot(kOrderMarch);
    s.packetId = 1;
    OrderCommand c = TickNonAttackOrder(s, ArrivedContext());
    CHECK_EQ(c, OrderCommand::SyncDone);
}

TEST(CombatOrders, MarchFarIssuesPath) {
    OrderSlot s = MakeSlot(kOrderMarch);
    s.packetId = 1;
    OrderCommand c = TickNonAttackOrder(s, FarContext());
    CHECK_EQ(c, OrderCommand::PathToTile);
    CHECK_EQ(s.phase, (u8)1);
}

TEST(CombatOrders, StandAlwaysSyncs) {
    OrderSlot s = MakeSlot(kOrderStand);
    s.packetId = -1;
    OrderCommand c = TickNonAttackOrder(s, FarContext());
    CHECK_EQ(c, OrderCommand::SyncDone);
}

TEST(CombatOrders, StandUpPlaysAnimOnce) {
    OrderSlot s = MakeSlot(kOrderStandUp);
    s.packetId = -1;
    OrderCommand c = TickNonAttackOrder(s, FarContext());
    CHECK_EQ(c, OrderCommand::AnimMode);
    CHECK_EQ(s.phase, (u8)1);
    // Second tick (phase already set) -> no further work.
    s.packetId = 1;
    OrderCommand c2 = TickNonAttackOrder(s, FarContext());
    CHECK_EQ(c2, OrderCommand::None);
}

TEST(CombatOrders, EscapeIssuesPathThenReaches) {
    OrderSlot s = MakeSlot(kOrderEscape);
    OrderCommand c = TickNonAttackOrder(s, FarContext());
    CHECK_EQ(c, OrderCommand::PathToTile);
    s.packetId = 1;
    OrderCommand c2 = TickNonAttackOrder(s, ArrivedContext());
    CHECK_EQ(c2, OrderCommand::Captured);
}

TEST(CombatOrders, WarePhase0FarWalks) {
    OrderSlot s = MakeSlot(kOrderWareCollect);
    s.packetId = -1;
    // WarePhase aliases hitFlag byte 2; phase 0 default.
    OrderCommand c = TickNonAttackOrder(s, FarContext());
    CHECK_EQ(c, OrderCommand::PathToTile);
}

TEST(CombatOrders, WarePhase2ArrivedSyncs) {
    OrderSlot s = MakeSlot(kOrderWareCollect);
    s.packetId = -1;
    s.hitFlag = (2 << 16);           // WarePhase() == 2
    CHECK_EQ(s.WarePhase(), (u8)2);
    OrderCommand c = TickNonAttackOrder(s, ArrivedContext());
    CHECK_EQ(c, OrderCommand::SyncDone);
}

TEST(CombatOrders, CaptureArrivedSyncs) {
    OrderSlot s = MakeSlot(kOrderCapture);
    s.packetId = -1;
    OrderCommand c = TickNonAttackOrder(s, ArrivedContext());
    CHECK_EQ(c, OrderCommand::SyncDone);
}

// ---------------------------------------------------------------------------
// Small action emitters.
// ---------------------------------------------------------------------------

TEST(CombatOrders, StandUpGate) {
    OrderSlot s = MakeSlot(kOrderStand);
    CHECK_EQ(StandUpUnitAction(s, true), UnitGesture::StandUp);
    CHECK_EQ(StandUpUnitAction(s, false), UnitGesture::None);   // unit gone
    s.state = 0;
    CHECK_EQ(StandUpUnitAction(s, true), UnitGesture::None);    // idle slot
    s.state = kOrderStand;
    s.unitId = -1;
    CHECK_EQ(StandUpUnitAction(s, true), UnitGesture::None);    // empty slot
}

TEST(CombatOrders, PickUpRequiresGroundObject) {
    OrderSlot s = MakeSlot(kOrderStand);
    CHECK_EQ(PickUpFromGroundAction(s, true, true), UnitGesture::PickUpFromGround);
    CHECK_EQ(PickUpFromGroundAction(s, true, false), UnitGesture::None);
}

TEST(CombatOrders, Celebrate) {
    OrderSlot s = MakeSlot(kOrderStand);
    CHECK_EQ(PlayCelebrateGesture(s, true), UnitGesture::Celebrate);
}

// ---------------------------------------------------------------------------
// Capture-flag / collectors / classifiers.
// ---------------------------------------------------------------------------

TEST(CombatOrders, CaptureUnitAction) {
    CaptureResult r = CaptureUnitAction(/*attackerSideOwner*/77, /*hasParent*/true);
    CHECK(r.applied);
    CHECK_EQ(r.newOwnerId, 77);
    CaptureResult r2 = CaptureUnitAction(77, false);
    CHECK(!r2.applied);
}

TEST(CombatOrders, WareConquerQualify) {
    CHECK(WareObjectQualifies(true, /*faction*/1, /*owner*/2));   // prefix + enemy
    CHECK(!WareObjectQualifies(true, 1, 1));                      // same owner
    CHECK(!WareObjectQualifies(false, 1, 2));                     // wrong prefix
    CHECK(ConquerObjectQualifies(true));
    CHECK(!ConquerObjectQualifies(false));
}

TEST(CombatOrders, ClassifyTileType) {
    CHECK_EQ(ClassifyTileType(19), (u8)0);
    CHECK_EQ(ClassifyTileType(16), (u8)1);
    CHECK_EQ(ClassifyTileType(4), (u8)2);
    CHECK_EQ(ClassifyTileType(7), (u8)0);
}

TEST(CombatOrders, SelectionFlag) {
    CHECK_EQ(GetSelectionFlag(true, false), kSelectionHighlightFlag);
    CHECK_EQ(GetSelectionFlag(false, false), 0);
    CHECK_EQ(GetSelectionFlag(false, true), kSelectionHighlightFlag);  // global
}

TEST(CombatOrders, CountActiveSlots) {
    i32 r1[16];
    for (int i = 0; i < 16; ++i) r1[i] = -1;
    CHECK_EQ(CountActiveSlots(r1), 0);
    r1[0] = 5; r1[1] = 6; // 2 populated then empty
    CHECK_EQ(CountActiveSlots(r1), 2);
    for (int i = 0; i < 16; ++i) r1[i] = i; // full
    CHECK_EQ(CountActiveSlots(r1), -1);
}

TEST(CombatOrders, IsTargetUnderfull) {
    CHECK(IsTargetUnderfull(5, 10.0f));
    CHECK(!IsTargetUnderfull(10, 10.0f));
    CHECK(!IsTargetUnderfull(20, 10.0f));
}

TEST(CombatOrders, AssignGuardTarget) {
    // capacity 3, target currently has 1, old target had 2.
    GuardAssign a = AssignGuardTarget(3, 1, true, 2);
    CHECK(a.assigned);
    CHECK_EQ(a.newTargetCount, (u8)2);
    CHECK_EQ(a.oldTargetCount, (u8)1);   // decremented
    // at capacity -> no assignment.
    GuardAssign b = AssignGuardTarget(3, 3, true, 2);
    CHECK(!b.assigned);
    CHECK_EQ(b.newTargetCount, (u8)3);
    CHECK_EQ(b.oldTargetCount, (u8)2);
}

// ---------------------------------------------------------------------------
// Bomb spawn allocator.
// ---------------------------------------------------------------------------

TEST(CombatOrders, SpawnDroppedBomb) {
    std::vector<Bomb> bombs;
    int s0 = SpawnDroppedBomb(bombs, 1000);
    CHECK_EQ(s0, 0);
    CHECK(bombs[0].active);
    CHECK_EQ(bombs[0].spawnTick, (u32)1000);
    int s1 = SpawnDroppedBomb(bombs, 1050);
    CHECK_EQ(s1, 1);
    // fill to capacity
    for (int i = 0; i < 32; ++i) { bombs[i].active = true; }
    CHECK_EQ(SpawnDroppedBomb(bombs, 1), -1);
}

// ---------------------------------------------------------------------------
// SelectBeatingTarget — RNG-driven; pin a seed and check determinism.
// ---------------------------------------------------------------------------

TEST(CombatOrders, SelectBeatingTargetSeeded) {
    crt::Srand(12345);
    std::vector<i32> seeds = {101, 202};
    std::vector<u8> table(25, 5);     // any filter byte
    // queryFn returns a fixed batch each attempt.
    auto q = [](int, u8) { return std::vector<i32>{301, 302, 303, 304}; };
    int pick = SelectBeatingTarget(seeds, table, q);
    CHECK(pick != -1);
    // Reproduce with the same seed -> identical pick (determinism).
    crt::Srand(12345);
    int pick2 = SelectBeatingTarget(seeds, table, q);
    CHECK_EQ(pick, pick2);
}

TEST(CombatOrders, SelectBeatingTargetNoBrawler) {
    crt::Srand(1);
    std::vector<i32> noSeeds;
    std::vector<u8> table(25, 0);
    auto qEmpty = [](int, u8) { return std::vector<i32>{}; };
    CHECK_EQ(SelectBeatingTarget(noSeeds, table, qEmpty), -1);
}

// ---------------------------------------------------------------------------
// Strength = loot/market (economy) — golden vectors.
// ---------------------------------------------------------------------------

TEST(CombatStrength, AttackerLootGolden) {
    CutsceneRng rng;
    rng.state = 0;
    // commanderCash 1000, one ware quantity 50, no market price.
    std::vector<LootWare> wares = { {/*ware*/3, /*qty*/50, /*owner*/0} };
    // base cash roll: RandInt(50) with state 0.
    CutsceneRng probe; probe.state = 0;
    u32 baseRoll = probe.RandInt(50);
    double baseCash = (baseRoll * kLootBaseCashScale + kLootBaseCashBias) * 1000.0;
    int expectBase = (int)baseCash;
    u32 qtyRoll = probe.RandInt(20);
    double v14 = (double)(qtyRoll + 80) * kLootWareQtyScale * 50.0;
    int qty = (int)v14; if (qty < 1) qty = 1;
    // marketPrice = 2.0 -> value += 2*qty
    int expectTotal = (int)(2.0 * qty + (double)expectBase);

    auto price = [](i32) { return 2.0; };
    LootResult r = ComputeAttackerStrength(1000, wares, /*seller*/9, /*buyer*/8,
                                           /*hasFamily*/true, /*before*/500, price, rng);
    CHECK_EQ(r.cashTotal, expectTotal);
    CHECK_EQ(r.familyCredited, 500 + expectTotal);
}

TEST(CombatStrength, AttackerLootEmitsCommands) {
    struct Sink : ILootCommandSink {
        int sells = 0; int cash = 0;
        void OnSellWare(i32, i32, int, i32) override { ++sells; }
        void OnCashCredit(int) override { ++cash; }
    } sink;
    CutsceneRng rng; rng.state = 7;
    std::vector<LootWare> wares = { {1, 30, 0}, {2, 40, 0} };
    auto price = [](i32) { return 1.0; };
    ComputeAttackerStrength(500, wares, 1, 2, false, 0, price, rng, &sink);
    CHECK_EQ(sink.cash, 1);      // one base cash credit
    CHECK_EQ(sink.sells, 2);     // one sell per ware
}

TEST(CombatStrength, DefenderLootHalvedWhenNoCommander) {
    CutsceneRng rng;
    std::vector<std::vector<LootWare>> stock = { { {1, 100, 5} } };
    auto price = [](i32) { return 3.0; };
    // qty = (int)(100 * 0.5) = 50; total = 3*50 = 150; no commander -> *0.5 = 75.
    LootResult r = ComputeDefenderStrength(stock, /*rate*/0.5f, /*seller*/-1,
                                           /*hasCommander*/false, /*hasFamily*/true,
                                           /*before*/0, price, nullptr);
    CHECK_EQ(r.cashTotal, 75);
    CHECK_EQ(r.familyCredited, 75);
}

TEST(CombatStrength, DefenderLootFullWithCommander) {
    std::vector<std::vector<LootWare>> stock = { { {1, 100, 5} } };
    auto price = [](i32) { return 3.0; };
    LootResult r = ComputeDefenderStrength(stock, 0.5f, 9, /*hasCommander*/true,
                                           true, 0, price, nullptr);
    CHECK_EQ(r.cashTotal, 150);  // not halved
}

// ---------------------------------------------------------------------------
// Projectile remainder.
// ---------------------------------------------------------------------------

TEST(CombatProjectile2, ArrowSpawnGate) {
    CHECK(ArrowSpawnGateOpen(5, 5, 700));      // 700 % 350 == 0, levels match
    CHECK(!ArrowSpawnGateOpen(5, 5, 701));     // off cadence
    CHECK(!ArrowSpawnGateOpen(5, 6, 700));     // wrong level
}

TEST(CombatProjectile2, ProjectileSlotLifetime) {
    // value 4 -> decrement to 3, fires.
    ProjectileTick t = TickProjectileSlot(4);
    CHECK(t.active); CHECK(t.fired); CHECK_EQ(t.count, (u8)3);
    // value 2 -> re-arm to 100, does NOT fire.
    ProjectileTick t2 = TickProjectileSlot(2);
    CHECK(t2.active); CHECK(!t2.fired); CHECK_EQ(t2.count, (u8)100);
    // value 0 -> inactive.
    ProjectileTick t3 = TickProjectileSlot(0);
    CHECK(!t3.active);
    // value 5 (>4) -> skipped.
    ProjectileTick t4 = TickProjectileSlot(5);
    CHECK(!t4.active);
}

TEST(CombatProjectile2, KnifeAimFalloff) {
    // dist 0 -> full opacity 255.
    KnifeAim a = ResolveKnifeAim(0.0f, false);
    CHECK(a.inRange);
    CHECK_EQ(a.transparency, (u8)255);
    // dist 50 -> 255 - 50*0.01*255 = 255 - 127.5 = 127.5 -> 127.
    KnifeAim b = ResolveKnifeAim(50.0f, false);
    CHECK_EQ(b.transparency, (u8)127);
    // highlighted -> fixed 128.
    KnifeAim c = ResolveKnifeAim(10.0f, true);
    CHECK(c.highlighted);
    CHECK_EQ(c.transparency, (u8)128);
    // no target -> hidden.
    KnifeAim d = ResolveKnifeAim(-1.0f, false);
    CHECK(!d.inRange);
}

TEST(CombatProjectile2, NearestKnifeTarget) {
    std::vector<KnifeTarget> ts = {
        {/*faction*/1, /*alive*/true, /*dist*/80.0f},   // self faction skip
        {/*faction*/2, true, 60.0f},                    // enemy in range
        {2, true, 30.0f},                               // nearer enemy
        {2, false, 5.0f},                               // dead skip
        {2, true, 200.0f},                              // out of range
    };
    float d = NearestKnifeTargetDist(/*selfFaction*/1, ts);
    CHECK(d == 30.0f);
    // no enemies in range.
    std::vector<KnifeTarget> far = { {2, true, 150.0f} };
    CHECK(NearestKnifeTargetDist(1, far) == -1.0f);
}
