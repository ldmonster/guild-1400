// Unit: RealMainMenuHooks composes Menu_RunMainMenu with the REAL sub-screen RunXxx.
// Each sub-screen runner override invokes the matching reconstructed driver.
#include "test.h"
#include "gui/main_menu_wire.h"

using namespace guild::gui;

TEST(MainMenuWireUnit, SubScreenRunnersInvokeRealDrivers) {
    RealMainMenuHooks h;
    h.subFrames = 8;   // bound each real sub-screen's headless loop

    // Each runner calls the real reconstructed RunXxx and bumps its counter.
    (void)h.EnterChooseCity();      CHECK(h.ranChooseCity == 1);
    (void)h.RunLoadGame();          CHECK(h.ranLoadGame == 1);
    (void)h.ChooseNetworkMode();    CHECK(h.ranNetwork == 1);
    h.RunOptionsGfx();              CHECK(h.ranOptGfx == 1);
    h.RunOptionsSfx();              CHECK(h.ranOptSfx == 1);
    h.RunOptionsGame();             CHECK(h.ranOptGame == 1);
    h.RunCreditsWindow();           CHECK(h.ranCreditsWin == 1);
    h.RunCreditsScroll();           CHECK(h.ranCreditsScroll == 1);
    (void)h.BuildChooseMissionDialog(); CHECK(h.ranMission == 1);
    char name[64] = {0};
    (void)h.RunFileSelector(name, sizeof name); CHECK(h.ranFileSelector == 1);
    h.MapLoadCityFile(0, "AUGSBURG");           CHECK(h.ranMapLoad == 1);
}

TEST(MainMenuWireUnit, InjectedSourcesDriveTheHooks) {
    RealMainMenuHooks h;
    h.runFrameLoop = [](int t) { return t < 5 ? 1 : 0; };
    h.clickEdge   = [](int f) { return f == 2; };
    h.hoverId     = [](int f) { return f == 2 ? 99 : -1; };
    h.escDown     = [](int) { return false; };
    CHECK(h.RunFrameLoop() == 1);          // tick 0
    CHECK(h.ClickEdge(2));
    CHECK(h.HoverId(2) == 99);
    CHECK(!h.EscDown(2));
    // FormLoadMainMenu does a real Window_Create -> a valid slot.
    CHECK(h.FormLoadMainMenu() >= 0);
}
