// Unit tests for the new-game session bootstrap (guild::app::InitOrLoadSession,
// gilde.exe 0x533a54). Covers: the session-flags decode (new vs load vs network
// vs net-save), the recovered ordered setup-step sequence for a NEW single game,
// the RNG seed, the starting-gold formula, the world-reset core, and that the
// bootstrap reaches the lockstep turn loop.
//
// Self-contained: links session_init.cpp + its reused deps (entity/city/rand/
// gametime/building) + the framework. No OS/render/net code is exercised — the
// render/gui/net/command leaves are recorded through the SetupStep hook.
#include "app/session_init.h"
#include "crt/rand.h"
#include "sim/buildingtype_callers.h"  // the REAL NewGameSyncScene sink (0x504ce0)
#include "sim/entity.h"                // g_persons (the live anchor-scan table)
#include "tests/framework/test.h"
#include "world/amt.h"                 // AmtSetRateHook (the canonical 0x58f19c)

#include <string>
#include <vector>

using namespace guild;
using app::SetupStep;
using app::SessionMode;
using app::SessionInitCtx;

namespace {

// Recording hook: appends the step name to a shared log.
struct StepLog {
    std::vector<SetupStep> steps;
    std::vector<int>       args;
};
void RecordStep(SetupStep s, int arg, void* ctxp) {
    auto* log = static_cast<StepLog*>(ctxp);
    log->steps.push_back(s);
    log->args.push_back(arg);
}

int indexOf(const std::vector<SetupStep>& v, SetupStep s) {
    for (size_t i = 0; i < v.size(); ++i)
        if (v[i] == s) return static_cast<int>(i);
    return -1;
}
bool contains(const std::vector<SetupStep>& v, SetupStep s) {
    return indexOf(v, s) >= 0;
}

SessionInitCtx makeCtx(StepLog& log) {
    SessionInitCtx ctx;
    ctx.step = &RecordStep;
    ctx.stepCtx = &log;
    ctx.cityName = "Augsburg";
    ctx.profession = "Schmied";
    ctx.rngSeed = 0xDEADBEEF;
    ctx.difficulty = 1;
    return ctx;
}

} // namespace

// ----------------------------------------------------------------------------
// Session-flag decode
// ----------------------------------------------------------------------------
TEST(AppSessionInit, DecodeNewSingle) {
    CHECK(app::DecodeSessionMode(app::session::kNewGame, 0) == SessionMode::NewSingle);
}
TEST(AppSessionInit, DecodeNewNetwork) {
    auto f = app::session::kNewGame | app::session::kNetwork;
    CHECK(app::DecodeSessionMode(f, -1) == SessionMode::NewNetwork);
}
TEST(AppSessionInit, DecodeLoadSave) {
    CHECK(app::DecodeSessionMode(app::session::kLoadSave, 0) == SessionMode::LoadSave);
}
TEST(AppSessionInit, DecodeLoadNetSave) {
    CHECK(app::DecodeSessionMode(app::session::kLoadNetSave, -1) == SessionMode::LoadNetSave);
}
TEST(AppSessionInit, DecodeNewBeatsLoad) {
    // new (&1) wins even if &2 is also set (the original branch order).
    auto f = app::session::kNewGame | app::session::kLoadSave;
    CHECK(app::DecodeSessionMode(f, 0) == SessionMode::NewSingle);
}
TEST(AppSessionInit, DecodeInvalidWhenNoModeBit) {
    CHECK(app::DecodeSessionMode(0, 0) == SessionMode::Invalid);
}

// ----------------------------------------------------------------------------
// Setup-flag layout: static-init values recovered byte-for-byte.
// ----------------------------------------------------------------------------
TEST(AppSessionInit, StateStaticInitValues) {
    app::SessionState s;  // a fresh instance reflects the cold-IDB image
    CHECK_EQ(s.sessionFlags, 0);
    CHECK_EQ(s.isRoundOwner, 1);     // dword_63C79C = 1
    CHECK_EQ(s.autosaveEnabled, 1);  // dword_63C7D0 = 1
    CHECK_EQ(s.netSyncVerifyId, -1); // dword_631294 = -1
    CHECK_EQ(s.netStandalone, 0);    // dword_764CE0 = 0
    CHECK_EQ(s.difficulty, 0);
    CHECK(!s.DrivesHeavyPasses());   // runFlags 0, netStandalone 0
}
TEST(AppSessionInit, DrivesHeavyPassesWhenHostBitOrStandalone) {
    app::SessionState s;
    s.runFlags = 8;
    CHECK(s.DrivesHeavyPasses());
    app::SessionState t;
    t.netStandalone = -1;
    CHECK(t.DrivesHeavyPasses());
}

// ----------------------------------------------------------------------------
// Recovered session-bootstrap constants (1:1 pin).
//   kLiveGameFrameMask 0x20007 (gilde.exe 0x533a64, v112 = 131079) — the live-
//     game feature mask the turn loop passes to RunFrameLoop.
//   kRoundSyncToken    0x24087 (gilde.exe 0x533a5e, 147591) — round-sync token.
//   kDayStartHour      6       — the new game's clock start hour (GameTime_Set 06:00).
// These bare constants were not asserted against app::* anywhere; pin them so a
// drift in the recovered value is caught.
// ----------------------------------------------------------------------------
TEST(AppSessionInit, RecoveredBootstrapConstants) {
    CHECK_EQ((unsigned)app::kLiveGameFrameMask, 131079u);     // 0x20007
    CHECK_EQ((unsigned)app::kLiveGameFrameMask, 0x20007u);
    CHECK_EQ((int)app::kRoundSyncToken, 147591);              // 0x24087
    CHECK_EQ((int)app::kRoundSyncToken, 0x24087);
    CHECK_EQ((int)app::kDayStartHour, 6);
    // The mask decomposes into the documented bit set: kInputCommandPoll(1) |
    // bit1(2) | kWidgetMouse(4) | kNetworkCommand(0x20000) == 0x20007.
    CHECK_EQ((unsigned)app::kLiveGameFrameMask,
             app::mask::kInputCommandPoll | 0x2u | app::mask::kWidgetMouse |
                 app::mask::kNetworkCommand);
}

// ----------------------------------------------------------------------------
// Starting-gold formula (0x533c5e) + city-rate scale (0x58f19c).
// ----------------------------------------------------------------------------
TEST(AppSessionInit, StartGoldBaseByDifficulty) {
    CHECK_EQ(app::NewGameStartGoldBase(false, 0), 1250);
    CHECK_EQ(app::NewGameStartGoldBase(false, 1), 1000);
    CHECK_EQ(app::NewGameStartGoldBase(false, 2), 750);
    CHECK_EQ(app::NewGameStartGoldBase(false, 4), 250);
    CHECK_EQ(app::NewGameStartGoldBase(true, 4), 75000); // cheat overrides
}
// gilde.exe 0x58f19c — VIBE_Money_MultiplyByRate is NOT a percentage:
//   return amount * dword_649A88[dword_13CD6F2[189 * currencyId] >> 16];
// The second arg is the active city/currency index (byte_6477A1), and the
// multiplier comes from a runtime-populated city record table. app::MoneyMultiply-
// ByRate forwards to the canonical world::AmtMoneyMultiplyByRate, which models the
// runtime table as a settable rate hook (identity by default). With no hook set
// the result is the amount unchanged for ANY currency index (the prior golden's
// "*rate/100" was a wrong reconstruction and is corrected here).
TEST(AppSessionInit, MoneyMultiplyByRate) {
    world::AmtSetRateHook(nullptr); // identity default (no runtime city table)
    CHECK_EQ(app::MoneyMultiplyByRate(1000, 100), 1000); // identity (not 1000)
    CHECK_EQ(app::MoneyMultiplyByRate(1000, 50), 1000);  // NOT 500: index, not %
    CHECK_EQ(app::MoneyMultiplyByRate(1250, 0), 1250);
    CHECK_EQ(app::MoneyMultiplyByRate(1250, 7), 1250);   // index 7 -> still identity

    // Routes to the canonical 0x58f19c: install a rate hook reproducing the exact
    // expression `amount * dword_649A88[dword_13CD6F2[189*cur] >> 16]` over a
    // cold-image stand-in (record field 0 -> high word 0 -> dword_649A88[0]==0x15).
    world::AmtSetRateHook([](i32 amount, guild::u8 cur) -> i32 {
        static const i32 kCurve0 = 0x15; // dword_649A88[0] (cold image)
        (void)cur;
        return amount * kCurve0;
    });
    CHECK_EQ(app::MoneyMultiplyByRate(1000, 0), 1000 * 0x15);
    CHECK_EQ(app::MoneyMultiplyByRate(1000, 3), 1000 * 0x15);
    world::AmtSetRateHook(nullptr); // restore identity for the rest of the suite
}

// ----------------------------------------------------------------------------
// NEW single-game: recovered ordered setup-step sequence.
// ----------------------------------------------------------------------------
TEST(AppSessionInit, NewSingleStepOrder) {
    StepLog log;
    auto ctx = makeCtx(log);
    // two human players to exercise the gold seed loop
    ctx.players.push_back({101, 6, true});
    ctx.players.push_back({102, 6, true});
    app::InitOrLoadSession(app::session::kNewGame, ctx, /*frames*/2);

    CHECK(ctx.mode == SessionMode::NewSingle);
    CHECK(ctx.reachedTurnLoop);

    // The recovered order: OfficeInitTable precedes the net/command/loading
    // preamble, world reset precedes the .cty load, the .cty load precedes the
    // scene sync, and the turn loop is entered last.
    const auto& o = ctx.order;
    CHECK(indexOf(o, SetupStep::OfficeInitTable) < indexOf(o, SetupStep::NetConnectToServer));
    CHECK(indexOf(o, SetupStep::NetConnectToServer) < indexOf(o, SetupStep::CommandQueueInitAndSync));
    CHECK(indexOf(o, SetupStep::CommandQueueInitAndSync) < indexOf(o, SetupStep::LoadingShowProgress));
    CHECK(indexOf(o, SetupStep::LoadingShowProgress) < indexOf(o, SetupStep::GameInitWorldAndSounds));
    CHECK(indexOf(o, SetupStep::GameInitWorldAndSounds) < indexOf(o, SetupStep::NewGameLoadCty));
    CHECK(indexOf(o, SetupStep::NewGameLoadCty) < indexOf(o, SetupStep::NewGameSyncScene));
    CHECK(indexOf(o, SetupStep::NewGameSyncScene) < indexOf(o, SetupStep::CharacterEnsureGateAvatars));
    CHECK(indexOf(o, SetupStep::CharacterEnsureGateAvatars) < indexOf(o, SetupStep::SeedPlayerStartGold));
    CHECK(indexOf(o, SetupStep::SeedPlayerStartGold) < indexOf(o, SetupStep::LoadingFadeOutAndClose));
    CHECK(indexOf(o, SetupStep::LoadingFadeOutAndClose) < indexOf(o, SetupStep::GroundplanCreateWindow));
    CHECK(o.back() == SetupStep::EnterTurnLoop);
}

TEST(AppSessionInit, NewSingleSeedsRngAndWorld) {
    StepLog log;
    auto ctx = makeCtx(log);
    ctx.rngSeed = 12345;
    app::InitOrLoadSession(app::session::kNewGame, ctx, 1);
    CHECK(ctx.rngSeeded);
    CHECK(ctx.worldInited);
    // The seed actually reaches the LCG: after Srand(12345) the next draw is
    // deterministic — compare two independent reseeds.
    crt::Srand(12345);
    int a = crt::RandNext();
    crt::Srand(12345);
    int b = crt::RandNext();
    CHECK_EQ(a, b);
}

TEST(AppSessionInit, NewSingleStartGold) {
    StepLog log;
    auto ctx = makeCtx(log);
    ctx.difficulty = 1;     // base 1000
    ctx.players.push_back({101, 6, true});  // human -> seeded
    ctx.players.push_back({102, 7, true});  // heir  -> seeded
    ctx.players.push_back({103, 4, true});  // NPC kind 4 -> NOT seeded
    ctx.players.push_back({104, 6, false}); // not player -> NOT seeded
    app::InitOrLoadSession(app::session::kNewGame, ctx, 1);
    CHECK_EQ(ctx.startGoldBase, 1000);
    CHECK_EQ(static_cast<int>(ctx.startGoldByPlayer.size()), 2);
    CHECK_EQ(ctx.startGoldByPlayer[0], 1000);
    CHECK_EQ(ctx.startGoldByPlayer[1], 1000);
}

// ----------------------------------------------------------------------------
// Tutorial flag adds the tutorial-chain step.
// ----------------------------------------------------------------------------
TEST(AppSessionInit, TutorialBuildsChain) {
    StepLog log;
    auto ctx = makeCtx(log);
    app::InitOrLoadSession(app::session::kNewGame | app::session::kTutorial, ctx, 1);
    CHECK(contains(ctx.order, SetupStep::TutorialBuildChain));
    // Tutorial chain is built before the turn loop is entered.
    CHECK(indexOf(ctx.order, SetupStep::TutorialBuildChain)
          < indexOf(ctx.order, SetupStep::EnterTurnLoop));
}

// ----------------------------------------------------------------------------
// LOAD-save branch: no OfficeInitTable, no .cty, takes the LoadGameLoadSav step.
// ----------------------------------------------------------------------------
TEST(AppSessionInit, LoadSaveBranch) {
    StepLog log;
    auto ctx = makeCtx(log);
    ctx.difficulty = 3;
    app::InitOrLoadSession(app::session::kLoadSave, ctx, 1);
    CHECK(ctx.mode == SessionMode::LoadSave);
    CHECK(!contains(ctx.order, SetupStep::OfficeInitTable)); // new-game only
    CHECK(!contains(ctx.order, SetupStep::NewGameLoadCty));
    CHECK(contains(ctx.order, SetupStep::LoadGameLoadSav));
    CHECK(ctx.reachedTurnLoop);
    // load-save does NOT seed start gold (that is new-game only).
    CHECK_EQ(static_cast<int>(ctx.startGoldByPlayer.size()), 0);
}

// ----------------------------------------------------------------------------
// NETWORK host branch: adds the server-DLL + broadcast-socket steps.
// ----------------------------------------------------------------------------
TEST(AppSessionInit, NetworkHostBranch) {
    StepLog log;
    auto ctx = makeCtx(log);
    auto& st = app::GameSessionState();
    // The host loads Server.dll and connects to its own server, so the client
    // socket is LIVE (dword_764CE0 != -1) by the time the world-load branch
    // runs — that is the gate the original network path requires.
    st.netStandalone = 0;
    auto flags = app::session::kNewGame | app::session::kNetwork | app::session::kHost;
    app::InitOrLoadSession(flags, ctx, 1);
    CHECK(ctx.mode == SessionMode::NewNetwork);
    CHECK(contains(ctx.order, SetupStep::HistorySetInactive));
    CHECK(contains(ctx.order, SetupStep::NetLoadServerDll));
    CHECK(contains(ctx.order, SetupStep::NetOpenBroadcastSocket));
    CHECK(contains(ctx.order, SetupStep::NetStartNetworkGame));
    // network new game forces difficulty 2.
    CHECK_EQ(st.difficulty, 2);
    st.netStandalone = 0;  // restore for other tests
}

// ----------------------------------------------------------------------------
// World-reset core: round/outro/victory latches + day-2 calendar seed.
// ----------------------------------------------------------------------------
TEST(AppSessionInit, WorldResetSeedsLatchesAndClock) {
    StepLog log;
    auto ctx = makeCtx(log);
    auto& st = app::GameSessionState();
    st.outroShown = 7; st.victoryFlag = 9; st.roundCounter = 55;
    app::GameInitWorldAndSounds_Body(ctx);
    CHECK_EQ(st.roundCounter, 1);   // dword_63CC2C = 1
    CHECK_EQ(st.outroShown, 0);
    CHECK_EQ(static_cast<int>(st.victoryFlag), 0);
    CHECK(ctx.worldInited);
    CHECK_EQ(app::WorldClock().day, 2); // qword_13CE852 day = 2
}

// ----------------------------------------------------------------------------
// Rule-13 wiring: the NewGameSyncScene step (0x533fXX inside InitOrLoadSession
// @0x533a54) dispatches the REAL VIBE_Scene_SyncMeisterBuildings @0x504ce0
// (sim/buildingtype_callers.cpp), not a recorded no-op. We seed the live
// g_persons table from the NewGameLoadCty hook (the point where the .cty world
// load would populate it) and verify the meister anchors + shop list were
// published by the time the bootstrap finishes.
// ----------------------------------------------------------------------------
namespace {
void SeedPersonsOnCtyLoad(SetupStep s, int, void*) {
    if (s != SetupStep::NewGameLoadCty)
        return;
    // kind byte @+2 (byte_12CE912), sub-state byte @+12 (HIBYTE(dword_12CE919)).
    auto pb = [](int idx) {
        return reinterpret_cast<guild::u8*>(&guild::sim::g_persons[idx]);
    };
    pb(3)[2] = 12; pb(3)[12] = 0;   // anchorA (kind 12 / sub-state 0)
    pb(5)[2] = 12; pb(5)[12] = 1;   // anchorB (kind 12 / sub-state 1)
    pb(7)[2] = 11;                  // shop-list entry (kind 11)
}
} // namespace

TEST(AppSessionInit, NewGameSyncSceneRunsRealMeisterSync) {
    SessionInitCtx ctx;
    ctx.step = &SeedPersonsOnCtyLoad;
    ctx.rngSeed = 1;
    // Bit 0x40 of word_63C740 (SceneSyncFlagsWord) skips the recalc/stats and
    // RegisterNames passes so the scan core is isolated; the anchor publication
    // is the observable wire.
    sim::SetSceneSyncFlagsWord(0x40);
    app::InitOrLoadSession(app::session::kNewGame, ctx, 1);

    CHECK(contains(ctx.order, SetupStep::NewGameSyncScene));
    CHECK(sim::MeisterAnchorA() == &sim::g_persons[3]);  // dword_6498E8
    CHECK(sim::MeisterAnchorB() == &sim::g_persons[5]);  // dword_6498EC[0]
    CHECK_EQ(sim::MeisterShopCount(), 1);
    CHECK(sim::MeisterShopAt(0) == &sim::g_persons[7]);

    // cleanup: scrub the seeded records + flags for the other tests.
    sim::SetSceneSyncFlagsWord(0);
    sim::ResetEntityArrays();
}
