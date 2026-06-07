#include "gui/hud_drag.h"
#include "gui/object.h"  // g_widgets (dword_69FFB4)

namespace guild::gui {

i32 g_hudDragMode = 0; // dword_649CD4

i16 g_highlightList[kHudHighlightMax] = {0}; // word_11BC35C
i32 g_highlightCount = 0;                     // dword_631E5C

i32 g_formActiveA[kHudFormCount] = {0}; // dword_676BF0
i32 g_formActiveB[kHudFormCount] = {0}; // dword_676C00

void ResetHudHighlight() {
    for (auto& v : g_highlightList) v = 0;
    g_highlightCount = 0;
    for (auto& v : g_formActiveA) v = 0;
    for (auto& v : g_formActiveB) v = 0;
}

void Hud_EnableDragMode()  { g_hudDragMode = 1; } // gilde.exe 0x595b8c
void Hud_DisableDragMode() { g_hudDragMode = 0; } // gilde.exe 0x595b80

// gilde.exe 0x4bd008 — VIBE_Hud_ToggleObjectHighlight.
// The original strides forms with i = 0,171,..,8037 (i != 8208) and widgets with the
// 740-byte stride; we use the form index (i/171) and widget index (v5) directly since
// our arrays are typed.  The +60 form link is modelled as (formIndex+1) stored in
// parentClip() (see header).
i32 Hud_ToggleObjectHighlight(i32 on) {
    i32 result = on;
    i32 v1 = g_highlightCount; // dword_631E5C
    if (on) {
        v1 = 0;
        for (int formIndex = 0; formIndex < kHudFormCount; ++formIndex) {
            result = formIndex * 171 * 4; // mirrors `result = i*4`
            if (g_formActiveA[formIndex] && g_formActiveB[formIndex]) {
                for (int v5 = 0; v5 < kMaxWidgets && v5 < 512; ++v5) {
                    Widget& w = g_widgets[v5];        // result = v6 + v2
                    if (w.type() != 0                 // *(BYTE*)(.+24)
                        && w.parentClip() == formIndex + 1 // &dword_676A60[i] == *(.+60)
                        && w.renderPtr() == 0) {      // !*(.+52)
                        g_highlightList[v1++] = static_cast<i16>(v5); // word_11BC35C[v1++] = v5
                        w.renderPtr() = 1;            // *(.+52) = 1
                    }
                }
            }
        }
    } else if (g_highlightCount > 0) {
        result = 0;
        int v3 = 0;
        do {
            int slot = static_cast<u16>(g_highlightList[v3++]); // 740*(u16)word_11BC35C[v3++]
            ++result;
            if (slot >= 0 && slot < kMaxWidgets)
                g_widgets[slot].renderPtr() = 0;                // *(v2 + v4 + 52) = 0
        } while (result < v1);
    }
    g_highlightCount = v1; // dword_631E5C = v1
    return result;
}

} // namespace guild::gui
