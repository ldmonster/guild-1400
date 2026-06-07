// Integration tests: the options sub-screens against their REAL siblings —
// the config setting structs (config::GfxSettings / SoundSettings / GameSettings,
// the genuine in-memory mirror of byte_1233xxx), the options sub-tab dispatch
// (gui/menu_sub) and the 7-button options column (gui/menu).
//
// Only the OS / render / audio / form-loader boundary is mocked (the OptionsScreenSink,
// the credits/load hosts); everything below the dispatch runs the real translated code,
// so we verify that a full "open sub-tab -> edit sliders -> OK" round-trip actually
// mutates the real config structs and fires the real apply hooks in the right order, and
// that the sub-tab / options-column dispatch routes to the matching screen.

#include "test.h"

#include "gui/options_screens.h"
#include "gui/credits.h"
#include "gui/loadgame.h"
#include "gui/menu_sub.h"
#include "gui/menu.h"

#include <string>
#include <vector>

using namespace guild::gui;

namespace {

// A sink that records the apply-hook order across all three screens.
struct RecordingSink : OptionsScreenSink {
    std::vector<std::string> log;
    void WriteGfxSettings() override { log.push_back("write"); }
    void ApplyVolumeSettings() override { log.push_back("volume"); }
    void ApplyGfxSettings() override { log.push_back("gfx"); }
    void ApplyCameraAndScroll() override { log.push_back("camera"); }
    void ReloadResolution() override { log.push_back("reload"); }
    void RebuildGroundplan() override { log.push_back("groundplan"); }
};

// A sub-tab sink that actually opens the matching options screen (Game/Gfx/Sfx) and
// commits a canned edit through the REAL commit functions, against shared config structs.
struct ScreenOpeningSubTabSink : SubTabCommandSink {
    guild::config::GfxSettings gfx;
    guild::config::SoundSettings snd;
    guild::config::GameSettings game;
    bool ranGame = false, ranGfx = false, ranSfx = false;

    void RunGame() override {
        ranGame = true;
        int v[kGameWidgetCount] = {90, 300, 60, 30, 1, 1, 1, 2, 1, 1, 1};
        Game_Commit(true, v, /*diffValue*/2, /*diffSaved*/0, game);
    }
    void RunGfx() override {
        ranGfx = true;
        int v[kGfxWidgetCount] = {0, 1, 1, 1, 1, 1, 1, 60, 1};
        Gfx_Commit(true, v, /*resValue*/0, /*resSaved*/0, gfx);
    }
    void RunSfx() override {
        ranSfx = true;
        int v[kSfxWidgetCount] = {100, 90, 80, 70, 2};
        Sfx_Commit(true, v, snd);
    }
};

} // namespace

// Sfx OK fires write + volume in order, and mutates the real SoundSettings.
TEST(GuiOptScreensI, SfxCommitOrder) {
    RecordingSink sink;
    Options_SetSink(&sink);
    guild::config::SoundSettings snd;
    int v[kSfxWidgetCount] = {120, 60, 30, 15, 1};
    OptionsResult r = Sfx_Commit(true, v, snd);
    Options_SetSink(nullptr);

    CHECK(r.accepted);
    CHECK_EQ((int)snd.masterVol, 120);
    CHECK_EQ((int)snd.msxFreq, 1);
    CHECK_EQ((int)sink.log.size(), 2);
    CHECK(sink.log[0] == "write");
    CHECK(sink.log[1] == "volume");
}

// Sfx Cancel does NOT mutate and does NOT fire any hook.
TEST(GuiOptScreensI, SfxCancelNoop) {
    RecordingSink sink;
    Options_SetSink(&sink);
    guild::config::SoundSettings snd; // defaults all 0
    int v[kSfxWidgetCount] = {120, 60, 30, 15, 1};
    OptionsResult r = Sfx_Commit(false, v, snd);
    Options_SetSink(nullptr);

    CHECK(!r.accepted);
    CHECK_EQ((int)snd.masterVol, 0);
    CHECK(sink.log.empty());
}

// Gfx OK with a resolution change fires write+gfx, then write+reload (resChanged).
TEST(GuiOptScreensI, GfxResolutionChange) {
    RecordingSink sink;
    Options_SetSink(&sink);
    guild::config::GfxSettings gfx;
    int v[kGfxWidgetCount] = {0, 2, 1, 1, 1, 0, 2, 40, 1};
    OptionsResult r = Gfx_Commit(true, v, /*resValue*/3, /*resSaved*/1, gfx);
    Options_SetSink(nullptr);

    CHECK(r.accepted);
    CHECK(r.resChanged);
    CHECK_EQ((int)gfx.curRes, 3);
    CHECK_EQ((int)gfx.details, 2);
    CHECK_EQ((int)gfx.fogPlane, 100 - 40); // gamma inverted
    // write, gfx, write, reload
    CHECK_EQ((int)sink.log.size(), 4);
    CHECK(sink.log[0] == "write");
    CHECK(sink.log[1] == "gfx");
    CHECK(sink.log[3] == "reload");
}

// Game OK with a difficulty change rebuilds the groundplan, then write+camera.
TEST(GuiOptScreensI, GameDifficultyChange) {
    RecordingSink sink;
    Options_SetSink(&sink);
    guild::config::GameSettings game;
    int v[kGameWidgetCount] = {100, 400, 80, 50, 1, 1, 1, 4, 1, 1, 1};
    OptionsResult r = Game_Commit(true, v, /*diffValue*/4, /*diffSaved*/1, game);
    Options_SetSink(nullptr);

    CHECK(r.accepted);
    CHECK_EQ(game.speed, 100);
    CHECK_EQ((int)game.difficulty, 4);
    CHECK_EQ((int)game.invertMouse, 0);
    // groundplan, write, camera
    CHECK_EQ((int)sink.log.size(), 3);
    CHECK(sink.log[0] == "groundplan");
    CHECK(sink.log[1] == "write");
    CHECK(sink.log[2] == "camera");
}

// The REAL sub-tab dispatch (menu_sub) routes each tab to the matching screen, which
// commits through the REAL commit functions into shared config structs.
TEST(GuiOptScreensI, SubTabRoutesToScreens) {
    RecordingSink sink;
    Options_SetSink(&sink);
    ScreenOpeningSubTabSink tabs;
    Menu_SetSubTabCommandSink(&tabs);

    CHECK(Menu_DispatchSubTab(0) == SubTab::kGame);
    CHECK(Menu_DispatchSubTab(1) == SubTab::kGfx);
    CHECK(Menu_DispatchSubTab(2) == SubTab::kSfx);

    Menu_SetSubTabCommandSink(nullptr);
    Options_SetSink(nullptr);

    CHECK(tabs.ranGame && tabs.ranGfx && tabs.ranSfx);
    CHECK_EQ(tabs.game.speed, 90);
    CHECK_EQ((int)tabs.gfx.fogPlane, 100 - 60);
    CHECK_EQ((int)tabs.snd.masterVol, 100);
}

// The 7-button options column (menu) routes Gfx/Sfx/Game to the sub-screen runners.
TEST(GuiOptScreensI, OptionsColumnSelect) {
    CHECK(Menu_OptionsSelect(2) == OptionsItem::kGame);
    CHECK(Menu_OptionsSelect(3) == OptionsItem::kGfx);
    CHECK(Menu_OptionsSelect(4) == OptionsItem::kSfx);
}

// Load-game: a confirmed slot pick builds the path + sets the load session flag (10).
TEST(GuiOptScreensI, LoadGameRunPicksSlot) {
    struct H : LoadGameHost {
        int frame = 0;
        bool RunFrame() override { return frame++ < 4; }
        int HoveredWidget() override { return 103; }
        bool Clicked() override { return frame == 2; } // click on the 2nd frame
        bool ConfirmGateActive() override { return true; }
        bool ConfirmLoad() override { return true; }
    } host;

    SaveSlot slots[2] = {{true, 101, 5, "ALPHA"}, {true, 103, 9, "BRAVO"}};
    LoadGameResult r = LoadGame_Run(host, slots, 2, 100);

    CHECK(r.chosen);
    CHECK_EQ(r.slot, 1);
    CHECK_EQ(r.sessionFlag, kSessionLoad); // 10
    CHECK(r.path == "Gamedata\\Saves\\BRAVO.SAV");
}

// Load-game: declining the confirm gate keeps the screen open (no pick).
TEST(GuiOptScreensI, LoadGameDeclineConfirm) {
    struct H : LoadGameHost {
        int frame = 0;
        bool RunFrame() override { return frame++ < 6; }
        int HoveredWidget() override { return 103; }
        bool Clicked() override { return true; }
        bool ConfirmGateActive() override { return true; }
        bool ConfirmLoad() override { return false; } // user says no
    } host;
    SaveSlot slots[1] = {{true, 103, 9, "BRAVO"}};
    LoadGameResult r = LoadGame_Run(host, slots, 1, 6);
    CHECK(!r.chosen);
    CHECK_EQ(r.sessionFlag, 0);
}
