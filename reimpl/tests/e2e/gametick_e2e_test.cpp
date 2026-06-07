// E2E: a FULL player round on a synthetic city + population + factions.
//
// Wires the real leaf rule cores (NPC turn-flag sweep, plant growth, per-turn
// accumulator reset) into the recovered orchestration, mocks the heavy world/ai
// passes through the pass hook, and verifies against a HAND-COMPUTED reference:
//   * the exact ordered pass invocation sequence,
//   * the per-faction MeisterAi iteration (alive && is-player only),
//   * the resulting economy/office/treasury state mutated by the mock passes,
//   * the turn-end Coord27 command broadcast set,
//   * the cleared turn flags / per-turn accumulators / grown plant stages.
#include <vector>

#include "sim/gametick.h"
#include "sim/turn_driver.h"
#include "tests/framework/test.h"

using namespace guild;
using namespace guild::sim;

namespace {

// A tiny synthetic "city economy" the mock passes mutate, so we can verify the
// passes ran in the right order AND produced the hand-computed end state.
struct SyntheticCity {
    std::vector<TurnPass> seq;     // mirror of ctx.order for an independent check
    // Mutated by the mock passes:
    i32 treasury = 1000;           // touched by tax/wage/loan passes
    i32 production = 0;            // RecalcAllProduction / Amt production
    int prosperity = 0;
    int officesUpdated = 0;
    int buildingTaxFlags = -1;     // last RunBuildingTaxPass flags seen
    int newsProcessed = 0;
    bool cityStatsBroadcast = false;
    bool turnStatesSynced = false;
    int meisterPlayerCalls = 0;
};

SyntheticCity* g_city = nullptr;

// The pass hook: mutate the synthetic city per pass, exactly mirroring what the
// real world/ai passes would do (the deterministic economic side-effects),
// while the driver controls WHEN each runs.
void RunPass(TurnPass p, int arg, void* ctx) {
    auto* c = static_cast<SyntheticCity*>(ctx);
    c->seq.push_back(p);
    switch (p) {
        case TurnPass::RecalcAllProduction:   c->production = 100; break;
        case TurnPass::MeisterProcessPlayers: c->meisterPlayerCalls++; break;
        case TurnPass::AmtRunProductionPass:  c->production += 50; break;
        case TurnPass::AmtUpdateOfficeProsperity: c->prosperity = 5; break;
        case TurnPass::AmtRunBuildingTaxPass: c->buildingTaxFlags = arg;
                                              c->treasury += 200; break;
        case TurnPass::AmtProcessLoanRepayments:  c->treasury -= 30; break;
        case TurnPass::AmtProcessOfficeWages:     c->treasury -= 80; break;
        case TurnPass::AmtUpdateOffices:          c->officesUpdated = 1; break;
        case TurnPass::HeProcessAllPlayerNews:    c->newsProcessed = 1; break;
        case TurnPass::CityTickStatsBroadcast:    c->cityStatsBroadcast = true; break;
        case TurnPass::SyncAllTurnStates:         c->turnStatesSynced = true; break;
        case TurnPass::AmtBuildingTaxPassLight:   c->buildingTaxFlags = arg;
                                                  c->treasury += 100; break;
        default: break;
    }
}

} // namespace

TEST(GameTickE2E, FullHostRound) {
    SyntheticCity city;
    g_city = &city;

    TurnState s; s.netStandalone = -1; s.difficulty = 1;  // host drives

    // Population: 6 Persons. Kinds: 30 = farm (plants), 6 = human, others NPC.
    const int N = 6;
    u16 alive[N] = {0, 0, 0, 0, 0xFFFF, 0};
    u8  kinds[N] = {30, 4, 6, 11, 6, 5};   // last human at index 4
    u32 flags[N];
    for (int i = 0; i < N; ++i) flags[i] = 0xDEADBEEFu;

    // Per-turn accumulators: 134 dwords per Person, all preloaded to 999.
    std::vector<i32> accum(kPerTurnAccumStride * N, 999);

    // Factions: 0 alive+player, 1 alive+player, 2 alive+nonplayer, 3 free+player.
    FactionSlot factions[4];
    factions[0] = {0x0000, true};
    factions[1] = {0x0000, true};
    factions[2] = {0x0000, false};
    factions[3] = {0xFFFF, true};

    // Two farms with plant nodes.
    std::vector<std::vector<PlantNode>> farms = {
        { {0x00, 1, 4}, {0xFF, 0, 0}, {0x01, 4, 4} },  // 1 advances (node0)
        { {0x02, 0, 2}, {0x03, 2, 2} },                // 1 advances (node0)
    };

    TurnDriverCtx ctx;
    ctx.pass = RunPass;
    ctx.passCtx = &city;
    ctx.npcAliveMarker = alive;
    ctx.npcKinds = kinds;
    ctx.npcTurnFlags = flags;
    ctx.npcCount = N;
    ctx.perTurnAccum = accum.data();
    ctx.factions = factions;
    ctx.factionCount = 4;
    ctx.farms = &farms;

    BeginPlayerRound(s, ctx);

    // ---- 1. exact ordered pass sequence (hand-computed reference) ----
    // Two factions (0,1) are alive+player -> two MeisterProcessPlayers entries.
    std::vector<TurnPass> ref = {
        TurnPass::ComputeWealthGrid,
        TurnPass::NpcTurnFlagSweep,
        TurnPass::TickRegisteredEvents,
        TurnPass::ExpireEventSlots,
        TurnPass::ExpireApEventSlots,
        TurnPass::PlantGrowth,
        TurnPass::RecalcAllProduction,
        TurnPass::CitySnapshotStats,
        TurnPass::StraftatSyncAll,
        TurnPass::MeisterProcessPlayers,   // faction 0
        TurnPass::MeisterProcessPlayers,   // faction 1
        TurnPass::AmtRunProductionPass,
        TurnPass::AmtUpdateOfficeProsperity,
        TurnPass::AmtRunBuildingTaxPass,
        TurnPass::AmtProcessLoanRepayments,
        TurnPass::AmtProcessOfficeWages,
        TurnPass::AmtUpdateOffices,
        TurnPass::MeisterRunBuildingTasks,
        TurnPass::HeProcessAllPlayerNews,
        TurnPass::AiMethodBroadcastGroup,
        TurnPass::CityTickStatsBroadcast,
        TurnPass::AdvanceTurnTimer,
        TurnPass::MeisterProcessBuildingNeeds,
        TurnPass::TurnEndCoord27Broadcast,
        TurnPass::ResetPerTurnAccumulators,
        TurnPass::SyncAllTurnStates,
    };
    CHECK_EQ((int)ctx.order.size(), (int)ref.size());
    CHECK_EQ((int)city.seq.size(), (int)ref.size());
    bool orderOk = ctx.order.size() == ref.size();
    for (size_t i = 0; orderOk && i < ref.size(); ++i) {
        CHECK(ctx.order[i] == ref[i]);
        CHECK(city.seq[i] == ref[i]);
    }

    // ---- 2. per-faction iteration ----
    CHECK_EQ((int)ctx.processedFactions.size(), 2);
    CHECK_EQ(ctx.processedFactions[0], 0);
    CHECK_EQ(ctx.processedFactions[1], 1);
    CHECK_EQ(city.meisterPlayerCalls, 2);

    // ---- 3. NPC sweep result ----
    CHECK_EQ(ctx.humanPersonIndex, 4);    // last kind==6
    for (int i = 0; i < N; ++i)
        CHECK_EQ(flags[i], 0xDEADBEEFu & 0xE0874703u);  // cleared

    // ---- 4. plant growth ----
    CHECK_EQ(ctx.plantsAdvanced, 2);
    CHECK_EQ((int)farms[0][0].stage, 2);   // 1 -> 2
    CHECK_EQ((int)farms[0][2].stage, 4);   // at cap, unchanged
    CHECK_EQ((int)farms[1][0].stage, 1);   // 0 -> 1
    CHECK_EQ((int)farms[1][1].stage, 2);   // at cap, unchanged

    // ---- 5. economy / office / treasury end state (hand-computed) ----
    //   production: 100 (recalc) + 50 (amt)   = 150
    //   treasury:   1000 + 200(tax) - 30(loan) - 80(wages) = 1090
    CHECK_EQ(city.production, 150);
    CHECK_EQ(city.treasury, 1090);
    CHECK_EQ(city.prosperity, 5);
    CHECK_EQ(city.officesUpdated, 1);
    CHECK_EQ(city.buildingTaxFlags, 3);    // heavy path uses flags=3
    CHECK_EQ(city.newsProcessed, 1);
    CHECK(city.cityStatsBroadcast);
    CHECK(city.turnStatesSynced);

    // ---- 6. turn-end Coord27 broadcast set (one per Person) ----
    CHECK_EQ((int)ctx.coord27Factions.size(), N);

    // ---- 7. per-turn accumulators cleared (row-0 of each Person) ----
    for (int i = 0; i < N; ++i)
        CHECK_EQ(accum[i * kPerTurnAccumStride], 0);
    CHECK_EQ(accum[1], 999);   // mid-row untouched

    g_city = nullptr;
}

// E2E: the non-driving peer path produces only the light tax pass + reset.
TEST(GameTickE2E, FullLightPeerRound) {
    SyntheticCity city;
    TurnState s; s.netStandalone = 0; s.runFlags = 0;   // networked non-host

    const int N = 3;
    u16 alive[N] = {0, 0, 0};
    u8  kinds[N] = {4, 6, 5};
    u32 flags[N] = {0xFFFFFFFFu, 0xFFFFFFFFu, 0xFFFFFFFFu};
    std::vector<i32> accum(kPerTurnAccumStride * N, 42);

    TurnDriverCtx ctx;
    ctx.pass = RunPass;
    ctx.passCtx = &city;
    ctx.npcAliveMarker = alive;
    ctx.npcKinds = kinds;
    ctx.npcTurnFlags = flags;
    ctx.npcCount = N;
    ctx.perTurnAccum = accum.data();

    BeginPlayerRound(s, ctx);

    std::vector<TurnPass> ref = {
        TurnPass::ComputeWealthGrid,
        TurnPass::NpcTurnFlagSweep,
        TurnPass::TickRegisteredEvents,
        TurnPass::ExpireEventSlots,
        TurnPass::ExpireApEventSlots,
        TurnPass::PlantGrowth,
        TurnPass::RecalcAllProduction,
        TurnPass::CitySnapshotStats,
        TurnPass::AmtBuildingTaxPassLight,
        TurnPass::ResetPerTurnAccumulators,
    };
    CHECK_EQ((int)ctx.order.size(), (int)ref.size());
    for (size_t i = 0; i < ref.size() && i < ctx.order.size(); ++i)
        CHECK(ctx.order[i] == ref[i]);

    // Light path: treasury only +100 from the light tax pass, flags=2.
    CHECK_EQ(city.buildingTaxFlags, 2);
    CHECK_EQ(city.treasury, 1100);
    CHECK_EQ(city.meisterPlayerCalls, 0);
    CHECK(!city.turnStatesSynced);

    CHECK_EQ(ctx.humanPersonIndex, 1);
    for (int i = 0; i < N; ++i) {
        CHECK_EQ(flags[i], 0xE0874703u);
        CHECK_EQ(accum[i * kPerTurnAccumStride], 0);
    }
}
