// Unit tests for gui::Menu_RunMainMenu @0x529d08 — the EXACT widget build for both
// dword_63C7CC (missionMode) layouts: 8 sprite buttons (gfx 174, +88=1, textcolor 300)
// at the right y, RadioGroup_Create(8, firstId), and the version label.
#include "test.h"

#include "gui/main_menu_run.h"
#include "gui/object.h"        // ResetWidgets / g_widgets
#include "gui/window.h"        // ResetWindows
#include "gui/radiogroup.h"    // ResetRadioGroups
#include "gui/widget_create.h" // ResetWidgetCreate

using namespace guild::gui;

namespace {

// Recording hooks: capture the build calls + label, drive zero frames (loop exits at
// once because RunFrameLoop returns 0 by default).
struct BuildHooks : MainMenuRunHooks {
    int labelX = 0, labelY = 0, labelColor = 0, labelId = 4242;
    int createdLabel = -1;
    int randMod = 0;
    int randCalls = 0;

    int  RandMod3() override { ++randCalls; return randMod; }
    int  CreateTextLabel(int x, int y, const char*) override {
        labelX = x; labelY = y; createdLabel = labelId; return labelId;
    }
    void ObjectSetColor(int, int color) override { labelColor = color; }
    // No clicks, loop exits immediately (RunFrameLoop default returns 0).
};

void ResetAll() {
    ResetWidgets();
    ResetWindows();
    ResetWidgetCreate();
    ResetRadioGroups();
}

} // namespace

TEST(MainMenuRun, NormalLayout_EightButtons_GfxSfx) {
    ResetAll();
    BuildHooks hooks;
    MainMenuRunHooks* prev = Menu_SetRunHooks(&hooks);

    MainMenuRunState st;
    st.missionMode = 0;   // dword_63C7CC == 0 -> gfx/sfx options pair
    st.cdMusic = 0;
    MainMenuRunRecord rec;
    Menu_RunMainMenu(st, &rec, /*maxFrames*/ 0);

    Menu_SetRunHooks(prev);

    // 8 sprite buttons built (6 fixed + gfx + sfx).
    CHECK_EQ(rec.buttonCount, 8);
    CHECK_EQ(rec.missionTrio, false);

    // Exact y table in build order.
    const int wantY[8] = {10, 53, 96, 139, 268, 311, 182, 225};
    for (int i = 0; i < 8; ++i) {
        CHECK_EQ(rec.buttons[i].y, wantY[i]);
        CHECK_EQ(rec.buttons[i].x, 32);
        CHECK_EQ(rec.buttons[i].gfx, 174);
        CHECK_EQ(rec.buttons[i].textColor, 300);
        CHECK_EQ(rec.buttons[i].label88, 1); // widget +88 = 1 (label flag)
        CHECK(rec.buttons[i].widgetId >= 0);
    }

    // The optional trio ids are -1 (not built); gfx/sfx ARE built.
    CHECK(rec.idGfxOptions >= 0);
    CHECK(rec.idSfxOptions >= 0);
    CHECK_EQ(rec.idMission, -1);
    CHECK_EQ(rec.idNetworkCity, -1);
    CHECK_EQ(rec.idCreditsWindow, -1);

    // RadioGroup_Create(8, NewGame).
    CHECK_EQ(rec.radioCountArg, 8);
    CHECK_EQ(rec.radioFirstButton, rec.idNewGame);
    CHECK(rec.radioGroup >= 0);

    // Version label created + coloured 67.
    CHECK_EQ(rec.versionLabel, 4242);
    CHECK_EQ(rec.versionLabelColor, 67);
    CHECK_EQ(hooks.labelX, 8);
    CHECK_EQ(hooks.labelColor, 67);

    // No CD music -> no random track.
    CHECK_EQ(hooks.randCalls, 0);
    CHECK_EQ(rec.cdTrackLoaded, -1);
}

TEST(MainMenuRun, MissionLayout_EightButtons_Trio) {
    ResetAll();
    BuildHooks hooks;
    MainMenuRunHooks* prev = Menu_SetRunHooks(&hooks);

    MainMenuRunState st;
    st.missionMode = 1;   // dword_63C7CC != 0 -> mission/network-city/credits-window trio
    MainMenuRunRecord rec;
    Menu_RunMainMenu(st, &rec, 0);

    Menu_SetRunHooks(prev);

    // 6 fixed + 3 trio = 9 sprite buttons built (radio group still 8).
    CHECK_EQ(rec.buttonCount, 9);
    CHECK_EQ(rec.missionTrio, true);

    const int wantY[9] = {10, 53, 96, 139, 268, 311, 139, 182, 225};
    for (int i = 0; i < 9; ++i) {
        CHECK_EQ(rec.buttons[i].y, wantY[i]);
        CHECK_EQ(rec.buttons[i].gfx, 174);
        CHECK_EQ(rec.buttons[i].textColor, 300);
        CHECK_EQ(rec.buttons[i].label88, 1);
    }

    // Trio built; gfx/sfx NOT.
    CHECK(rec.idMission >= 0);
    CHECK(rec.idNetworkCity >= 0);
    CHECK(rec.idCreditsWindow >= 0);
    CHECK_EQ(rec.idGfxOptions, -1);
    CHECK_EQ(rec.idSfxOptions, -1);

    // Radio group is still 8 wide; first button is NewGame.
    CHECK_EQ(rec.radioCountArg, 8);
    CHECK_EQ(rec.radioFirstButton, rec.idNewGame);
    CHECK(rec.radioGroup >= 0);
}

TEST(MainMenuRun, CdMusic_LoadsRandomTrack) {
    ResetAll();
    BuildHooks hooks;
    hooks.randMod = 2; // KraeuterUndPhiolen
    MainMenuRunHooks* prev = Menu_SetRunHooks(&hooks);

    MainMenuRunState st;
    st.missionMode = 0;
    st.cdMusic = 1;    // dword_63C8F8 != 0
    MainMenuRunRecord rec;
    Menu_RunMainMenu(st, &rec, 0);

    Menu_SetRunHooks(prev);

    CHECK_EQ(hooks.randCalls, 1);
    CHECK_EQ(rec.cdTrackLoaded, 2);
}
