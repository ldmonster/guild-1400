#pragma once
// guild::gui — main / options menus: button-row layout and option-select dispatch.
//
// gilde.exe 0x56dccc — VIBE_Menu_RunOptionsMain builds the in-game options menu: a
// vertical column of seven sprite buttons at a fixed x with a fixed y table, wires
// them into a radio group, and in its frame loop dispatches the hovered button
// (dword_62D22C) to the matching sub-screen (load / save / quit / game / gfx / sfx /
// resume).  This module recovers the LAYOUT (the button y-table + x) and the
// SELECT->action mapping byte-for-byte; the sub-screen runners and the fade/render
// transitions are reused/forward-declared, and the mutation each option triggers is
// routed through the command hook.

#include "gui/types.h"

namespace guild::gui {

// ---------------------------------------------------------------------------
// Options-main button column layout — gilde.exe 0x56dccc.
// All seven buttons share x = 32; the y table is {10,56,102,148,194,240,332}.
// The first six are a 46px run; the seventh (Resume) is offset further down.
// Each button uses sprite gfx 174 and is created on the current window.
// ---------------------------------------------------------------------------
inline constexpr int kOptionsButtonX     = 32;
inline constexpr int kOptionsButtonCount = 7;
inline constexpr int kOptionsButtonSprite = 174;
extern const int kOptionsButtonY[kOptionsButtonCount];

// The seven options in build order (the order they are created + added to the radio
// group; index = radio slot).
enum class OptionsItem {
    kLoad   = 0, // v31  -> VIBE_Menu_RunLoadGame
    kSave   = 1, // v8   -> RunSaveGame / RunSaveNetworkGame
    kGame   = 2, // v36  -> VIBE_Menu_RunOptionsGame
    kGfx    = 3, // v37  -> VIBE_Menu_RunOptionsGfx
    kSfx    = 4, // v38  -> VIBE_Menu_RunOptionsSfx
    kQuit   = 5, // v35  -> confirm-quit messagebox 6321
    kResume = 6, // v34  -> set dword_631614=1 (close menu, resume)
};

// gilde.exe 0x56dccc (the frame-loop dispatch chain).  Given the index of the hovered
// button (its build order / radio slot, 0..6), return the OptionsItem it selects.
// Returns kResume for an out-of-range index (the original's final else does nothing,
// i.e. resume).
OptionsItem Menu_OptionsSelect(int buttonIndex);

// Network mode (word_63C740 & 4) changes which save runner is used (RunSaveNetworkGame
// vs RunSaveGame) and disables Load/Save in some states.  We expose the predicate so
// the dispatch behaviour can be checked.
inline constexpr int kFlagNetwork = 0x4;   // word_63C740 & 4  -> network game
inline constexpr int kFlagMission = 0x80;  // word_63C740 & 0x80 -> mission (no load/save)

// gilde.exe 0x56dccc — whether the Load/Save buttons are enabled given the session
// flags.  Network (0x4): Load is disabled unconditionally, Save is disabled unless the
// host bit 0x10 is set.  Mission (0x80): both Load and Save are disabled.
bool Menu_OptionEnabled(OptionsItem item, int sessionFlags);

// ---------------------------------------------------------------------------
// Command hook (mockable).  Each option runs a sub-screen / mutation through the sim
// + render clusters; route them through a sink so the select dispatch is testable.
// ---------------------------------------------------------------------------
struct MenuCommandSink {
    virtual ~MenuCommandSink() = default;
    virtual void RunLoad() {}
    virtual void RunSave(bool /*network*/) {}
    virtual void RunGameOptions() {}
    virtual void RunGfxOptions() {}
    virtual void RunSfxOptions() {}
    virtual void RunQuitConfirm() {}
    virtual void Resume() {}
};
void Menu_SetCommandSink(MenuCommandSink* sink);

// gilde.exe 0x56dccc (one iteration of the option-click dispatch).  Resolve the
// selected option for `buttonIndex` and fire it on the command sink, honoring the
// session flags (e.g. network -> RunSave(true)).  Returns the resolved item.
OptionsItem Menu_DispatchOption(int buttonIndex, int sessionFlags);

} // namespace guild::gui
