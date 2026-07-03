// tests/unit/oneone_screens_wave14_test.cpp — W14-SCREENS 1:1 GOLDEN PINS
//
// Wave-14 fidelity audit (MCP down): this file PINS recovered 1:1 values in the
// play menu/screen-flow cluster that the existing suites exercised but never
// asserted as golden constants. Every value below traces to the SOURCE's own
// provenance comment or the cluster progress docs (menu-choosecity.md,
// menu-newgame-flow.md, menu-loadgame.md, harden-screens-wave12.md) — none is
// invented. The behavioral suites stay byte-identical; these are additive pins.
#include "test.h"
#include "play/native_main_menu.h"          // MenuButtonScreenRect @0x529d08 layout
#include "play/sdl_options_screen.h"        // OptionsRowsFor value steps
#include "play/sdl_charintro_screen.h"      // CharIntroComputeLayout (form window)
#include "play/sdl_choosehistory_screen.h"  // ChooseHistory_RowToFlag mapping
#include "play/sdl_loadgame_screen.h"       // slot-table constants @0x569d00
#include "gui/main_menu.h"                  // kMainMenuButtons y-table, session bits

using namespace guild;

// ---------------------------------------------------------------------------
// 1) Main-menu button layout — VIBE_Menu_RunMainMenu @0x529d08.
//    The eight sprite buttons at x=kMainMenuButtonX(32)+winX in a 300x320
//    MAIN_MENU window centred in 800x600, y-table {10,53,96,139,182,225,268,
//    311}.  MenuButtonScreenRect maps a design-y to the framebuffer rect.
// ---------------------------------------------------------------------------
TEST(OneOneScreensW14, MainMenuButtonYTablePinned) {
    // gilde.exe 0x529d08: the AddSpriteToWindow build order y-values.
    const int expect[8] = {10, 53, 96, 139, 182, 225, 268, 311};
    CHECK_EQ(gui::kMainMenuButtonCount, 8);
    for (int i = 0; i < 8; ++i)
        CHECK_EQ(gui::kMainMenuButtons[i].y, expect[i]);
    CHECK_EQ(gui::kMainMenuButtonX, 32);
    CHECK_EQ(gui::kMainMenuButtonSprite, 174);   // _BUTTON_RED gfx id
}

TEST(OneOneScreensW14, MenuButtonScreenRectLayoutPinned) {
    // 1:1 layout from the decoded MENU\MAIN_MENU.form (FRM2) window (x=232,y=160)
    // + AddSpriteToWindow(32,Y,174) -> design (264, 160+Y). The GUI is drawn at
    // NATIVE pixel size and only CENTER-TRANSLATED by ((fbW-800)/2, (fbH-600)/2)
    // (VIBE_Window_PositionAtCoord @0x41d7e0: x + (screenW-800)/2 with A=screenW/2,
    // s=screenW/800) — sizes are NOT scaled. Height = _BUTTON_RED record +82 = 33.
    const int kBtnX = 232 + 32;        // 264 (window x + AddSprite x)
    play::MenuButtonRect r0 = play::MenuButtonScreenRect(/*designY=*/10, 800, 600);
    CHECK_EQ(r0.x, kBtnX);             // 264 (offset 0 at native 800x600)
    CHECK_EQ(r0.y, 160 + 10);          // 170
    CHECK_EQ(r0.w, 124);               // native nominal
    CHECK_EQ(r0.h, 33);               // native

    play::MenuButtonRect r7 = play::MenuButtonScreenRect(/*designY=*/311, 800, 600);
    CHECK_EQ(r7.x, kBtnX);             // 264
    CHECK_EQ(r7.y, 160 + 311);         // 471
    CHECK_EQ(r7.w, 124);
    CHECK_EQ(r7.h, 33);

    // 1024x768: NATIVE size, position center-translated by (1024-800)/2=112,
    // (768-600)/2=84. Size unchanged.
    play::MenuButtonRect r1k = play::MenuButtonScreenRect(/*designY=*/10, 1024, 768);
    CHECK_EQ(r1k.x, kBtnX + (1024 - 800) / 2);   // 264 + 112 = 376
    CHECK_EQ(r1k.y, (160 + 10) + (768 - 600) / 2); // 170 + 84 = 254
    CHECK_EQ(r1k.w, 124);              // NATIVE — not scaled
    CHECK_EQ(r1k.h, 33);              // NATIVE
}

// The button width is per-label in the original: VIBE_Object_RecomputeSize
// @0x41b164 (sprite kind 9) sets width = VIBE_Property_Get(label) + cap(12) +
// cap(12) + 4 — in NATIVE pixels. MenuButtonScreenRect passes that design width
// through at native size (no resolution scaling); only the position translates.
TEST(OneOneScreensW14, MenuButtonScreenRectVariableWidthPinned) {
    // A measured design width passes through at native size (800x600, offset 0).
    play::MenuButtonRect a = play::MenuButtonScreenRect(/*designY=*/10, 800, 600, /*designW=*/149);
    CHECK_EQ(a.w, 149);
    CHECK_EQ(a.x, 232 + 32);
    CHECK_EQ(a.h, 33);
    // Default design width is the 124 nominal (cap12 + centre100 + cap12).
    play::MenuButtonRect d = play::MenuButtonScreenRect(/*designY=*/10, 800, 600);
    CHECK_EQ(d.w, 124);
    // At 1024x768 the width stays NATIVE (149, not scaled); the x translates by +112.
    play::MenuButtonRect s = play::MenuButtonScreenRect(/*designY=*/10, 1024, 768, /*designW=*/149);
    CHECK_EQ(s.w, 149);                          // native, not 191
    CHECK_EQ(s.x, 232 + 32 + (1024 - 800) / 2);  // 376
}

// ---------------------------------------------------------------------------
// 2) word_63C740 session-flag bits — the main-menu dispatch arms one of these.
//    Pinned because the mode_fsm transition table keys off them.
// ---------------------------------------------------------------------------
TEST(OneOneScreensW14, SessionFlagBitsPinned) {
    CHECK_EQ(gui::kSessionNewGame,  0x0001);   // word_63C740 | 1
    CHECK_EQ(gui::kSessionLoadSave, 0x0002);   // word_63C740 & 2
    CHECK_EQ(gui::kSessionNetwork,  0x0004);   // word_63C740 & 4
}

// ---------------------------------------------------------------------------
// 3) Options pages — value STEPS (the existing suite pins ranges, not steps).
//    Steps are the original SetValueOrText increments recovered in
//    sdl_options_screen.cpp's per-row comments.
// ---------------------------------------------------------------------------
TEST(OneOneScreensW14, OptionsValueStepsPinned) {
    config::GfxSettings g; config::SoundSettings s; config::GameSettings m;
    auto gfx  = play::OptionsRowsFor(play::OptionsPage::kGfx,  g, s, m);
    auto sfx  = play::OptionsRowsFor(play::OptionsPage::kSfx,  g, s, m);
    auto game = play::OptionsRowsFor(play::OptionsPage::kGame, g, s, m);

    // Gfx @0x56c21c: cycles step 1, gamma slider steps 10 over [50,100].
    CHECK_EQ((int)gfx.size(), 9);
    CHECK_EQ(gfx[0].step, 1);   // resolution cycle
    CHECK_EQ(gfx[1].step, 1);   // details
    CHECK_EQ(gfx[7].step, 10);  // gamma (fog_plane) slider
    CHECK_EQ(gfx[7].minV, 50);  CHECK_EQ(gfx[7].maxV, 100);

    // Sfx @0x56c808: four 0..127 volumes step 16; music-quality cycle 0..4 step 1.
    CHECK_EQ((int)sfx.size(), 5);
    CHECK_EQ(sfx[0].step, 16);  CHECK_EQ(sfx[0].maxV, 127);  // master
    CHECK_EQ(sfx[1].step, 16);  CHECK_EQ(sfx[2].step, 16);  CHECK_EQ(sfx[3].step, 16);
    CHECK_EQ(sfx[4].step, 1);   CHECK_EQ(sfx[4].maxV, 4);    // msx_freq

    // Game @0x56cc44: speed 0..160/16, mouse 0..500/50, scroll 0..100/10,
    // camera 0..100/10; panel_mode cycle 0..4/1.
    CHECK_EQ((int)game.size(), 11);
    CHECK_EQ(game[0].step, 16);  CHECK_EQ(game[0].maxV, 160);  // speed
    CHECK_EQ(game[1].step, 50);  CHECK_EQ(game[1].maxV, 500);  // mouse_speed
    CHECK_EQ(game[2].step, 10);  CHECK_EQ(game[2].maxV, 100);  // scroll_speed
    CHECK_EQ(game[3].step, 10);  CHECK_EQ(game[3].maxV, 100);  // camera_speed
    CHECK_EQ(game[7].step, 1);   CHECK_EQ(game[7].maxV, 4);    // panel_mode

    // The hidden "Invert Mouse" row (child 4) keeps its layout slot (0x56cf04).
    CHECK(game[4].hidden);
}

TEST(OneOneScreensW14, GammaSeedInversionPinned) {
    // 0x56c5df: slider seed = 100 - saved fog_plane.
    config::GfxSettings g; config::SoundSettings s; config::GameSettings m;
    g.fogPlane = 0;
    CHECK_EQ(play::OptionsRowsFor(play::OptionsPage::kGfx, g, s, m)[7].value, 100);
    g.fogPlane = 100;
    CHECK_EQ(play::OptionsRowsFor(play::OptionsPage::kGfx, g, s, m)[7].value, 0);
    g.fogPlane = 40;
    CHECK_EQ(play::OptionsRowsFor(play::OptionsPage::kGfx, g, s, m)[7].value, 60);
}

// ---------------------------------------------------------------------------
// 4) ChooseHistory row -> History flag — VIBE_Menu_RunChooseHistory @0x52d684.
//    id0->flag1 (factual), id1->flag2 (individual), id2/else->flag0 (none).
//    Pure 1:1 mapping; previously only in the asset-guarded e2e.
// ---------------------------------------------------------------------------
TEST(OneOneScreensW14, ChooseHistoryRowToFlagPinned) {
    CHECK_EQ(play::ChooseHistory_RowToFlag(0), 1);  // факт. историческое -> 1
    CHECK_EQ(play::ChooseHistory_RowToFlag(1), 2);  // индивидуальная     -> 2
    CHECK_EQ(play::ChooseHistory_RowToFlag(2), 0);  // без справки         -> 0
    CHECK_EQ(play::ChooseHistory_RowToFlag(3), 0);  // back/other          -> 0
}

// ---------------------------------------------------------------------------
// 5) Difficulty screen form window — VIBE_Menu_ChooseCharacterIntroVariant
//    @0x52e4e0: form menu\choosecharacter_intro window. frida Window_Create ground
//    truth (this build): (x=128,y=72,w=490,h=441). The form is center-translated, so
//    the parchment FORM rect is x=(800-490)/2=155, y=72, 490x441 (matches the measured
//    on-screen parchment sheet). The 6 buttons + title sit on it at screen centre.
// ---------------------------------------------------------------------------
TEST(OneOneScreensW14, CharIntroFormWindowPinned) {
    play::CharIntroLayout L = play::CharIntroComputeLayout(800, 600, /*rowCount=*/6);
    CHECK_EQ(L.pw, 490);
    CHECK_EQ(L.ph, 441);
    CHECK_EQ(L.px, 155);       // center-translated: (800-490)/2
    CHECK_EQ(L.py, 72);
    CHECK_EQ(L.cx, 400);       // buttons + title centre on screen
    CHECK_EQ(L.rowCount, 6);   // five difficulty levels + the back row
}

// ---------------------------------------------------------------------------
// 6) Load-game slot table — VIBE_SaveBrowser_LoadSlotMetadata @0x569d00.
//    16 slot rows; the widget-id base; the back-row index. (The 544-byte
//    stride + +4/+8/+12/+25 record offsets are the internal table layout, see
//    progress/menu-loadgame.md; the externally observable contract pinned here.)
// ---------------------------------------------------------------------------
TEST(OneOneScreensW14, LoadGameSlotConstantsPinned) {
    CHECK_EQ(play::kLoadGameSlotCount, 16);           // cap 16 (0x569d64)
    CHECK_EQ(play::kLoadGameSlotWidgetBase, 0x4C00);  // row+4/+8 occupied window id base
}
