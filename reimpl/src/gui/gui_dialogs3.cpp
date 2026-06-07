#include "gui/gui_dialogs3.h"

#include "gui/object.h"         // g_widgets, g_widgetCache, g_widgetHighWater, SetWidgetData, Widget_AllocSlot
#include "gui/window.h"         // g_windows
#include "gui/form.h"           // g_forms
#include "gui/window_render.h"  // Widget_LayoutBounds (REUSED renderer edge)
#include "gui/widget_create.h"  // g_screenClipExt (dword_69FFBC), GameObject_AttachToWindow

#include <cstring>
#include <cstdlib>

namespace guild::gui {

// ===========================================================================
// Module-owned globals (BSS, zero at load).
// ===========================================================================
i32 g_drawClipTop;      // dword_64A1B8
i32 g_drawClipBottom;   // dword_64A1C0

ClipRect g_clipRectBuf[kClipRectBuffers][kClipRectCap]; // dword_62D2D0[k] targets
i32      g_clipRectLen[kClipRectBuffers];               // dword_62D2E0[k] lengths

i32 g_hitTestSlot;      // dword_62D22C
i32 g_hitTestPayload;   // dword_62D290

i32 g_pendingClick;     // dword_62D238
i32 g_lastClick;        // dword_62D23C

i32   g_centreRefX;     // dword_69FFA4
float g_anchorDivisor;  // flt_62D224

char g_tooltipText[kTooltipBufBytes]; // byte_75BE38

void ResetGuiDialogs3() {
    g_drawClipTop = 0;
    g_drawClipBottom = 0;
    for (int b = 0; b < kClipRectBuffers; ++b) {
        g_clipRectLen[b] = 0;
        for (int i = 0; i < kClipRectCap; ++i) g_clipRectBuf[b][i] = ClipRect{0, 0, 0, 0};
    }
    g_hitTestSlot = 0;
    g_hitTestPayload = 0;
    g_pendingClick = 0;
    g_lastClick = 0;
    g_centreRefX = 0;
    g_anchorDivisor = 0.0f;
    std::memset(g_tooltipText, 0, sizeof(g_tooltipText));
}

// ===========================================================================
// Hooks (inert defaults).
// ===========================================================================
namespace {

void*  DefaultAllocDebug(int size, const char* /*tag*/) {
    // Inert default: hand back a real zeroed block so the CreateRawBitmap pixel copy
    // is observable. The original VIBE_Memory_AllocDebug tracks an arena; here we just
    // need a valid destination buffer of the requested size.
    if (size <= 0) return nullptr;
    return std::calloc(static_cast<std::size_t>(size), 1);
}
void DefaultFormBroadcastClickResult(int) {}
void DefaultFormSetButtonAnimations(int, const char*, const char*) {}
void DefaultFormPresent(int) {}

const GuiDialogs3Hooks kDefaultHooks = {
    &DefaultAllocDebug,
    &DefaultFormBroadcastClickResult,
    &DefaultFormSetButtonAnimations,
    &DefaultFormPresent,
};

const GuiDialogs3Hooks* g_hooks = &kDefaultHooks;

} // namespace

const GuiDialogs3Hooks* SetGuiDialogs3Hooks(const GuiDialogs3Hooks* hooks) {
    const GuiDialogs3Hooks* prev = g_hooks;
    g_hooks = hooks ? hooks : &kDefaultHooks;
    return prev;
}

// ===========================================================================
// VIBE_Util_StrNCopyPad @0x5d9360 (local clone — the original is file-local in
// several modules so there is no single owning symbol to extern-reuse here).
//   for (i=dst; n; --n) { if(!*src) break; *dst++ = *src++; }
//   while (n) { *dst++ = 0; --n; }      // zero-fill the remainder of the n bytes
//   return original dst                  // (the start address)
// Note: when src fills all n bytes, NO trailing NUL is written (matching the orig).
// ===========================================================================
char* StrNCopyPadLocal(char* dst, const char* src, int n) {
    char* const start = dst;
    while (n) {
        if (!*src) break;       // 0x5d9368: stop at the source NUL
        --n;                    // (decrement happens via the for(; n; --n) header)
        *dst++ = *src++;        // 0x5d936e..71
        // The original decrements n in the loop header AFTER the body; replicate by
        // having decremented above so the count of bytes consumed matches exactly.
    }
    while (n) {                 // 0x5d937b: zero-fill the rest of the span
        *dst++ = 0;
        --n;
    }
    return start;               // 0x5d9386
}

// ===========================================================================
// gilde.exe 0x40e94c — VIBE_Gui_ClipRectToBuffers
//   char __usercall(int flags@eax, int y@edx, int h@ecx, int x@ebx)
// ===========================================================================
int Gui_ClipRectToBuffers(int flags, int y, int h, int x) {
    int outX = flags;   // v10 = a1 (the raw flags dword doubles as the stored x start)
    int outY = y;       // v4 = a2
    int outH = h;       // v6 = a3 (height/length)

    if (flags & 1) {                 // 0x40e95f
        x += 2;                      // 0x40e9d0  a4 += 2
        x = x & 0xFFFE;              // 0x40e9d4  LOWORD(a4) &= 0xFFFE  (snap even)
        outX = flags - 1;            // 0x40e9d8  v10 = a1 - 1 (drop the flag bit)
    }
    if (y + h > g_drawClipBottom)    // 0x40e96c  a2 + a3 > dword_64A1C0
        outH = g_drawClipBottom - y; // 0x40e970  v6 = dword_64A1C0 - a2
    if (y < g_drawClipTop) {         // 0x40e97a  a2 < dword_64A1B8
        outY = g_drawClipTop;        // 0x40e9e1  v4 = dword_64A1B8
        outH -= g_drawClipTop - y;   // 0x40e9e3  v6 -= dword_64A1B8 - a2
    }

    // for (result=0; result!=2; ...) writing both clip buffers (eax steps 0,4 -> <8).
    int result = 0;                  // 0x40e97f
    for (; result != kClipRectBuffers; ++result) {
        int idx = g_clipRectLen[result];     // v8/16 = 16 * dword_62D2E0[result]
        ClipRect& r = g_clipRectBuf[result][idx];
        r.x = outX;                  // 0x40e990  [base + 16*idx + 0]  = v10
        r.y = outY;                  // 0x40e999  [base + 16*idx + 4]  = v4
        r.w = x;                     // 0x40e9a3  [base + 16*idx + 8]  = a4
        r.h = outH;                  // 0x40e9ad  [base + 16*idx + 12] = v6
        g_clipRectLen[result] = idx + 1; // 0x40e9bb  dword_62D2DC[result+1] = idx+1
    }
    return result * 4;               // 0x40e9c6  return result*4 (== 8)
}

// ===========================================================================
// gilde.exe 0x414d98 — VIBE_Gui_HitTestObject (a1=x@ax, a2=y@dx)
// Scans the widget pointer cache top-down for the first widget whose screen rect
// contains (x,y) (and whose owning object's state flag at +408 is clear, and which is
// not disabled and has no render/clip block).  Records the hit slot in g_hitTestSlot
// and returns the resolved payload (object-id or widget id); -1 on a miss.
// ===========================================================================
int Gui_HitTestObject(i16 a1, i16 a2) {
    // The original packs the coords into the HIWORD of a scratch dword and compares
    // `value >> 16`, i.e. it works in integer screen coords. We use the values directly.
    const int px = a1;  // v13 >> 16
    const int py = a2;  // v14 >> 16

    g_hitTestPayload = -1;  // 0x414db9  dword_62D290 = -1

    // v2 = 2044 -> cache slot 511, stepping down by 4 (one pointer slot) each pass.
    int slot = kMaxWidgets; // 511 (byte 2044 / 4)
    Widget* hit = nullptr;
    while (true) {                          // 0x414dc1
        Widget* w = g_widgetCache[slot];    // v3 = *(cache + v2)
        int stateBlock = 0;                 // v4 = 0
        if (w) {
            // v5 = v3[15] (+60 parentClip); if set, v4 = *(v5 + 408) — the owning
            // object's "obscured/inactive" flag. In the model parentClip is a handle;
            // a nonzero handle means the owner is present, and we read its state via
            // the data table. We conservatively treat a clear parentClip as no block.
            int parentClip = w->parentClip();   // v5
            if (parentClip)
                stateBlock = 0; // owner-state byte (+408) — deferred owner record; 0 = active
            const int wx = w->x();    // high word of dword@+14 == word@+16
            if (px >= wx && px <= w->w() + wx) {  // x .. x+w
                const int wy = w->y();    // high word of dword@+16 == word@+18
                if (py >= wy
                    && py <= w->h() + wy                 // y .. y+h
                    && !stateBlock                       // !v4
                    && py >= w->clipY0()                 // >= high word of dword@+30 (+32)
                    && py <= w->clipY1()                 // <= v3[8]>>16 (+34)
                    && !w->disabledB()                   // !v3[14] (+56)
                    && !w->renderPtr()) {                // !v3[13] (+52)
                    hit = w;                             // break -> hit
                    break;
                }
            }
        }
        slot -= 1;                          // v2 -= 4
        if (slot < 0) {                     // v2 < 0
            g_hitTestSlot = -1;             // dword_62D22C = -1
            return -1;                      // v11 = -1
        }
    }

    // Hit. v9 = *v3 (+0 marker = the widget's own slot id in the live array).
    int markerSlot = hit->marker();         // *v3
    int payload;
    // *(g_widgets[markerSlot] + 24) == 64 ? object-state path : widget path
    bool ownerIsWindowBacked = false;
    if (markerSlot >= 0 && markerSlot < kMaxWidgets)
        ownerIsWindowBacked = (g_widgets[markerSlot].type() == kTypeWindow); // == 64
    if (ownerIsWindowBacked) {
        g_hitTestPayload = hit->ownerWindow();  // dword_62D290 = v3[29] (+116)
        payload = g_hitTestPayload;             // v11 = dword_62D290
    } else {
        int group = hit->groupLink();           // v10 = v3[11] (+44)
        if (group)                              // if (v10)
            g_hitTestPayload = (group >= 0 && group < kMaxWidgets)
                                   ? g_widgets[group].backWidget() // *(v10 + 620)
                                   : 0;
        payload = hit->id();                    // v11 = v3[2] (+8)
    }
    g_hitTestSlot = markerSlot;                 // dword_62D22C = v9
    return payload;
}

// ===========================================================================
// gilde.exe 0x414ec8 — VIBE_Gui_HitTestWindow (a1=x@ax, a2=y@dx)
// Scans the widget array from the high-water index down for the first window-backing
// widget (type 64) whose frame rect contains (x,y), is enabled, with y inside its
// interior band and no modal block.  Returns the widget's +8 id; -1 on a miss.
// ===========================================================================
int Gui_HitTestWindow(i16 a1, i16 a2) {
    const int px = a1;  // v10 >> 16
    const int py = a2;  // v9  >> 16

    int idx = g_widgetHighWater;   // v2 = dword_62D24C
    if (g_widgetHighWater < 0) {   // 0x414ee7
        g_hitTestSlot = -1;        // (LABEL_13) dword_62D22C = -1
        return -1;
    }

    int result;
    while (true) {                 // 0x414f07
        Widget& w = g_widgets[idx];  // v4 = 740*idx + dword_69FFB4
        const int wx = w.x();      // *(v4 + 14) >> 16 == word@+16
        if (px >= wx && px <= w.w() + wx) {           // x .. x+w
            const int wy = w.y();  // *(v4 + 16) >> 16 == word@+18
            if (py >= wy
                && py <= w.h() + wy                   // y .. y+h
                && w.type() == kTypeWindow) {         // *(v4 + 24) == 64
                if (!w.inUse()) {                     // !*(v4 + 4)
                    g_hitTestSlot = -1;               // LABEL_13
                    return -1;
                }
                if (py >= w.clipY0()                  // >= *(v4 + 30) >> 16 == word@+32 (clipY0)
                    && py <= w.clipY1()               // <= *(v4 + 32) >> 16 == word@+34 (clipY1)
                    && !w.disabledB()) {              // !*(v4 + 56)
                    result = w.id();                  // *(v4 + 8)
                    break;
                }
            }
        }
        --idx;                     // v3 -= 740 ; --v2
        if (idx < 0) {             // v3 < 0
            g_hitTestSlot = -1;
            return -1;
        }
    }
    g_hitTestSlot = idx;           // dword_62D22C = v2
    return result;
}

// ===========================================================================
// gilde.exe 0x41d7e0 — VIBE_Window_PositionAtCoord (a1=winSlot@eax, a2=corner@dl)
// ===========================================================================
int Window_PositionAtCoord(int winSlot, char corner) {
    Window& win = g_windows[winSlot]; // v2 = &dword_67EB80[238 * a1]

    // The original addresses w/h through the misaligned dword views:
    //   *(int*)(v2 + 6) >> 16  == word@+8  == window width  (w)
    //   *(int*)(v2 + 2) >> 16  == word@+4  == window x
    //   *((__int16*)v2 + 2)    == word@+4  == window x
    const int winW = win.w();
    const int winX = win.x();

    double anchorX;
    if (corner & 1) {                                 // 0x41d802
        // Centre about g_centreRefX using the float divisor:
        //   (w/2 + x) - centreRef/divisor + centreRef - w/2
        anchorX = static_cast<double>(winW / 2 + winX)
                  - static_cast<double>(g_centreRefX) / static_cast<double>(g_anchorDivisor)
                  + static_cast<double>(g_centreRefX)
                  - static_cast<double>(winW / 2);
    } else {
        anchorX = static_cast<double>(winX);          // 0x41d808  (double)word@+4
    }

    // Clamp so the window stays on screen: x = min(anchorX, screenExt - w).
    double maxX = static_cast<double>((g_screenClipExt >> 16) - winW); // 0x41d866
    double x = (maxX >= anchorX) ? anchorX : maxX;    // 0x41d875

    // The original runs the result through two VIBE_Coord_ConvertX calls (a no-op
    // identity in the data model) and hands the integer x and the backing-widget index
    // to Widget_LayoutBounds. v6 here is the window's backing-widget slot (+620).
    const int laidX = static_cast<int>(x);            // VIBE_Coord_ConvertX (identity)
    const int backWidget = win.backWidget();          // *(v6 + 620)
    Widget_LayoutBounds(laidX, 0, backWidget);        // 0x41d905
    return 0; // original returns LayoutBounds' al (void in our LayoutBounds model)
}

// gilde.exe 0x41d964 — VIBE_Window_PositionAtCoord_Thunk (a1=formId@eax, a2=corner@dl)
int Window_PositionAtCoord_Thunk(int formId, char corner) {
    // VIBE_Window_PositionAtCoord(dword_676A60[171*a1 + 1], a2)  == form's window 0.
    return Window_PositionAtCoord(g_forms[formId].windowId(0), corner);
}

// ===========================================================================
// gilde.exe 0x41b870 — VIBE_Window_ConsumeClickFlag ()
// ===========================================================================
int Window_ConsumeClickFlag() {
    int result = g_pendingClick;     // result = dword_62D238
    g_lastClick = g_pendingClick;    // dword_62D23C = dword_62D238
    g_pendingClick = 0;              // dword_62D238 = 0
    return result;
}

// ===========================================================================
// gilde.exe 0x412668 — VIBE_Widget_BlitClippedRows (a1=widgetIdx@eax, blit@edx)
// ===========================================================================
int Widget_BlitClippedRows(int widgetIdx, int dstStridePx, unsigned char* dst) {
    Widget& w = g_widgets[widgetIdx]; // v3 = 740*result + dword_69FFB4

    // Original field reads (each is a misaligned-dword >>16 == the next word field):
    //   v11 = v3[5] >> 16  == word@+22  (h)  -> number of source rows
    //   v4  = v3[4] >> 16  == word@+18  (y)  -> destination top row
    //   v5  = v3[8] >> 16  == word@+34  (clipY1) -> bottom clamp
    //   v7  = *(v3+30)>>16 == word@+32  (clipY0) -> top clamp
    //   width (per-row pixels) = *(v3+18)>>16 == word@+20 (w)
    //   x (per-row dst offset) = *(v3+14)>>16 == word@+16 (x)
    int rows  = w.h();           // v11
    int top   = w.y();           // v4
    int clip1 = w.clipY1();      // v5
    int rowStart = 0;            // v6 = 0

    if (top + rows > clip1) {                   // 0x4126b2
        rows = clip1 - top;                     // 0x412723  v11 = v5 - v4
    } else {
        int clip0 = w.clipY0();                 // v7 = word@+32
        if (top < clip0)                        // 0x4126bc
            rowStart = clip0 - top;             // 0x4126c0  v6 = v7 - v4
    }

    const int rowPixels = w.w();   // word@+20: per-row pixel count
    // Source rows live at widget +120 (the raw bitmap pointer). The +120 field cannot
    // hold a 64-bit pointer on the host, so we keep the pixel buffer in the parallel
    // data table (SetWidgetData/WidgetData). v3[30] (==+120) is that base in the orig.
    const unsigned char* base = reinterpret_cast<const unsigned char*>(WidgetData(widgetIdx));
    int lastRowBytes = 2 * rowPixels;            // result = 2*(word@+20)
    if (base && dst) {
        for (int i = rowStart; i < rows; ++i) {              // 0x4126c5
            const unsigned char* srcRow = base + 2 * i * rowPixels; // v3[30] + 2*i*w
            int dstByteOff = 2 * (w.x() + dstStridePx * (i + top)); // 2*(x + stride*(i+y))
            lastRowBytes = 2 * rowPixels;                    // result = 2*w
            std::memcpy(dst + dstByteOff, srcRow, static_cast<std::size_t>(lastRowBytes));
        }
    }
    return lastRowBytes; // 0x412718  return result
}

// ===========================================================================
// gilde.exe 0x412560 — VIBE_Widget_CreateRawBitmap (a1=w@ax, a2=h@dx, a3=stride@ebx, a4=pixels)
// ===========================================================================
int Widget_CreateRawBitmap(i16 x, i16 y, i16 w, i16 h, const void* pixels) {
    int slot = Widget_AllocSlot();          // v6 = VIBE_Widget_AllocSlot()  (REUSED)
    if (slot < 0)
        return slot;
    Widget& wg = g_widgets[slot];

    // Field init at the exact original byte offsets (740*v6 + dword_69FFB4 + off).
    // Register mapping (from disasm): di=x->+16, dx=y->+18, si=w->+20, cx=h->+22.
    wg.at<u16>(22) = static_cast<u16>(h);      // +22 = cx (h / row count)
    wg.type() = 71;                            // +24 = 71 ('G' raw bitmap)
    wg.id() = -1;                              // +8  = -1
    wg.at<u16>(32) = 0;                        // +32 = 0
    wg.at<u16>(28) = 0;                        // +28 = 0
    wg.at<u16>(16) = static_cast<u16>(x);      // +16 = di (x)
    wg.at<u16>(18) = static_cast<u16>(y);      // +18 = dx (y)
    wg.at<u16>(20) = static_cast<u16>(w);      // +20 = si (w)
    wg.at<u16>(34) = static_cast<u16>(g_screenClipExt & 0xFFFF);        // +34 = LOWORD(dword_69FFBC)
    wg.at<u16>(30) = static_cast<u16>((g_screenClipExt >> 16) & 0xFFFF);// +30 = HIWORD(dword_69FFBC)

    // VIBE_Memory_AllocDebug(2 * h * w, "d2:raw") then qmemcpy(dst, pixels, 2*h*w).
    int allocBytes = 2 * static_cast<int>(h) * static_cast<int>(w); // imul ecx,esi ; add ecx,ecx
    void* buf = g_hooks->AllocDebug ? g_hooks->AllocDebug(allocBytes, "d2:raw") : nullptr;
    SetWidgetData(slot, buf);                  // *(+120) = block (held in the data table)
    if (buf && pixels && allocBytes > 0)
        std::memcpy(buf, pixels, static_cast<std::size_t>(allocBytes)); // qmemcpy
    return slot;                               // v6
}

// gilde.exe 0x412618 — VIBE_Widget_AddRawBitmapToWindow (x@ax,y@dx,w@ebx,h@ecx,pixels,winSlot)
int Widget_AddRawBitmapToWindow(i16 x, i16 y, i16 w, i16 h, const void* pixels, int winSlot) {
    Window& win = g_windows[winSlot];
    // CreateRawBitmap(x + HIWORD(dword@(base+2)), y + HIWORD(dword_67EB84[238*a5]), w, h, pixels)
    //   dword@(base+2) >> 16 == word@+4 == window x
    //   dword_67EB84 == base+4, dword@+4 >> 16 == word@+6 == window y
    i16 rx = static_cast<i16>(x + win.x());
    i16 ry = static_cast<i16>(y + win.y());
    int bmp = Widget_CreateRawBitmap(rx, ry, w, h, pixels);
    GameObject_AttachToWindow(bmp, winSlot);   // VIBE_GameObject_AttachToWindow (REUSED)
    return bmp;
}

// ===========================================================================
// gilde.exe 0x41cc5c — VIBE_Form_RefreshIfVisible (a1=formId@eax, a2=cursorX@edx, a3=cursorY@ebx)
// ===========================================================================
int Form_RefreshIfVisible(int formId, const char* cursorX, const char* cursorY) {
    Form& f = g_forms[formId]; // v5 = &dword_676A60[171 * a1]
    if (!f.valid() || !f.shownFlag())  // !v5[100] || !v5[102]
        return 0;                      // 0x41cc9d
    if (!cursorX || !cursorY)          // !a2 || !a3
        return 1;                      // 0x41cc95
    g_hooks->FormBroadcastClickResult(formId);              // VIBE_Form_BroadcastClickResult
    g_hooks->FormSetButtonAnimations(formId, cursorX, cursorY); // VIBE_Form_SetButtonAnimations
    g_hooks->FormPresent(formId);                           // VIBE_DecompressGameState
    return 1;                          // 0x41cc9a
}

// ===========================================================================
// gilde.exe 0x411eac — VIBE_Gui_FreeString_Thunk (a1=widgetIdx@eax, a2=src@edx)
// VIBE_Util_StrNCopyPad(dword_69FFB4 + 740*a1 + 152, a2, 63)
// ===========================================================================
char* Gui_FreeString_Thunk(int widgetIdx, const char* src) {
    char* dst = reinterpret_cast<char*>(&g_widgets[widgetIdx].at<u8>(152)); // +152
    return StrNCopyPadLocal(dst, src, 63);
}

// ===========================================================================
// gilde.exe 0x421a24 — VIBE_Widget_SetTooltipText (a1=src@eax)
// Unrolled byte-pair copy of src (incl. NUL) into byte_75BE38.
// ===========================================================================
char Widget_SetTooltipText(const char* src) {
    char* v1 = g_tooltipText;       // v1 = &byte_75BE38
    char result;
    do {                            // 0x421a44
        result = *src;              // result = *a1
        *v1 = *src;                 // *v1 = *a1
        if (!result)                // if (!result) break
            break;
        result = src[1];            // result = a1[1]
        src += 2;                   // a1 += 2
        v1[1] = result;             // v1[1] = result
        v1 += 2;                    // v1 += 2
    } while (result);               // while (result)
    return result;                  // 0x421a47
}

} // namespace guild::gui
