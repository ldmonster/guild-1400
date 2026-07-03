#include "gui/form_lifecycle.h"
#include "gui/form.h"
#include "gui/window.h"
#include "gui/object.h"
#include "gui/window_render.h" // Widget_LayoutBounds (shared weak edge)
#include "gui/window_mgmt.h"   // Window_PositionCentered

#include <cstdint>

// GUILD_WEAK: weak "default edge" on ELF (tests override); strong on PE/COFF
// (MinGW has no usable weak-definition support; no overrides in the app build).
#if defined(_WIN32)
#define GUILD_WEAK
#else
#define GUILD_WEAK __attribute__((weak))
#endif

namespace guild::gui {

// Screen-center globals (owned here). Original BSS dword_69FFA4 / dword_69FFA0.
i32 g_screenCenterX = 0; // dword_69FFA4
i32 g_screenCenterY = 0; // dword_69FFA0

// ---------------------------------------------------------------------------
// Placeholder render/OS leaves (not yet translated). Weak so a test or a future
// translation can supply the real body without an ODR clash. These have no
// data-model effect of their own in the originals' caller-visible contract that
// the lifecycle functions here depend on.
// ---------------------------------------------------------------------------
void GUILD_WEAK Widget_DestroyByType(int /*widgetIdx*/, int /*a2*/, int /*a3*/) {}
int  GUILD_WEAK Surface_DestroySurface(i32 /*surfaceHandle*/) { return 0; }
// gilde.exe 0x41aae8 — VIBE_ZOrder_RaiseWindow. DEFERRED: its body shuffles the widget
// pointer cache using host-pointer identity (widget marker/+0 vs window backWidget index,
// widget +44 group link vs Window*); translating that over our index model would not be
// behaviour-identical, so it is left as a placeholder (Form_RaiseWindows still drives the
// per-window dispatch faithfully). Weak so the real translation can supersede it.
int  GUILD_WEAK ZOrder_RaiseWindow(int /*winSlot*/) { return 0; }

// gilde.exe 0x41e544 — VIBE_Form_GetWindowId
int Form_GetWindowId(int formId, int slot) {
    Form& f = g_forms[formId];                 // 171*formId base
    if (slot > f.windowCount() || slot < 0)    // dword_676BE4[171*id] bound (inclusive)
        return -1;
    return f.windowId(slot);                   // dword_676A64[slot + 171*id]
}

// gilde.exe 0x41d634 — VIBE_Form_SetObjectsVisible
void Form_SetObjectsVisible(int formId, int hide) {
    Form& f = g_forms[formId];                 // v5 = &dword_676A60[171*formId]
    if (!f.valid())                            // dword_676A60[171*id + 100]
        return;

    // Sweep every widget slot (the original strides result += 185 dwords == 740 bytes,
    // result != 94720 == 740*512/4*... i.e. 512 slots). For each in-use (type@+24 != 0)
    // widget whose +60 (parentClip) handle equals this form record, set +52 (renderPtr)
    // to (hide == 0). parentClip holds the owning form record handle in the original; on
    // a 64-bit host that handle is the low 32 bits of the form-record pointer (the +60
    // field is i32), so the comparison narrows both sides to 32 bits to stay faithful.
    i32 formKey = static_cast<i32>(reinterpret_cast<std::intptr_t>(&f));
    for (int i = 0; i < kMaxWidgets; ++i) {
        Widget& wg = g_widgets[i];
        if (wg.type() && wg.parentClip() == formKey)
            wg.renderPtr() = (hide == 0);      // *(v6+52) = a2 == 0
    }
    f.objectsVisible() = hide;                 // v5[104] = a2
}

// gilde.exe 0x41d568 — VIBE_Form_SetChildrenVisible
void Form_SetChildrenVisible(int formId, int hide) {
    Form& f = g_forms[formId];                 // v10 = (char*)dword_676A60 + 684*formId
    if (!f.valid())                            // dword_676A60[100 + 171*id]
        return;

    // For each of the form's windows, walk its child list and set every child widget's
    // +56 (disabledA) to (hide == 0). (Original v5 < *(int*)(w+26)>>16 == objCount().)
    for (int g = 0; g < f.windowCount(); ++g) {     // v11 < *((int*)v10+97)
        int slot = f.windowId(g);                   // *((DWORD*)v4+1)
        Window& win = g_windows[slot];              // &dword_67EB80[238*slot]
        i32* list = WindowChildList(slot);          // v7[6]
        for (int k = 0; k < win.objCount(); ++k) {  // v5 < objCount
            int childIdx = list[k];                 // *(v7[6] + v6)
            g_widgets[childIdx].disabledA() = (hide == 0); // *(v2 + 740*childIdx + 56)
        }
    }
    f.childrenVisible() = hide;                // *((DWORD*)v10+103) = a2
}

// gilde.exe 0x41be6c — VIBE_Form_RaiseWindows
void Form_RaiseWindows(int formId) {
    Form& f = g_forms[formId];
    int count = f.windowCount();               // *(int*)((char*)dword_676BE4 + 684*formId)
    // Original loops while v4+1 < count (so it visits indices 0..count-1).
    for (int i = 0; i < count; ++i)
        ZOrder_RaiseWindow(f.windowId(i));     // VIBE_ZOrder_RaiseWindow(676A64[i + 171*id])
}

// gilde.exe 0x41da04 — VIBE_Form_Destroy
void Form_Destroy(int formId) {
    Form& f = g_forms[formId];                 // v3 = &dword_676A60[171*formId]
    if (!f.valid())                            // v3[100]
        return;

    // Destroy each window's backing widgets, iterating windows back-to-front.
    for (int i = f.windowCount() - 1; i >= 0; --i) {   // v4 = count-1; v4 >= 0; --v4
        int slot = f.windowId(i);                       // dword_676A64[684*id + 4*v4]
        Window& win = g_windows[slot];                  // 238*slot
        if (win.enabled()) {                            // dword_67EE00[238*slot]
            // VIBE_Widget_DestroyByType(dword_67EDEC[238*slot] == win.backWidget(), v3, v4)
            int formHandle = static_cast<int>(reinterpret_cast<std::intptr_t>(&f));
            Widget_DestroyByType(win.backWidget(), formHandle, i);
        }
    }

    if (f.surfaceA())                          // v3[105]
        Surface_DestroySurface(f.surfaceA());
    if (f.surfaceB())                          // v3[106]
        Surface_DestroySurface(f.surfaceB());

    // Original: VIBE_Light_SetGrayColorThunk(0, 684) — clears the form record block.
    // Faithful effect on the data model: mark the form free (the in-use flag at +100).
    f.valid() = 0;
}

// gilde.exe 0x41d6ac — VIBE_Form_CenterChildWindows
void Form_CenterChildWindows(int formId) {
    Form& f = g_forms[formId];                 // v2 = (char*)dword_676A60 + 684*formId
    for (int i = 0; i < f.windowCount(); ++i) {        // v3 < *((int*)v2+97)
        int slot = f.windowId(i);                       // *((DWORD*)v4+1)
        Window& win = g_windows[slot];                  // &dword_67EB80[238*slot]
        int backIdx = win.backWidget();                 // v5[155]
        // Only top-level windows (backing widget's +44 group link == 0) get centered.
        if (g_widgets[backIdx].groupLink() == 0) {      // !*(dword_69FFB4 + 740*backIdx + 44)
            // *(v5+6)>>16 == word at +8 == w ; v5[2]>>16 == word at +10 == h.
            int cx = g_screenCenterX - (win.w() / 2);   // dword_69FFA4 - (winW)/2
            int cy = g_screenCenterY - (win.h() / 2);   // dword_69FFA0 - (winH)/2
            Widget_LayoutBounds(cx, cy, backIdx);       // VIBE_Widget_LayoutBounds
        }
    }
}

// gilde.exe 0x41d990 — VIBE_Form_PositionChildWindows
void Form_PositionChildWindows(int formId, char mode) {
    Form& f = g_forms[formId];                 // v4 = (char*)dword_676A60 + 684*formId
    for (int i = 0; i < f.windowCount(); ++i) {        // v5 < *((int*)v4+97)
        int slot = f.windowId(i);                       // *((DWORD*)v6+1)
        int backIdx = g_windows[slot].backWidget();     // dword_67EB80[238*slot + 155]
        if (g_widgets[backIdx].groupLink() == 0)        // !*(dword_69FFB4 + 740*backIdx + 44)
            Window_PositionCentered(slot, mode);        // VIBE_Window_PositionCentered(slot, mode)
    }
}

} // namespace guild::gui
