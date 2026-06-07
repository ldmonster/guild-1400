// End-to-end: one turn of the deferred Amt/Event/History/Statistics/Mission flow
// over a synthetic city. The flow, in heartbeat order:
//   1. goods-distribution pass: drain over-stocked businesses, then replenish the
//      city back toward 40 firms (mutations routed to a mock command ledger).
//   2. a scripted fire event fires: compute its burn duration from building spacing,
//      run its price-spike phase machine to the penalise phase.
//   3. record the outcome in the in-memory chronicle (a dated event), then scan it.
//   4. build the economy report on the resulting accumulators and assert the weak
//      sector + trend ids + broadcast decision against a hand-computed reference.
//   5. fail the player's missions when their business burned down.
#include <cmath>
#include <cstring>

#include "crt/rand.h"
#include "tests/framework/test.h"
#include "world/amt_goods.h"
#include "world/event_effects.h"
#include "world/history_chronicle.h"
#include "world/history_parse.h"
#include "world/mission.h"
#include "world/mission_reward.h"
#include "world/statistics.h"
#include "world/statistics_report.h"

using namespace guild;
using namespace guild::world;

namespace {
bool feq(float a, float b) { return std::fabs(a - b) <= 1e-5f; }

// Shared command ledger the goods mock writes into.
struct Ledger {
    int drains = 0;
    int specialSpawns = 0;
    int businessSpawns = 0;
};

struct GoodsLedgerMock : GoodsDistribHooks {
    Ledger* led;
    const int* rng;
    int rngLen;
    int cursor = 0;
    explicit GoodsLedgerMock(Ledger* l, const int* r, int n) : led(l), rng(r), rngLen(n) {}
    void DrainStock(int) override { ++led->drains; }
    bool SpawnSpecialFirm() override { ++led->specialSpawns; return true; }
    bool SpawnBusiness(int) override { ++led->businessSpawns; return true; }
    int RandomModulo(int n) override {
        if (n == 0) return 0;
        int v = (cursor < rngLen) ? rng[cursor++] : 0;
        return v % n;
    }
};
} // namespace

TEST(WorldAmtEventE2E, FullTurn) {
    // ----- synthetic city: 4 production businesses (all over-stocked, drainable),
    // plus low firm count so the replenish step spawns toward 40. -----
    GoodsBuilding b[4];
    for (auto& x : b) {
        x.present = true; x.occupied = 1; x.typeByte = 3;
        x.worthWord = 200; x.guardsClear = true; x.hasSupplier = true;
    }

    Ledger led;
    // RNG script: 4 type-3 worth draws (all 0 -> worth>=44 passes), then the
    // special-firm gate draw == 1 (no special spawn), then replenish wing/kind draws.
    int rng[64];
    for (int i = 0; i < 64; ++i) rng[i] = (i == 4) ? 1 : 0;
    GoodsLedgerMock gm(&led, rng, 64);

    GoodsDistribResult gr = GoodsRunDistributionPass(b, 4, gm);

    // Reference: all 4 drained, active 4 -> 0; firms started at 0 (< 30) so
    // spawnCount = (40 - 0)/2 = 20, all committed.
    CHECK_EQ(gr.drained, 4);
    CHECK_EQ(led.drains, 4);
    CHECK_EQ(gr.activeCount, 0);
    CHECK(!gr.spawnedSpecial);
    CHECK_EQ(gr.spawnCount, 20);
    CHECK_EQ(gr.spawned, 20);
    CHECK_EQ(led.businessSpawns, 20);
    CHECK_EQ(led.specialSpawns, 0);
    CHECK_EQ(gr.firms, 20);          // 0 + 20 replenished

    // ----- a scripted fire event: burn duration from building spacing + the
    // price-spike phase machine driven to teardown. -----
    // Two same-type buildings 800 units apart -> factor 0.8, base value 500.
    i32 burnDuration = FireRaidComputeDuration(800.0, true, 500);
    CHECK_EQ(burnDuration, 400);     // 0.8 * 500

    // The price event: counter 0 -> spike, counter 1 -> penalise, counter -1 -> free.
    CHECK(PriceEventClassify(0) == PriceEventPhase::kSpike);
    CHECK(PriceEventClassify(1) == PriceEventPhase::kPenalise);
    CHECK_EQ(PriceEventMoodPenalty(2), -7);   // -(2 + 5)

    // A production trigger on a surviving building: gauge full -> produce.
    CHECK(ProductionTriggerStep(0, false, 1.0f) == ProductionTriggerAction::kProduce);

    // ----- record the event in the chronicle and scan it. -----
    Chronicle chron;
    // The fire destroyed the player's business -> a crime/loss chronicle line.
    int crimeId = HistoryNotifyTargetReachedATextId(6);  // kind 6 player -> 3963
    CHECK_EQ(crimeId, 3963);
    ChronicleEntry e1{ /*day*/100, /*month*/4, /*year*/1402,
                       HistoryNotifyTargetFoundTextId(6), nullptr };
    ChronicleEntry e2{ /*day*/101, /*month*/4, /*year*/1402, crimeId, nullptr };
    chron.Add(e1);
    chron.Add(e2);
    CHECK_EQ(chron.Count(), 2);
    // "yesterday" window for current day 102 -> the day-101 entry (index 1).
    CHECK_EQ(chron.ScanNextForward(102), 1);
    CHECK_EQ(chron.At(1).textId, 3963);
    // its formatted date roundtrips through the parser.
    char label[16];
    HistoryFormatDate(chron.At(1).day % 100, chron.At(1).month, chron.At(1).year, label);
    CHECK(std::strcmp(label, "01.04.1402") == 0);

    // ----- build the economy report on the resulting accumulators. -----
    // Construct accumulators so the four totals are: trade 0.40, goods 0.10,
    // services 0.30, luxury 0.60. column k = a[k]+a[k+5]+a[k+10]+a[k+15], *0.25.
    float acc[kStatAccumCount];
    std::memset(acc, 0, sizeof(acc));
    acc[0] = 0.4f;  // goods column 0 = 0.4 -> 0.10
    acc[1] = 1.2f;  // services column 1 = 1.2 -> 0.30
    acc[2] = 1.6f;  // trade column 2 = 1.6 -> 0.40
    acc[3] = 2.4f;  // luxury column 3 = 2.4 -> 0.60
    EconomyReportInput rin = EconomyReportFromAccum(acc, /*lawScore*/-0.20f);
    CHECK(feq(rin.total0, 0.10f));
    CHECK(feq(rin.total1, 0.30f));
    CHECK(feq(rin.total2, 0.40f));
    CHECK(feq(rin.luxury, 0.60f));

    // Reference: the weakest VALUE is the law score (-0.20); the index follows the
    // original's comparison chain (services last, v34 <= v10 false) -> 3.
    EconomyReportWeak weak = EconomyReportWeakest(rin);
    CHECK_EQ(weak.index, 3);
    CHECK(feq(weak.value, -0.20f));
    // -0.20 <= 0.20 gate -> no weak-sector line emitted.
    CHECK(!EconomyReportEmitsWeakLine(weak));
    // luxury 0.60 > 0.55 -> booming summary.
    CHECK(EconomyReportLuxuryBranch(rin.luxury) == EconomyReportLuxury::kBooming);
    // trend ids: trade up (6178), goods down (6175), law down (6180), services up (6181).
    EconomyReportTrends tr = EconomyReportTrendIds(rin);
    CHECK_EQ(tr.trade, 6178);
    CHECK_EQ(tr.goods, 6175);
    CHECK_EQ(tr.law, 6180);
    CHECK_EQ(tr.services, 6181);

    // ----- fail the player's missions tied to the burned business. -----
    MissionSlotTableReset();
    MissionSlotRegister(/*owner*/55, /*type*/9);
    MissionSlotRegister(/*owner*/55, /*type*/11);
    MissionSlotRegister(/*owner*/12, /*type*/3);
    static int failed = 0;
    failed = 0;
    int matched = MissionFinishByOwner(55, [](int, u8, void*) { ++failed; }, nullptr);
    CHECK_EQ(matched, 2);
    CHECK_EQ(failed, 2);
    // the result dialog: a reward was claimed -> reload session.
    int rc = MissionResultCode(/*rewardClaimed*/true);
    CHECK_EQ(rc, 2);
    CHECK(MissionResultTriggersReload(rc));
}
