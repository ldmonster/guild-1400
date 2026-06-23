#include "gui/dragtext.h"

#include <cstring>

namespace guild::gui {

// gilde.exe 0x4ad508 — VIBE_DragSlot_BeginDragText.
i32 BeginDragText(DragTextState& st, const char* pendingText,
                  i32 cursorX, i32 cursorY, i32 gameTick,
                  const DragTextHooks& h) {
    // The three resets run BEFORE the string copy completes its loop in the original
    // (SetSprite/ResetTable/ResetMouseButtonState are called between v1/v2 load and
    // the do/while). Order preserved.
    if (h.cursorSetSprite)   h.cursorSetSprite();    // DragCursor_SetSprite(this, 0)
    if (h.resetSlotTable)    h.resetSlotTable();     // DragSlot_ResetTable()
    if (h.resetMouseButtons) h.resetMouseButtons();  // Input_ResetMouseButtonState()

    // strcpy(byte_11B6B20, byte_61D9C8) — the 2-byte-stride loop copies the source
    // string including its NUL into the active buffer.
    {
        const char* src = pendingText ? pendingText : "";
        char* dst = st.activeText;
        std::size_t i = 0;
        for (; i + 1 < kDragTextBufBytes; ++i) {
            dst[i] = src[i];
            if (src[i] == '\0') break;
        }
        if (i + 1 >= kDragTextBufBytes) dst[kDragTextBufBytes - 1] = '\0';
    }

    st.startTick = gameTick;                          // dword_631678 = dword_62EB38
    if (h.setTooltipText)                             // Widget_SetTooltipText(byte_61D9C8)
        h.setTooltipText(pendingText ? pendingText : "");

    st.active = 1;                                    // dword_62D0D4 = 1
    st.boxX   = cursorX - 8;                          // dword_62D0C4 = (cursorX>>16 already) - 8
    st.boxX2  = 0;                                    // dword_62D0CC = 0
    st.boxY2  = 0;                                    // dword_62D0D0 = 0
    st.boxY   = cursorY;                              // dword_62D0C8 = cursorY

    return st.boxY;                                  // return dword_62D0C8
}

} // namespace guild::gui
