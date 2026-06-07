// Integration: drive the REAL Menu_RunMainMenu via RealMainMenuHooks with a scripted
// click on each menu item -> assert the matching REAL sub-screen ran + the flag effect.
#include "test.h"
#include "gui/main_menu_wire.h"

using namespace guild::gui;

namespace {
// Run one full menu session that clicks the widget id `targetIdSelector(rec)` on frame 1.
// Returns the final state + the wire (counters) via out-params.
struct Outcome { MainMenuRunState st; MainMenuRunRecord rec; RealMainMenuHooks h; int ret = 0; };

void RunClicking(Outcome& o, int (*pickId)(const MainMenuRunRecord&), int missionMode = 0) {
    o.st.missionMode = missionMode;
    o.h.subFrames = 4;
    o.h.runFrameLoop = [](int t) { return t < 4 ? 1 : 0; };   // ~4 loop frames then stop
    o.h.clickEdge = [](int f) { return f == 1; };             // a click edge on frame 1
    MainMenuRunRecord* recp = &o.rec;
    o.h.hoverId = [recp, pickId](int f) { return f == 1 ? pickId(*recp) : -1; };
    o.h.escDown = [](int) { return false; };
    InstallRealMainMenuHooks(&o.h);
    o.ret = Menu_RunMainMenu(o.st, &o.rec, /*maxFrames=*/12);
    InstallRealMainMenuHooks(nullptr);   // restore inert defaults
}
} // namespace

TEST(MainMenuWireItest, NewGameRunsRealEnterChooseCity) {
    Outcome o;
    RunClicking(o, [](const MainMenuRunRecord& r) { return r.idNewGame; });
    CHECK(o.h.ranChooseCity == 1);   // the REAL Menu_EnterChooseCity executed
    CHECK(o.rec.buttonCount == 8);
}

TEST(MainMenuWireItest, OptionsButtonsRunRealOptionScreens) {
    { Outcome o; RunClicking(o, [](const MainMenuRunRecord& r){ return r.idGfxOptions; }); CHECK(o.h.ranOptGfx == 1); }
    { Outcome o; RunClicking(o, [](const MainMenuRunRecord& r){ return r.idSfxOptions; }); CHECK(o.h.ranOptSfx == 1); }
    { Outcome o; RunClicking(o, [](const MainMenuRunRecord& r){ return r.idGameOptions; }); CHECK(o.h.ranOptGame == 1); }
}

TEST(MainMenuWireItest, LoadAndCreditsRunRealScreens) {
    { Outcome o; RunClicking(o, [](const MainMenuRunRecord& r){ return r.idLoad; });    CHECK(o.h.ranLoadGame == 1); }
    { Outcome o; RunClicking(o, [](const MainMenuRunRecord& r){ return r.idCredits; }); CHECK(o.h.ranCreditsScroll == 1); }
    { Outcome o; RunClicking(o, [](const MainMenuRunRecord& r){ return r.idMultiplayer; }); CHECK(o.h.ranNetwork == 1); }
}

TEST(MainMenuWireItest, QuitArmsQuitFlags) {
    Outcome o;
    RunClicking(o, [](const MainMenuRunRecord& r) { return r.idQuit; });
    // Quit dispatch: dword_63CC30 = dword_63CC48 = dword_631614 = 1.
    CHECK(o.st.quit == 1);
    CHECK(o.st.outroShown == 1);
    CHECK(o.st.close == 1);
}

TEST(MainMenuWireItest, MissionLayoutRunsRealMissionAndMapLoad) {
    // dword_63C7CC layout: the trio (Mission / NetworkCity / CreditsWindow) replaces gfx/sfx.
    { Outcome o; RunClicking(o, [](const MainMenuRunRecord& r){ return r.idMission; }, /*missionMode=*/1);
      CHECK(o.rec.missionTrio); CHECK(o.h.ranMission == 1); }
    { Outcome o; RunClicking(o, [](const MainMenuRunRecord& r){ return r.idNetworkCity; }, 1);
      CHECK(o.h.ranFileSelector == 1); }   // network-city -> file selector (+ map load on a pick)
    { Outcome o; RunClicking(o, [](const MainMenuRunRecord& r){ return r.idCreditsWindow; }, 1);
      CHECK(o.h.ranCreditsWin == 1); }
}
