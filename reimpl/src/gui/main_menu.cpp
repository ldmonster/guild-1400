#include "gui/main_menu.h"

namespace guild::gui {

// gilde.exe 0x529d08 — the menu buttons, built with VIBE_Widget_AddSpriteToWindow(32, Y,
// 174, win).  This table is the y -> action MAPPING for the default (non-mission) layout
// dword_63C7CC == 0; it is NOT the literal build order.  In the binary the build order is
// y = 10,53,96,139 (NewGame/Load/Multiplayer/GameOptions), then 268,311 (Credits/Quit),
// then the dword_63C7CC branch appends either {139,182,225} (mission) or {182,225}
// (GfxOptions/SfxOptions); see main_menu_run.cpp for the faithful build-order model and the
// full dispatch chain.  Here the rows are sorted by y for the static caption table only.
// The caption argument is a runtime-resolved MENU\MAIN_MENU handle, summarised by its role.
const MainMenuButton kMainMenuButtons[kMainMenuButtonCount] = {
    {10,  MainMenuItem::kNewGame,     "New Game"},     // v74 / v79  dword_8C9870
    {53,  MainMenuItem::kLoad,        "Load Game"},    // v75 / v84  dword_8C9854
    {96,  MainMenuItem::kMultiplayer, "Multiplayer"},  // v17 / v81  dword_8C9874
    {139, MainMenuItem::kGameOptions, "Game Options"}, // v77 / v72  dword_8C985C
    {182, MainMenuItem::kGfxOptions,  "Gfx Options"},  // v64 / v85  dword_8C9860
    {225, MainMenuItem::kSfxOptions,  "Sfx Options"},  // v18 / v86  dword_8C9864
    {268, MainMenuItem::kCredits,     "Credits"},      // v68 / v82  dword_8C9878
    {311, MainMenuItem::kQuit,        "Quit"},         // v76 / v83  dword_8C987C
};

int MainMenu_ButtonY(int index) {
    if (index < 0 || index >= kMainMenuButtonCount) return -1;
    return kMainMenuButtons[index].y;
}

MainMenuItem MainMenu_Select(int index) {
    // The original compares dword_62D22C against the eight stored widget ids; out of range
    // matches none, so we clamp to a defined item (the loop simply continues for unknowns).
    if (index < 0 || index >= kMainMenuButtonCount) return MainMenuItem::kQuit;
    return kMainMenuButtons[index].item;
}

namespace {
MainMenuCommandSink g_defaultSink;
MainMenuCommandSink* g_sink = &g_defaultSink;
} // namespace

void MainMenu_SetCommandSink(MainMenuCommandSink* sink) {
    g_sink = sink ? sink : &g_defaultSink;
}

MainMenuTransition MainMenu_Dispatch(int index) {
    MainMenuTransition t{};
    t.item = MainMenu_Select(index);
    t.close = false;
    t.sessionFlags = 0;

    if (index < 0 || index >= kMainMenuButtonCount) {
        // Unknown id: the original frame loop takes no action and continues.
        return t;
    }

    switch (t.item) {
        case MainMenuItem::kNewGame:
            // 0x52a341: if (EnterChooseCity()) { word_63C740 |= 1; dword_631614 = 1; }
            if (g_sink->EnterChooseCity()) {
                t.sessionFlags = kSessionNewGame;
                t.close = true;
            }
            break;
        case MainMenuItem::kLoad:
            // 0x52a838: if (RunLoadGame()) dword_631614 = 1;  (word_63C740 = 10 set inside)
            if (g_sink->RunLoadGame()) {
                t.sessionFlags = kSessionLoadSave; // word_63C740 = 10 -> &2 load + &8 net-ish
                t.close = true;
            }
            break;
        case MainMenuItem::kMultiplayer:
            // 0x52a86e: if (ChooseNetworkMode()) dword_631614 = 1; else word_63C740 = 0.
            if (g_sink->ChooseNetworkMode()) {
                t.sessionFlags = kSessionNetwork;
                t.close = true;
            } else {
                t.sessionFlags = 0; // word_63C740 = 0
            }
            break;
        case MainMenuItem::kGameOptions: g_sink->RunGameOptions(); break;
        case MainMenuItem::kGfxOptions:  g_sink->RunGfxOptions();  break;
        case MainMenuItem::kSfxOptions:  g_sink->RunSfxOptions();  break;
        case MainMenuItem::kCredits:     g_sink->RunCreditsScroll(); break;
        case MainMenuItem::kQuit:
            // 0x52a8cc: dword_63CC30 = dword_63CC48 = 1; dword_631614 = 1.
            g_sink->Quit();
            t.close = true;
            break;
    }
    return t;
}

} // namespace guild::gui
