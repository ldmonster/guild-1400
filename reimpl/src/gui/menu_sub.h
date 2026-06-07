#pragma once
// guild::gui — options sub-tabs menu (the Game / Graphics / Sound tab strip).
//
// gilde.exe 0x56e30c — VIBE_Menu_RunOptionsSubTabs builds the three-tab options
// sub-panel: three sprite buttons in a vertical column at x=32, y={56,102,148}, sprite
// 174, each marked as a label (+88=1) with text colour 300, wired into a 3-button radio
// group; its frame loop dispatches the hovered tab (dword_62D22C) to the matching
// sub-screen runner (Game / Gfx / Sfx).  This module recovers the LAYOUT (button
// column) + the tab->runner dispatch; the sub-screen runners and the fade/render are
// routed through a command hook.  (The main 7-button options column lives in menu.h.)

#include "gui/types.h"

namespace guild::gui {

// ---------------------------------------------------------------------------
// Sub-tab button column — gilde.exe 0x56e30c.
// Three AddSpriteToWindow(32, Y, 174, win) calls: Y = 56, 102, 148.  All share the
// label flag (+88=1) and text colour 300, then RadioGroup_Create(3, first).
// ---------------------------------------------------------------------------
inline constexpr int kSubTabButtonX      = 32;
inline constexpr int kSubTabButtonCount  = 3;
inline constexpr int kSubTabButtonSprite = 174;
inline constexpr int kSubTabTextColor    = 300;
extern const int kSubTabButtonY[kSubTabButtonCount];

enum class SubTab {
    kGame = 0, // y=56  -> VIBE_Menu_RunOptionsGame
    kGfx  = 1, // y=102 -> VIBE_Menu_RunOptionsGfx
    kSfx  = 2, // y=148 -> VIBE_Menu_RunOptionsSfx
};

// gilde.exe 0x56e30c (the v4/v11/v7 == dword_62D22C dispatch chain).  Given the index
// of the hovered tab button (0..2, build order = radio slot), return the SubTab it
// selects.  Returns kGame for an out-of-range index (the original's chain falls through
// without acting; we map the first tab as the default for testability).
SubTab Menu_SubTabSelect(int buttonIndex);

// ---------------------------------------------------------------------------
// Command hook (mockable).  Each tab runs a sub-screen through the wider GUI/sim
// clusters; route them through a sink so the tab dispatch is testable in isolation.
// ---------------------------------------------------------------------------
struct SubTabCommandSink {
    virtual ~SubTabCommandSink() = default;
    virtual void RunGame() {}
    virtual void RunGfx() {}
    virtual void RunSfx() {}
};
void Menu_SetSubTabCommandSink(SubTabCommandSink* sink);

// gilde.exe 0x56e30c (one frame's tab-click dispatch).  Resolve the tab for
// `buttonIndex` and fire it on the sink (the original wraps each in
// SetObjectsVisible(form,0)/RunOptions*()/SetObjectsVisible(form,1)).  Returns the tab.
SubTab Menu_DispatchSubTab(int buttonIndex);

} // namespace guild::gui
