#pragma once
// guild::gui — Window array, window creation (data-model core) and child-object list.
//
// Original global state:
//   dword_67EB80  Window array (238-dword / 952-byte stride, capacity 96)   -> g_windows
//   dword_67EE00  per-slot enabled/in-use flags (== g_windows[i].enabled(),
//                 i.e. dword_67EB80[238*i + 160]); 0 == free slot
//   dword_62D2F0  rotating window-create counter (wraps at 96)              -> g_windowCounter
//   dword_62D230  current window id   (set by Create and SelectWindow)      -> g_currentWindowId
//   dword_62D298  cached current Window* (= &g_windows[curWin])             -> g_currentWindow
//   dword_62D2B0  default font id for new windows                          -> g_defaultFont
//   dword_62D2B8  default palette                                          -> g_defaultPalette

#include "gui/types.h"

namespace guild::gui {

extern Window g_windows[kMaxWindows]; // dword_67EB80

// Per-window child object-id list backing store. In the original this is a
// separately-allocated 0x600-byte buffer pointed to by Window +24 (objListPtr).
// We keep the ids here (indexed by window slot) and use objListPtr() as a marker.
extern i32 g_windowChildren[kMaxWindows][kMaxChildren];

// Child id list for a window slot (g_windowChildren[slot]).
i32* WindowChildList(int slot);

// Reset the window array + child lists + window globals to the zeroed state.
void ResetWindows();

extern int     g_windowCounter;   // dword_62D2F0
extern int     g_currentWindowId; // dword_62D230
extern Window* g_currentWindow;   // dword_62D298
extern i32     g_defaultFont;     // dword_62D2B0
extern i32     g_defaultPalette;  // dword_62D2B8

// Window flag bits (recovered from VIBE_Window_Create / recon §1b).
inline constexpr i32 kWinFlagColorKey   = 0x1;   // alpha/colorkey surface
inline constexpr i32 kWinFlagPalette    = 0x4;   // use default palette dword_62D2B8
inline constexpr i32 kWinFlagBackground = 0x8;   // add background object
inline constexpr i32 kWinFlagTextBuffer = 0x10;  // allocate 0x17D0 text buffer
inline constexpr i32 kWinFlagScrollbar  = 0x20;  // add scrollbar buttons
inline constexpr i32 kWinFlagClipRegion = 0x200; // clip region

// gilde.exe 0x419c38 — VIBE_Window_Create  (x@ax, y@dx, w@cx, h@bx, flags@stack)
//
// Allocates a free window slot, initialises geometry/margins/flags, allocates the
// 0x600-byte object-id child list, creates a window-backing widget (type '@', id =
// slot+1024) wired to this Window, optionally allocates the 0x17D0 text buffer, and
// caches the new window as current (dword_62D230/dword_62D298). Returns the slot
// index, or -1 when all 96 slots are in use.
//
// NOTE on scope: this is the GUI *data-model* core. The original additionally creates
// up to three software surfaces (VIBE_Surface_Create) and, for the scrollbar/
// background flags, spawns child objects via VIBE_Object_AddToWindow (which calls the
// renderer/sim clusters). Those side effects are deferred to those clusters; here we
// faithfully reproduce slot allocation, field init, the backing widget, the child
// list, the text buffer and the current-window caching. The flag bits that would
// trigger surface/child creation are honored only for the parts that live in this
// module (text buffer; field state).
int Window_Create(i16 x, i16 y, i16 w, i16 h, i32 flags);

// gilde.exe 0x41a90c — VIBE_Window_Destroy (data-model core)
// Frees the child-id list and text buffer, marks the slot free (enabled=0 and stores
// the recycled id in dword[0]), and decrements the rotating counter. Surface/widget
// teardown (VIBE_Surface_Destroy / VIBE_Widget_DestroyByType) is deferred. Returns 1
// on success, 0 if the slot was out of range or already free.
int Window_Destroy(int slot);

// gilde.exe 0x41ae10 — VIBE_Object_AddToWindow  (win@ecx, y@dx, x@ax, gfx@ebx)
//
// Core "create a child widget and link it into the window" routine. Here we model the
// *child-list* half that lives in this module: bounds-check (<384 children, window
// enabled), allocate a widget slot, store its index into the window's object-id list
// at the current count, copy parent geometry/clip, bump the child count, and grow the
// window content-height. The original first spawns the underlying object via
// VIBE_GameLogic_Objects (sim cluster) and inserts it into the Z-order; those steps
// are deferred. Returns the new widget slot index, or -1 on failure.
int Object_AddToWindow(int winSlot, i16 y, i16 x, i32 gfxId);

} // namespace guild::gui
