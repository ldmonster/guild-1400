#include "gui/window_layout.h"
#include "gui/object.h"
#include "gui/window.h"
#include "gui/widget_create.h" // g_screenClipW / g_screenClipExt

#include <cstdint>

namespace guild::gui {

// gilde.exe 0x41a0f8 — VIBE_Window_Resize  (child-reflow + geometry model).
//
// The original reads window geometry via misaligned `dword >> 16` views that all resolve
// to the int16 pixel words x=+4, y=+6, w=+8, h=+10. We use those words directly.
int Window_Resize(i16 newW, i16 newH, int winSlot) {
    if (winSlot < 0 || winSlot >= kMaxWindows) return 0;
    Window& win = g_windows[winSlot];
    if (!win.enabled())                         return 952 * winSlot; // free slot: original returns base*4

    // --- height delta (v36) and an in-place "fitted width" accumulator (v37). ----------
    int heightDelta = newH - win.h();           // (a1>>16) - (win h)
    int fittedW     = newW - win.w();           // (a2>>16) - (win w) : width growth

    // Pass 1: expand fittedW to bound the widest child (matches the +8 padding loop).
    i32* list = WindowChildList(winSlot);
    for (int i = 0; i < win.objCount(); ++i) {
        Widget& c = g_widgets[list[i]];
        int childRight = c.x() + c.w();          // (+16>>16) + (+20>>16)
        int origin     = win.x() + win.y();      // (winX) + (winY)  (as the original sums)
        if (childRight > fittedW + origin)
            fittedW = childRight - origin + 8;
    }

    // --- Surface re-creation (+32/+36/+40) is DEFERRED to the renderer cluster. ----------

    // Apply the width/height growth to the window words (+8 w, +10 h).
    win.w() = static_cast<i16>(win.w() + fittedW); // *((WORD*)w+4) += v36-equivalent (width)
    win.h() = static_cast<i16>(win.h() + heightDelta); // *((WORD*)w+5) += heightDelta

    // --- The scrollbar-tile snap (flags 0x400 / 0x4) needs the 84-byte gfx-metric table
    //     and is DEFERRED. ----------------------------------------------------------------

    // Backing widget clip-bound update (+30 = w+x, +34 = h+y).
    Widget& backing = g_widgets[win.backWidget()];
    backing.clipY1() = static_cast<i16>(win.h() + win.y()); // +34
    backing.clipX1() = static_cast<i16>(win.w() + win.x()); // +30

    // Clamp to the screen extent (dword_69FFB8 width, dword_69FFBC extent).
    if (win.y() + win.x() > (g_screenClipW >> 16))
        win.h() = static_cast<i16>(g_screenClipExt - win.y());
    if (win.x() + win.w() > (g_screenClipExt >> 16))
        win.w() = static_cast<i16>((g_screenClipExt >> 16) - win.x());

    // Pass 2: reflow every child's clip bounds to the new window extent (+30/+34).
    for (int i = 0; i < win.objCount(); ++i) {
        Widget& c = g_widgets[list[i]];
        c.clipY1() = static_cast<i16>(win.h() + win.y()); // +34 = h + y
        c.clipX1() = static_cast<i16>(win.w() + win.x()); // +30 = w + x
    }

    // Backing widget size (+20/+22) tracks the window's new w/h.
    backing.w() = win.w();   // +20
    backing.h() = win.h();   // +22
    return win.backWidget();
}

// gilde.exe 0x41a500 — VIBE_Window_AutoFitHeight  (winSlot@eax, minHeight@edi)
int Window_AutoFitHeight(int winSlot, int minHeight) {
    if (winSlot < 0 || winSlot >= kMaxWindows) return 952 * winSlot;
    Window& win = g_windows[winSlot];
    if (!win.enabled())                         return 952 * winSlot;

    // When the text-buffer flag (0x10) is set, start from the scroll thumb extent (+580).
    if (win.flags() & kWinFlagTextBuffer)
        minHeight = win.contentHeight();        // v3[145] == +580

    i32* list = WindowChildList(winSlot);
    int count = win.objCount();
    for (int i = 0; i < count; ++i) {
        Widget& c = g_widgets[list[i]];
        int childBottom = c.x() + c.w();        // (+16>>16) + (+20>>16)  (per the original)
        int winTop      = win.y();              // v3[1] >> 16
        if (childBottom > minHeight + winTop)
            minHeight = childBottom - winTop + 8;
    }
    // Resize(curWidth, fittedHeight): width word @+6 (the original passes HIWORD(w+6)).
    return Window_Resize(win.w(), static_cast<i16>(minHeight), winSlot);
}

} // namespace guild::gui
