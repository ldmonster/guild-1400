// Unit tests for the boot->play main-menu + new-game-setup UI flow.
//
//   main_menu      : the startup button column (8 sprite buttons, fixed x/y/sprite) and
//                    each button -> its transition (New Game / Load / Multiplayer /
//                    Game / Gfx / Sfx / Credits / Quit), with the session-flag + close
//                    mutations the original applies.
//   newgame_setup  : the city/profession/wappen grid layout math, the per-screen
//                    selection rules, and the parameter collection into NewGameParams.
#include "gui/main_menu.h"
#include "gui/newgame_setup.h"
#include "tests/framework/test.h"

#include <string>

using namespace guild::gui;

// ===========================================================================
// main_menu — layout
// ===========================================================================
TEST(GuiMainMenu, ButtonColumnLayout) {
    // x and sprite are shared; the y table is the load-bearing layout constant.
    CHECK_EQ(kMainMenuButtonX, 32);
    CHECK_EQ(kMainMenuButtonSprite, 174);
    CHECK_EQ(kMainMenuTextColor, 300);
    CHECK_EQ(kMainMenuButtonCount, 8);

    const int expectedY[8] = {10, 53, 96, 139, 182, 225, 268, 311};
    for (int i = 0; i < kMainMenuButtonCount; ++i) {
        CHECK_EQ(MainMenu_ButtonY(i), expectedY[i]);
        CHECK_EQ(kMainMenuButtons[i].y, expectedY[i]);
    }
    CHECK_EQ(MainMenu_ButtonY(-1), -1);
    CHECK_EQ(MainMenu_ButtonY(8), -1);
}

TEST(GuiMainMenu, ButtonItemMapping) {
    CHECK(MainMenu_Select(0) == MainMenuItem::kNewGame);
    CHECK(MainMenu_Select(1) == MainMenuItem::kLoad);
    CHECK(MainMenu_Select(2) == MainMenuItem::kMultiplayer);
    CHECK(MainMenu_Select(3) == MainMenuItem::kGameOptions);
    CHECK(MainMenu_Select(4) == MainMenuItem::kGfxOptions);
    CHECK(MainMenu_Select(5) == MainMenuItem::kSfxOptions);
    CHECK(MainMenu_Select(6) == MainMenuItem::kCredits);
    CHECK(MainMenu_Select(7) == MainMenuItem::kQuit);
}

// ===========================================================================
// main_menu — dispatch / transitions
// ===========================================================================
namespace {
struct MenuSink : MainMenuCommandSink {
    int newGame = 0, load = 0, net = 0, game = 0, gfx = 0, sfx = 0, credits = 0, quit = 0;
    bool newGameOk = false, loadOk = false, netOk = false;
    bool EnterChooseCity() override { ++newGame; return newGameOk; }
    bool RunLoadGame() override { ++load; return loadOk; }
    bool ChooseNetworkMode() override { ++net; return netOk; }
    void RunGameOptions() override { ++game; }
    void RunGfxOptions() override { ++gfx; }
    void RunSfxOptions() override { ++sfx; }
    void RunCreditsScroll() override { ++credits; }
    void Quit() override { ++quit; }
};
} // namespace

TEST(GuiMainMenu, NewGameTransition) {
    MenuSink s;
    MainMenu_SetCommandSink(&s);

    // City cancelled -> no close, no flags.
    s.newGameOk = false;
    MainMenuTransition t = MainMenu_Dispatch(0);
    CHECK(t.item == MainMenuItem::kNewGame);
    CHECK_EQ(s.newGame, 1);
    CHECK(!t.close);
    CHECK_EQ(t.sessionFlags, 0);

    // City confirmed -> close + word_63C740 |= 1.
    s.newGameOk = true;
    t = MainMenu_Dispatch(0);
    CHECK(t.close);
    CHECK_EQ(t.sessionFlags, kSessionNewGame);
    CHECK_EQ(t.sessionFlags & 0x1, 0x1);

    MainMenu_SetCommandSink(nullptr);
}

TEST(GuiMainMenu, LoadAndMultiplayerTransitions) {
    MenuSink s;
    MainMenu_SetCommandSink(&s);

    s.loadOk = true;
    MainMenuTransition t = MainMenu_Dispatch(1);
    CHECK(t.item == MainMenuItem::kLoad);
    CHECK_EQ(s.load, 1);
    CHECK(t.close);
    CHECK_EQ(t.sessionFlags, kSessionLoadSave);

    // Multiplayer cancelled -> word_63C740 = 0, no close.
    s.netOk = false;
    t = MainMenu_Dispatch(2);
    CHECK(t.item == MainMenuItem::kMultiplayer);
    CHECK(!t.close);
    CHECK_EQ(t.sessionFlags, 0);

    // Multiplayer confirmed -> close + network flag.
    s.netOk = true;
    t = MainMenu_Dispatch(2);
    CHECK(t.close);
    CHECK_EQ(t.sessionFlags, kSessionNetwork);

    MainMenu_SetCommandSink(nullptr);
}

TEST(GuiMainMenu, OptionsAndCreditsDoNotClose) {
    MenuSink s;
    MainMenu_SetCommandSink(&s);

    MainMenuTransition t = MainMenu_Dispatch(3);
    CHECK_EQ(s.game, 1);
    CHECK(!t.close);
    t = MainMenu_Dispatch(4);
    CHECK_EQ(s.gfx, 1);
    CHECK(!t.close);
    t = MainMenu_Dispatch(5);
    CHECK_EQ(s.sfx, 1);
    CHECK(!t.close);
    t = MainMenu_Dispatch(6);
    CHECK_EQ(s.credits, 1);
    CHECK(!t.close);

    MainMenu_SetCommandSink(nullptr);
}

TEST(GuiMainMenu, QuitTransition) {
    MenuSink s;
    MainMenu_SetCommandSink(&s);
    MainMenuTransition t = MainMenu_Dispatch(7);
    CHECK(t.item == MainMenuItem::kQuit);
    CHECK_EQ(s.quit, 1);
    CHECK(t.close);
    MainMenu_SetCommandSink(nullptr);
}

TEST(GuiMainMenu, UnknownIndexIsNoOp) {
    MenuSink s;
    MainMenu_SetCommandSink(&s);
    MainMenuTransition t = MainMenu_Dispatch(99);
    CHECK(!t.close);
    CHECK_EQ(t.sessionFlags, 0);
    CHECK_EQ(s.newGame, 0);
    CHECK_EQ(s.quit, 0);
    MainMenu_SetCommandSink(nullptr);
}

// ===========================================================================
// newgame_setup — form names + extensions
// ===========================================================================
TEST(GuiNewGame, FormNamesByteForByte) {
    CHECK(std::string(kFormChooseCityHeader) == "Menu\\CHOOSECITY_HEADER");
    CHECK(std::string(kFormChooseCity) == "Menu\\CHOOSECITY");
    CHECK(std::string(kFormChooseHistory) == "Menu\\CHOOSEHISTORY");
    CHECK(std::string(kFormChoosePlayer) == "Menu\\CHOOSEPLAYER");
    CHECK(std::string(kFormChooseProfession) == "Menu\\CHOOSEPROFESSION");
    CHECK(std::string(kCityDir) == "gamedata/cities");
    CHECK(std::string(kCityExtLocal) == ".CTY");
    CHECK(std::string(kCityExtNet) == ".NET");
    CHECK(std::string(kCityNamePrefix) == "stadt_");
}

// ===========================================================================
// newgame_setup — profession grid
// ===========================================================================
TEST(GuiNewGame, ProfessionGridLayout) {
    // x = 96*(i%3)+100, y = 80*(i/3)+100.
    const int expX[8] = {100, 196, 292, 100, 196, 292, 100, 196};
    const int expY[8] = {100, 100, 100, 180, 180, 180, 260, 260};
    for (int i = 0; i < kProfessionCount; ++i) {
        CHECK_EQ(Profession_ButtonX(i), expX[i]);
        CHECK_EQ(Profession_ButtonY(i), expY[i]);
    }
    // gfx = berufByte + 1349.
    CHECK_EQ(Profession_ButtonGfx(0), 1349);
    CHECK_EQ(Profession_ButtonGfx(5), 1354);

    // widget-id -> profession byte (no table check).
    CHECK_EQ(ChooseProfession_ByteForWidgetId(1349, nullptr), 0);
    CHECK_EQ(ChooseProfession_ByteForWidgetId(1356, nullptr), 7);
    CHECK_EQ(ChooseProfession_ByteForWidgetId(1348, nullptr), -1);

    // widget-id -> byte with a beruf table.
    const int beruf[8] = {3, 5, 7, 9, 11, 13, 15, 17};
    CHECK_EQ(ChooseProfession_ByteForWidgetId(1349 + 5, beruf), 5);
    CHECK_EQ(ChooseProfession_ByteForWidgetId(1349 + 4, beruf), -1); // 4 not in table
}

// ===========================================================================
// newgame_setup — wappen grid (window-width-driven column count)
// ===========================================================================
TEST(GuiNewGame, WappenGridLayout) {
    // columns = (winW - 32) / 48.  For winW = 416 -> (384)/48 = 8 columns (one row).
    CHECK_EQ(Wappen_ColumnCount(416), 8);
    // For winW = 224 -> (192)/48 = 4 columns (two rows of 4).
    int cols = Wappen_ColumnCount(224);
    CHECK_EQ(cols, 4);
    // x = 48*(i%cols)+80, y = 48*(i/cols)+32.
    CHECK_EQ(Wappen_ButtonX(0, cols), 80);
    CHECK_EQ(Wappen_ButtonY(0, cols), 32);
    CHECK_EQ(Wappen_ButtonX(4, cols), 80);   // wraps to next row
    CHECK_EQ(Wappen_ButtonY(4, cols), 80);
    CHECK_EQ(Wappen_ButtonX(3, cols), 80 + 3 * 48);
    // id/index round-trip (gfx/widget+8 id == 1342 + index).
    CHECK_EQ(Wappen_ButtonGfx(0), 1342);
    CHECK_EQ(Wappen_ButtonGfx(7), 1349);
    CHECK_EQ(Wappen_IdForIndex(5), 1347);
    CHECK_EQ(Wappen_IndexForId(1347), 5);
}

// ===========================================================================
// newgame_setup — per-screen selection rules
// ===========================================================================
TEST(GuiNewGame, CitySelectionRules) {
    CHECK(std::string(ChooseCity_Extension(false)) == ".CTY");
    CHECK(std::string(ChooseCity_Extension(true)) == ".NET");

    CHECK(ChooseCity_IsConfirm(1210, 0));    // OK click
    CHECK(ChooseCity_IsConfirm(-1, 28));     // Enter key
    CHECK(!ChooseCity_IsConfirm(1155, 0));   // Cancel id, no enter
    CHECK(!ChooseCity_IsConfirm(-1, 0));

    CHECK(ChooseCity_IsCityObject("stadt_koeln"));
    CHECK(!ChooseCity_IsCityObject("npc_baker"));
    CHECK(!ChooseCity_IsCityObject("stad"));
}

TEST(GuiNewGame, HistoryButtonFlags) {
    CHECK_EQ(ChooseHistory_FlagForButton(0), 1);
    CHECK_EQ(ChooseHistory_FlagForButton(1), 2);
    CHECK_EQ(ChooseHistory_FlagForButton(2), 0);
    CHECK_EQ(ChooseHistory_FlagForButton(3), -1); // Cancel (id 1155)
    CHECK_EQ(ChooseHistory_FlagForButton(9), -1);
}

// ===========================================================================
// newgame_setup — parameter collection + commit
// ===========================================================================
namespace {
struct NgSink : NewGameSink {
    int historyFlag = -99;
    int variantArg = -1;
    int started = 0;
    NewGameParams captured;
    void SetHistoryFlag(int flag) override { historyFlag = flag; }
    int ComputeProfessionVariant(int b) override { variantArg = b; return b + 100; }
    void StartSession(const NewGameParams& p) override { ++started; captured = p; }
};
} // namespace

TEST(GuiNewGame, CollectAndCommit) {
    NgSink s;
    NewGame_SetSink(&s);

    NewGameParams p;
    CHECK(NewGame_ApplyCity(p, "stadt_koeln", "koeln", false));
    CHECK_EQ(p.cityName, std::string("stadt_koeln"));
    CHECK(!p.network);

    CHECK(NewGame_ApplyHistory(p, 1));   // -> flag 2
    CHECK_EQ(p.historyFlag, 2);
    CHECK_EQ(s.historyFlag, 2);

    NewGame_ApplyPlayer(p, "Hans", "Schmidt", /*wappen*/3, /*gender*/1, /*faith*/0);
    CHECK_EQ(p.firstName, std::string("Hans"));
    CHECK_EQ(p.familyName, std::string("Schmidt"));
    CHECK_EQ(p.wappen, 3);
    CHECK_EQ(p.gender, 1);
    CHECK_EQ(p.faith, 0);

    CHECK(NewGame_ApplyProfession(p, 5)); // beruf byte 5
    CHECK_EQ(p.profession, 5);
    CHECK_EQ(s.variantArg, 5);
    CHECK_EQ(p.professionVariant, 105);   // sink returned b + 100

    NewGame_Commit(p);
    CHECK(p.started);
    CHECK_EQ(s.started, 1);
    // New game + history flags, no network.
    CHECK_EQ(p.sessionFlags & kSessionNewGame, kSessionNewGame);
    CHECK_EQ(p.sessionFlags & kSessionHistory, kSessionHistory);
    CHECK_EQ(p.sessionFlags & kSessionNetwork, 0);
    CHECK_EQ(s.captured.profession, 5);

    NewGame_SetSink(nullptr);
}

TEST(GuiNewGame, HistoryCancelDoesNotSet) {
    NgSink s;
    NewGame_SetSink(&s);
    NewGameParams p;
    CHECK(!NewGame_ApplyHistory(p, 3)); // Cancel button
    CHECK_EQ(s.historyFlag, -99);       // sink not called
    CHECK_EQ(p.historyFlag, 0);
    NewGame_SetSink(nullptr);
}

TEST(GuiNewGame, NetworkCommitAddsNetworkFlag) {
    NgSink s;
    NewGame_SetSink(&s);
    NewGameParams p;
    NewGame_ApplyCity(p, "stadt_x", "x", /*network*/true);
    NewGame_Commit(p);
    CHECK_EQ(p.sessionFlags & kSessionNetwork, kSessionNetwork);
    CHECK_EQ(p.sessionFlags & kSessionNewGame, kSessionNewGame);
    NewGame_SetSink(nullptr);
}

// ===== Wave-11 hardening: out-of-range / "bad button count" indices ==================
// The main-menu dispatch is indexed by the hovered radio slot. A malformed menu (a stale
// or out-of-range hovered id) must not index the 8-entry button table out of bounds.

TEST(GuiMainMenuHarden, OutOfRangeButtonIndexIsSafe) {
    // Negative, == count, and far-past indices must all be handled without OOB reads.
    CHECK_EQ(MainMenu_ButtonY(-1), -1);
    CHECK_EQ(MainMenu_ButtonY(kMainMenuButtonCount), -1);
    CHECK_EQ(MainMenu_ButtonY(99999), -1);

    // Dispatch on an out-of-range index returns a no-op transition (no close, no flags).
    MainMenuTransition t = MainMenu_Dispatch(kMainMenuButtonCount);
    CHECK(!t.close);
    CHECK_EQ(t.sessionFlags, 0);
    MainMenuTransition tn = MainMenu_Dispatch(-7);
    CHECK(!tn.close);
    CHECK_EQ(tn.sessionFlags, 0);
}
