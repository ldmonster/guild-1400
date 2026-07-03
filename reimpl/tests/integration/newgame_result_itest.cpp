// Integration: the native new-game chain (RunNativeMainMenu -> EnterChooseCity:
// city -> difficulty -> history -> player wizard -> char-create) fills EVERY
// field of NativeMenuResult.params — the full gui::NewGameParams block the
// original accumulates in the 0x122F4A0.. globals and the session start
// (play::ApplyNewGameParams / VIBE_Command_EnqueueInheritanceTransfer @0x5336f0)
// consumes. Asset-FREE: gameDir == "" -> the 3D city screen falls back to the
// 2D list, the radio panels use the English fallback rows, the wizard/charcreate
// draw labelled cells — the CHAIN PLUMBING under test is identical.
//
// Scripting model (see playable_flow_e2e_test.cpp): every play-layer screen
// pumps IPlatform::pumpMessages() exactly once per frame, so one pump-indexed
// timeline deterministically steers the whole multi-screen flow.
#include "test.h"

#include "play/native_main_menu.h"
#include "play/sdl_charintro_screen.h"   // CharIntroComputeLayout (radio rows)
#include "gui/main_menu.h"               // MainMenu_ButtonY / MainMenuItem
#include "gui/newgame_setup.h"           // Profession_ButtonX/Y
#include "shim_impl/memory_graphics.h"
#include "shim_impl/mem_filesystem.h"
#include "shim_impl/scripted_platform.h"

#include <string>
#include <utility>
#include <vector>

using namespace guild;

namespace {

constexpr int kVkReturn = 0x0D, kVkBackspace = 0x08;
constexpr int kW = 800, kH = 600;   // design resolution == screen layout coords

std::vector<std::pair<std::string, std::string>> Cities() {
    return { {"AUGSBURG", "Resources/gamedata/Cities/AUGSBURG.cty"},
             {"BERLIN",   "Resources/gamedata/Cities/BERLIN.cty"} };
}

void MenuButtonCentre(gui::MainMenuItem item, int& cx, int& cy) {
    const play::MenuButtonRect r =
        play::MenuButtonScreenRect(gui::MainMenu_ButtonY((int)item), kW, kH);
    cx = r.x + r.w / 2; cy = r.y + r.h / 2;
}

void RadioRowCentre(int rowCount, int i, int& cx, int& cy) {
    const play::CharIntroLayout L = play::CharIntroComputeLayout(kW, kH, rowCount);
    int rx, ry, rw, rh; L.RowRect(i, rx, ry, rw, rh);
    cx = rx + rw / 2; cy = ry + rh / 2;
}

} // namespace

TEST(NewGameResultItest, ChainFillsEveryNewGameParamsField) {
    shim::MemoryGraphicsDevice dev; CHECK(dev.init(kW, kH, 32, false));
    shim::MemFileSystem fs;
    shim::ScriptedPlatform plat;
    plat.quitAfterPumps(600);            // watchdog: desync ends in window close

    int ngX, ngY; MenuButtonCentre(gui::MainMenuItem::kNewGame, ngX, ngY);
    // 2D city list fallback (sdl_city_screen.cpp layout): row 0 centre.
    const int cityX = 236 + 328 / 2;     // kRowX + kRowW/2
    const int cityY = 126 + 40 / 2;      // kRowY0 + kRowH/2
    // Difficulty fallback panel: 5 levels + back = 6 rows; row 2 = "normal".
    int diffX, diffY; RadioRowCentre(6, 2, diffX, diffY);
    // History panel: buttons centred at x=W/2, row i top y=324+40*i (height 33). Pick
    // row 1 = PERSONAL (flag 2): the FACTUAL row (0) branches into the tasks screen;
    // personal/none go straight to the wizard, keeping this script linear.
    const int histX = kW / 2;
    const int histY = (324 + 40) * kH / 600 + (33 * kH / 600) / 2;
    // Player wizard private layout (sdl_chooseplayer_screen.cpp, 800x600):
    // panel (128,72,441,490); radio rows y0=py+120, rowH=46, rh=38.
    const int rowX = kW / 2;                          // gender/faith rows centre on screen
    const int row0Y = 72 + 170 + 38 / 2;              // rowsY=py+170, row 0 centre
    // Wappen grid: gx=px+30=185, gy=202, cw=(490-60)/4=107 (cell w = cw-8), ch=70 -> cell 0.
    const int wapX = 185 + (107 - 8) / 2, wapY = 202 + 70 / 2;
    // Charcreate profession cell 0 (real geometry; cell 84x68) + confirm.
    const int profX = gui::Profession_ButtonX(0) + 84 / 2;
    const int profY = gui::Profession_ButtonY(0) + 68 / 2;
    const int cfmX = 520 + 220 / 2, cfmY = 520 + 44 / 2;

    // ---- the timeline ----
    // Menu: hover New Game, click on pump 3 -> EnterChooseCity.
    plat.scriptAt(0, ngX, ngY, false);
    plat.scriptAt(3, ngX, ngY, true);
    // 2D city list (first pump 4): click row 0 (AUGSBURG), then Enter confirms.
    plat.scriptAt(4, cityX, cityY, false);
    plat.scriptAt(5, cityX, cityY, true);
    plat.scriptAt(6, cityX, cityY, false);
    plat.scriptAt(7, 10, 10, false, {kVkReturn});
    // Difficulty (first pump 8): release Enter, hover row 2, click -> variant 2.
    plat.scriptAt(8, 10, 10, false);
    plat.scriptAt(9, diffX, diffY, false);
    plat.scriptAt(10, diffX, diffY, true);
    // History (first pump 11): release, hover row 1 (personal), click -> flag 2.
    plat.scriptAt(11, 10, 10, false);
    plat.scriptAt(12, histX, histY, false);
    plat.scriptAt(13, histX, histY, true);
    // Player wizard (first pump 14). Page 0 (first name) is seeded "Spieler":
    // 7 Backspace EDGES clear it, then type "Test", Enter advances.
    plat.scriptAt(14, 10, 10, false);
    for (int k = 0; k < 7; ++k) {
        plat.scriptAt(15 + 2 * k, 10, 10, false, {kVkBackspace});
        plat.scriptAt(16 + 2 * k, 10, 10, false);
    }
    plat.scriptAt(29, 10, 10, false, {}, "Test");
    plat.scriptAt(30, 10, 10, false, {kVkReturn});
    plat.scriptAt(31, 10, 10, false);
    // Page 1 (family name, empty): type "Player", Enter advances.
    plat.scriptAt(32, 10, 10, false, {}, "Player");
    plat.scriptAt(33, 10, 10, false, {kVkReturn});
    plat.scriptAt(34, 10, 10, false);
    // Page 2 (gender): click row 0 (male).
    plat.scriptAt(35, rowX, row0Y, false);
    plat.scriptAt(36, rowX, row0Y, true);
    // Page 3 (faith): click row 0 (catholic).
    plat.scriptAt(37, rowX, row0Y, false);
    plat.scriptAt(38, rowX, row0Y, true);
    // Page 4 (wappen): click cell 0 -> page 5 auto-commits next frame.
    plat.scriptAt(39, wapX, wapY, false);
    plat.scriptAt(40, wapX, wapY, true);
    plat.scriptAt(41, 10, 10, false);
    // Charcreate (first pump ~42): profession cell 0, then CONFIRM.
    plat.scriptAt(42, profX, profY, false);
    plat.scriptAt(43, profX, profY, true);
    plat.scriptAt(44, cfmX, cfmY, false);
    plat.scriptAt(45, cfmX, cfmY, true);
    plat.scriptAt(46, 10, 10, false);

    play::NativeMenuResult r = play::RunNativeMainMenu(
        dev, plat, fs, /*gameDir=*/"", Cities(),
        kW, kH, /*frameCapMs=*/0, /*maxFrames=*/-1);

    std::printf("[newgame-itest] action=%d city='%s' pumps=%d started=%d "
                "name='%s %s' diff=%d hist=%d prof=%d/%d wappen=%d\n",
                (int)r.action, r.cityPath.c_str(), plat.pumps(),
                (int)r.params.started, r.params.firstName.c_str(),
                r.params.familyName.c_str(), r.params.difficulty,
                r.params.historyFlag, r.params.profession,
                r.params.professionVariant, r.params.wappen);

    CHECK(r.action == play::NativeMenuResult::kPlayCity);
    CHECK(!r.quitByWindow);                       // no watchdog close
    CHECK_EQ(r.cityPath, "Resources/gamedata/Cities/AUGSBURG.cty");

    // ---- EVERY NewGameParams field carried through the chain ----
    const gui::NewGameParams& p = r.params;
    CHECK_EQ(p.cityName, "stadt_AUGSBURG");       // the "stadt_" map id
    CHECK_EQ(p.cityFile, "AUGSBURG");             // ReturnedString base name
    CHECK(!p.network);
    CHECK_EQ(p.difficulty, 2);                    // byte_12335BA pick ("normal")
    CHECK_EQ(p.historyFlag, 2);                   // personal history -> flag 2 (skips tasks)
    CHECK_EQ(p.taskMode, -1);                      // tasks screen only for factual (flag 1)
    CHECK_EQ(p.firstName, "Test");                // String @0x122F4AA
    CHECK_EQ(p.familyName, "Player");             // byte_122F4CA
    CHECK_EQ(p.gender, 0);                        // byte_122F4A8
    CHECK_EQ(p.faith, 0);                         // byte_122F4A9
    CHECK_EQ(p.wappen, 0);                        // dword_122F4A4 - 1342
    CHECK_EQ(p.profession, 1);                    // beruf byte (grid cell 0)
    CHECK_EQ(p.professionVariant, 1);             // SHIBYTE(dword_122F4A0)
    CHECK(p.started);                             // the session-start arm
    CHECK((p.sessionFlags & gui::kSessionNewGame) != 0);
    CHECK((p.sessionFlags & gui::kSessionHistory) != 0);
}

// A cancelled chain (window close mid-flow) must NOT arm params.started.
TEST(NewGameResultItest, CancelledChainLeavesParamsUnarmed) {
    shim::MemoryGraphicsDevice dev; CHECK(dev.init(kW, kH, 32, false));
    shim::MemFileSystem fs;
    shim::ScriptedPlatform plat;
    int ngX, ngY; MenuButtonCentre(gui::MainMenuItem::kNewGame, ngX, ngY);
    plat.scriptAt(0, ngX, ngY, false);
    plat.scriptAt(3, ngX, ngY, true);
    plat.quitAfterPumps(8);                        // window closes in the city list
    play::NativeMenuResult r = play::RunNativeMainMenu(
        dev, plat, fs, "", Cities(), kW, kH, 0, -1);
    CHECK(r.action == play::NativeMenuResult::kQuit);
    CHECK(!r.params.started);
    CHECK_EQ(r.params.profession, -1);
}
