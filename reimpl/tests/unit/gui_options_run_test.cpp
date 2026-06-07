// Unit tests for the options sub-screen RUNNERS (gui/options_run.*).
//
// Asserts the EXACT per-page widget build recorded via the run hooks: the form name +
// title, the per-child GetChildObjectId index, the SetValueOrText min/max/seed, the
// scroll-limit code and the dropdown line count, byte-for-byte against the gilde.exe
// decompiles (0x56c21c / 0x56c808 / 0x56cc44).

#include "test.h"

#include "gui/options_run.h"

using namespace guild::gui;

namespace {

// A recording hook that captures the build and (optionally) ends the loop immediately.
struct RecHooks : OptionsRunHooks {
    int RunFrameLoop(int) override { return 0; } // no frames -> Cancel path by default
};

} // namespace

// ---------------------------------------------------------------------------
// Sfx page — 5 children: 4 range-127 sliders + the range-4 / 5-line freq dropdown.
// ---------------------------------------------------------------------------
TEST(GuiOptRun, SfxBuild) {
    RecHooks h;
    OptionsRunHooks* prev = Menu_SetOptionsRunHooks(&h);

    OptionsRunState st;
    st.snd.masterVol = 100;
    st.snd.sfxVol    = 90;
    st.snd.msxVol    = 80;
    st.snd.speechVol = 70;
    st.snd.msxFreq   = 2;

    OptionsRunRecord rec;
    Menu_RunOptionsSfx(st, &rec, 0);

    CHECK(rec.formName != nullptr);
    CHECK_EQ(std::string(rec.formName), std::string(kOptionsSfxForm));
    CHECK_EQ(rec.title, kOptionsSfxTitle);
    CHECK_EQ(rec.widgetCount, kSfxWidgetCount);

    // sliders 0..3 range 127, slider scroll-limit, no dropdown text.
    const int seeds[5] = {100, 90, 80, 70, 2};
    for (int i = 0; i < 4; ++i) {
        CHECK_EQ(rec.widgets[i].childIndex, i);
        CHECK_EQ(rec.widgets[i].maxV, 127);
        CHECK_EQ(rec.widgets[i].seedValue, seeds[i]);
        CHECK_EQ(rec.widgets[i].scrollLim, kScrollLimitSlider);
        CHECK_EQ(rec.widgets[i].lines, 0);
    }
    // child 4: freq dropdown range 4, 5 lines, dropdown scroll-limit.
    CHECK_EQ(rec.widgets[4].childIndex, 4);
    CHECK_EQ(rec.widgets[4].maxV, 4);
    CHECK_EQ(rec.widgets[4].seedValue, 2);
    CHECK_EQ(rec.widgets[4].scrollLim, kScrollLimitDropdown);
    CHECK_EQ(rec.widgets[4].lines, 5);

    Menu_SetOptionsRunHooks(prev);
}

// ---------------------------------------------------------------------------
// Gfx page — child 0 resolution dropdown (seeded from cur_res, range = device caps),
// children 1..8 detail dropdowns + the gamma slider (inverted seed, range 100, min 50).
// ---------------------------------------------------------------------------
TEST(GuiOptRun, GfxBuild) {
    RecHooks h;
    OptionsRunHooks* prev = Menu_SetOptionsRunHooks(&h);

    OptionsRunState st;
    st.gfxResCap2 = true;     // -> resolution dropdown range 2
    st.gfx.curRes = 1;
    st.gfx.details = 2;
    st.gfx.textureScale = 1;
    st.gfx.floorLod = 2;
    st.gfx.floorMipmapping = 1;
    st.gfx.lodHandling = 1;
    st.gfx.shadowDetail = 2;
    st.gfx.fogPlane = 30;     // gamma seed = 100 - 30 = 70
    st.gfx.cameraLimits = 1;

    OptionsRunRecord rec;
    Menu_RunOptionsGfx(st, &rec, 0);

    CHECK_EQ(std::string(rec.formName), std::string(kOptionsGfxForm));
    CHECK_EQ(rec.title, kOptionsGfxTitle);
    CHECK_EQ(rec.widgetCount, kGfxWidgetCount);

    // child 0 resolution.
    CHECK_EQ(rec.widgets[0].childIndex, 0);
    CHECK_EQ(rec.widgets[0].maxV, 2);          // gfxResCap2 -> range 2
    CHECK_EQ(rec.widgets[0].seedValue, 1);     // cur_res
    CHECK_EQ(rec.widgets[0].scrollLim, kScrollLimitDropdown);
    CHECK_EQ(rec.widgets[0].lines, 3);
    CHECK(!rec.widgets[0].hidden);             // not in-game

    // child 7 gamma slider (build index 7).
    CHECK_EQ(rec.widgets[7].childIndex, 7);
    CHECK_EQ(rec.widgets[7].minV, 50);
    CHECK_EQ(rec.widgets[7].maxV, 100);
    CHECK_EQ(rec.widgets[7].seedValue, 70);    // 100 - fogPlane(30)
    CHECK_EQ(rec.widgets[7].scrollLim, kScrollLimitSlider);
    CHECK_EQ(rec.widgets[7].lines, 0);

    // detail dropdowns: child 1 details seed 2, child 4 floor-mipmap 2-line.
    CHECK_EQ(rec.widgets[1].seedValue, 2);
    CHECK_EQ(rec.widgets[4].lines, 2);
    CHECK_EQ(rec.widgets[5].lines, 2);

    Menu_SetOptionsRunHooks(prev);
}

// In-game (byte_63CC40) hides the resolution dropdown.
TEST(GuiOptRun, GfxResHiddenInGame) {
    RecHooks h;
    OptionsRunHooks* prev = Menu_SetOptionsRunHooks(&h);
    OptionsRunState st;
    st.inGame = true;
    st.gfxResCap1 = true;
    OptionsRunRecord rec;
    Menu_RunOptionsGfx(st, &rec, 0);
    CHECK(rec.widgets[0].hidden);
    Menu_SetOptionsRunHooks(prev);
}

// ---------------------------------------------------------------------------
// Game page — sparse form child indices (0..6, 9..12); 4 sliders + 7 dropdowns; the
// invert-mouse child (form child 4) is built then force-hidden.
// ---------------------------------------------------------------------------
TEST(GuiOptRun, GameBuild) {
    RecHooks h;
    OptionsRunHooks* prev = Menu_SetOptionsRunHooks(&h);

    OptionsRunState st;
    st.game.speed = 120;
    st.game.scrollSpeed = 400;
    st.game.mouseSpeed = 50;
    st.game.cameraSpeed = 60;
    st.game.difficulty = 3;

    OptionsRunRecord rec;
    Menu_RunOptionsGame(st, &rec, 0);

    CHECK_EQ(std::string(rec.formName), std::string(kOptionsGameForm));
    CHECK_EQ(rec.title, kOptionsGameTitle);
    CHECK_EQ(rec.widgetCount, kGameWidgetCount);

    // build index 0..3 are the sliders, with the recovered ranges + seeds.
    CHECK_EQ(rec.widgets[0].childIndex, 0); CHECK_EQ(rec.widgets[0].maxV, 160); CHECK_EQ(rec.widgets[0].seedValue, 120);
    CHECK_EQ(rec.widgets[1].childIndex, 1); CHECK_EQ(rec.widgets[1].maxV, 500); CHECK_EQ(rec.widgets[1].seedValue, 400);
    CHECK_EQ(rec.widgets[2].childIndex, 2); CHECK_EQ(rec.widgets[2].maxV, 100); CHECK_EQ(rec.widgets[2].seedValue, 50);
    CHECK_EQ(rec.widgets[3].childIndex, 3); CHECK_EQ(rec.widgets[3].maxV, 100); CHECK_EQ(rec.widgets[3].seedValue, 60);

    // build index 4 == form child 4 == invert mouse, hidden.
    CHECK_EQ(rec.widgets[4].childIndex, 4);
    CHECK(rec.widgets[4].hidden);

    // build index 7 == form child 9 == difficulty dropdown (range 4, 5 lines).
    CHECK_EQ(rec.widgets[7].childIndex, 9);
    CHECK_EQ(rec.widgets[7].maxV, 4);
    CHECK_EQ(rec.widgets[7].seedValue, 3);
    CHECK_EQ(rec.widgets[7].lines, 5);

    // last three dropdowns map to sparse children 10,11,12.
    CHECK_EQ(rec.widgets[8].childIndex, 10);
    CHECK_EQ(rec.widgets[9].childIndex, 11);
    CHECK_EQ(rec.widgets[10].childIndex, 12);

    Menu_SetOptionsRunHooks(prev);
}
