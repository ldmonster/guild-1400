#include "gui/object_add_animated.h"
#include "gui/object.h"
#include "gui/window.h"
#include "gui/widget_create.h"   // g_screenClipExt (dword_69FFBC)
#include "gui/zorder.h"

namespace guild::gui {

// ---- Inert-default coupled edges --------------------------------------------------------
// The originals reach the object/draw-slot factory (sim+render cluster) and the anim-flags
// cluster. To keep AddAnimatedToWindow's MODEL (window bounds-check, child-list linkage,
// z-order, clip inheritance, content-height growth) faithful and exercisable without those
// clusters, the edges have neutral defaults here. They are weak so a test or a future wiring
// TU can supply the real factory / scale logic without touching this file (no ODR clash).

// VIBE_GameLogic_Objects @0x412fa0: allocate the animated object's widget slot and seed the
// fields AddAnimatedToWindow itself does NOT set (x/y/type), mirroring the factory's
// responsibility split. Returns the slot index or -1.
int __attribute__((weak)) AnimObjectFactory(i16 screenX, i16 screenY, int entityIdx) {
    int idx = Widget_AllocSlot();
    if (idx == -1)
        return -1;
    Widget& obj   = g_widgets[idx];
    obj.x()       = screenX;            // +16
    obj.y()       = screenY;            // +18
    obj.dataPtr() = entityIdx;          // +12 (the entity/anim selector)
    obj.type()    = kTypeAnim;          // +24 = 'A' (animated object)
    obj.order()   = 2;                  // +26 default draw order
    return idx;
}

// VIBE_Object_ApplyAnimScale @0x41e388: inert by default (scale left as the factory seeded).
void __attribute__((weak)) ApplyAnimScaleEdge(int /*objId*/) {}

// ---- gilde.exe 0x41af64 — VIBE_Object_AddAnimatedToWindow -------------------------------
int Object_AddAnimatedToWindow(i16 x, i16 y, int winSlot, int entityIdx, int animArg) {
    (void)animArg; // forwarded to ApplyAnimScale in edx; the callee ignores it (see header).

    if (winSlot < 0 || winSlot >= kMaxWindows)
        return -1;
    Window& win = g_windows[winSlot];          // &dword_67EB80[238*winSlot]

    if (!win.enabled())                        // cmp [ecx+280h],0 / jz -> -1
        return -1;
    if (win.objCount() >= kMaxChildren) {      // cmp word [ecx+1Ch],180h / jge
        // VIBE_ErrorLog_ReportMessage("Too many objects on window!")
        return -1;
    }

    // Object factory at window-relative screen coords. NOTE the x term has NO -word[300]
    // bias here (AddToWindow subtracts it; the animated variant does not), and y subtracts
    // the window's current scroll (word[292] == scrollCur @+584).
    i16 screenX = static_cast<i16>(x + win.x());                 // a3 + w.x
    i16 screenY = static_cast<i16>(y + win.y() - win.scrollCur()); // a2 + w.y - scrollCur
    int id = AnimObjectFactory(screenX, screenY, entityIdx);

    // *(w.objListPtr + 4 * (w.objCount)) = id  — store into the child id list at count.
    i32* list = WindowChildList(winSlot);
    list[win.objCount()] = id;

    if (id == -1)                              // cmp eax,-1 / jz -> return the stored -1
        return id;

    ZOrder_InsertObject(id, win.backWidget()); // edx = window record; we pass backWidget idx

    Widget& obj = g_widgets[id];               // &dword_69FFB4[740*id]
    obj.clipY0() = 0;                          // word [edx+20h]
    obj.clipX0() = 0;                          // word [edx+1Ch]
    obj.groupLink() = win.backWidget();        // [edx+2Ch] = (Window*)ecx (parent back-link)

    // Screen clip extent (dword_69FFBC): low word -> +34, high word -> +30 (== clipX1()).
    // NOTE: +34 has no named accessor (clipX1() is +30 in gui/types.h), so the low word is
    // written via raw at<i16>(34) to match `*(_WORD*)(v10+34)=LOWORD(dword_69FFBC)`.
    u32 clipExt = static_cast<u32>(g_screenClipExt);
    obj.at<i16>(34) = static_cast<i16>(clipExt & 0xFFFF);  // word [edx+22h] +34 = low word
    obj.at<i16>(30) = static_cast<i16>(clipExt >> 16);     // word [edx+1Eh] +30 = high word

    // Inherit render/clip pointers from the window's backing widget (window +620 selects it).
    Widget& bw     = g_widgets[win.backWidget()];        // &dword_69FFB4[740*w[+620]]
    obj.parentClip() = bw.parentClip();                  // [edx+3Ch] = [edi+...+3Ch]
    obj.renderPtr()  = bw.renderPtr();                   // [edx+34h] = [edi+...+34h]

    ApplyAnimScaleEdge(id);                    // VIBE_Object_ApplyAnimScale(id)

    ++win.objCount();                          // ++word [ecx+1Ch]

    // Content-height growth: bottom = (obj[+20] >> 16) + y  ==  obj.h() + y.
    i32 bottom = (static_cast<i32>(obj.at<i32>(20)) >> 16) + static_cast<i32>(y);
    if (bottom > win.contentHeight())          // cmp eax,[ecx+244h] / jle
        win.contentHeight() = bottom;          // mov [ecx+244h],eax

    return id;
}

} // namespace guild::gui
