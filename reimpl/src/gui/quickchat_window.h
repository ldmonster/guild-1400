#pragma once
// guild::gui — VIBE_QuickChat_UpdateWindow @0x4bff90.  The transient "quick chat" popup
// shown when another player's chat arrives.
//
// Lifecycle (called every frame):
//   DESTROY the window (form id dword_631E8C) when ANY of:
//     - the active console builder is the full chat console (g_consoleBuilder ==
//       VIBE_ChatConsole_BuildWindow), OR
//     - the message has aged out: now - msgTick >= 570 (dword_631E88), OR
//     - there is no pending message (msgTick == -1).
//   else CREATE it (when not already open):
//     form = "misc\quickchat";  Window_PositionAtCoord(form, 3);   // anchor corner 3.
//   then, while open:
//     SelectWindow(form, 0);  RenderRichString(senderName);  RenderRichString(messageText).
//
// We recover the form name, the anchor corner (3), the timeout (570 ticks), and the
// open/close decision as a pure state function.  The text engine and console-builder
// comparison are forward-declared / mocked.

#include "gui/types.h"

namespace guild::gui {

inline constexpr const char* kFormQuickChat = "misc\\quickchat";

inline constexpr int kQuickChatTimeout    = 570; // dword_62EB38 - 570 >= msgTick -> expire
inline constexpr int kQuickChatAnchor     = 3;   // Window_PositionAtCoord corner
inline constexpr int kQuickChatTitleSlot  = 0;   // SelectWindow(form, 0)

// The per-frame state the updater reads.
struct QuickChatState {
    int  formId = -1;          // dword_631E8C — current window form (-1 = closed)
    int  msgTick = -1;         // dword_631E88 — tick the pending message arrived (-1 = none)
    int  now = 0;              // dword_62EB38 — current tick
    bool fullConsoleActive = false; // g_consoleBuilder == ChatConsole_BuildWindow
};

// The action the updater takes this frame.
enum class QuickChatAction {
    kNone,    // already in the right state
    kCreate,  // open the popup (form not yet present)
    kDestroy, // close the popup
    kRefresh, // already open & valid: re-render the two text lines
};

// gilde.exe 0x4bff90 — decide what to do this frame.  Mirrors the original's branch:
//   if (consoleActive || now-570 >= msgTick || msgTick==-1) { if (formId!=-1) destroy; }
//   else if (formId==-1) create;
//   then if (formId!=-1) refresh.
QuickChatAction QuickChat_Update(const QuickChatState& s);

} // namespace guild::gui
