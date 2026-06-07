// End-to-end: drive real PlayableApp frames under a HUD mask (input + widgets +
// HUD mouse + HUD selection + tooltips + HUD labels) so the spine's inert hud*
// per-frame hooks reach the reconstructed gui code, THEN bind the HUD overlay with
// real content and assert observable, non-inert HUD output: caption/button-row laid
// out at expected positions, status text + player-bar slots registered, and a
// tooltip subject classified for a hovered scene object. Headless throughout
// (ScriptedPlatform / Memory / Null). Determinism: two runs are identical.
#include "test.h"

#include "play/hud_binder.h"
#include "play/playable_app.h"
#include "shim_impl/scripted_platform.h"
#include "shim_impl/memory_graphics.h"
#include "shim_impl/null_audio.h"
#include "shim_impl/mem_filesystem.h"
#include "shim_impl/loopback_socket.h"
#include "app/wiring.h"
#include "config/ini.h"
#include "crt/rand.h"

using namespace guild;
using guild::app::RealSubsystems;
using guild::play::HudOverlayBinder;

namespace {
struct Rig {
    shim::ScriptedPlatform plat;
    shim::MemoryGraphicsDevice gfx;
    shim::NullAudioDevice audio;
    shim::MemFileSystem fs;
    config::IniFile ini;
    std::unique_ptr<RealSubsystems> sub;
    std::unique_ptr<app::GameApp> game;
    std::pair<std::unique_ptr<shim::INetSocket>, std::unique_ptr<shim::INetSocket>> pair;
    Rig() {
        pair = shim::LoopbackSocket::makePair();
        sub = std::make_unique<RealSubsystems>(&plat, &gfx, &audio, &fs,
                                               pair.first.get(), &ini);
        game = std::make_unique<app::GameApp>(plat, gfx, audio, *sub);
    }
};

// The live HUD feature mask: input poll + widget mouse + HUD mouse + HUD selection
// + tooltips + the HUD draw/labels block. hudLabelsAndCaption sits inside the
// kGameObjects-gated HUD draw block (frameloop.cpp), so it must be set too.
constexpr std::uint32_t kHudMask =
    app::mask::kInputCommandPoll | app::mask::kWidgetMouse | app::mask::kHudMouse |
    app::mask::kHudSelection | app::mask::kTooltips | app::mask::kHudLabels |
    app::mask::kGameObjects;
} // namespace

// The spine's HUD per-frame hooks reach real gui code across many frames, and the
// in-frame HUD label step places the centered caption (anchor 320, width 80 -> 280).
TEST(HudBinderE2E, LiveFramesDriveHudHooksToRealCode) {
    Rig rig;
    rig.plat.quitAfterPumps(20);
    play::PlayableApp pa(*rig.game, rig.plat);
    CHECK(pa.init(/*displayMode=*/1));

    int frames = pa.runUntilQuit(kHudMask, /*maxFrames=*/-1);
    CHECK(pa.quitRequested());
    CHECK_EQ(frames, 20);

    // Every HUD per-frame step reached real reconstructed code (not inert no-ops).
    CHECK(rig.sub->firedReal("hudLabelsAndCaption"));
    CHECK(rig.sub->firedReal("hudSelectionAndTargets"));
    CHECK(rig.sub->firedReal("tooltipDispatch"));
    CHECK_EQ(rig.sub->frameCount(), 20);

    // The in-frame HUD label layout placed the centered caption at 320 - 80/2 = 280.
    CHECK_EQ(rig.sub->hudLabelX(), 280);

    pa.shutdown();
}

// Bind the HUD overlay with real content while the app is live: caption + button row
// laid out, status + player-bar registered, a hovered object classified to a builder.
TEST(HudBinderE2E, OverlayBoundWithRealHudContent) {
    Rig rig;
    rig.plat.quitAfterPumps(10);
    play::PlayableApp pa(*rig.game, rig.plat);
    CHECK(pa.init(1));

    // Drive frames; after each, the live HUD hooks have run on the spine side
    // (registering their own status-text/damage entries into the shared gui tables).
    int frames = pa.runUntilQuit(kHudMask, /*maxFrames=*/-1);
    CHECK_EQ(frames, 10);
    CHECK(rig.sub->firedReal("hudLabelsAndCaption"));

    // Bind the overlay over a CLEAN HUD table (reset claims the shared gui globals
    // back from the in-frame step). A small reconstructed scene: 6 owned objects,
    // class byte 32 (object builder).
    HudOverlayBinder binder;
    binder.reset({32, 32, 32, 32, 32, 32});

    // Now bind a full HUD overlay with real content: hover scene object 4, owning a
    // 3-object player bar.
    play::HudOverlay ov = binder.bind(/*hoverSceneObject=*/4, /*hoverTooltipId=*/0,
                                      /*ownedObjects=*/{101, 202, 303});

    // Caption laid out at the expected centered position.
    CHECK_EQ(ov.caption.x, 280);
    CHECK_EQ(ov.caption.flag88, 1);

    // Button row: 3 buttons spread evenly across 640px (golden 130/290/450).
    CHECK_EQ(ov.buttonRowPitch, 160);
    CHECK_EQ(static_cast<int>(ov.buttonRowX.size()), 3);
    CHECK_EQ(ov.buttonRowX[0], 130);
    CHECK_EQ(ov.buttonRowX[2], 450);

    // Player bar: 3 owned objects laid out at slots 0,1,2 with 78px row pitch.
    CHECK_EQ(static_cast<int>(ov.barSlots.size()), 3);
    CHECK_EQ(ov.barSlots[0].slotIndex, 0);
    CHECK_EQ(ov.barSlots[2].slotIndex, 2);
    CHECK_EQ(ov.barSlots[2].layout.rowY, 156); // 78 * 2

    // Status text: one entry per owned object, allocated 0,1,2.
    CHECK_EQ(static_cast<int>(ov.statusSlots.size()), 3);
    CHECK_EQ(ov.statusSlots[0], 0);
    CHECK_EQ(ov.statusSlots[2], 2);
    CHECK(ov.damageSlot >= 0);

    // A real tooltip subject was classified for the hovered scene object (index 4 ->
    // object code 4, class byte 32 -> Object builder). NOT the inert kNone path the
    // headless in-frame step takes with a null scene reference.
    CHECK_EQ(static_cast<int>(ov.hoverKind), static_cast<int>(gui::TooltipKind::kObject));
    CHECK_EQ(ov.hoverObjCode, 4);

    pa.shutdown();
}

// Determinism (PLAYABLE_PLAN B1): same inputs -> identical overlay across two full
// app runs, and the bound overlay is non-inert (a real subject classified).
TEST(HudBinderE2E, DeterministicOverlayAcrossRuns) {
    auto runOnce = [](int seed) {
        Rig rig;
        crt::Srand(static_cast<u32>(seed));
        rig.plat.quitAfterPumps(8);
        play::PlayableApp pa(*rig.game, rig.plat);
        CHECK(pa.init(1));
        pa.runUntilQuit(kHudMask, /*maxFrames=*/-1);

        HudOverlayBinder binder;
        binder.reset({32, 11, 32, 11, 32, 11}); // mixed classes (object/upgrade)
        play::HudOverlay ov = binder.bind(/*hoverSceneObject=*/3, /*hoverTooltipId=*/0,
                                          {7, 8, 9, 7}); // last 7 de-dups to slot 0
        pa.shutdown();
        return ov;
    };

    play::HudOverlay a = runOnce(12345);
    play::HudOverlay b = runOnce(12345);

    CHECK_EQ(a.caption.x, b.caption.x);
    CHECK_EQ(a.buttonRowPitch, b.buttonRowPitch);
    CHECK_EQ(a.buttonRowX[1], b.buttonRowX[1]);
    CHECK_EQ(static_cast<int>(a.barSlots.size()), static_cast<int>(b.barSlots.size()));
    CHECK_EQ(a.barSlots[3].slotIndex, b.barSlots[3].slotIndex); // de-dup'd 7 -> slot 0
    CHECK_EQ(static_cast<int>(a.hoverKind), static_cast<int>(b.hoverKind));
    CHECK_EQ(a.hoverObjCode, b.hoverObjCode);

    // Non-inert + the de-dup actually happened: object index 3 class 11 -> kUpgrade,
    // the 4th owned object (id 7) reused slot 0.
    CHECK_EQ(static_cast<int>(a.hoverKind), static_cast<int>(gui::TooltipKind::kUpgrade));
    CHECK_EQ(a.hoverObjCode, 3);
    CHECK_EQ(a.barSlots[3].slotIndex, 0);
}
