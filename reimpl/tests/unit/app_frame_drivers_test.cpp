// Unit tests for the app per-frame outer-loop drivers + the two newly-wired app
// hooks (the SIXTH wave of guild::app spine wiring):
//
// Translated VIBE_ functions under test:
//   VIBE_GameLogic_RunFrameLoopWrapper @0x50c720 -> app::RunFrameLoopWrapper
//   VIBE_GameLogic_RunPauseLoop        @0x56e7e0 -> app::RunPauseLoop
//   VIBE_GameLogic_RunEndRoundScreen   @0x527be8 -> app::RunEndRoundScreen
//   VIBE_Camera_UpdateCombatScroll     @0x487b2c -> app::ResolveCombatScroll
//   VIBE_GameLogic_Movement            @0x412f18 -> app::EntitySubRefcount
//
// Newly-wired hooks (mock -> real):
//   cameraCombatScroll      -> app::ResolveCombatScroll
//   soundWaveInitSineTables -> audio::InitSineTables
#include "app/frame_drivers.h"
#include "app/combat_scroll.h"
#include "app/entity_movement.h"
#include "app/wiring.h"
#include "config/ini.h"
#include "shim_impl/loopback_socket.h"
#include "shim_impl/mem_filesystem.h"
#include "shim_impl/memory_graphics.h"
#include "shim_impl/null_audio.h"
#include "shim_impl/null_platform.h"
#include "test.h"

#include <cstring>
#include <memory>
#include <vector>

using namespace guild;
using guild::app::RealSubsystems;

namespace {

// A counting ISubsystems whose RunFrameLoop-driven hooks just tally invocations so
// the driver loop structure can be tested without a full RealSubsystems. We reuse
// GameApp + RealSubsystems (headless) so the real RunFrameLoop body runs.
struct DriverFixture {
    shim::NullPlatform plat;
    shim::MemoryGraphicsDevice gfx;
    shim::NullAudioDevice audio;
    shim::MemFileSystem fs;
    std::pair<std::unique_ptr<shim::LoopbackSocket>, std::unique_ptr<shim::LoopbackSocket>> sockPair
        = shim::LoopbackSocket::makePair();
    config::IniFile ini;
    RealSubsystems sub{&plat, &gfx, &audio, &fs, sockPair.first.get(), &ini};
    app::GameApp appObj{plat, gfx, audio, sub};
};

} // namespace

// ---------------------------------------------------------------------------
// RunFrameLoopWrapper: the main playable-session loop runs exactly maxFrames
// frames under the 0x67FFF mask, publishing that mask to the spine.
// ---------------------------------------------------------------------------
TEST(AppFrameDrivers, RunFrameLoopWrapperMaskAndCount) {
    DriverFixture f;
    CHECK_EQ(app::kMainSessionMask, 0x67FFFu);
    CHECK_EQ(app::kMainSessionMask, 425983u);

    int frames = app::RunFrameLoopWrapper(f.appObj, /*maxFrames=*/7);
    CHECK_EQ(frames, 7);
    CHECK_EQ(f.sub.frameCount(), 7);
    // The wrapper publishes the main session mask every tick.
    CHECK_EQ(f.appObj.lastFeatureMask(), 0x67FFFu);
}

// ---------------------------------------------------------------------------
// RunPauseLoop: paused frames run a 0 mask and terminate on the un-pause key
// (scancode 57). Before the key is hit they keep pumping; once held, the loop
// ends on that frame.
// ---------------------------------------------------------------------------
TEST(AppFrameDrivers, RunPauseLoopEndsOnUnpauseKey) {
    DriverFixture f;
    CHECK_EQ(app::kUnpauseKeyScancode, 57);

    int probes = 0;
    // Report the un-pause key only on the 4th iteration.
    auto keyState = [&]() -> int {
        ++probes;
        return probes >= 4 ? app::kUnpauseKeyScancode : 0;
    };
    int frames = app::RunPauseLoop(f.appObj, keyState, /*maxFrames=*/50);
    CHECK_EQ(frames, 4);                 // ran exactly until the key was reported
    CHECK_EQ(f.appObj.lastFeatureMask(), 0u); // paused frames use the 0 mask
}

// RunPauseLoop respects the frame cap when the key never comes.
TEST(AppFrameDrivers, RunPauseLoopRespectsCap) {
    DriverFixture f;
    int frames = app::RunPauseLoop(f.appObj, []() { return 0; }, /*maxFrames=*/5);
    CHECK_EQ(frames, 5);
}

// ---------------------------------------------------------------------------
// RunEndRoundScreen: the loop raises the advance flag when a pending action is
// posted, and runs the full 0x67FFF mask.
// ---------------------------------------------------------------------------
TEST(AppFrameDrivers, RunEndRoundScreenRaisesAdvance) {
    DriverFixture f;
    int probes = 0;
    // dword_75BF38 != -1 on the 2nd frame onward -> raises the advance flag.
    auto pending = [&]() -> int { return ++probes >= 2 ? 42 : -1; };
    bool advance = false;
    int frames = app::RunEndRoundScreen(f.appObj, pending, /*maxFrames=*/6, &advance);
    CHECK_EQ(frames, 6);
    CHECK(advance); // the pending action was posted
    CHECK_EQ(f.appObj.lastFeatureMask(), 0x67FFFu);
}

// Without a pending action the advance flag stays clear.
TEST(AppFrameDrivers, RunEndRoundScreenNoAdvanceWhenIdle) {
    DriverFixture f;
    bool advance = true;
    app::RunEndRoundScreen(f.appObj, []() { return -1; }, /*maxFrames=*/3, &advance);
    CHECK(!advance);
}

// ===========================================================================
// ResolveCombatScroll — the combat edge-scroll decision core.
// ===========================================================================
TEST(AppCombatScroll, SettledVsActiveCodes) {
    // A "settled" vector is within 0.2 of {0,0,0}; an "active" one is outside.
    float settled[3] = {0.1f, -0.05f, 0.0f};   // |components| <= 0.2 -> within
    float active[3]  = {0.5f, 0.0f, 0.0f};     // 0.5 > 0.2 -> outside

    // scrollX = -1 selects the RIGHT edge: settled -> code 1, active -> code 2.
    {
        auto d = app::ResolveCombatScroll(-1, 0, nullptr, nullptr, nullptr, settled);
        CHECK_EQ((int)d.right, (int)app::CombatScrollCode::kSettled);
        CHECK_EQ((int)d.left, (int)app::CombatScrollCode::kNone);
    }
    {
        auto d = app::ResolveCombatScroll(-1, 0, nullptr, nullptr, nullptr, active);
        CHECK_EQ((int)d.right, (int)app::CombatScrollCode::kActive);
    }
    // scrollX = 1 selects the LEFT edge symmetrically.
    {
        auto d = app::ResolveCombatScroll(1, 0, nullptr, nullptr, active, nullptr);
        CHECK_EQ((int)d.left, (int)app::CombatScrollCode::kActive);
        CHECK_EQ((int)d.right, (int)app::CombatScrollCode::kNone);
    }
}

TEST(AppCombatScroll, ZeroDirectionUsesCode1NotCode2) {
    // scrollX = 0: both edges report !within -> code 1 (NOT 2), per the original.
    float active[3] = {1.0f, 0.0f, 0.0f};
    auto d = app::ResolveCombatScroll(0, 0, nullptr, nullptr, active, active);
    CHECK_EQ((int)d.left, (int)app::CombatScrollCode::kSettled);  // value 1
    CHECK_EQ((int)d.right, (int)app::CombatScrollCode::kSettled); // value 1

    // scrollY = -1 selects TOP, +1 selects BOTTOM.
    auto v = app::ResolveCombatScroll(0, -1, active, nullptr, nullptr, nullptr);
    CHECK_EQ((int)v.top, (int)app::CombatScrollCode::kActive);    // value 2
    auto w = app::ResolveCombatScroll(0, 1, nullptr, active, nullptr, nullptr);
    CHECK_EQ((int)w.bottom, (int)app::CombatScrollCode::kActive); // value 2
}

TEST(AppCombatScroll, UnknownDirectionLeavesCodesZero) {
    float active[3] = {9.0f, 9.0f, 9.0f};
    auto d = app::ResolveCombatScroll(7, 7, active, active, active, active);
    CHECK_EQ((int)d.left, 0);
    CHECK_EQ((int)d.right, 0);
    CHECK_EQ((int)d.top, 0);
    CHECK_EQ((int)d.bottom, 0);
}

// ===========================================================================
// EntitySubRefcount — VIBE_GameLogic_Movement d2_SubRefcount.
// ===========================================================================
TEST(AppEntityMovement, DecrementsThisRecord) {
    std::vector<app::EntityMoveRecord> recs(4);
    std::memset(recs.data(), 0, recs.size() * sizeof(app::EntityMoveRecord));
    recs[1].type = 3;        // not 5/8 -> no redirect
    recs[1].refcount = 5;
    bool underflow = false;
    app::EntitySubRefcount(recs.data(), 1, [&](const char*) { underflow = true; });
    CHECK_EQ(recs[1].refcount, 4);
    CHECK(!underflow);
}

TEST(AppEntityMovement, RedirectsToLinkedPartnerForType5And8) {
    std::vector<app::EntityMoveRecord> recs(4);
    std::memset(recs.data(), 0, recs.size() * sizeof(app::EntityMoveRecord));
    // record 0 is a type-5 pair whose suppress word is 0 -> redirect to +76 link.
    recs[0].type = 5;
    recs[0].suppress = 0;
    recs[0].linkIndex = 2;
    recs[0].refcount = 100;  // must NOT change (redirected away)
    recs[2].refcount = 9;    // partner takes the decrement
    app::EntitySubRefcount(recs.data(), 0);
    CHECK_EQ(recs[0].refcount, 100);
    CHECK_EQ(recs[2].refcount, 8);

    // type 8 with suppress != 0 -> NO redirect, this record is decremented.
    recs[0].type = 8;
    recs[0].suppress = 1;
    recs[0].refcount = 4;
    app::EntitySubRefcount(recs.data(), 0);
    CHECK_EQ(recs[0].refcount, 3);
    CHECK_EQ(recs[2].refcount, 8); // partner untouched this time
}

TEST(AppEntityMovement, UnderflowGuardReportsAndDoesNotDecrement) {
    std::vector<app::EntityMoveRecord> recs(2);
    std::memset(recs.data(), 0, recs.size() * sizeof(app::EntityMoveRecord));
    recs[0].type = 1;
    recs[0].refcount = 0;   // already at zero -> underflow guard fires
    std::string msg;
    app::EntitySubRefcount(recs.data(), 0, [&](const char* m) { msg = m; });
    CHECK_EQ(recs[0].refcount, 0); // unchanged
    CHECK(msg == "d2_SubRefcount: invalid refcount!");
}

// ===========================================================================
// Newly-wired hooks reach real reconstructed code.
// ===========================================================================
TEST(AppWiring6, CameraCombatScrollIsReal) {
    DriverFixture f;
    f.sub.cameraCombatScroll();
    CHECK(f.sub.firedReal("cameraCombatScroll"));
    // The headless edge vectors are the settled {0,0,0} block; scrollX=-1 -> the
    // real core reports the right edge SETTLED (code 1).
    CHECK_EQ(f.sub.combatScrollRightCode(), (int)app::CombatScrollCode::kSettled);
    // scrollY=1 over a settled bottom vector -> code 1 too.
    CHECK_EQ(f.sub.combatScrollBottomCode(), (int)app::CombatScrollCode::kSettled);
}

TEST(AppWiring6, SoundWaveInitSineTablesIsReal) {
    DriverFixture f;
    f.sub.soundWaveInitSineTables();
    CHECK(f.sub.firedReal("soundWaveInitSineTables"));
    CHECK_EQ(f.sub.sineTableCount(), 256); // real audio::InitSineTables(256)
}
