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

    // 0x41a143/0x41a156: widthDelta (v39, var_18) = newW - w  (a1@ax = newW);
    //                    heightAccum (var_14)    = newH - h  (a2@dx = newH).
    // NOTE: the original's child-fit loop grows the HEIGHT accumulator (var_14), NOT the
    // width.  The width delta is never grown by the loop.  (Verified against the disasm:
    // var_18 = a1-w[+8], var_14 = a2-h[+10]; the loop writes *(DWORD*)&var_14.)
    int widthDelta   = newW - win.w();          // v39 (NOT grown below)
    int heightAccum  = newH - win.h();          // var_14 (grown by the child loop)

    // Pass 1 (0x41a160 loop): expand heightAccum to bound the children. The original
    // compares child (x + w) against (heightAccum + y + h) and grows on overflow.
    //   v26 = y (word@+6), a4 = h (word@+10); v36 = heightAccum + y + h;
    //   v27 = child.x(+16) + child.w(+20); if (v27 > v36) heightAccum = v27 - y - h + 8.
    i32* list = WindowChildList(winSlot);
    for (int i = 0; i < win.objCount(); ++i) {
        Widget& c = g_widgets[list[i]];
        int childRight = c.x() + c.w();          // (+16>>16) + (+20>>16)
        int origin     = win.y() + win.h();      // v26 + a4 = y + h
        if (childRight > heightAccum + origin)
            heightAccum = childRight - origin + 8;
    }

    // --- Surface re-creation (+32/+36/+40) is DEFERRED to the renderer cluster. ----------

    // 0x41a228/0x41a231: w (word@+8) += widthDelta ; h (word@+10) = heightAccum + old_h.
    win.w() = static_cast<i16>(win.w() + widthDelta);
    win.h() = static_cast<i16>(win.h() + heightAccum);

    // --- The scrollbar-tile snap (flags 0x400 / 0x4) needs the 84-byte gfx-metric table
    //     and is DEFERRED. ----------------------------------------------------------------

    // Backing widget clip-bound update (+30 = w+x, +34 = h+y).
    Widget& backing = g_widgets[win.backWidget()];
    backing.clipY1() = static_cast<i16>(win.h() + win.y()); // +34
    backing.clipX1() = static_cast<i16>(win.w() + win.x()); // +30

    // 0x41a335: if (y + h > LOWORD(dword_69FFBC)) h = LOWORD(dword_69FFBC) - y.
    //   ([&dword_69FFB8+2]>>16 resolves to the low word of dword_69FFBC == (i16)g_screenClipExt)
    if (win.y() + win.h() > static_cast<i16>(g_screenClipExt))
        win.h() = static_cast<i16>(static_cast<i16>(g_screenClipExt) - win.y());
    // 0x41a35f: if (x + w > dword_69FFBC>>16) w = HIWORD(dword_69FFBC) - x.
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

    // 0x41a537 loop: the original walks the GLOBAL widget array (dword_69FFB4 + 740*i,
    // i = 0..objCount-1) — NOT the window's child id list (v5 steps 0x2E4 from base 0).
    // Reproduce that flat iteration exactly.
    int count = win.objCount();
    for (int i = 0; i < count && i < kMaxWidgets; ++i) {
        Widget& c = g_widgets[i];
        int childBottom = c.x() + c.w();        // (+16>>16) + (+20>>16)  (per the original)
        int winTop      = win.y();              // v3[1] >> 16
        if (childBottom > minHeight + winTop)
            minHeight = childBottom - winTop + 8;
    }
    // Resize(curWidth, fittedHeight): width word @+6 (the original passes HIWORD(w+6)).
    return Window_Resize(win.w(), static_cast<i16>(minHeight), winSlot);
}

} // namespace guild::gui
