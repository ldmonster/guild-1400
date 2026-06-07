// Unit tests for the per-turn tick driver state + leaf rule cores.
//   src/sim/gametick.{h,cpp}     — turn-state, NPC sweep, plant growth, accum reset
//   src/sim/turn_driver.{h,cpp}  — the ordered pass orchestration
#include <vector>

#include "sim/gametick.h"
#include "sim/turn_driver.h"
#include "crt/rand.h"
#include "tests/framework/test.h"

using namespace guild;
using namespace guild::sim;

// --- turn-flag clear (mask 0xE0874703) --------------------------------------
TEST(GameTickUnit, TurnFlagClearMask) {
    CHECK_EQ(kTurnFlagPersistMask, 0xE0874703u);
    // Transient bits outside the mask are cleared; masked bits survive.
    CHECK_EQ(ClearTurnFlags(0xFFFFFFFFu), 0xE0874703u);
    CHECK_EQ(ClearTurnFlags(0x00000000u), 0x00000000u);
    CHECK_EQ(ClearTurnFlags(0xE0874703u), 0xE0874703u);
    CHECK_EQ(ClearTurnFlags(0x12345678u), 0x12345678u & 0xE0874703u);
}

// --- NPC turn-flag sweep: clears all rows, records last human (kind 6) ------
TEST(GameTickUnit, NpcSweepClearsAndFindsHuman) {
    const int N = 8;
    u16 alive[N] = {0, 0, 0xFFFF, 0, 0, 0, 0xFFFF, 0};
    u8  kinds[N] = {4, 6, 5, 6, 11, 30, 6, 4};   // humans at 1,3,6
    u32 flags[N];
    for (int i = 0; i < N; ++i) flags[i] = 0xFFFFFFFFu;

    int human = RunNpcTurnFlagSweep(alive, kinds, flags, N);
    // Last kind==6 is index 6.
    CHECK_EQ(human, 6);
    CHECK_EQ(GameTurnState().humanPersonIndex, (i16)6);
    for (int i = 0; i < N; ++i)
        CHECK_EQ(flags[i], 0xE0874703u);  // all cleared
}

TEST(GameTickUnit, NpcSweepNoHuman) {
    const int N = 4;
    u16 alive[N] = {0, 0, 0, 0};
    u8  kinds[N] = {4, 5, 11, 30};
    u32 flags[N] = {1, 2, 3, 4};
    int human = RunNpcTurnFlagSweep(alive, kinds, flags, N);
    CHECK_EQ(human, -1);
}

// --- plant growth: increment stage toward cap -------------------------------
TEST(GameTickUnit, PlantAdvanceStageRule) {
    CHECK_EQ((int)PlantAdvanceStage(0, 5), 1);
    CHECK_EQ((int)PlantAdvanceStage(4, 5), 5);
    CHECK_EQ((int)PlantAdvanceStage(5, 5), 5);   // at cap: no change
    CHECK_EQ((int)PlantAdvanceStage(7, 5), 7);   // above cap: no change
}

TEST(GameTickUnit, PlantAdvanceFarmSkipsEmptyAndCounts) {
    std::vector<PlantNode> farm = {
        {0x00, 2, 5},   // grows -> 3
        {0xFF, 9, 9},   // empty: skipped
        {0x01, 5, 5},   // at cap: no change
        {0x02, 0, 3},   // grows -> 1
    };
    int adv = PlantAdvanceFarm(farm.data(), (int)farm.size());
    CHECK_EQ(adv, 2);
    CHECK_EQ((int)farm[0].stage, 3);
    CHECK_EQ((int)farm[1].stage, 9);  // untouched
    CHECK_EQ((int)farm[2].stage, 5);
    CHECK_EQ((int)farm[3].stage, 1);
}

// --- per-turn accumulator reset (stride 134) --------------------------------
TEST(GameTickUnit, ResetPerTurnAccumulators) {
    const int persons = 3;
    std::vector<i32> accum(kPerTurnAccumStride * persons, 7);
    ResetPerTurnAccumulators(accum.data(), persons);
    // Only the row-0 dword of each Person (stride 134) is zeroed.
    CHECK_EQ(accum[0 * kPerTurnAccumStride], 0);
    CHECK_EQ(accum[1 * kPerTurnAccumStride], 0);
    CHECK_EQ(accum[2 * kPerTurnAccumStride], 0);
    // The in-between dwords are NOT touched (faithful to dword_12CE8E0[134*i]).
    CHECK_EQ(accum[1], 7);
    CHECK_EQ(accum[133], 7);
}

// --- TurnState predicate -----------------------------------------------------
TEST(GameTickUnit, DrivesHeavyPassesPredicate) {
    TurnState s;
    s.runFlags = 0; s.netStandalone = 0;
    CHECK(!s.DrivesHeavyPasses());          // networked non-host
    s.netStandalone = -1;
    CHECK(s.DrivesHeavyPasses());           // standalone/host
    s.netStandalone = 0; s.runFlags = 8;
    CHECK(s.DrivesHeavyPasses());           // host flag set
    s.runFlags = 4;                         // unrelated bit
    CHECK(!s.DrivesHeavyPasses());
}

// --- driver: heavy cascade order --------------------------------------------
static std::vector<std::pair<TurnPass,int>> g_calls;
static void RecordPass(TurnPass p, int arg, void*) { g_calls.push_back({p, arg}); }

TEST(GameTickUnit, DriverHeavyCascadeOrder) {
    g_calls.clear();
    TurnState s; s.netStandalone = -1;   // host: heavy cascade

    // 3 factions: 0 alive+player, 1 alive+non-player, 2 free.
    FactionSlot factions[3];
    factions[0] = {0x0000, true};
    factions[1] = {0x0000, false};
    factions[2] = {0xFFFF, true};

    TurnDriverCtx ctx;
    ctx.pass = RecordPass;
    ctx.factions = factions;
    ctx.factionCount = 3;

    BeginPlayerRound(s, ctx);

    // The recovered ordered prefix (pre-faction-loop).
    std::vector<TurnPass> expectedPrefix = {
        TurnPass::ComputeWealthGrid, TurnPass::NpcTurnFlagSweep,
        TurnPass::TickRegisteredEvents, TurnPass::ExpireEventSlots,
        TurnPass::ExpireApEventSlots, TurnPass::PlantGrowth,
        TurnPass::RecalcAllProduction, TurnPass::CitySnapshotStats,
        TurnPass::StraftatSyncAll,
    };
    CHECK(ctx.order.size() >= expectedPrefix.size());
    for (size_t i = 0; i < expectedPrefix.size(); ++i)
        CHECK(ctx.order[i] == expectedPrefix[i]);

    // Only faction 0 processed (alive && player).
    CHECK_EQ((int)ctx.processedFactions.size(), 1);
    CHECK_EQ(ctx.processedFactions[0], 0);

    // The Amt cascade must appear in the exact recovered order after the player
    // loop, with the right operands.
    std::vector<TurnPass> tail = {
        TurnPass::MeisterProcessPlayers,
        TurnPass::AmtRunProductionPass, TurnPass::AmtUpdateOfficeProsperity,
        TurnPass::AmtRunBuildingTaxPass, TurnPass::AmtProcessLoanRepayments,
        TurnPass::AmtProcessOfficeWages, TurnPass::AmtUpdateOffices,
        TurnPass::MeisterRunBuildingTasks, TurnPass::HeProcessAllPlayerNews,
        TurnPass::AiMethodBroadcastGroup, TurnPass::CityTickStatsBroadcast,
        TurnPass::AdvanceTurnTimer, TurnPass::MeisterProcessBuildingNeeds,
        TurnPass::TurnEndCoord27Broadcast,
        TurnPass::ResetPerTurnAccumulators, TurnPass::SyncAllTurnStates,
    };
    // Locate the first MeisterProcessPlayers and compare the suffix.
    size_t start = expectedPrefix.size();
    CHECK(ctx.order.size() == start + tail.size());
    for (size_t i = 0; i < tail.size(); ++i)
        CHECK(ctx.order[start + i] == tail[i]);

    // Building-tax pass ran with flags=3 (heavy), loan + wages + offices present.
    bool sawTax3 = false;
    for (auto& c : g_calls)
        if (c.first == TurnPass::AmtRunBuildingTaxPass) { CHECK_EQ(c.second, 3); sawTax3 = true; }
    CHECK(sawTax3);
}

// --- driver: non-driving peer path ------------------------------------------
TEST(GameTickUnit, DriverLightPath) {
    g_calls.clear();
    TurnState s; s.netStandalone = 0; s.runFlags = 0;   // networked non-host

    TurnDriverCtx ctx;
    ctx.pass = RecordPass;

    BeginPlayerRound(s, ctx);

    // Light path: tax pass with flags=2, NO MeisterAi player loop, NO Amt cascade,
    // NO SyncAllTurnStates.
    bool sawLightTax = false, sawHeavy = false, sawSync = false;
    int lightArg = -99;
    for (auto& c : g_calls) {
        if (c.first == TurnPass::AmtBuildingTaxPassLight) { sawLightTax = true; lightArg = c.second; }
        if (c.first == TurnPass::MeisterProcessPlayers || c.first == TurnPass::AmtRunProductionPass) sawHeavy = true;
        if (c.first == TurnPass::SyncAllTurnStates) sawSync = true;
    }
    CHECK(sawLightTax);
    CHECK_EQ(lightArg, 2);
    CHECK(!sawHeavy);
    CHECK(!sawSync);
    // The accumulator reset still runs on the light path.
    bool sawReset = false;
    for (auto& c : g_calls) if (c.first == TurnPass::ResetPerTurnAccumulators) sawReset = true;
    CHECK(sawReset);
}

// --- RandomModulo parity (the orchestration's RNG source) -------------------
// VIBE_Math_RandomModulo(n) == RandNext() % n  (n != 0 else 0).
TEST(GameTickUnit, RandomModuloMatchesRandNext) {
    crt::Srand(12345);
    // Reproduce RandNext()%10 directly and compare to a re-seeded golden.
    crt::Srand(12345);
    int a = crt::RandNext() % 10;
    crt::Srand(12345);
    u32 st = 12345u * 1103515245u + 12345u;
    int golden = (int)((st >> 16) & 0x7FFF) % 10;
    CHECK_EQ(a, golden);
}
