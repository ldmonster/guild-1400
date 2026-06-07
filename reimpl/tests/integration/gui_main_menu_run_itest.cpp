// Integration tests for gui::Menu_RunMainMenu @0x529d08 — scripted click edges
// (dword_672228 / dword_62D22C) against each widget id -> EXACT global-flag mutations +
// which sub-screen hook fired, for BOTH dword_63C7CC layouts; byte_67225C quit; loop end.
#include "test.h"

#include "gui/main_menu_run.h"
#include "gui/object.h"
#include "gui/window.h"
#include "gui/radiogroup.h"
#include "gui/widget_create.h"

#include <string>

using namespace guild::gui;

namespace {

void ResetAll() {
    ResetWidgets(); ResetWindows(); ResetWidgetCreate(); ResetRadioGroups();
}

// A scripted-click harness: one click on a chosen widget id on frame 0, then the loop
// ends (RunFrameLoop returns 0 after the first frame). Records which sub-screen fired.
struct ClickHooks : MainMenuRunHooks {
    int clickFrame = 0;
    int clickWidget = -1;   // resolved widget id to "hover" on clickFrame
    bool esc = false;

    // hook return values (drive the success branches)
    bool chooseCityOK = false;
    bool loadGameOK = false;
    bool networkOK = false;
    bool missionOK = false;
    bool fileSelOK = false;
    int  gfxRestart = 0;
    std::string fileSelName = "BERLIN";

    // which fired
    std::string fired;
    int mapLoadMode = -99;
    std::string mapCity;

    bool ClickEdge(int frame) override { return frame == clickFrame; }
    int  HoverId(int frame) override { return frame == clickFrame ? clickWidget : -1; }
    bool EscDown(int frame) override { return esc; }

    bool EnterChooseCity() override { fired = "ChooseCity"; return chooseCityOK; }
    bool RunLoadGame() override { fired = "LoadGame"; return loadGameOK; }
    bool ChooseNetworkMode() override { fired = "Network"; return networkOK; }
    void RunOptionsGfx() override { fired = "Gfx"; }
    void RunOptionsSfx() override { fired = "Sfx"; }
    void RunOptionsGame() override { fired = "Game"; }
    void RunCreditsWindow() override { fired = "CreditsWindow"; }
    void RunCreditsScroll() override { fired = "CreditsScroll"; }
    bool BuildChooseMissionDialog() override { fired = "Mission"; return missionOK; }
    bool RunFileSelector(char* out, int cap) override {
        std::snprintf(out, (size_t)cap, "%s", fileSelName.c_str());
        return fileSelOK;
    }
    void MapLoadCityFile(int mode, const char* name) override {
        mapLoadMode = mode; mapCity = name ? name : "";
    }
    int  GfxRestartRequested() override { return gfxRestart; }

    // The loop runs exactly 1 frame: RunFrameLoop returns 0 -> exit after frame 0.
    int  RunFrameLoop() override { return 0; }
};

// Build the menu once (no click) to learn the widget ids, then re-run with the click.
MainMenuRunRecord BuildIds(int missionMode) {
    ResetAll();
    ClickHooks h; h.clickWidget = -2; // never hovers
    MainMenuRunHooks* prev = Menu_SetRunHooks(&h);
    MainMenuRunState st; st.missionMode = missionMode;
    MainMenuRunRecord rec;
    Menu_RunMainMenu(st, &rec, 0);
    Menu_SetRunHooks(prev);
    return rec;
}

} // namespace

TEST(MainMenuRunIT, NewGame_Success_ArmsSessionAndCloses) {
    MainMenuRunRecord ids = BuildIds(0);
    ResetAll();
    ClickHooks h; h.clickWidget = ids.idNewGame; h.chooseCityOK = true;
    MainMenuRunHooks* prev = Menu_SetRunHooks(&h);
    MainMenuRunState st; st.missionMode = 0;
    MainMenuRunRecord rec;
    Menu_RunMainMenu(st, &rec, 4);
    Menu_SetRunHooks(prev);

    CHECK_EQ(h.fired, std::string("ChooseCity"));
    CHECK(st.sessionFlags & kRunSessNewGame);  // word_63C740 |= 1
    CHECK_EQ(st.close, 1);                      // dword_631614 = 1
    CHECK_EQ(st.quit, 0);
}

TEST(MainMenuRunIT, NewGame_Cancel_NoFlags) {
    MainMenuRunRecord ids = BuildIds(0);
    ResetAll();
    ClickHooks h; h.clickWidget = ids.idNewGame; h.chooseCityOK = false;
    MainMenuRunHooks* prev = Menu_SetRunHooks(&h);
    MainMenuRunState st;
    MainMenuRunRecord rec;
    Menu_RunMainMenu(st, &rec, 4);
    Menu_SetRunHooks(prev);
    CHECK_EQ(h.fired, std::string("ChooseCity"));
    CHECK_EQ(st.sessionFlags & kRunSessNewGame, 0);
    CHECK_EQ(st.close, 0);
}

TEST(MainMenuRunIT, Load_Success_Closes) {
    MainMenuRunRecord ids = BuildIds(0);
    ResetAll();
    ClickHooks h; h.clickWidget = ids.idLoad; h.loadGameOK = true;
    MainMenuRunHooks* prev = Menu_SetRunHooks(&h);
    MainMenuRunState st; MainMenuRunRecord rec;
    Menu_RunMainMenu(st, &rec, 4);
    Menu_SetRunHooks(prev);
    CHECK_EQ(h.fired, std::string("LoadGame"));
    CHECK_EQ(st.close, 1);
}

TEST(MainMenuRunIT, Multiplayer_Cancel_ClearsFlags) {
    MainMenuRunRecord ids = BuildIds(0);
    ResetAll();
    ClickHooks h; h.clickWidget = ids.idMultiplayer; h.networkOK = false;
    MainMenuRunHooks* prev = Menu_SetRunHooks(&h);
    MainMenuRunState st; st.sessionFlags = 0x55; // pre-set to prove it gets cleared
    MainMenuRunRecord rec;
    Menu_RunMainMenu(st, &rec, 4);
    Menu_SetRunHooks(prev);
    CHECK_EQ(h.fired, std::string("Network"));
    CHECK_EQ(st.sessionFlags, 0);  // word_63C740 = 0
    CHECK_EQ(st.close, 0);
}

TEST(MainMenuRunIT, Multiplayer_Success_Closes) {
    MainMenuRunRecord ids = BuildIds(0);
    ResetAll();
    ClickHooks h; h.clickWidget = ids.idMultiplayer; h.networkOK = true;
    MainMenuRunHooks* prev = Menu_SetRunHooks(&h);
    MainMenuRunState st; MainMenuRunRecord rec;
    Menu_RunMainMenu(st, &rec, 4);
    Menu_SetRunHooks(prev);
    CHECK_EQ(st.close, 1);
}

TEST(MainMenuRunIT, GfxOptions_Restart_Closes) {
    MainMenuRunRecord ids = BuildIds(0);
    ResetAll();
    ClickHooks h; h.clickWidget = ids.idGfxOptions; h.gfxRestart = 1;
    MainMenuRunHooks* prev = Menu_SetRunHooks(&h);
    MainMenuRunState st; MainMenuRunRecord rec;
    Menu_RunMainMenu(st, &rec, 4);
    Menu_SetRunHooks(prev);
    CHECK_EQ(h.fired, std::string("Gfx"));
    CHECK_EQ(st.restartDisplay, 1);  // dword_63CC38
    CHECK_EQ(st.close, 1);           // dword_631614 = 1
}

TEST(MainMenuRunIT, GfxOptions_NoRestart_StaysOpen) {
    MainMenuRunRecord ids = BuildIds(0);
    ResetAll();
    ClickHooks h; h.clickWidget = ids.idGfxOptions; h.gfxRestart = 0;
    MainMenuRunHooks* prev = Menu_SetRunHooks(&h);
    MainMenuRunState st; MainMenuRunRecord rec;
    Menu_RunMainMenu(st, &rec, 4);
    Menu_SetRunHooks(prev);
    CHECK_EQ(st.restartDisplay, 0);
    CHECK_EQ(st.close, 0);
}

TEST(MainMenuRunIT, SfxAndGameOptions_FireNoClose) {
    {
        MainMenuRunRecord ids = BuildIds(0);
        ResetAll();
        ClickHooks h; h.clickWidget = ids.idSfxOptions;
        MainMenuRunHooks* prev = Menu_SetRunHooks(&h);
        MainMenuRunState st; MainMenuRunRecord rec;
        Menu_RunMainMenu(st, &rec, 4);
        Menu_SetRunHooks(prev);
        CHECK_EQ(h.fired, std::string("Sfx"));
        CHECK_EQ(st.close, 0);
    }
    {
        MainMenuRunRecord ids = BuildIds(0);
        ResetAll();
        ClickHooks h; h.clickWidget = ids.idGameOptions;
        MainMenuRunHooks* prev = Menu_SetRunHooks(&h);
        MainMenuRunState st; MainMenuRunRecord rec;
        Menu_RunMainMenu(st, &rec, 4);
        Menu_SetRunHooks(prev);
        CHECK_EQ(h.fired, std::string("Game"));
        CHECK_EQ(st.close, 0);
    }
}

TEST(MainMenuRunIT, Quit_SetsAllQuitFlags) {
    MainMenuRunRecord ids = BuildIds(0);
    ResetAll();
    ClickHooks h; h.clickWidget = ids.idQuit;
    MainMenuRunHooks* prev = Menu_SetRunHooks(&h);
    MainMenuRunState st; MainMenuRunRecord rec;
    Menu_RunMainMenu(st, &rec, 4);
    Menu_SetRunHooks(prev);
    CHECK_EQ(st.outroShown, 1); // dword_63CC30
    CHECK_EQ(st.quit, 1);       // dword_63CC48
    CHECK_EQ(st.close, 1);      // dword_631614
}

TEST(MainMenuRunIT, Credits_FiresScroll) {
    MainMenuRunRecord ids = BuildIds(0);
    ResetAll();
    ClickHooks h; h.clickWidget = ids.idCredits;
    MainMenuRunHooks* prev = Menu_SetRunHooks(&h);
    MainMenuRunState st; MainMenuRunRecord rec;
    Menu_RunMainMenu(st, &rec, 4);
    Menu_SetRunHooks(prev);
    CHECK_EQ(h.fired, std::string("CreditsScroll"));
    CHECK_EQ(st.close, 0);
}

// ---- mission layout (dword_63C7CC != 0) ----

TEST(MainMenuRunIT, Mission_NetworkCity_LoadsHostCity) {
    MainMenuRunRecord ids = BuildIds(1);
    ResetAll();
    ClickHooks h; h.clickWidget = ids.idNetworkCity; h.fileSelOK = true; h.fileSelName = "BERLIN";
    MainMenuRunHooks* prev = Menu_SetRunHooks(&h);
    MainMenuRunState st; st.missionMode = 1;
    MainMenuRunRecord rec;
    Menu_RunMainMenu(st, &rec, 4);
    Menu_SetRunHooks(prev);
    CHECK_EQ(h.mapLoadMode, 1); // Map_LoadCityFile(1, ...) network host
    CHECK_EQ(st.close, 0);      // network-city does NOT close
}

TEST(MainMenuRunIT, Mission_CreditsWindow_Fires) {
    MainMenuRunRecord ids = BuildIds(1);
    ResetAll();
    ClickHooks h; h.clickWidget = ids.idCreditsWindow;
    MainMenuRunHooks* prev = Menu_SetRunHooks(&h);
    MainMenuRunState st; st.missionMode = 1;
    MainMenuRunRecord rec;
    Menu_RunMainMenu(st, &rec, 4);
    Menu_SetRunHooks(prev);
    CHECK_EQ(h.fired, std::string("CreditsWindow"));
}

// ---- byte_67225C quit + loop end ----

TEST(MainMenuRunIT, EscQuit_SetsQuitAndCloses) {
    MainMenuRunRecord ids = BuildIds(0); (void)ids;
    ResetAll();
    struct EscHooks : ClickHooks { } h;
    h.clickWidget = -3; // no button click
    h.esc = true;       // byte_67225C == 1
    MainMenuRunHooks* prev = Menu_SetRunHooks(&h);
    MainMenuRunState st; MainMenuRunRecord rec;
    Menu_RunMainMenu(st, &rec, 4);
    Menu_SetRunHooks(prev);
    CHECK_EQ(st.quit, 1);   // dword_63CC48
    CHECK_EQ(st.close, 1);  // dword_631614
}

TEST(MainMenuRunIT, LoopEndsWhenRunFrameLoopReturnsZero) {
    MainMenuRunRecord ids = BuildIds(0); (void)ids;
    ResetAll();
    ClickHooks h; h.clickWidget = -3;
    MainMenuRunHooks* prev = Menu_SetRunHooks(&h);
    MainMenuRunState st; MainMenuRunRecord rec;
    Menu_RunMainMenu(st, &rec, 100); // bound is high; loop must end on RunFrameLoop==0
    Menu_SetRunHooks(prev);
    CHECK_EQ(rec.frames, 1); // exactly one frame ran, then RunFrameLoop()==0 exits
}
