// End-to-end test for the new-game session bootstrap (guild::app::InitOrLoad-
// Session, gilde.exe 0x533a54). Runs a FULL new-game session init on synthetic
// params (city + profession + seed + a small player population) and verifies,
// against a hand-computed reference:
//   * the exact ordered setup-step sequence (the recovered orchestration),
//   * the resulting world / player / economy state (RNG seeded, world reset,
//     per-player starting purse, day-2/06:00 calendar), and
//   * that the bootstrap reaches the lockstep turn loop and advances rounds.
//
// Links session_init.cpp + its real reused deps (entity/city/rand/gametime/
// building); the render/gui/net/command leaves are recorded through the hook.
#include "app/session_init.h"
#include "crt/rand.h"
#include "sim/gametime.h"
#include "tests/framework/test.h"

#include <string>
#include <vector>

using namespace guild;
using app::SetupStep;
using app::SessionMode;

namespace {

struct E2ELog {
    std::vector<SetupStep> steps;
    std::vector<int>       args;
};
void Rec(SetupStep s, int arg, void* p) {
    auto* l = static_cast<E2ELog*>(p);
    l->steps.push_back(s);
    l->args.push_back(arg);
}

} // namespace

TEST(AppSessionInitE2E, FullNewGameAgainstReference) {
    // ---- synthetic params ---------------------------------------------------
    E2ELog log;
    app::SessionInitCtx ctx;
    ctx.step = &Rec;
    ctx.stepCtx = &log;
    ctx.cityName = "Nuernberg";
    ctx.profession = "Baecker";
    ctx.rngSeed = 0xC0FFEE;
    ctx.difficulty = 2;            // base gold 750
    ctx.cheatStartGold = false;
    // population: 1 human player, 1 heir player, 1 guard NPC (not seeded)
    ctx.players.push_back({201, 6, true});
    ctx.players.push_back({202, 7, true});
    ctx.players.push_back({203, 11, true});  // guard kind 11 -> not seeded

    // reset the shared session-state mirror to the cold image before the run
    app::GameSessionState() = app::SessionState{};

    // ---- run the full bootstrap --------------------------------------------
    app::InitOrLoadSession(app::session::kNewGame, ctx, /*frames*/3);

    // ---- hand-computed reference: the exact NEW-single step sequence -------
    const std::vector<SetupStep> kExpected = {
        SetupStep::OfficeInitTable,
        SetupStep::NetConnectToServer,
        SetupStep::CommandQueueInitAndSync,
        SetupStep::LoadingShowProgress,
        SetupStep::GameInitWorldAndSounds,
        SetupStep::NewGameLoadCty,
        SetupStep::NewGameSyncScene,
        SetupStep::CharacterEnsureGateAvatars,
        SetupStep::SeedPlayerStartGold,
        SetupStep::HistoryLoadChronicleText,
        SetupStep::MapViewLoadBackground,
        SetupStep::LoadingFadeOutAndClose,
        SetupStep::GroundplanCreateWindow,
        SetupStep::SceneRefreshAndDaylight,
        SetupStep::ConfigApplyCameraScroll,
        SetupStep::RenderSetupViewTransform,
        SetupStep::EnterTurnLoop,
    };
    CHECK_EQ(ctx.order.size(), kExpected.size());
    bool seqOk = (ctx.order.size() == kExpected.size());
    for (size_t i = 0; seqOk && i < kExpected.size(); ++i) {
        if (ctx.order[i] != kExpected[i]) {
            seqOk = false;
            std::printf("    step[%zu] = %s, expected %s\n", i,
                        app::SetupStepName(ctx.order[i]),
                        app::SetupStepName(kExpected[i]));
        }
    }
    CHECK(seqOk);

    // ---- resulting state vs reference --------------------------------------
    CHECK(ctx.mode == SessionMode::NewSingle);
    CHECK(ctx.reachedTurnLoop);
    CHECK(ctx.worldInited);
    CHECK(ctx.rngSeeded);

    // economy: base = 1250 - 250*2 = 750 ; rate 100% -> 750 each.
    CHECK_EQ(ctx.startGoldBase, 750);
    CHECK_EQ(static_cast<int>(ctx.startGoldByPlayer.size()), 2); // human + heir only
    CHECK_EQ(ctx.startGoldByPlayer[0], 750);
    CHECK_EQ(ctx.startGoldByPlayer[1], 750);

    // calendar: a fresh game starts on day 2; after 3 bounded turn rounds the
    // clock has rolled forward and parks at the day-start hour (06:00).
    CHECK_EQ(app::WorldClock().hour, app::kDayStartHour);
    CHECK_EQ(app::WorldClock().minute, 0);

    // round counter: world reset seeds it to 1; each bounded round increments.
    auto& st = app::GameSessionState();
    CHECK(st.roundCounter > 1);
    CHECK(!st.outroShown);  // loop exited on the frame bound, not an abort

    // RNG determinism: re-running with the same seed reproduces the LCG stream.
    crt::Srand(ctx.rngSeed);
    int x1 = crt::RandNext();
    int x2 = crt::RandNext();
    crt::Srand(ctx.rngSeed);
    CHECK_EQ(crt::RandNext(), x1);
    CHECK_EQ(crt::RandNext(), x2);
}

TEST(AppSessionInitE2E, AbortOnInvalidModeDoesNotReachTurnLoop) {
    // No mode bit set -> Invalid -> the bootstrap aborts before the turn loop.
    E2ELog log;
    app::SessionInitCtx ctx;
    ctx.step = &Rec;
    ctx.stepCtx = &log;
    app::GameSessionState() = app::SessionState{};
    app::InitOrLoadSession(/*flags*/0, ctx, 4);
    CHECK(ctx.mode == SessionMode::Invalid);
    CHECK(!ctx.reachedTurnLoop);
    // the preamble (connect/command/loading/world) still ran; the world-load
    // switch hit the error path and returned.
    bool sawWorldInit = false;
    for (auto s : ctx.order) if (s == SetupStep::GameInitWorldAndSounds) sawWorldInit = true;
    CHECK(sawWorldInit);
}
