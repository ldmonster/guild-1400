// Unit tests for the deferred Amt + Event/History/Statistics/Mission remainder:
//   amt_goods          (full goods-distribution pass + RefreshGuildState pump)
//   event_effects      (fire-duration, price-spike, production-trigger phase machines)
//   history_parse      (DD.MM.YYYY format/roundtrip + target Notify selectors)
//   statistics_report  (economy-report trend/argmin/gate decision logic)
//   mission_reward     (FinishByOwner slot scan + RunResultDialog outcome)
#include <cmath>
#include <cstring>

#include "crt/rand.h"
#include "tests/framework/test.h"
#include "world/amt_goods.h"
#include "world/event_effects.h"
#include "world/history_parse.h"
#include "world/mission.h"
#include "world/mission_reward.h"
#include "world/statistics.h"
#include "world/statistics_report.h"

using namespace guild;
using namespace guild::world;

namespace {
bool feq(float a, float b) { return std::fabs(a - b) <= 1e-5f; }

// A deterministic RNG-and-command mock for the goods pass. RandomModulo is fed
// from a fixed script so spawn counts are predictable.
struct GoodsMock : GoodsDistribHooks {
    int drains = 0;
    int specialSpawns = 0;
    int businessSpawns = 0;
    int rngCursor = 0;
    const int* rngScript = nullptr;
    int rngScriptLen = 0;
    bool specialOk = true;
    bool businessOk = true;

    void DrainStock(int) override { ++drains; }
    bool SpawnSpecialFirm() override { ++specialSpawns; return specialOk; }
    bool SpawnBusiness(int) override { ++businessSpawns; return businessOk; }
    int RandomModulo(int n) override {
        if (n == 0) return 0;
        if (rngScript && rngCursor < rngScriptLen)
            return rngScript[rngCursor++] % n;
        return 0;
    }
};
} // namespace

// ===========================================================================
// amt_goods.
// ===========================================================================
TEST(WorldAmtEventGoods, CountActiveCountsOccupiedPresent) {
    GoodsBuilding b[4];
    b[0].present = true;  b[0].occupied = 1;   // active
    b[1].present = false; b[1].occupied = 1;   // free slot -> skip
    b[2].present = true;  b[2].occupied = 0;   // present but vacant -> skip
    b[3].present = true;  b[3].occupied = 2;   // active
    CHECK_EQ(GoodsCountActive(b, 4), 2);
}

TEST(WorldAmtEventGoods, SpawnCountSelector) {
    // firms < 30 -> (40 - firms)/2
    CHECK_EQ(GoodsSpawnCount(10, 0), 15);
    CHECK_EQ(GoodsSpawnCount(29, 1), 5);    // (40-29)/2 = 5 (rand ignored)
    // 30..39 -> rand2 + 1
    CHECK_EQ(GoodsSpawnCount(30, 0), 1);
    CHECK_EQ(GoodsSpawnCount(35, 1), 2);
    // >= 40 -> 0
    CHECK_EQ(GoodsSpawnCount(40, 1), 0);
    CHECK_EQ(GoodsSpawnCount(99, 0), 0);
}

TEST(WorldAmtEventGoods, Type3DrainPredicate) {
    GoodsBuilding b;
    b.present = true; b.occupied = 1; b.typeByte = 3;
    b.guardsClear = true; b.hasSupplier = true;
    b.worthWord = 50;
    // worth(50) >= rand4(4)+44 == 48 -> gate passes
    CHECK(GoodsType3ShouldDrain(b, /*active*/0, /*rand4*/4));
    // worth too low and city under threshold -> no drain
    b.worthWord = 40;
    CHECK(!GoodsType3ShouldDrain(b, 0, 0));   // 40 >= 44? no; active 0 < 652.8
    // worth too low but city over threshold -> drain anyway
    CHECK(GoodsType3ShouldDrain(b, 700, 0));
    // missing supplier blocks the drain
    b.worthWord = 50; b.hasSupplier = false;
    CHECK(!GoodsType3ShouldDrain(b, 0, 0));
    // closed/destroyed building blocked by guards
    b.hasSupplier = true; b.guardsClear = false;
    CHECK(!GoodsType3ShouldDrain(b, 0, 0));
    // wrong type
    b.guardsClear = true; b.typeByte = 8;
    CHECK(!GoodsType3ShouldDrain(b, 0, 0));
}

TEST(WorldAmtEventGoods, FullPassDrainsAndReplenishes) {
    // A small city: two over-stocked type-3 businesses to drain, low firm count so
    // the replenish step spawns. Active count is small (<= threshold).
    GoodsBuilding b[3];
    for (auto& x : b) { x.present = true; x.occupied = 1; }
    b[0].typeByte = 3; b[0].worthWord = 100; b[0].guardsClear = true; b[0].hasSupplier = true;
    b[1].typeByte = 3; b[1].worthWord = 100; b[1].guardsClear = true; b[1].hasSupplier = true;
    b[2].typeByte = 8; b[2].occupied = 2; b[2].activeType = true; // one firm tally

    // RNG: the two type-3 draws (RandomModulo(4)) return 0 (worth>=44 passes), then
    // the special-firm gate RandomModulo(4) returns 1 (!=0 -> no special spawn),
    // then the replenish loop's per-spawn (wing rand2, kind rand12) draws.
    int script[64];
    for (int i = 0; i < 64; ++i) script[i] = (i == 2) ? 1 : 0;
    GoodsMock m;
    m.rngScript = script; m.rngScriptLen = 64;

    GoodsDistribResult r = GoodsRunDistributionPass(b, 3, m);
    // Both type-3 buildings drained; active count dropped from 3 to 1.
    CHECK_EQ(r.drained, 2);
    CHECK_EQ(r.activeCount, 1);
    CHECK_EQ(r.firms, 1 + r.spawned);         // one firm + the replenish spawns
    CHECK(!r.spawnedSpecial);                 // gate draw was 1 (!=0)
    // firms started at 1 (< 30) -> spawnCount = (40 - 1)/2 = 19, all committed.
    CHECK_EQ(r.spawnCount, 19);
    CHECK_EQ(r.spawned, 19);
}

TEST(WorldAmtEventGoods, RefreshGuildStatePumpHook) {
    static int calls = 0;
    calls = 0;
    AmtSetRefreshGuildStateHook([](void*) { ++calls; return 7; }, nullptr);
    CHECK_EQ(AmtRefreshGuildState(), 7);
    CHECK_EQ(calls, 1);
    AmtSetRefreshGuildStateHook(nullptr, nullptr);
    CHECK_EQ(AmtRefreshGuildState(), 0);
}

// ===========================================================================
// event_effects.
// ===========================================================================
TEST(WorldAmtEventEffects, FireDurationFactorAndDuration) {
    // distance * 0.001, clamped at 1.5.
    CHECK(std::fabs(FireRaidDurationFactor(500.0, true) - 0.5) < 1e-9);
    CHECK(std::fabs(FireRaidDurationFactor(2000.0, true) - 1.5) < 1e-9); // clamp
    CHECK(std::fabs(FireRaidDurationFactor(0.0, false) - 1.0) < 1e-9);   // no neighbour
    // duration = trunc(factor * base).
    CHECK_EQ(FireRaidComputeDuration(500.0, true, 100), 50);   // 0.5 * 100
    CHECK_EQ(FireRaidComputeDuration(0.0, false, 80), 80);     // 1.0 * 80
    CHECK_EQ(FireRaidComputeDuration(2000.0, true, 100), 150); // 1.5 * 100
}

TEST(WorldAmtEventEffects, PriceEventPhaseAndPenalty) {
    CHECK(PriceEventClassify(-2) == PriceEventPhase::kFreeHandler); // counter+2 == 0
    CHECK(PriceEventClassify(-1) == PriceEventPhase::kFreeHandler); // == 1
    CHECK(PriceEventClassify(0)  == PriceEventPhase::kSpike);       // == 2
    CHECK(PriceEventClassify(1)  == PriceEventPhase::kPenalise);    // == 3
    CHECK(PriceEventClassify(5)  == PriceEventPhase::kIdle);
    // mood penalty = -(rand5 + 5) in [-9,-5].
    CHECK_EQ(PriceEventMoodPenalty(0), -5);
    CHECK_EQ(PriceEventMoodPenalty(4), -9);
}

TEST(WorldAmtEventEffects, ProductionTriggerPhases) {
    CHECK(ProductionTriggerStep(-2, false, 0.0f) == ProductionTriggerAction::kTeardown);
    CHECK(ProductionTriggerStep(-1, false, 0.0f) == ProductionTriggerAction::kTeardown);
    CHECK(ProductionTriggerStep(-3, false, 0.0f) == ProductionTriggerAction::kIdle);
    CHECK(ProductionTriggerStep(5,  false, 0.0f) == ProductionTriggerAction::kIdle);
    // phase 0 gauge branch.
    CHECK(ProductionTriggerStep(0, true,  0.5f) == ProductionTriggerAction::kWaitHours);
    CHECK(ProductionTriggerStep(0, false, 0.5f) == ProductionTriggerAction::kWaitSeconds);
    CHECK(ProductionTriggerStep(0, false, 1.0f) == ProductionTriggerAction::kProduce);
    CHECK(ProductionTriggerStep(0, false, 2.0f) == ProductionTriggerAction::kProduce);
}

// ===========================================================================
// history_parse.
// ===========================================================================
TEST(WorldAmtEventHistory, FormatAndRoundtrip) {
    char buf[16];
    HistoryFormatDate(5, 3, 1407, buf);
    CHECK(std::strcmp(buf, "05.03.1407") == 0);
    HistoryFormatDate(0, 0, 1400, buf);          // 0 day/month normalise to 1
    CHECK(std::strcmp(buf, "01.01.1400") == 0);

    // parse -> format roundtrip is stable for a normalised input.
    ParsedDate p;
    char out[16];
    CHECK(HistoryRoundtripDate("17.09.1412", out, &p));
    CHECK(std::strcmp(out, "17.09.1412") == 0);
    CHECK_EQ(p.day, 17);
    CHECK_EQ(p.month, 9);
    CHECK_EQ(p.year, 1412);
    CHECK_EQ(p.yearOffset, 12);                  // 1412 - 1400
    // a malformed (wrong length) date is rejected.
    CHECK(!HistoryRoundtripDate("1.1.1400", out, &p));
}

TEST(WorldAmtEventHistory, TargetNotifySelectors) {
    // kind 6/7 gated chronicle ids.
    CHECK_EQ(HistoryNotifyTargetFoundTextId(6), 3953);
    CHECK_EQ(HistoryNotifyTargetFoundTextId(3), -1);
    CHECK_EQ(HistoryNotifyTargetReachedATextId(7), 3963);
    CHECK_EQ(HistoryNotifyTargetReachedATextId(0), -1);
    CHECK_EQ(HistoryNotifyTargetReachedBTextId(6), 3973);
    // detained-flag voice bases.
    CHECK_EQ(HistoryNotifyTargetReachedAVoiceBase(false), 3964);
    CHECK_EQ(HistoryNotifyTargetReachedAVoiceBase(true),  3967);
    CHECK_EQ(HistoryNotifyTargetReachedBVoiceBase(false), 3974);
    CHECK_EQ(HistoryNotifyTargetReachedBVoiceBase(true),  3977);
}

// ===========================================================================
// statistics_report.
// ===========================================================================
TEST(WorldAmtEventStats, AccumulationTotalsGolden) {
    // Fill the 20-float accumulator so each column sums to a known value.
    float a[kStatAccumCount];
    for (int i = 0; i < kStatAccumCount; ++i) a[i] = static_cast<float>(i);
    // column k = a[k]+a[k+5]+a[k+10]+a[k+15], scaled by 0.25.
    // k=0: 0+5+10+15 = 30 -> 7.5;  k=1: 1+6+11+16 = 34 -> 8.5
    // k=2: 2+7+12+17 = 38 -> 9.5;  k=3: 3+8+13+18 = 42 -> 10.5
    float out[kStatCategoryCnt];
    StatisticsAccumulateCategoryTotals(a, out);
    CHECK(feq(out[0], 7.5f));
    CHECK(feq(out[1], 8.5f));
    CHECK(feq(out[2], 9.5f));
    CHECK(feq(out[3], 10.5f));
}

TEST(WorldAmtEventStats, CadenceGate) {
    CHECK(!EconomyReportShouldRun(4));    // < 8
    CHECK(!EconomyReportShouldRun(8 + 1)); // 9 % 4 != 0
    CHECK(EconomyReportShouldRun(8));
    CHECK(EconomyReportShouldRun(12));
}

TEST(WorldAmtEventStats, TrendIdsAndArgmin) {
    EconomyReportInput in;
    in.total2 = 0.20f;  // trade   > 0.15 -> up
    in.total0 = 0.10f;  // goods   <= 0.15 -> down
    in.lawScore = -0.5f; // law    < 0 -> down
    in.total1 = 0.40f;  // services > 0.15 -> up
    in.luxury = 0.60f;  // luxury booming
    EconomyReportTrends t = EconomyReportTrendIds(in);
    CHECK_EQ(t.trade, 6178);
    CHECK_EQ(t.goods, 6175);
    CHECK_EQ(t.law, 6180);
    CHECK_EQ(t.services, 6181);

    // weakest value is lawScore (-0.5); the index follows the original's exact
    // comparison chain (NOT a strict argmin): with services last in the chain and
    // v34 <= v10 false, v12 falls through to 3 here. Reproduced 1:1 from the binary.
    EconomyReportWeak w = EconomyReportWeakest(in);
    CHECK(feq(w.value, -0.5f));
    CHECK_EQ(w.index, 3);

    // luxury branch + weak-line gate.
    CHECK(EconomyReportLuxuryBranch(in.luxury) == EconomyReportLuxury::kBooming);
    CHECK(!EconomyReportEmitsWeakLine(w));   // -0.5 <= 0.20 -> no weak line
}

TEST(WorldAmtEventStats, WeakLineSeverityTiers) {
    EconomyReportWeak w;
    w.value = 0.25f;
    CHECK(EconomyReportEmitsWeakLine(w));    // > 0.20
    CHECK(EconomyReportWeakSeverity(0.25f) == EconomyReportSeverity::kMild);
    CHECK(EconomyReportWeakSeverity(0.35f) == EconomyReportSeverity::kModerate);
    CHECK(EconomyReportWeakSeverity(0.55f) == EconomyReportSeverity::kSevere);
    // luxury slump vs neutral.
    CHECK(EconomyReportLuxuryBranch(0.40f) == EconomyReportLuxury::kSlump);
    CHECK(EconomyReportLuxuryBranch(0.50f) == EconomyReportLuxury::kNeutral);
}

TEST(WorldAmtEventStats, FromAccumProducesTotals) {
    float a[kStatAccumCount];
    std::memset(a, 0, sizeof(a));
    a[0] = 1.0f; a[5] = 1.0f; a[10] = 1.0f; a[15] = 1.0f;   // column 0 = 4 -> 1.0
    EconomyReportInput in = EconomyReportFromAccum(a, 0.25f);
    CHECK(feq(in.total0, 1.0f));
    CHECK(feq(in.lawScore, 0.25f));
}

// ===========================================================================
// mission_reward.
// ===========================================================================
TEST(WorldAmtEventMission, FinishByOwnerScansAndFails) {
    MissionSlotTableReset();
    // Register three missions: two for owner 42, one for owner 7.
    MissionSlotRegister(42, 5);
    MissionSlotRegister(7, 3);
    MissionSlotRegister(42, 9);

    static int failed = 0;
    static int lastType = 0;
    failed = 0; lastType = 0;
    int matched = MissionFinishByOwner(42,
        [](int, u8 type, void*) { ++failed; lastType = type; }, nullptr);
    CHECK_EQ(matched, 2);
    CHECK_EQ(failed, 2);
    CHECK_EQ(lastType, 9);   // last owned slot (type 9) failed
    // owner 7's mission was not touched.
    CHECK_EQ(MissionFinishByOwner(7, nullptr, nullptr), 1);
    // unknown owner -> no matches.
    CHECK_EQ(MissionFinishByOwner(999, nullptr, nullptr), 0);
}

TEST(WorldAmtEventMission, ResultCodeAndReload) {
    CHECK_EQ(MissionResultCode(false), 1);   // no reward claimed
    CHECK_EQ(MissionResultCode(true), 2);    // reward claimed
    CHECK(!MissionResultTriggersReload(1));
    CHECK(MissionResultTriggersReload(2));
}
