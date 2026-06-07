#include "gui/window_mgmt.h"
#include "gui/window.h"
#include "gui/object.h"
#include "gui/window_render.h"   // Widget_LayoutBounds (shared weak edge)
#include "gui/form_lifecycle.h"  // g_screenCenterX / g_screenCenterY, Widget_DestroyByType

#include <cstdint>

namespace guild::gui {

// gilde.exe 0x41d764 — VIBE_Window_PositionCentered
int Window_PositionCentered(int winSlot, char mode) {
    Window& win = g_windows[winSlot];          // v2 = &dword_67EB80[238*winSlot]

    // *(v2+2)>>16 == word at +4 == x ; *(v2+6)>>16 == word at +8 == w.
    int xc;
    if (mode & 1)                              // (a2 & 1) != 0
        xc = static_cast<i16>(g_screenCenterX - (win.w() / 2)); // LOWORD: dword_69FFA4 - w/2
    else
        xc = win.x();                          // *(v2+2)>>16
    // v2[1]>>16 == word at +6 == y ; v2[2]>>16 == word at +10 == h.
    int yc;
    if (mode & 2)                              // (a2 & 2) != 0
        yc = static_cast<i16>(g_screenCenterY - (win.h() / 2)); // LOWORD: dword_69FFA0 - h/2
    else
        yc = win.y();                          // v2[1]>>16

    Widget_LayoutBounds(xc, yc, win.backWidget()); // VIBE_Widget_LayoutBounds(x, y, v2[155])
    return 0; // original returns LayoutBounds' al (void in our model)
}

// gilde.exe 0x41a7a8 — VIBE_Window_RemoveIfActive
int Window_RemoveIfActive(int winSlot, int a2, int a3) {
    Window& win = g_windows[winSlot];          // 238*winSlot
    if (!win.enabled())                        // !dword_67EE00[238*winSlot]
        return 0;
    Widget_DestroyByType(win.backWidget(), a2, a3); // dword_67EDEC[238*winSlot] == backWidget
    return 1;
}

// gilde.exe 0x41a7dc — VIBE_Window_RemoveChildren
int Window_RemoveChildren(int winSlot, int keepBacking) {
    if (winSlot > kMaxWindows)                 // a1 > 96
        return 0;
    Window& win = g_windows[winSlot];          // v4 = &dword_67EB80[238*winSlot]
    if (!win.enabled())                        // !v4[160]
        return 0;

    i32* list = WindowChildList(winSlot);      // v4[6]
    // Walk children back-to-front: v5 = objCount-1 .. 0.
    for (int i = win.objCount() - 1; i >= 0; --i) { // (*(int*)(w+26)>>16) - 1
        int childIdx = list[i];                // *(v6 + v4[6])
        // Reset whichever of the three cached "special" child slots this child occupies.
        if (childIdx == win.reset234())        win.reset234() = -1; // v4[234]
        else if (childIdx == win.reset235())   win.reset235() = -1; // v4[235]
        else if (childIdx == win.reset236())   win.reset236() = -1; // v4[236]

        // A window-backing child ('@'/0x40) is only torn down when keepBacking != 0.
        if (g_widgets[childIdx].type() != kTypeWindow || keepBacking)
            Widget_DestroyByType(childIdx, 0, i); // VIBE_Widget_DestroyByType(childIdx, v6, v5)
    }

    // Clear the window text buffer marker (original zeroes *(BYTE*)v4[11]).
    if (win.textBuffer())                      // v9 = (BYTE*)v4[11]
        win.textBuffer() = 0;                  // *v9 = 0

    // Reset the scroll bookkeeping dwords (67EDC8/67EDC4/67EDD0 == +584/+580/+592,
    // 67EDCC == +588 -> -1). Indices 14*a1 + 224*a1 == 238*a1.
    win.scrollCur()    = 0;   // dword_67EDC8[238*winSlot]
    win.contentHeight()= 0;   // dword_67EDC4[238*winSlot]
    win.scrollOffset() = 0;   // dword_67EDD0[238*winSlot]
    win.scrollPrev()   = -1;  // dword_67EDCC[238*winSlot]
    return 1;
}

// gilde.exe 0x41a024 — VIBE_Window_Scroll
int Window_Scroll(int delta, int amount, int winSlot) {
    Window& win = g_windows[winSlot];          // v6 = &dword_67EB80[238*winSlot]
    int v7 = win.scrollOffset();               // v6[148]

    if (v7 + win.scrollCur() > 0 && amount < 0) {        // scrollOffset + scrollCur > 0 && amount<0
        int floorAmt = -win.scrollCur() - v7;            // -v6[146] - v7
        if (amount <= floorAmt)                          // a2 <= -v6[146]-v7
            amount = floorAmt;
        win.scrollOffset() = amount;                     // dword_67EDD0[...] = a2
    } else {
        int v8 = win.scrollOffset() + win.scrollCur() + win.h(); // v6[148]+v6[146]+(v6[2]>>16)
        if (v8 < win.contentHeight() && amount > 0) {    // v8 < v6[145] && amount>0
            int v9 = win.contentHeight() - v8;           // v6[145] - v8
            if (amount < v9)                             // a2 < v9
                v9 = amount;
            win.scrollOffset() += v9;                    // dword_67EDD0[...] += v9
        }
    }

    win.scrollExtraX() += delta;               // dword_67EDD4[238*winSlot] += a1
    return 16 * (56 * winSlot);                // 16 * v10
}

} // namespace guild::gui
