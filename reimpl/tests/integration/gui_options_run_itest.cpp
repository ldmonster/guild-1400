// Integration tests for the options runners: scripted click edges drive the OK / Cancel
// dispatch and we assert the EXACT setting persistence (OK saves the live widget values via
// the write hook; Cancel discards), deterministically.
//
// gilde.exe 0x56c21c / 0x56c808 / 0x56cc44 (loop + OK save-back), 0x56af54 (the write).

#include "test.h"

#include "gui/options_run.h"

using namespace guild::gui;

namespace {

// Scriptable driver hook.  It builds the form (default leaves), counts persist/apply via a
// sink, scripts a click on a chosen button on a chosen frame, and returns per-widget live
// values that DIFFER from the seeds (so an OK actually changes the settings).
struct DriverHooks : OptionsRunHooks {
    int clickFrame = 0;       // frame on which a click edge happens
    int clickTarget = 0;      // 0 == OK (row[0]), 1 == Cancel (row[1])
    int totalFrames = 2;      // RunFrameLoop returns nonzero this many frames
    int okId = -1, cancelId = -1;

    // The live value each widget reports on OK read-back (keyed by widget id == childIndex).
    int liveValue = 42;

    int FormGetChildObjectId(int, int, int childIndex) override { return 100 + childIndex; }
    void HudBuildButtonRow(int row[2]) override { row[0] = 900; row[1] = 901; okId = 900; cancelId = 901; }

    int RunFrameLoop(int frame) override { return frame < totalFrames ? 1 : 0; }
    bool ClickEdge(int frame) override { return frame == clickFrame; }
    int HoverId(int frame) override {
        if (frame != clickFrame) return -1;
        return clickTarget == 0 ? okId : cancelId;
    }
    int ObjectGetDataPtr(int) override { return liveValue; }
};

// Sink counting WriteGfxSettings / apply calls (reused options_screens commit path).
struct CountSink : OptionsScreenSink {
    int writes = 0, applyVol = 0, applyGfx = 0, applyCam = 0, reloadRes = 0, rebuildGp = 0;
    void WriteGfxSettings() override { ++writes; }
    void ApplyVolumeSettings() override { ++applyVol; }
    void ApplyGfxSettings() override { ++applyGfx; }
    void ApplyCameraAndScroll() override { ++applyCam; }
    void ReloadResolution() override { ++reloadRes; }
    void RebuildGroundplan() override { ++rebuildGp; }
};

} // namespace

// ---------------------------------------------------------------------------
// Sfx — OK persists the live slider values; Cancel leaves the settings untouched.
// ---------------------------------------------------------------------------
TEST(GuiOptRunIt, SfxOkPersists) {
    DriverHooks h; h.clickTarget = 0; h.liveValue = 55;
    CountSink sink;
    OptionsRunHooks* prev = Menu_SetOptionsRunHooks(&h);
    OptionsScreenSink* prevSink = nullptr; Options_SetSink(&sink);

    OptionsRunState st; st.snd.masterVol = 10;
    OptionsResult r = Menu_RunOptionsSfx(st, nullptr, 4);

    CHECK(r.accepted);
    // All five sound fields take the live value (clamped to u8).
    CHECK_EQ((int)st.snd.masterVol, 55);
    CHECK_EQ((int)st.snd.sfxVol, 55);
    CHECK_EQ((int)st.snd.msxVol, 55);
    CHECK_EQ((int)st.snd.speechVol, 55);
    CHECK_EQ((int)st.snd.msxFreq, 55);
    CHECK_EQ(sink.writes, 1);
    CHECK_EQ(sink.applyVol, 1);

    Options_SetSink(prevSink);
    Menu_SetOptionsRunHooks(prev);
}

TEST(GuiOptRunIt, SfxCancelDiscards) {
    DriverHooks h; h.clickTarget = 1; h.liveValue = 55;
    CountSink sink;
    OptionsRunHooks* prev = Menu_SetOptionsRunHooks(&h);
    Options_SetSink(&sink);

    OptionsRunState st; st.snd.masterVol = 10; st.snd.sfxVol = 11;
    OptionsResult r = Menu_RunOptionsSfx(st, nullptr, 4);

    CHECK(!r.accepted);
    CHECK_EQ((int)st.snd.masterVol, 10); // unchanged
    CHECK_EQ((int)st.snd.sfxVol, 11);
    CHECK_EQ(sink.writes, 0);
    CHECK_EQ(sink.applyVol, 0);

    Options_SetSink(nullptr);
    Menu_SetOptionsRunHooks(prev);
}

// ---------------------------------------------------------------------------
// Gfx — OK saves details/etc + the inverted gamma; a changed resolution triggers reload.
// ---------------------------------------------------------------------------
TEST(GuiOptRunIt, GfxOkPersistsAndGammaInverted) {
    DriverHooks h; h.clickTarget = 0; h.liveValue = 2; // every widget reports 2
    CountSink sink;
    OptionsRunHooks* prev = Menu_SetOptionsRunHooks(&h);
    Options_SetSink(&sink);

    OptionsRunState st;
    st.gfxResCap2 = true;
    st.gfx.curRes = 0;       // live res reads 2 -> changed -> reload
    st.gfx.fogPlane = 0;
    OptionsResult r = Menu_RunOptionsGfx(st, nullptr, 4);

    CHECK(r.accepted);
    CHECK_EQ((int)st.gfx.details, 2);
    CHECK_EQ((int)st.gfx.textureScale, 2);
    // gamma slider live value 2 -> save = 100 - 2 = 98 (stored in fogPlane slot).
    CHECK_EQ((int)st.gfx.fogPlane, 98);
    // resolution live (2) != saved (0) -> reload + curRes updated.
    CHECK(r.resChanged);
    CHECK_EQ((int)st.gfx.curRes, 2);
    CHECK_EQ(sink.reloadRes, 1);
    CHECK_EQ(sink.applyGfx, 1);
    CHECK(sink.writes >= 1);

    Options_SetSink(nullptr);
    Menu_SetOptionsRunHooks(prev);
}

// ---------------------------------------------------------------------------
// Game — OK saves sliders+dropdowns, forces invert_mouse=0; difficulty change in-game
// rebuilds the groundplan.  Cancel discards everything.
// ---------------------------------------------------------------------------
TEST(GuiOptRunIt, GameOkPersistsRebuildOnDifficulty) {
    DriverHooks h; h.clickTarget = 0; h.liveValue = 3;
    CountSink sink;
    OptionsRunHooks* prev = Menu_SetOptionsRunHooks(&h);
    Options_SetSink(&sink);

    OptionsRunState st;
    st.inGame = true;        // difficulty change -> rebuild
    st.game.difficulty = 1;  // live reads 3 -> changed
    st.game.speed = 0; st.game.invertMouse = 1;
    OptionsResult r = Menu_RunOptionsGame(st, nullptr, 4);

    CHECK(r.accepted);
    CHECK_EQ(st.game.speed, 3);
    CHECK_EQ((int)st.game.invertMouse, 0); // forced 0
    CHECK_EQ((int)st.game.difficulty, 3);
    CHECK_EQ(sink.rebuildGp, 1);
    CHECK_EQ(sink.applyCam, 1);
    CHECK(sink.writes >= 1);

    Options_SetSink(nullptr);
    Menu_SetOptionsRunHooks(prev);
}

TEST(GuiOptRunIt, GameCancelDiscards) {
    DriverHooks h; h.clickTarget = 1; h.liveValue = 7;
    CountSink sink;
    OptionsRunHooks* prev = Menu_SetOptionsRunHooks(&h);
    Options_SetSink(&sink);

    OptionsRunState st; st.game.speed = 99; st.game.difficulty = 2;
    OptionsResult r = Menu_RunOptionsGame(st, nullptr, 4);

    CHECK(!r.accepted);
    CHECK_EQ(st.game.speed, 99);          // unchanged
    CHECK_EQ((int)st.game.difficulty, 2);
    CHECK_EQ(sink.writes, 0);
    CHECK_EQ(sink.rebuildGp, 0);

    Options_SetSink(nullptr);
    Menu_SetOptionsRunHooks(prev);
}

// Determinism: two identical OK runs produce identical settings + sink counts.
TEST(GuiOptRunIt, Deterministic) {
    auto run = [](guild::config::SoundSettings& out, int& writes) {
        DriverHooks h; h.clickTarget = 0; h.liveValue = 33;
        CountSink sink;
        OptionsRunHooks* prev = Menu_SetOptionsRunHooks(&h);
        Options_SetSink(&sink);
        OptionsRunState st; st.snd.masterVol = 1;
        Menu_RunOptionsSfx(st, nullptr, 5);
        out = st.snd; writes = sink.writes;
        Options_SetSink(nullptr);
        Menu_SetOptionsRunHooks(prev);
    };
    guild::config::SoundSettings a, b; int wa = 0, wb = 0;
    run(a, wa); run(b, wb);
    CHECK_EQ((int)a.masterVol, (int)b.masterVol);
    CHECK_EQ((int)a.msxFreq, (int)b.msxFreq);
    CHECK_EQ(wa, wb);
    CHECK_EQ(wa, 1);
}
