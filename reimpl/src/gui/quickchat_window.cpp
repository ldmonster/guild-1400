#include "gui/quickchat_window.h"

namespace guild::gui {

// gilde.exe 0x4bff90 — VIBE_QuickChat_UpdateWindow.
//   if ( g_consoleBuilder[..] == ChatConsole_BuildWindow
//        || dword_62EB38 - 570 >= dword_631E88
//        || dword_631E88 == -1 ) {
//      if (dword_631E8C != -1) { Form_Destroy(dword_631E8C); dword_631E8C = -1; }
//   } else if (dword_631E8C == -1) {
//      dword_631E8C = GameTick_Finalize(0,0,"misc\\quickchat");
//      Window_PositionAtCoord(dword_631E8C, 3);
//   }
//   if (dword_631E8C != -1) { SelectWindow(.,0); RenderRichString(name);
//                             RenderRichString(text); }
QuickChatAction QuickChat_Update(const QuickChatState& s) {
    bool expired = s.fullConsoleActive ||
                   (unsigned)(s.now - kQuickChatTimeout) >= (unsigned)s.msgTick ||
                   s.msgTick == -1;

    if (expired) {
        if (s.formId != -1)
            return QuickChatAction::kDestroy;
        return QuickChatAction::kNone; // already closed
    }

    if (s.formId == -1)
        return QuickChatAction::kCreate;

    // Open and valid: re-render the two lines this frame.
    return QuickChatAction::kRefresh;
}

} // namespace guild::gui
