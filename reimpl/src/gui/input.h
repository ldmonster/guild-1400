#pragma once
// guild::gui — widget input dispatch: hit-testing, click routing, scrollbar drag.
//
// VIBE_Widget_DispatchMouseClick @0x421594 is the master per-frame routine. It:
//   1. advances the frame tick, runs the drag handler and the hover update;
//   2. resolves which child slot of the last-clicked window the hovered widget is
//      (dword_75BF10);
//   3. on a mouse click over a clickable widget (btnFlagA/B set) records the clicked
//      widget id in dword_75BF38, toggles a togglable button's value, plays a click
//      sound, and runs the radio-group mutual-exclusion update;
//   4. scans the hotspot table for a hit (dword_62D31C / dword_62D318);
//   5. if the last-clicked window has the scrollbar flag (0x20) and the mouse is in the
//      bottom scrollbar band, drags the thumb and recomputes the scroll offset.
//
// The original is wired to render-side mouse state (16.16 packed mouse coords in the
// 0x6722xx / 0x75BF46 globals, VIBE_Coord_* transforms), the audio mixer, and the hover
// state machine. Those edges are forward-declared/stubbed; the model translated here is
// the hit-test, the click-routing id bookkeeping (including the 1024 window-backing
// offset and the 1210/1155 OK/Cancel ids), and the scrollbar-drag value math.

#include "gui/types.h"

namespace guild::gui {

// Special widget ids the dispatcher recognises (recovered from the messagebox loop and
// the click router).  A window-backing widget's id is its window slot + 1024.
inline constexpr int kIdOk          = 1210; // dword_75BF38 == 1210 -> OK pressed
inline constexpr int kIdCancel      = 1155; // dword_75BF38 == 1155 -> Cancel/right
inline constexpr int kWindowIdBase  = 1024; // window-backing id = winSlot + 1024

// ---- Mouse / click state globals (the 0x6722xx / 0x75BFxx selectors) --------------
extern int g_lastClickedId;     // dword_75BF38 (id of the last-clicked widget; -1 none)
extern int g_lastClickedPrevId; // dword_75BEEC (sticky copy of the above)
extern int g_lastClickedWindow; // dword_75BF08 (window slot of the last click)
extern int g_lastClickedSlot;   // dword_75BF00 / dword_75BF10 (child slot within window)
extern int g_hoverObject;       // dword_62D22C (widget index currently hovered; -1 none)
extern int g_hoverWindow;       // dword_62D290 (window slot currently hovered; -1 none)
extern int g_mouseClick;        // dword_672228 (left-button click edge this frame)
extern int g_mouseDown;         // dword_672220 (left button held)

void ResetInputState();

// Window-backing widget id for a window slot.
inline int WindowBackingId(int winSlot) { return winSlot + kWindowIdBase; }

// gilde.exe 0x421594 (hit-test core) — find the child slot of `winSlot`'s object list
// whose widget index equals `hoverObject`; returns that local slot index, or -1.
// (This is the dword_75BF10 resolution loop.)
int ResolveClickedSlot(int winSlot, int hoverObject);

// gilde.exe 0x421594 (click-routing core) — given a mouse click over `hoverObject` in
// window `winSlot`, route it: when the widget is clickable (btnFlagA/B) record its id as
// the last-clicked id, toggle a togglable button (+36/+40 ^= 1), and run the radio-group
// exclusive select for whichever group contains it. Returns the routed widget id, or -1
// when the widget is not clickable. Updates g_lastClicked* state.
int RouteClick(int winSlot, int hoverObject);

// gilde.exe 0x421594 (scrollbar-drag math) — recompute a window's scroll thumb from a
// mouse position inside the bottom scrollbar band. `thumbMax` is the window thumb range
// (w[145]/+580 in the original), winX/winW the bar geometry in pixels, mouseX the cursor
// x. Returns the new scroll-current value (w[146]); when the point is outside the active
// band [winX+10 .. winX+winW-10] it returns the supplied `current` unchanged.
//   value = thumbMax * (mouseX - winX - 12) / (winW - 24) + 4
int ScrollbarDragValue(int thumbMax, int winX, int winW, int mouseX, int current);

} // namespace guild::gui
