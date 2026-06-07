// End-to-end test for the boot->play UI path:
//   main menu (New Game) -> city select -> history -> player name/identity ->
//   profession -> session start.
//
// Drives the main-menu dispatch and the new-game-setup flow across both modules, then
// verifies the screen transitions, the collected session-params, and the emitted (mock)
// start command against a hand-built reference.
#include "gui/main_menu.h"
#include "gui/newgame_setup.h"
#include "tests/framework/test.h"

#include <string>
#include <vector>

using namespace guild::gui;

namespace {

// A driver that wires the main-menu "New Game" button to the full new-game setup flow,
// recording every screen it visits (for transition verification) and collecting the
// parameters, exactly as RunMainMenu -> EnterChooseCity -> RunChooseCity ->
// RunChooseHistory -> RunChoosePlayer -> ChooseProfession does in the binary.
struct FlowRecorder : MainMenuCommandSink, NewGameSink {
    // Screen visit log.
    std::vector<std::string> screens;
    NewGameParams params;
    int startCommands = 0;

    // Scripted inputs for the new-game flow.
    std::string cityName, cityFile;
    bool        network = false;
    int         historyButton = 0;
    std::string firstName, familyName;
    int         wappen = 0, gender = 0, faith = 0;
    int         berufByte = 0;

    // --- NewGameSink ---
    void SetHistoryFlag(int flag) override {
        screens.push_back("history:flag=" + std::to_string(flag));
    }
    int ComputeProfessionVariant(int b) override { return b * 10 + 1; }
    void StartSession(const NewGameParams& p) override {
        ++startCommands;
        params = p; // capture the final block the start command carries
    }

    // --- MainMenuCommandSink: New Game runs the whole setup flow ---
    bool EnterChooseCity() override {
        screens.push_back("menu:NewGame");
        NewGame_SetSink(this);

        // 1) City select (form Menu\CHOOSECITY).
        screens.push_back(std::string("city:") + ChooseCity_Extension(network));
        if (!ChooseCity_IsCityObject(cityName)) return false; // not a city marker
        if (!ChooseCity_IsConfirm(1210, 0)) return false;     // OK click
        NewGame_ApplyCity(params, cityName, cityFile, network);

        // 2) History (form Menu\CHOOSEHISTORY).
        screens.push_back("history");
        if (!NewGame_ApplyHistory(params, historyButton)) return false;

        // 3) Player identity (form Menu\CHOOSEPLAYER).
        screens.push_back("player");
        NewGame_ApplyPlayer(params, firstName, familyName, wappen, gender, faith);

        // 4) Profession (form Menu\CHOOSEPROFESSION).
        screens.push_back("profession");
        if (!NewGame_ApplyProfession(params, berufByte)) return false;

        // 5) Commit -> arm the start command.
        NewGame_Commit(params);
        return params.started;
    }
    // The other buttons are not used by this flow but must exist.
    bool RunLoadGame() override { screens.push_back("menu:Load"); return false; }
    bool ChooseNetworkMode() override { screens.push_back("menu:Multiplayer"); return false; }
    void RunGameOptions() override { screens.push_back("menu:GameOpts"); }
    void RunGfxOptions() override { screens.push_back("menu:GfxOpts"); }
    void RunSfxOptions() override { screens.push_back("menu:SfxOpts"); }
    void RunCreditsScroll() override { screens.push_back("menu:Credits"); }
    void Quit() override { screens.push_back("menu:Quit"); }
};

} // namespace

TEST(GuiMainMenuE2E, BootToPlayNewGamePath) {
    FlowRecorder rec;
    rec.cityName  = "stadt_augsburg";
    rec.cityFile  = "augsburg";
    rec.network   = false;
    rec.historyButton = 0;   // -> History flag 1
    rec.firstName = "Albrecht";
    rec.familyName = "Duerer";
    rec.wappen    = 2;
    rec.gender    = 0;
    rec.faith     = 1;
    rec.berufByte = 4;

    MainMenu_SetCommandSink(&rec);

    // Hover the first button (New Game) and click it.
    CHECK(MainMenu_Select(0) == MainMenuItem::kNewGame);
    MainMenuTransition t = MainMenu_Dispatch(0);

    // --- Transition verification: the screens visited, in order ---
    const std::vector<std::string> expectedScreens = {
        "menu:NewGame",
        "city:.CTY",
        "history",
        "history:flag=1",  // SetHistoryFlag(1) fires inside ApplyHistory
        "player",
        "profession",
    };
    CHECK_EQ(rec.screens.size(), expectedScreens.size());
    for (size_t i = 0; i < expectedScreens.size() && i < rec.screens.size(); ++i)
        CHECK(rec.screens[i] == expectedScreens[i]);

    // --- The main-menu transition closes the menu with the New Game flag ---
    CHECK(t.item == MainMenuItem::kNewGame);
    CHECK(t.close);
    CHECK_EQ(t.sessionFlags, kSessionNewGame);

    // --- The collected session-params vs a reference block ---
    NewGameParams ref;
    ref.cityName = "stadt_augsburg";
    ref.cityFile = "augsburg";
    ref.network = false;
    ref.historyFlag = 1;
    ref.firstName = "Albrecht";
    ref.familyName = "Duerer";
    ref.wappen = 2;
    ref.gender = 0;
    ref.faith = 1;
    ref.profession = 4;
    ref.professionVariant = 4 * 10 + 1; // ComputeProfessionVariant
    ref.started = true;
    ref.sessionFlags = kSessionNewGame | kSessionHistory;

    CHECK_EQ(rec.params.cityName, ref.cityName);
    CHECK_EQ(rec.params.cityFile, ref.cityFile);
    CHECK_EQ(rec.params.network, ref.network);
    CHECK_EQ(rec.params.historyFlag, ref.historyFlag);
    CHECK_EQ(rec.params.firstName, ref.firstName);
    CHECK_EQ(rec.params.familyName, ref.familyName);
    CHECK_EQ(rec.params.wappen, ref.wappen);
    CHECK_EQ(rec.params.gender, ref.gender);
    CHECK_EQ(rec.params.faith, ref.faith);
    CHECK_EQ(rec.params.profession, ref.profession);
    CHECK_EQ(rec.params.professionVariant, ref.professionVariant);
    CHECK(rec.params.started);
    CHECK_EQ(rec.params.sessionFlags, ref.sessionFlags);

    // --- Exactly one start command was emitted ---
    CHECK_EQ(rec.startCommands, 1);

    MainMenu_SetCommandSink(nullptr);
    NewGame_SetSink(nullptr);
}

TEST(GuiMainMenuE2E, NetworkNewGameArmsNetworkFlag) {
    FlowRecorder rec;
    rec.cityName = "stadt_hamburg";
    rec.cityFile = "hamburg";
    rec.network  = true;     // -> ".NET" extension, network session flag
    rec.historyButton = 2;   // -> History flag 0
    rec.firstName = "Klaus";
    rec.familyName = "Stoertebeker";
    rec.berufByte = 1;

    MainMenu_SetCommandSink(&rec);
    MainMenuTransition t = MainMenu_Dispatch(0);

    CHECK(t.close);
    CHECK(rec.params.network);
    CHECK_EQ(rec.params.historyFlag, 0);
    CHECK_EQ(rec.params.sessionFlags & kSessionNetwork, kSessionNetwork);
    CHECK_EQ(rec.params.sessionFlags & kSessionNewGame, kSessionNewGame);
    // The city screen used the network extension.
    bool sawNet = false;
    for (auto& s : rec.screens) if (s == "city:.NET") sawNet = true;
    CHECK(sawNet);
    CHECK_EQ(rec.startCommands, 1);

    MainMenu_SetCommandSink(nullptr);
    NewGame_SetSink(nullptr);
}

TEST(GuiMainMenuE2E, CancelledCityDoesNotStart) {
    FlowRecorder rec;
    rec.cityName = "npc_not_a_city"; // fails the "stadt_" prefix check
    MainMenu_SetCommandSink(&rec);
    MainMenuTransition t = MainMenu_Dispatch(0);
    CHECK(!t.close);
    CHECK_EQ(t.sessionFlags, 0);
    CHECK_EQ(rec.startCommands, 0);
    CHECK(!rec.params.started);
    MainMenu_SetCommandSink(nullptr);
}

TEST(GuiMainMenuE2E, LoadGameBranchIsDistinct) {
    FlowRecorder rec;
    MainMenu_SetCommandSink(&rec);
    // Load button: distinct from New Game; here the mock returns false (no save picked).
    MainMenuTransition t = MainMenu_Dispatch(1);
    CHECK(t.item == MainMenuItem::kLoad);
    CHECK(!t.close);
    CHECK_EQ(rec.startCommands, 0);
    // No new-game screens were visited.
    CHECK_EQ(rec.screens.size(), 1u);
    CHECK(rec.screens[0] == "menu:Load");
    MainMenu_SetCommandSink(nullptr);
}
