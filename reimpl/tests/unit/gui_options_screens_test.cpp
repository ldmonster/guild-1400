// Unit (golden-vector) tests for the Game / Gfx / Sfx options sub-screens,
// the credits crawl arithmetic, and the load-game slot dispatch.
//
// Goldens are the literal constants/maps recovered from the gilde.exe decompiles
// (0x56cc44 / 0x56c21c / 0x56c808 / 0x56e524 / 0x529c30 / 0x56a270).

#include "test.h"

#include "gui/options_screens.h"
#include "gui/credits.h"
#include "gui/loadgame.h"

using namespace guild::gui;

// ---------------------------------------------------------------------------
// Options layout tables.
// ---------------------------------------------------------------------------
TEST(GuiOptScreens, SfxLayout) {
    CHECK_EQ(kSfxWidgetCount, 5);
    for (int i = 0; i < 4; ++i) {
        CHECK_EQ(kSfxWidgets[i].range, 127);
        CHECK_EQ(kSfxWidgets[i].scrollLim, kScrollLimitSlider);
        CHECK_EQ(kSfxWidgets[i].lines, 0);
    }
    CHECK(kSfxWidgets[4].field == SfxField::kMsxFreq);
    CHECK_EQ(kSfxWidgets[4].range, 4);
    CHECK_EQ(kSfxWidgets[4].scrollLim, kScrollLimitDropdown);
    CHECK_EQ(kSfxWidgets[4].lines, 5);
}

TEST(GuiOptScreens, GfxLayout) {
    CHECK_EQ(kGfxWidgetCount, 9);
    // child 0 resolution dropdown
    CHECK(kGfxWidgets[0].field == GfxField::kResolution);
    CHECK_EQ(kGfxWidgets[0].lines, 3);
    // child 7 is the gamma slider (range 100), not a dropdown
    CHECK(kGfxWidgets[7].field == GfxField::kGamma);
    CHECK_EQ(kGfxWidgets[7].range, 100);
    CHECK_EQ(kGfxWidgets[7].scrollLim, kScrollLimitSlider);
    CHECK_EQ(kGfxWidgets[7].lines, 0);
    // the 2-line dropdowns are children 4 and 5 (floor mipmap, lod handling)
    CHECK_EQ(kGfxWidgets[4].lines, 2);
    CHECK_EQ(kGfxWidgets[5].lines, 2);
}

TEST(GuiOptScreens, GameLayout) {
    CHECK_EQ(kGameWidgetCount, 11);
    // sliders 0..3 with the recovered ranges
    CHECK_EQ(kGameWidgets[0].range, 160); // speed
    CHECK_EQ(kGameWidgets[1].range, 500); // scroll
    CHECK_EQ(kGameWidgets[2].range, 100); // mouse
    CHECK_EQ(kGameWidgets[3].range, 100); // camera
    for (int i = 0; i < 4; ++i)
        CHECK_EQ(kGameWidgets[i].lines, 0);
    // the non-contiguous child indices: ..,6 then jump to 9..12
    CHECK_EQ(kGameWidgets[6].childIndex, 6);
    CHECK_EQ(kGameWidgets[7].childIndex, 9);  // difficulty
    CHECK_EQ(kGameWidgets[7].range, 4);
    CHECK_EQ(kGameWidgets[7].lines, 5);
    CHECK_EQ(kGameWidgets[10].childIndex, 12); // panel mode
}

// ---------------------------------------------------------------------------
// Gamma inversion (0x56c5df / 0x56c79d).
// ---------------------------------------------------------------------------
TEST(GuiOptScreens, GammaInversion) {
    // seed: 100 - saved ; save: 100 - live -> round-trip is the identity.
    for (int saved = 0; saved <= 100; saved += 10) {
        int seeded = Gfx_GammaSeed(saved);
        CHECK_EQ(seeded, 100 - saved);
        CHECK_EQ(Gfx_GammaSave(seeded), saved);
    }
    CHECK_EQ(Gfx_GammaSave(50), 50); // 50 - (50 - 50)
}

// ---------------------------------------------------------------------------
// Save-back maps.
// ---------------------------------------------------------------------------
TEST(GuiOptScreens, SfxSaveBack) {
    int v[kSfxWidgetCount] = {10, 20, 30, 40, 3};
    guild::config::SoundSettings snd;
    Sfx_SaveBack(v, snd);
    CHECK_EQ((int)snd.masterVol, 10);
    CHECK_EQ((int)snd.sfxVol, 20);
    CHECK_EQ((int)snd.msxVol, 30);
    CHECK_EQ((int)snd.speechVol, 40);
    CHECK_EQ((int)snd.msxFreq, 3);
}

TEST(GuiOptScreens, GfxSaveBack) {
    // index order matches GfxField enum: [0]=res,[1]=details,[2]=texture,[3]=floorlod,
    // [4]=floormip,[5]=lod,[6]=shadow,[7]=gamma,[8]=cameralimits.
    int v[kGfxWidgetCount] = {0, 1, 2, 6, 1, 1, 2, 70, 2};
    guild::config::GfxSettings gfx;
    Gfx_SaveBack(v, gfx);
    CHECK_EQ((int)gfx.details, 1);
    CHECK_EQ((int)gfx.textureScale, 2);
    CHECK_EQ((int)gfx.floorLod, 6);
    CHECK_EQ((int)gfx.floorMipmapping, 1);
    CHECK_EQ((int)gfx.lodHandling, 1);
    CHECK_EQ((int)gfx.shadowDetail, 2);
    CHECK_EQ((int)gfx.cameraLimits, 2);
    // gamma slider lands inverted in the fogPlane byte slot (byte_123351C)
    CHECK_EQ((int)gfx.fogPlane, 100 - 70);
}

TEST(GuiOptScreens, GameSaveBack) {
    // index order: speed,scroll,mouse,camera,invert,nacht,cursor,diff,hints,panelhelp,panelmode
    int v[kGameWidgetCount] = {80, 250, 50, 40, 1, 1, 0, 3, 1, 0, 1};
    guild::config::GameSettings game;
    Game_SaveBack(v, game);
    CHECK_EQ(game.speed, 80);
    CHECK_EQ(game.scrollSpeed, 250);
    CHECK_EQ(game.mouseSpeed, 50);
    CHECK_EQ((int)game.cameraSpeed, 40);
    CHECK_EQ((int)game.invertMouse, 0);       // forced 0
    CHECK_EQ((int)game.nachtwaechter, 1);
    CHECK_EQ((int)game.showCursorTxt, 0);
    CHECK_EQ((int)game.difficulty, 3);
    CHECK_EQ((int)game.hints, 1);
    CHECK_EQ((int)game.panelHelp, 0);
    CHECK_EQ((int)game.panelMode, 1);
}

// ---------------------------------------------------------------------------
// Credits scroll arithmetic.
// ---------------------------------------------------------------------------
TEST(GuiCredits, ScrollStepRamp) {
    CHECK_EQ(Credits_ScrollStep(0.0f), 1);
    CHECK_EQ(Credits_ScrollStep(29.9f), 1);
    CHECK_EQ(Credits_ScrollStep(30.0f), 2);
    CHECK_EQ(Credits_ScrollStep(79.9f), 2);
    CHECK_EQ(Credits_ScrollStep(80.0f), 4);
    CHECK_EQ(Credits_ScrollStep(200.0f), 4);
}

TEST(GuiCredits, AdvanceAndComplete) {
    // step=2: offset advances on even frames only.
    CHECK_EQ(Credits_AdvanceOffset(5, 0, 2), 6);
    CHECK_EQ(Credits_AdvanceOffset(5, 1, 2), 5);
    CHECK_EQ(Credits_AdvanceOffset(5, 2, 2), 6);
    // initial offset = -screenHeight
    CHECK_EQ(Credits_InitialOffset(600), -600);
    // complete when textBottom < textHeight*scale + offset
    CHECK(!Credits_ScrollComplete(100, 50, 1.0, -10)); // 100 < 40 ? no
    CHECK(Credits_ScrollComplete(100, 50, 1.0, 80));   // 100 < 130 ? yes
}

// ---------------------------------------------------------------------------
// Load-game slot dispatch.
// ---------------------------------------------------------------------------
TEST(GuiLoadGame, MatchAndPath) {
    SaveSlot slots[3];
    slots[0] = {true,  101, 5,  "ALPHA"};
    slots[1] = {false, 102, -1, ""};       // not present
    slots[2] = {true,  103, 7,  "BRAVO"};
    CHECK_EQ(LoadGame_MatchSlot(slots, 3, 103), 2);
    CHECK_EQ(LoadGame_MatchSlot(slots, 3, 102), -1); // present flag clear
    CHECK_EQ(LoadGame_MatchSlot(slots, 3, 999), -1); // no match
    // empty obj id is skipped even if widget id matches
    SaveSlot empty[1] = {{true, 200, -1, "X"}};
    CHECK_EQ(LoadGame_MatchSlot(empty, 1, 200), -1);

    CHECK(LoadGame_BuildPath("BRAVO") == "Gamedata\\Saves\\BRAVO.SAV");
}
