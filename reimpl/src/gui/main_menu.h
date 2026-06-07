#pragma once
// guild::gui — the TOP-LEVEL (boot->play) main menu shown at startup.
//
// gilde.exe 0x529d08 — VIBE_Menu_RunMainMenu builds the startup menu (form
// "MENU\MAIN_MENU"): a vertical column of eight sprite buttons (sprite gfx 174) at a
// fixed x = 32 with a fixed y table, wires them into an 8-button radio group, and runs
// a frame loop that dispatches the hovered button (dword_62D22C) to the matching screen
// transition (New Game / Load / Multiplayer / Game-Options / Gfx-Options / Sfx-Options /
// Credits / Quit).  This module recovers the LAYOUT (the button y-table + x + sprite)
// and the button->transition mapping byte-for-byte; the 3D-scene setup, the fade/render,
// the radio-group runner and the sub-screen runners are reused/forward-declared, and the
// mutation each button triggers (session flags word_63C740, the close flag dword_631614)
// is routed through a command hook so the dispatch is testable in isolation.
//
// ODR: this builds on the already-translated retained-mode core (gui/window, gui/object,
// gui/widget_create, gui/radiogroup) and the options menu (gui/menu) — REUSED, not
// redefined.  The options-main 7-button column lives in gui/menu.h; this is the distinct
// top-level startup column.

#include "gui/types.h"

namespace guild::gui {

// ---------------------------------------------------------------------------
// Main-menu button column layout — gilde.exe 0x529d08.
// All buttons share x = 32 and sprite gfx 174; each is created via
// VIBE_Widget_AddSpriteToWindow(32, Y, 174, labelId, win), flagged as a label
// (widget +88 = 1), text colour 300.  The y table is the build-order y argument of the
// eight AddSpriteToWindow calls:
//   New Game=10, Load=53, Multiplayer=96, GameOptions=139, GfxOptions=182,
//   SfxOptions=225, Credits=268, Quit=311.
// ---------------------------------------------------------------------------
inline constexpr int kMainMenuButtonX      = 32;
inline constexpr int kMainMenuButtonSprite = 174;  // gfx id passed to AddSpriteToWindow
inline constexpr int kMainMenuTextColor    = 300;  // VIBE_Widget_SetTextColor arg
inline constexpr int kMainMenuButtonCount  = 8;

// The eight top-level actions in build order (= radio slot index 0..7).
enum class MainMenuItem {
    kNewGame     = 0, // y=10   v79 -> VIBE_Menu_EnterChooseCity (new-game setup flow)
    kLoad        = 1, // y=53   v84 -> VIBE_Menu_RunLoadGame
    kMultiplayer = 2, // y=96   v81 -> VIBE_Menu_ChooseNetworkMode
    kGameOptions = 3, // y=139  v72 -> VIBE_Menu_RunOptionsGame
    kGfxOptions  = 4, // y=182  v85 -> VIBE_Menu_RunOptionsGfx
    kSfxOptions  = 5, // y=225  v86 -> VIBE_Menu_RunOptionsSfx
    kCredits     = 6, // y=268  v82 -> VIBE_Menu_RunCreditsScroll
    kQuit        = 7, // y=311  v83 -> set dword_63CC30/dword_63CC48 + dword_631614 (quit)
};

// Per-button build descriptor (one row of the build loop).
struct MainMenuButton {
    int          y;        // AddSpriteToWindow y argument
    MainMenuItem item;     // the action this button selects
    const char*  label;    // diagnostic name (the German MENU\MAIN_MENU caption role)
};

// gilde.exe 0x529d08 — the eight AddSpriteToWindow rows in build order.
extern const MainMenuButton kMainMenuButtons[kMainMenuButtonCount];

// gilde.exe 0x529d08 — the y argument of the i-th AddSpriteToWindow call (build order).
int MainMenu_ButtonY(int index);

// gilde.exe 0x529d08 (the frame-loop dispatch chain `dword_62D22C == vNN`).
// Given the hovered button index (its build order / radio slot, 0..7), return the
// MainMenuItem it selects.  Returns kQuit for an out-of-range index? No — the original's
// chain falls through (no-op) on an unknown id; we map out-of-range to kNewGame's inverse
// by returning a sentinel via the bool overload below.  Use MainMenu_Select for the
// in-range mapping.
MainMenuItem MainMenu_Select(int index);

// ---------------------------------------------------------------------------
// Session flags (word_63C740) the New Game / Load transitions set.  These mirror the
// guild::app::session::* bit values exactly (kept local so gui has no app dependency).
// ---------------------------------------------------------------------------
inline constexpr int kSessionNewGame  = 0x0001; // word_63C740 | 1   (New Game)
inline constexpr int kSessionLoadSave = 0x0002; // word_63C740 & 2   (Load savegame)
inline constexpr int kSessionNetwork  = 0x0004; // word_63C740 & 4
inline constexpr int kSessionHistory  = 0x0008; // word_63C740 | 8   (history chosen)

// ---------------------------------------------------------------------------
// Command hook (mockable).  Each button runs a sub-screen / mutation through the wider
// GUI/sim/render clusters; route them through a sink so the dispatch is testable.  The
// return value of the new-game / load entry points decides whether the menu closes
// (dword_631614 = 1) and which session flags are armed.
// ---------------------------------------------------------------------------
struct MainMenuCommandSink {
    virtual ~MainMenuCommandSink() = default;
    // VIBE_Menu_EnterChooseCity @0x52ee38 -> RunChooseCity ...; returns true to start a
    // new game (the menu then sets word_63C740 |= 1 and closes).
    virtual bool EnterChooseCity() { return false; }
    // VIBE_Menu_RunLoadGame @0x56a270; returns true when a save was picked (close).
    virtual bool RunLoadGame() { return false; }
    // VIBE_Menu_ChooseNetworkMode; returns true to enter a network session (close).
    virtual bool ChooseNetworkMode() { return false; }
    virtual void RunGameOptions() {}
    virtual void RunGfxOptions() {}
    virtual void RunSfxOptions() {}
    virtual void RunCreditsScroll() {}
    virtual void Quit() {} // sets dword_63CC30=dword_63CC48=1, dword_631614=1
};
void MainMenu_SetCommandSink(MainMenuCommandSink* sink);

// The result of one dispatch: the resolved item, whether the menu should close
// (dword_631614 set) and the session flags now armed in word_63C740.
struct MainMenuTransition {
    MainMenuItem item;
    bool         close;        // dword_631614 == 1 afterwards
    int          sessionFlags; // word_63C740 after the transition
};

// gilde.exe 0x529d08 (one iteration of the option-click dispatch).  Resolve the hovered
// button, fire it on the sink, apply the session-flag / close mutations exactly as the
// original (New Game -> |1 on success; Multiplayer -> |network or clear on cancel; Load
// -> close on success; Quit -> close), and return the resulting transition.  `index` is
// the hovered button's radio slot.
MainMenuTransition MainMenu_Dispatch(int index);

} // namespace guild::gui
