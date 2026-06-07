// E2E: a full 1:1 menu session end-to-end — the REAL Menu_RunMainMenu running the REAL
// sub-screens via RealMainMenuHooks, scripted New Game -> ChooseCity -> back -> Quit.
#include "test.h"
#include "gui/main_menu_wire.h"

using namespace guild::gui;

TEST(MainMenuWireE2E, FullSessionNewGameThenQuitIsDeterministic) {
    auto runOnce = [](MainMenuRunRecord& rec, RealMainMenuHooks& h) {
        MainMenuRunState st;
        h.subFrames = 4;
        // Frame 1: click New Game (real EnterChooseCity runs, returns false -> menu re-shows).
        // Frame 4: click Quit (arms quit flags). Loop runs to frame 7 then RunFrameLoop=0.
        h.runFrameLoop = [](int t) { return t < 7 ? 1 : 0; };
        h.clickEdge = [](int f) { return f == 1 || f == 4; };
        MainMenuRunRecord* recp = &rec;
        h.hoverId = [recp](int f) {
            if (f == 1) return recp->idNewGame;
            if (f == 4) return recp->idQuit;
            return -1;
        };
        h.escDown = [](int) { return false; };
        InstallRealMainMenuHooks(&h);
        int ret = Menu_RunMainMenu(st, &rec, /*maxFrames=*/16);
        InstallRealMainMenuHooks(nullptr);
        return std::pair<MainMenuRunState, int>(st, ret);
    };

    MainMenuRunRecord r1; RealMainMenuHooks h1;
    auto [st1, ret1] = runOnce(r1, h1);

    // The full build happened (8 radio buttons + version label + radiogroup).
    CHECK(r1.buttonCount == 8);
    CHECK(r1.radioCountArg == 8);
    CHECK(r1.radioFirstButton == r1.idNewGame);
    // The REAL sub-screen ran and Quit armed the quit flags.
    CHECK(h1.ranChooseCity == 1);
    CHECK(st1.quit == 1 && st1.close == 1);
    CHECK(ret1 == 5000);   // original returns SetGlobalVolume(5000)

    // Deterministic: a second identical run produces the same build + outcome.
    MainMenuRunRecord r2; RealMainMenuHooks h2;
    auto [st2, ret2] = runOnce(r2, h2);
    CHECK(r2.buttonCount == r1.buttonCount);
    CHECK(h2.ranChooseCity == h1.ranChooseCity);
    CHECK(st2.quit == st1.quit);
    CHECK(ret2 == ret1);
}
