#include "gui/window.h"
#include "gui/object.h"

#include <cstdint>
#include <cstdlib>

namespace guild::gui {

Window  g_windows[kMaxWindows]; // dword_67EB80
int     g_windowCounter   = 0;  // dword_62D2F0
int     g_currentWindowId = 0;  // dword_62D230
Window* g_currentWindow   = nullptr; // dword_62D298
i32     g_defaultFont     = 0;  // dword_62D2B0
i32     g_defaultPalette  = 0;  // dword_62D2B8

// The original child-id list is a separately-allocated 0x600-byte buffer (<=384 int32
// ids) pointed to by Window +24 (objListPtr). On a 64-bit host the buffer pointer
// does not fit in the i32 field, so we keep the actual id storage in this parallel
// per-window array and use objListPtr() purely as the "allocated?" marker (0/1).
i32 g_windowChildren[kMaxWindows][kMaxChildren]; // d2:win_obj backing store

namespace {
// Window +44 text buffer (0x17D0) pointer — kept here for the same 64-bit reason as
// the child list; textBuffer() acts as the 0/non-0 marker.
void* g_windowText[kMaxWindows];
int SlotOf(const Window& w) { return static_cast<int>(&w - &g_windows[0]); }
i32* ChildList(Window& w)   { return g_windowChildren[SlotOf(w)]; }
} // namespace

i32* WindowChildList(int slot) { return g_windowChildren[slot]; }

void ResetWindows() {
    for (int s = 0; s < kMaxWindows; ++s) {
        if (g_windowText[s]) { std::free(g_windowText[s]); g_windowText[s] = nullptr; }
        g_windows[s] = Window{};
        for (auto& v : g_windowChildren[s]) v = 0;
    }
    g_windowCounter   = 0;
    g_currentWindowId = 0;
    g_currentWindow   = nullptr;
    g_defaultFont     = 0;
    g_defaultPalette  = 0;
}

// gilde.exe 0x419c38 — VIBE_Window_Create
int Window_Create(i16 x, i16 y, i16 w, i16 h, i32 flags) {
    // Rotating create counter (wraps at 96). dword_62D2F0.
    if (++g_windowCounter > kMaxWindows)
        g_windowCounter = 1;

    // Find a free slot. A slot is free when enabled()(+160 == dword_67EE00[238*idx])
    // is 0. The original special-cases slot 0 (if dword_67EE00[0]==0 -> use slot 0),
    // otherwise scans slots 1.. while the flag is set.
    int slot = -1;
    if (g_windows[0].enabled() != 0) {
        for (int i = 1; i < kMaxWindows; ++i) {
            if (g_windows[i].enabled() == 0) { slot = i; break; }
        }
    } else {
        slot = 0;
    }
    if (slot == -1)
        return -1;

    Window& win = g_windows[slot];

    // Margins (=4,4,4,4) and a couple of reset fields.
    win.margin0() = 4;
    win.margin1() = 4;
    win.margin2() = 4;
    win.margin3() = 4;
    win.at<i32>(912) = 0;        // v8[228]
    win.raw[608] = 4;            // *((BYTE*)v8 + 608)
    win.at<i32>(612) = 0;        // v8[153]

    // Geometry + flags.
    win.y()     = y;            // word[3] = a2 (y@dx)
    win.h()     = h;            // word[4] = a4 (h@bx)
    win.x()     = x;            // word[2] = a1 (x@ax)
    win.flags() = flags;        // dword[3]
    win.w()     = w;            // word[5] = a3 (w@cx)

    if (flags & kWinFlagPalette)
        win.at<i32>(632) = g_defaultPalette; // v8[158]

    // Allocate the backing widget (type '@'), wire it to this window.
    int wIdx = Widget_AllocSlot();
    Widget& bw = g_widgets[wIdx];
    bw.type()        = kTypeWindow;           // +24 = 0x40 '@'
    bw.x()           = win.x();               // +16
    bw.y()           = win.y();               // +18
    bw.w()           = win.h();               // +20 (original copies word[3]=h here)
    bw.h()           = win.w();               // +22 (and word[5]=w here)
    bw.id()          = slot + 1024;           // +8
    bw.ownerWindow() = slot;                  // +116
    bw.clipY0()      = win.y();               // +32 = word[3]
    bw.clipX1()      = win.h() + win.x();     // +30 = word[4]+word[2]
    bw.clipX0()      = win.x();               // +28 = word[2]
    bw.order()       = 2;                     // +26
    bw.groupLink()   = 0;                     // +44
    SetWidgetData(wIdx, &win);                // +12 -> Window* (pointer-valued, side table)
    bw.clipY1()      = win.w() + win.y();     // +34 = word[5]+word[3]

    win.stateFlag()  = 1;        // v8[156]
    win.at<i16>(616) = 2;        // *((WORD*)v8+308)
    win.at<i32>(904) = -1;       // v8[226]
    win.at<i32>(628) = -1;       // v8[157]
    win.enabled()    = 1;        // v8[160] -> slot now in use (dword_67EE00)
    win.at<i32>(940) = -1;       // v8[235]
    win.at<i32>(936) = -1;       // v8[234]
    win.at<i16>(636) = static_cast<i16>(g_defaultFont); // *((WORD*)v8+318) = dword_62D2B0
    win.at<i32>(944) = -1;       // v8[236]
    win.backWidget() = wIdx;     // v8[155]

    // Child object-id list (0x600 buffer) marker + reset of its trailing counters.
    // (Actual id storage lives in g_windowChildren[slot]; see ChildList note above.)
    win.objListPtr() = 1;        // v8[6] -> allocated buffer (marker)
    win.objCount()   = 0;
    for (int i = 0; i < kMaxChildren; ++i)
        g_windowChildren[slot][i] = 0;
    win.at<i32>(920) = 0;        // v8[230]
    win.at<i32>(924) = 0;        // v8[231]
    win.at<i32>(928) = 0;        // v8[232]
    win.at<i32>(932) = 0;        // v8[233]

    // Optional text buffer (0x17D0) when flag 0x10. (Surface/scrollbar/background
    // side effects are deferred to the renderer/sim clusters — see header note.)
    if (flags & kWinFlagTextBuffer) {
        g_windowText[slot] = std::calloc(kTextBufBytes, 1);
        win.textBuffer()   = 1; // marker (real buffer in g_windowText[slot])
    }

    g_currentWindowId = slot;        // dword_62D230
    g_currentWindow   = &g_windows[slot]; // dword_62D298
    return slot;
}

// gilde.exe 0x41a90c — VIBE_Window_Destroy (data-model core)
int Window_Destroy(int slot) {
    if (slot > kMaxWindows)
        return 0;
    Window& win = g_windows[slot];
    if (!win.enabled())
        return 0;

    // Free + recycle each child widget (original loops while objCount()!=0 calling
    // VIBE_Widget_DestroyByType on list[0]; we recycle the widget slots in order).
    i32* list = ChildList(win);
    while (win.objCount()) {
        if (list)
            Widget_FreeSlot(list[0]);
        // shift remaining ids down (DestroyByType compacts the list in the original)
        if (list) {
            for (int i = 1; i < win.objCount(); ++i)
                list[i - 1] = list[i];
        }
        --win.objCount();
    }

    if (win.objListPtr()) {
        win.objListPtr() = 0; // release the child-id buffer marker
    }
    if ((win.flags() & kWinFlagTextBuffer) && win.textBuffer()) {
        std::free(g_windowText[slot]);
        g_windowText[slot] = nullptr;
        win.textBuffer()   = 0;
    }

    int counter = g_windowCounter;
    win.at<i32>(944) = -1;   // v4[236]
    win.at<i32>(0)   = slot; // *v4 = a1 (recycled slot id stored in dword[0])
    win.enabled()    = 0;    // slot is now free again
    g_windowCounter  = counter - 1;
    return 1;
}

// gilde.exe 0x41ae10 — VIBE_Object_AddToWindow (child-list core)
int Object_AddToWindow(int winSlot, i16 y, i16 x, i32 gfxId) {
    Window& win = g_windows[winSlot]; // &dword_67EB80[238*a1]
    if (!win.enabled())               // !v5[160]
        return -1;
    if (win.objCount() >= kMaxChildren) { // *((__int16*)v5+14) >= 384
        // original: VIBE_ErrorLog_ReportMessage("Too many objects on window!")
        return -1;
    }

    // Allocate the child widget slot (in the original the underlying object index
    // comes from VIBE_GameLogic_Objects; the slot it occupies is a widget slot).
    int idx = Widget_AllocSlot();
    if (idx == -1)
        return -1;
    Widget& child = g_widgets[idx];

    // Store the child index into the window's id list at the current count.
    i32* list = ChildList(win);
    list[win.objCount()] = idx; // *(w[6] + 4*count) = idx

    // Copy parent clip/render pointers + geometry (mirrors the +52/+60 inheritance).
    Widget& backing = g_widgets[win.backWidget()];
    child.clipY0()     = 0;                 // +32
    child.clipX0()     = 0;                 // +28
    child.groupLink()  = win.backWidget();  // +44 (parent link)
    child.parentClip() = backing.parentClip();
    child.renderPtr()  = backing.renderPtr();
    child.ownerWindow()= winSlot;           // +116
    child.x()          = static_cast<i16>(win.x() + x);
    child.y()          = static_cast<i16>(win.y() + y);
    child.dataPtr()    = gfxId;             // +12

    ++win.objCount();   // bump child count

    // Grow content height: y + (widget height >>16 in the original; we keep height).
    i32 bottom = y + child.h();
    if (bottom > win.contentHeight())
        win.contentHeight() = bottom;

    return idx;
}

} // namespace guild::gui
