#include "gui/input.h"
#include "gui/object.h"
#include "gui/window.h"
#include "gui/radiogroup.h"

#include <cstdint>

namespace guild::gui {

int g_lastClickedId     = -1; // dword_75BF38
int g_lastClickedPrevId = -1; // dword_75BEEC
int g_lastClickedWindow = -1; // dword_75BF08
int g_lastClickedSlot   = -1; // dword_75BF00 / dword_75BF10
int g_hoverObject       = -1; // dword_62D22C
int g_hoverWindow       = -1; // dword_62D290
int g_mouseClick        = 0;  // dword_672228
int g_mouseDown         = 0;  // dword_672220

void ResetInputState() {
    g_lastClickedId = g_lastClickedPrevId = -1;
    g_lastClickedWindow = g_lastClickedSlot = -1;
    g_hoverObject = g_hoverWindow = -1;
    g_mouseClick = g_mouseDown = 0;
}

// gilde.exe 0x421594 — dword_75BF10 resolution loop.
// for (i=0; i < objCount; ++i) if (hover == window.childList[i]) return i;
int ResolveClickedSlot(int winSlot, int hoverObject) {
    if (winSlot < 0 || winSlot >= kMaxWindows)
        return -1;
    Window& win = g_windows[winSlot];           // &dword_67EB80[238*winSlot]
    i32* list = WindowChildList(winSlot);        // w[6]
    int count = win.objCount();                  // *(int*)((char*)w+26) >> 16
    for (int i = 0; i < count; ++i) {
        if (hoverObject == list[i])
            return i;
    }
    return -1;
}

// gilde.exe 0x421594 — click-routing block.
int RouteClick(int winSlot, int hoverObject) {
    if (hoverObject < 0 || hoverObject >= kMaxWidgets)
        return -1;
    Widget& w = g_widgets[hoverObject];          // dword_69FFB4 + 740*hover

    // Only clickable widgets (btnFlagA/+68 or btnFlagB/+72) route.
    if (!w.btnFlagA() && !w.btnFlagB())
        return -1;

    int id = w.id();                             // the widget's id (+8)
    g_lastClickedId = id;                        // dword_75BF38 = clicked id

    // Togglable button: btnFlagA set -> flip the low bit of value (+36) and mirror (+40).
    if (w.btnFlagA()) {
        w.value() ^= 1;                          // *(+36) ^= 1
        w.valueMirror() = w.value();             // *(+40) = *(+36)
        // original also plays a click voice sample (VIBE_Audio_StartVoiceSample) here.
    }
    // (else: the non-toggle branch plays a sample with v25 = btnFlagA, suppressed for
    //  the Cancel id 1155 — purely audio, no model effect.)

    // Radio-group mutual exclusion: find the group containing this widget and select it.
    bool done = false;
    for (int g = 0; g < kMaxRadioGroups && !done; ++g) {
        RadioGroup& grp = g_radioGroups[g];      // 35*g stride
        for (int k = 0; k < grp.count; ++k) {
            if (hoverObject == grp.button[k]) {
                Selection_Update(g, k);          // exclusive select (sets selected = k)
                done = true;
                break;
            }
        }
    }

    // Sticky copy + remember the resolved slot.
    if (g_lastClickedId != g_lastClickedPrevId)
        g_lastClickedPrevId = g_lastClickedId;   // dword_75BEEC
    g_lastClickedSlot = ResolveClickedSlot(winSlot, hoverObject); // dword_75BF00
    return id;
}

// gilde.exe 0x421594 — scrollbar-drag math.
//   value = thumbMax * (mouseX - winX - 12) / (winW - 24) + 4
// guarded by the active band: mouseX in (winX+10, winX+winW-10).
int ScrollbarDragValue(int thumbMax, int winX, int winW, int mouseX, int current) {
    if (mouseX > winX + 10 && mouseX < winX + winW - 10) {
        return thumbMax * (mouseX - winX - 12) / (winW - 24) + 4;
    }
    return current;
}

} // namespace guild::gui
