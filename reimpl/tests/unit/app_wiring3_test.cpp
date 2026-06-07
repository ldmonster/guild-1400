// Unit tests for the THIRD wave of app-spine wiring (guild::app::RealSubsystems)
// plus the second-wave sim hook installer (guild::sim::InstallRealSimHooks2).
//
// Verifies the hooks moved STUB->REAL in this pass now forward to real
// reconstructed module entry points (observed via recorded events + module
// state): market-price recompute (world::Production*), music-thread init +
// outdoor-track selection (audio::music_world), cutscene poll (sim::Cutscene*),
// weather sky update (render::Weather*), HUD selection/labels (gui::Hud*/Status*),
// and the live-actor character driver (sim::CharacterUpdate). It also verifies
// InstallRealSimHooks2 binds the interaction/pamphlet command sinks and the
// character turn bridge to real reconstructed targets (codec enqueue + heading
// interp).
#include "app/wiring.h"
#include "config/ini.h"
#include "sim/real_hooks.h"
#include "sim/real_hooks2.h"
#include "sim/interaction_handlers.h"
#include "sim/charaction.h"
#include "sim/character.h"
#include "ai/intrigue.h"
#include "shim_impl/loopback_socket.h"
#include "shim_impl/mem_filesystem.h"
#include "shim_impl/memory_graphics.h"
#include "shim_impl/null_audio.h"
#include "shim_impl/null_platform.h"
#include "tests/framework/test.h"

using namespace guild;
using guild::app::RealSubsystems;

namespace {

struct Fixture3 {
    shim::NullPlatform plat;
    shim::MemoryGraphicsDevice gfx;
    shim::NullAudioDevice audio;
    shim::MemFileSystem fs;
    std::pair<std::unique_ptr<shim::LoopbackSocket>, std::unique_ptr<shim::LoopbackSocket>> sockPair
        = shim::LoopbackSocket::makePair();
    config::IniFile ini;
    RealSubsystems sub{&plat, &gfx, &audio, &fs, sockPair.first.get(), &ini};
};

} // namespace

// ---- init: market prices + music thread ------------------------------------

TEST(AppWiring3, BuildingMarketPricesIsReal) {
    Fixture3 f;
    f.sub.buildingComputeMarketPrices();
    CHECK(f.sub.firedReal("buildingComputeMarketPrices"));
    // The real production integrator returned the in-window work minutes for the
    // [6,22) work day (positive).
    CHECK(f.sub.marketWorkMinutes() > 0);
}

TEST(AppWiring3, SoundInitMusicThreadSeedsRealTrackTable) {
    Fixture3 f;
    f.sub.soundInitMusicThread(44100);
    CHECK(f.sub.firedReal("soundInitMusicThread"));
    // One OUTDOOR track entry was seeded into the real director table.
    CHECK_EQ(f.sub.musicTrackCount(), 1);
}

// ---- per-frame: cutscene poll / weather / HUD / character ------------------

TEST(AppWiring3, InputCommandPollDrivesCutsceneTable) {
    Fixture3 f;
    f.sub.inputCommandPoll(); // allocs a slot + picks lowest priority
    f.sub.inputCommandPoll();
    CHECK(f.sub.firedReal("inputCommandPoll"));
    // The real FindLowestPriority scan returned the active slot on each poll.
    CHECK(f.sub.cutscenesProcessed() >= 2);
}

TEST(AppWiring3, WeatherUpdateSkyIsReal) {
    Fixture3 f;
    // Advance the clock to mid-day so the arc peak is non-trivial.
    for (int i = 0; i < 5; ++i) f.sub.dayCycleAndOutdoorMusic();
    f.sub.weatherUpdateSky();
    CHECK(f.sub.firedReal("weatherUpdateSky"));
    // The real peak-of-3 intensity is the seeded arc baseline (>= 20) at minimum.
    CHECK(f.sub.weatherIntensity() >= 20);
}

TEST(AppWiring3, HudSelectionAndLabelsAreReal) {
    Fixture3 f;
    f.sub.hudSelectionAndTargets();
    f.sub.hudLabelsAndCaption();
    CHECK(f.sub.firedReal("hudSelectionAndTargets"));
    CHECK(f.sub.firedReal("hudLabelsAndCaption"));
    // Centered label at anchorX 320, width 80 -> x = 320 - 80/2 = 280.
    CHECK_EQ(f.sub.hudLabelX(), 280);
}

TEST(AppWiring3, CharacterCollectByOwnerRunsRealDriver) {
    Fixture3 f;
    f.sub.characterCollectByOwner();
    f.sub.characterCollectByOwner();
    CHECK(f.sub.firedReal("characterCollectByOwner"));
    CHECK_EQ(f.sub.characterUpdates(), 2);
}

TEST(AppWiring3, DayCycleAlsoDrivesOutdoorMusic) {
    Fixture3 f;
    f.sub.soundInitMusicThread(44100); // seed the outdoor track entry
    f.sub.dayCycleAndOutdoorMusic();   // selects + ticks the real music state machine
    CHECK(f.sub.firedReal("dayCycleAndOutdoorMusic"));
    // The real SelectOutdoorSeasonTrack loaded a track -> non-zero stream handle.
    CHECK(f.sub.musicHandle() != 0);
}

TEST(AppWiring3, NullDeviceMusicThreadDegradesToStub) {
    config::IniFile ini;
    RealSubsystems sub(nullptr, nullptr, nullptr, nullptr, nullptr, &ini);
    sub.soundInitMusicThread(44100);
    CHECK(sub.fired("soundInitMusicThread"));
    CHECK(!sub.firedReal("soundInitMusicThread"));
}

// ---- sim hook installer 2: command sinks + turn bridge ---------------------

TEST(AppWiring3, InteractionCommandSinkEnqueuesRealPacket) {
    sim::InstallRealSimHooks();   // creates + Init()s the shared real queue
    sim::InstallRealSimHooks2();  // binds the interaction command sink to it

    sim::CommandQueue* q = sim::RealCommandQueue();
    CHECK(q != nullptr);
    u32 before = q->send_count();

    // A real interaction handler (PerformRenovate) emits "renovieren"/15 through
    // the now-real command sink, which stages + enqueues a real opcode-17 packet.
    char r = sim::PerformRenovate(nullptr, nullptr, nullptr);
    CHECK_EQ(r, 15);
    CHECK(q->send_count() > before);
}

TEST(AppWiring3, RealTurnStepUsesRealHeadingInterp) {
    sim::InstallRealSimHooks2(); // binds CharActionHooks.turnStep to the real interp

    // A character already aligned with its target -> the real interpolation
    // reports the turn complete (done) on the first step.
    sim::Character ch{};
    ch.yaw = 1.0f;
    ch.turnTarget = 1; // delta ~0 -> aligned
    const sim::CharActionHooks& hooks = sim::GetCharActionHooks();
    CHECK(hooks.turnStep != nullptr);
    int done = hooks.turnStep(&ch, nullptr);
    CHECK_EQ(done, 1);
}

TEST(AppWiring3, PamphletCmdEnqueuesRealPacket) {
    sim::InstallRealSimHooks();
    sim::InstallRealSimHooks2();
    sim::CommandQueue* q = sim::RealCommandQueue();
    u32 before = q->send_count();
    // The AI pamphlet path emits via the real codec when ExecPamphlet succeeded.
    u8 code = guild::ai::EvalPamphlet(/*execOk=*/true, /*targetId=*/7);
    CHECK_EQ(code, 41);
    // A real opcode-16 packet was staged + enqueued onto the shared queue.
    CHECK(q->send_count() > before);
}
