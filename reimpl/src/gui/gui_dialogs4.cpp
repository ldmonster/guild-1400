#include "gui/gui_dialogs4.h"

#include "gui/object.h"         // g_widgets, Widget_AllocSlot
#include "gui/window.h"         // g_windows, g_currentWindowId, g_defaultFont
#include "gui/form.h"           // g_forms, g_currentFormId
#include "gui/widget_create.h"  // g_defaultCtrlH, g_screenClipW, g_screenClipExt, Object_AddTextLabel, Object_AddToWindow, Property_Get
#include "gui/window_render.h"  // Widget_LayoutBounds (REAL sibling — reused by the LayoutBounds bridge)
#include "gui/window_mgmt.h"    // Window_Scroll (REAL sibling @0x41a024 — wired by HoverUpdate, Rule 13)
#include "gui/gui_dialogs3.h"   // g_tooltipText (byte_75BE38) — owned by gui_dialogs3
#include "util/coord.h"         // util::ConvertX (real sibling for coordConvertX default)
#include "world/money_format.h" // world::MoneyFormatWithSeparators (REAL sibling — integration anchor)

#include <cstdint>
#include <cstring>
#include <string>

namespace guild::gui {

// ===========================================================================
// Module-owned interaction-state globals.
// ===========================================================================
std::uintptr_t g_focusWidget;
i32 g_focusFlag, g_dragArmed, g_caretWidget, g_caretBaseW;
i32 g_focusIndex, g_caretPenX, g_caretPenY, g_focusValue, g_focusValuePrev, g_focusValueAcc;
i32 g_groupSlots[18];
i32 g_d4_dragOriginX, g_d4_dragOriginY, g_d4_savedClampY0, g_d4_savedClampY1, g_caretCreateBase;
i32 g_hoverPrev, g_hoverLast, g_hoverSlot, g_hoverX, g_hoverY, g_hoverEngaged, g_hoverTick;
i32 g_thumbStateId, g_scrollThumbX, g_scrollThumbY;
i32 g_hitTestSlot4;
i32 g_hoverScrollWin, g_scrollWheelWin, g_stateFinalizeReq, g_wheelUp, g_wheelDn;
i32 g_pendingMouseX;
u8  g_cursorPtBuf[8];
u8  g_pendingKey;
u8  g_shiftHeldL, g_shiftHeldR;
// g_mouseDown (dword_672220) is owned by input.cpp; referenced via the extern in our header.
i32 g_pendingMouseUp, g_mouseWheelUp, g_mouseRelease, g_mouseDownHover, g_frameTick;
unsigned char g_wndDeactivated;
i32 g_wndAudioActive, g_renderRetryFlag, g_frameBlob2, g_audioPausedFlag;
std::uintptr_t g_frameBlob;
std::uintptr_t g_renderContext;  // dword_62D268
u8  g_renderPhaseFlag;           // byte_62D25C
std::uintptr_t g_tileAnimRec;    // dword_62D21C
std::uintptr_t g_animTableBase;  // dword_62D204
i32 g_drawClipLeft, g_drawClipRight; // dword_64A1B4 / dword_64A1BC

// ===========================================================================
// Raw-memory helpers: the original addresses the focused-widget "data record"
// (the type-'A'/'E' scene payload owned by the renderer cluster) and the widget
// pool by absolute pointer arithmetic. We translate those accesses 1:1 by
// treating the recovered handle as an integer pointer into caller-supplied
// storage, exactly as the live process does.
// ===========================================================================
namespace {

// The original addresses these records by a 32-bit pointer. On a 64-bit test host
// a real record address does not fit in 32 bits, so the handle is stored full-width
// (std::uintptr_t) and NOT truncated here — the byte arithmetic is identical.
template <typename T> T& Mem(std::uintptr_t base, int off) {
    return *reinterpret_cast<T*>(reinterpret_cast<char*>(base) + off);
}
// Unaligned by-value read, byte-identical to the original x86 unaligned `mov`/read.
// Some record reads are at unaligned offsets (e.g. +2/+6/+14/+26) and immediately
// `>> 16`; binding a misaligned `int&` is C++ UB (and faults on strict-alignment
// hosts), so load through memcpy. (Read-only; lvalue uses keep Mem<T>.)
template <typename T> T MemR(std::uintptr_t base, int off) {
    T v;
    std::memcpy(&v, reinterpret_cast<char*>(base) + off, sizeof(T));
    return v;
}
inline std::uintptr_t Ptr(std::uintptr_t base, int off) {
    return reinterpret_cast<std::uintptr_t>(reinterpret_cast<char*>(base) + off);
}

// Widget pool element by slot, addressed as the original "dword_69FFB4 + 740*idx".
inline Widget& W(int slot) { return g_widgets[slot]; }

// The widget's data-record pointer (original *(pool + 740*idx + 12)). Stored host
// pointer-width in the parallel g_widgetData table.
inline std::uintptr_t WidgetDataPtr(int slot) {
    return reinterpret_cast<std::uintptr_t>(WidgetData(slot));
}

} // namespace

// ===========================================================================
// Hooks (inert defaults).
// ===========================================================================
namespace {

double DefCoordConvertX(double x) { return util::ConvertX(x); }  // real sibling
double DefLookupPrice(int, unsigned char) { return 0.0; }
int  DefStateUpdate(int) { return 0; }
int  DefStateFinalize(int) { return 0; }
int  DefStateGetCurrent(int, int, int, int) { return 0; }
std::uintptr_t DefCoordTransform(int, unsigned) { return 0; }
void DefCoordPush(int, int, int, int) {}
int  DefAnimBasic(int, int, int, int, int) { return 0; }
int  DefAnimApply(int, int, int, int, const char*, int) { return 0; }
void DefAnimStateUpdate(int, void*, unsigned) {}
void DefSurfaceColorFill(int, int, int, int, int) {}
void DefSurfaceRectOutline(int, int, int, int, int, int, int, int) {}
int  DefSurfaceCreate(void*, int, int) { return 0; }
void DefLightSetGray(int, int) {}
int  DefResultHandler(int, int, int, int, int, int, int, int) { return 0; }
int  DefDecompressStateBlob(int, int) { return 0; }
void DefDecompressFinalize(int) {}
void DefRenderDrawTexturedQuad(int, int, int, int, float, float, float) {}
void DefRenderWithSurfaceContext(int) {}
int  DefEntityAnimationUpdate(int, int, int, int, void*) { return 0; }
int  DefResultFinalize(int, int, int, int, int, int, int, int, int) { return 0; }
i16  DefPropertyGet(const char* t, int f) { return Property_Get(t, f); } // real sibling
int  DefWidgetCreateObjectThunk(int, int) { return -1; }
int  DefSliderComputeStep(int) { return 0; }
void DefScrollbarDragThumb() {}
void DefSliderUpdateFromMouse(int, int) {}
int  DefInputCharToScancode(int, int, int, int) { return 0; }
int  DefUtilParseInt(const char*) { return 0; }
void DefInputClearMouseButtons(int) {}
void DefInputSetIconTextById(int, int) {}
void DefAudioReacquireDigital(unsigned long, void*, unsigned) {}
void DefAudioReacquireAll(int, unsigned) {}
void DefAudioReleaseAll() {}
void DefInputAcquireMouse(int, int) {}
int  DefRenderIsSurfaceLost() { return 1; }  // 1 => the busy-wait loops terminate at once
int  DefRenderRetTrue() { return 1; }
void DefGameObjectDispatch() {}
void DefGameLogicInteractions(int*) {}
void DefRenderPresentFrame(void*) {}
void DefValidateRect(void*) {}
void DefPostQuitMessage(int) {}
long DefDefWindowProc(void*, unsigned, unsigned long, long) { return 0; }
void DefSetWindowPos(void*, void*, int, int, int, int, unsigned) {}
int  DefGameTickFinalize(i16, i16, const char*) { return 0; } // inert: "no form loaded"
int  DefWidgetDestroyByType(int, int, int) { return 0; }

const GuiDialogs4Hooks kDefaultHooks = {
    &DefCoordConvertX, &DefLookupPrice,
    &DefStateUpdate, &DefStateFinalize, &DefStateGetCurrent, &DefCoordTransform,
    &DefCoordPush, &DefAnimBasic, &DefAnimApply, &DefAnimStateUpdate,
    &DefSurfaceColorFill, &DefSurfaceRectOutline, &DefSurfaceCreate, &DefLightSetGray,
    &DefResultHandler, &DefDecompressStateBlob, &DefDecompressFinalize,
    &DefRenderDrawTexturedQuad, &DefRenderWithSurfaceContext, &DefEntityAnimationUpdate,
    &DefResultFinalize, &DefPropertyGet, &DefWidgetCreateObjectThunk, &DefSliderComputeStep,
    &DefScrollbarDragThumb, &DefSliderUpdateFromMouse, &DefInputCharToScancode,
    &DefUtilParseInt, &DefInputClearMouseButtons, &DefInputSetIconTextById,
    &DefAudioReacquireDigital, &DefAudioReacquireAll, &DefAudioReleaseAll,
    &DefInputAcquireMouse, &DefRenderIsSurfaceLost, &DefRenderRetTrue,
    &DefGameObjectDispatch, &DefGameLogicInteractions, &DefRenderPresentFrame,
    &DefValidateRect, &DefPostQuitMessage, &DefDefWindowProc, &DefSetWindowPos,
    &DefGameTickFinalize, &DefWidgetDestroyByType,
};

const GuiDialogs4Hooks* g_hooks = &kDefaultHooks;

} // namespace

// ===========================================================================
// Bridges.
// ===========================================================================
int Widget_LayoutBoundsBridge(int x, int y, int widgetIdx) {
    Widget_LayoutBounds(x, y, widgetIdx);  // REAL sibling (window_render.cpp, returns void)
    return widgetIdx;
}
int Widget_DestroyByTypeBridge(int widget, int a, int b) {
    return g_hooks->widgetDestroyByType(widget, a, b);
}
int GameTickFinalizeBridge(i16 a, i16 b, const char* name) {
    return g_hooks->gameTickFinalize(a, b, name);
}

const GuiDialogs4Hooks* SetGuiDialogs4Hooks(const GuiDialogs4Hooks* hooks) {
    const GuiDialogs4Hooks* prev = g_hooks;
    g_hooks = hooks ? hooks : &kDefaultHooks;
    return prev;
}
const GuiDialogs4Hooks* GuiDialogs4Hooks_Default() { return &kDefaultHooks; }

void ResetGuiDialogs4() {
    g_focusWidget = 0;
    g_focusFlag = g_dragArmed = 0;
    g_caretWidget = -1; g_caretBaseW = -1;
    g_focusIndex = -1; g_caretPenX = g_caretPenY = 0;
    g_focusValue = g_focusValuePrev = g_focusValueAcc = 0;
    for (int i = 0; i < 18; ++i) g_groupSlots[i] = -1;
    g_d4_dragOriginX = g_d4_dragOriginY = g_d4_savedClampY0 = g_d4_savedClampY1 = 0;
    g_caretCreateBase = 0;
    g_hoverPrev = g_hoverLast = -1; g_hoverSlot = -1;
    g_hoverX = g_hoverY = g_hoverEngaged = g_hoverTick = 0;
    g_thumbStateId = 0; g_scrollThumbX = g_scrollThumbY = 0;
    g_hitTestSlot4 = -1;
    g_hoverScrollWin = g_scrollWheelWin = g_stateFinalizeReq = -1;
    g_wheelUp = g_wheelDn = 0;
    g_pendingMouseX = 0;
    for (int i = 0; i < 8; ++i) g_cursorPtBuf[i] = 0;
    g_pendingKey = 0;
    g_shiftHeldL = g_shiftHeldR = 0;
    g_pendingMouseUp = g_mouseDown = g_mouseWheelUp = 0;
    g_mouseRelease = g_mouseDownHover = g_frameTick = 0;
    g_wndDeactivated = 0;
    g_wndAudioActive = g_renderRetryFlag = g_frameBlob2 = g_audioPausedFlag = 0;
    g_frameBlob = 0;
    g_renderContext = 0;
    g_renderPhaseFlag = 0;
    g_tileAnimRec = 0;
    g_animTableBase = 0;
    g_drawClipLeft = g_drawClipRight = 0;
    g_hooks = &kDefaultHooks;
}

// ===========================================================================
// 0x421370 — VIBE_Widget_SetFocus
// Move keyboard focus to the data record owned by widget slot `widgetSlot`.
// ===========================================================================
int Widget_SetFocus(int widgetSlot) {
    const GuiDialogs4Hooks* h = g_hooks;
    int focusIdx = g_focusIndex;                       // v1 = dword_75BEBC

    // *(dword_69FFB4 + 740*a1 + 12) — the data-record pointer. The model stores the
    // pointer-valued +12 slot in the parallel g_widgetData table (host pointer width).
    std::uintptr_t dataRec = reinterpret_cast<std::uintptr_t>(WidgetData(widgetSlot));
    int ret = static_cast<unsigned char>(widgetSlot);  // LOBYTE(Object_Thunk)

    if (dataRec != g_focusWidget) {                    // 0x4213a6
        if (g_focusWidget)                             // clear old caret highlight
            W(Mem<i32>(g_focusWidget, 304)).at<i32>(40) = 0;   // +40 = 0
        W(Mem<i32>(dataRec, 304)).at<i32>(40) = 1;     // new record highlighted
        g_focusWidget = dataRec;

        i32 group = W(Mem<i32>(dataRec, 304)).at<i32>(44);     // *(v5 + 44)
        if (group) {                                   // walk the group's child id list
            int n = MemR<i32>(group, 26) >> 16;         // *(group+26)>>16
            i32 ids = Mem<i32>(group, 24);             // *(group+24)
            for (int k = 0; k < n; ++k)
                if (Mem<i32>(dataRec, 304) == Mem<i32>(ids + 4 * k, 0))
                    focusIdx = k;
        }

        // Auto-default a flag-0x40 record with no value to -1.
        if ((Mem<u8>(g_focusWidget, 38) & 0x40) != 0
            && !Mem<i32>(g_focusWidget, 24)
            && !Mem<i32>(g_focusWidget, 28)) {
            int active = 0;
            for (int i = 0; i != 18; i += 3)
                if (g_groupSlots[i] != -1) ++active;
            if (!active && !Mem<i32>(g_focusWidget, 296))
                Mem<i32>(g_focusWidget, 296) = -1;
        }

        ret = static_cast<unsigned char>(g_focusWidget);
        if ((Mem<u8>(g_focusWidget, 38) & 2) != 0 && Mem<i32>(g_focusWidget, 24))
            Mem<i32>(g_focusWidget, 296) = 1;

        u8 flags = Mem<u8>(g_focusWidget, 38);
        g_focusIndex = focusIdx;
        if ((flags & 1) != 0) {                        // edit field: place caret
            int w = h->propertyGet(
                        reinterpret_cast<const char*>(Ptr(g_focusWidget, 40)),
                        Mem<i32>(g_focusWidget, 308));
            g_caretPenX = Mem<i32>(g_focusWidget, 4) + w;  // dword_75BEC8 = *(v13+4)+v12 (best-effort)
            g_caretPenY = Mem<i32>(g_focusWidget, 8);
            i16 caretX = static_cast<i16>(g_caretPenY - 7);
            // 0x421532/0x421559: both calls take eax = dword_75BEC4>>16 (caretPenY high
            // word), dx = (i16)caretPenY - 7.  g_caretPenX is NOT passed (Hex-Rays
            // mislabeled eax as dword_75BEC8 — the disasm reads dword_75BEC4+2).
            int caretYHi = g_caretPenY >> 16;
            if (g_caretWidget == -1) {
                ret = h->widgetCreateObjectThunk(caretYHi, caretX);
                g_caretWidget = ret;
            } else {
                ret = static_cast<unsigned char>(
                    Widget_LayoutBoundsBridge(caretYHi, caretX, g_caretWidget));
            }
        } else if (g_caretWidget != -1) {
            ret = static_cast<unsigned char>(
                Widget_DestroyByTypeBridge(g_caretWidget,
                                           static_cast<int>(g_focusWidget), flags));
            g_caretWidget = -1;
        }
    }
    return ret;
}

// ===========================================================================
// 0x41e1c4 — VIBE_Form_SetObjectValueOrText
// ===========================================================================
int Form_SetObjectValueOrText(int childIdx, int winSlotInForm, const char* text,
                              int valA, int valB, int valC) {
    // v6 = &dword_67EB80[238 * 676A64[171*form + winSlotInForm]]  (window record)
    int winSlot = g_forms[g_currentFormId].windowId(winSlotInForm);
    Window& win = g_windows[winSlot];
    if (childIdx > (win.objCount()))                   // a1 > *(v6+26)>>16
        return static_cast<unsigned char>(winSlot);

    // v6 = dword_69FFB4 + 740 * *(window.objListPtr + 4*a1)  (the child widget)
    int childSlot = WindowChildList(winSlot)[childIdx];
    Widget& w = W(childSlot);

    if (w.type() == kTypeAnim) {                       // == 65 'A'
        std::uintptr_t rec = reinterpret_cast<std::uintptr_t>(WidgetData(childSlot)); // v7 = v6[3]
        u8 fl = Mem<u8>(rec, 38);
        if ((fl & 1) != 0) {                            // text record: copy widechar-ish
            int ret = 0;
            if (std::strlen(text) <= 0xFE) {
                char* dst = reinterpret_cast<char*>(Ptr(rec, 40));
                const char* src = text;
                // verbatim 2-byte-stride copy (the original advances dst/src by 2)
                while (true) {
                    ret = static_cast<unsigned char>(*src);
                    dst[0] = *src;
                    if (!*src) break;
                    ret = static_cast<unsigned char>(src[1]);
                    src += 2;
                    dst[1] = static_cast<char>(ret);
                    dst += 2;
                    if (!ret) break;
                }
            }
            return ret;
        }
        if ((fl & 2) != 0) {                            // numeric edit record
            Mem<i32>(rec, 28) = static_cast<i32>(reinterpret_cast<std::intptr_t>(text)); // store 32-bit ptr (verbatim)
            Mem<i32>(rec, 24) = valA;
            Mem<i32>(rec, 296) = valB;
            return static_cast<unsigned char>(valB);
        }
    }
    // LABEL_5: not a flag-1/flag-2 anim record.
    if (w.type() == kTypeEdit) {                        // == 69 'E'
        w.at<i32>(124) = static_cast<i32>(reinterpret_cast<std::intptr_t>(text)); // v6[31] (32-bit ptr)
        w.at<i32>(128) = valA;                          // v6[32]
        i16 efl = w.at<i16>(132);                        // *(v6+132)
        w.at<i32>(120) = valB;                           // v6[30]
        if ((efl & 0x10) != 0)
            w.at<i32>(140) = valC;                       // v6[35]
    } else if (w.at<i32>(68) || w.at<i32>(72)) {         // button flags
        w.at<i32>(36) = static_cast<unsigned char>(reinterpret_cast<std::intptr_t>(text)); // v6[9]
        w.at<i32>(40) = static_cast<unsigned char>(reinterpret_cast<std::intptr_t>(text)); // v6[10]
    }
    return static_cast<unsigned char>(childSlot);
}

// ===========================================================================
// 0x41cae0 — VIBE_Form_MarkDirtyAndRender
// ===========================================================================
int Form_MarkDirtyAndRender(i16 a, i16 b, const char* name) {
    const GuiDialogs4Hooks* h = g_hooks;
    int formId = GameTickFinalizeBridge(b, a, name);   // VIBE_GameTick_Finalize
    if (!formId) return 0;

    Form& f = g_forms[formId];
    int winCount = f.windowCount();                    // v21[97]
    for (int wi = 0; wi < winCount; ++wi) {
        int winSlot = f.windowId(wi);                  // v6[1]
        Window& win = g_windows[winSlot];
        W(win.backWidget()).at<i32>(52) = 1;           // backing widget dirty
        int n = win.objCount();                        // *(v7+26)>>16
        const i32* ids = WindowChildList(winSlot);      // *(window+24) child id list
        for (int k = 0; k < n; ++k)
            W(ids[k]).at<i32>(52) = 1;
        winCount = f.windowCount();                    // re-read (original v12 = v21[97])
    }
    f.shownFlag() = 1;                                  // v21[102] = 1

    // Allocate the two render surfaces (hook leaf).
    char scratch[16];
    h->lightSetGray(0, 64);
    f.surfaceA() = h->surfaceCreate(scratch, 3, 0);    // v21[105]
    h->lightSetGray(0, 64);
    f.surfaceB() = h->surfaceCreate(scratch, 3, 0);    // v21[106]
    return formId;
}

// ===========================================================================
// 0x420360 — VIBE_Widget_HandleKeyInput
// Apply the pending keystroke (byte_67225C) to the focused edit field. Tab
// (0x0F) walks the focused widget's owning group forward (no shift) or backward
// (shift held) to the next type-'A' editable child and transfers focus; enter
// (0x1C) clears focus; backspace (0x0E) trims; printable/numeric keys insert.
// Reconstructed 1:1 from the decompile + disasm 0x420360-0x42088b.
// ===========================================================================
int Widget_HandleKeyInput() {
    const GuiDialogs4Hooks* h = g_hooks;
    int v1 = g_pendingMouseX >> 16;                    // v1 = dword_672210>>16
    int v20 = g_pendingMouseX >> 16;                   // v20 = dword_672210>>16 (drag-accum base)
    if (!g_focusWidget || !g_pendingKey)               // !dword_62D328 || !byte_67225C
        return v1;

    unsigned key = g_pendingKey;
    v1 = static_cast<unsigned char>(key);

    // The dispatch below mirrors the original's nested range ladder. Three keys
    // are handled specially (enter/backspace/tab); 0x02..0x0B (flag-2 numeric) and
    // the default printable path fall through to the flag-2 / insert block.
    bool doInsertBlock = false;   // reaches LABEL_20 (the strlen/cap insert path)
    bool returnedEarly = false;

    if (key == kKeyEnter) {                            // 0x1C — clear focus, early return
        int slot = Mem<i32>(g_focusWidget, 304);       // 740 * *(record+304)
        v1 = static_cast<unsigned char>(0);            // LOBYTE(v1) = dword_69FFB4 (low byte; inert)
        g_focusWidget = 0;                             // dword_62D328 = 0
        g_focusFlag = 0;                               // dword_62D348 = 0
        W(slot).at<i32>(40) = 0;                        // *(slot+40) = 0
        return v1;                                      // 0x42073d return
    } else if (key == kKeyBackspace) {                 // 0x0E — delete last char
        v1 = 0;
        std::size_t v9 = std::strlen(reinterpret_cast<char*>(Ptr(g_focusWidget, 40))) + 1;
        if (static_cast<int>(v9 - 1) > 0) {
            v1 = static_cast<unsigned char>(g_focusWidget);
            *reinterpret_cast<char*>(Ptr(g_focusWidget, 40) + (v9 - 1) - 1) = 0; // +39+v9-1
        }
        g_pendingKey = 0;                              // byte_67225C = 0
        // falls through to LABEL_7
    } else if (key == kKeyTab) {                       // 0x0F — group navigation
        int v0 = reinterpret_cast<std::intptr_t>(nullptr); (void)v0; // ebp == dword_69FFB4 (pool base)
        int focusSlot = Mem<i32>(g_focusWidget, 304);  // 740 * *(record+304)
        i32 group = W(focusSlot).at<i32>(44);          // v10 = *(slot+44) — owning group widget
        int v11 = 0;

        if (g_shiftHeldL || g_shiftHeldR) {            // backward walk (shift held)
            if (!g_focusIndex)                         // dword_75BEBC == 0 -> seed from group count
                g_focusIndex = MemR<i32>(group, 26) >> 16;
            v11 = g_focusIndex - 1;
            if (v11 >= 0) {
                i32 ids = Mem<i32>(group, 24);         // *(group+24) child-id list
                std::uintptr_t p = ids + 4 * v11;
                bool found = false;
                while (true) {
                    int child = Mem<i32>(p, 0);        // *(p)
                    Widget& cw = W(child);
                    if (cw.type() == 65 && !cw.at<i32>(52)) { found = true; break; }
                    --v11; p -= 4;
                    if (v11 < 0) break;
                }
                if (found) {
                    g_focusIndex = v11;
                    if (g_focusWidget)                 // clear old highlight
                        W(Mem<i32>(g_focusWidget, 304)).at<i32>(40) = 0;
                    int newChild = Mem<i32>(Mem<i32>(group, 24) + 4 * v11, 0);
                    g_focusWidget = WidgetDataPtr(newChild);  // *(pool + 740*newChild + 12)
                    W(Mem<i32>(g_focusWidget, 304)).at<i32>(40) = 1;
                    g_focusValueAcc = g_d4_dragOriginX - v20; // dword_75BED4 = dword_62D0C8 - v20
                    g_focusValue   = Mem<i32>(g_focusWidget, 296);
                    v11 = 0;
                    g_focusValuePrev = 0;
                }
            }
        } else {                                       // forward walk (no shift)
            if ((MemR<i32>(group, 26) >> 16) - 1 <= g_focusIndex)
                g_focusIndex = -1;
            v11 = g_focusIndex + 1;
            std::uintptr_t off = 4 * v11;
            while (v11 < (MemR<i32>(group, 26) >> 16)) {
                int child = Mem<i32>(Mem<i32>(group, 24) + off, 0);
                Widget& cw = W(child);
                if (cw.type() == 65 && !cw.at<i32>(52)) {
                    g_focusIndex = v11;
                    if (g_focusWidget)
                        W(Mem<i32>(g_focusWidget, 304)).at<i32>(40) = 0;
                    int newChild = Mem<i32>(Mem<i32>(group, 24) + 4 * v11, 0);
                    g_focusWidget = WidgetDataPtr(newChild);
                    W(Mem<i32>(g_focusWidget, 304)).at<i32>(40) = 1;
                    v11 = 0;
                    g_focusValueAcc = g_d4_dragOriginX - v20;
                    g_focusValue   = Mem<i32>(g_focusWidget, 296);
                    g_focusValuePrev = 0;
                    break;
                }
                off += 4; ++v11;
            }
        }

        // LABEL_48 tail of the tab block.
        if (g_caretWidget != -1) {                     // dword_62D350 != -1
            v1 = static_cast<unsigned char>(
                Widget_DestroyByTypeBridge(g_caretWidget, v11, g_caretWidget));
            g_caretWidget = -1;
        }
        if (g_focusWidget && (Mem<u8>(g_focusWidget, 38) & 1) != 0) {
            int w = h->propertyGet(reinterpret_cast<const char*>(Ptr(g_focusWidget, 40)),
                                   Mem<i32>(g_focusWidget, 308));
            g_caretPenX = Mem<i32>(g_focusWidget, 4) + w;
            g_caretPenY = Mem<i32>(g_focusWidget, 8);
            v1 = g_caretPenY;
        }
        g_pendingKey = 0;
        // falls through to LABEL_7
    } else {
        // The flag-2 numeric / printable insert ladder. Only entered when the
        // focused record has the editable bit (+38 & 2) for numeric keys, or always
        // for the printable path (flag-2 == 0).
        v1 = static_cast<unsigned char>(g_focusWidget);
        if ((Mem<u8>(g_focusWidget, 38) & 2) != 0) {
            // numeric: keys 0x02..0x0B (no shift) update the numeric value display.
            if (key >= 2 && key <= 0x0B && !g_shiftHeldL && !g_shiftHeldR) {
                char* buf = reinterpret_cast<char*>(Ptr(g_focusWidget, 40));
                h->animStateUpdate(Mem<i32>(g_focusWidget, 296), buf, 0xA);
                doInsertBlock = true;
            }
        } else {
            doInsertBlock = true;                       // flag-2==0 -> LABEL_20 insert path
        }
    }

    if (doInsertBlock) {                                // LABEL_20
        std::size_t v4 = std::strlen(reinterpret_cast<char*>(Ptr(g_focusWidget, 40))) + 1;
        std::size_t v5 = v4 - 1;
        int cap = (Mem<u8>(g_focusWidget, 38) & 1) != 0
                      ? Mem<i32>(g_focusWidget, 24)         // text record cap
                      : Mem<u8>(g_focusWidget, 36);         // byte cap
        if (static_cast<int>(v4 - 1) < cap) {
            v1 = static_cast<unsigned char>(g_focusWidget);
            int v7 = Mem<i32>(g_focusWidget, 344);          // +344 overflow guard
            if (v7 && v7 <= Mem<i32>(g_focusWidget, 16)) {  // <= +16
                g_pendingKey = 0;
                // jumps straight to LABEL_7 (skip the char insert).
            } else {
                int shift = (g_shiftHeldL || g_shiftHeldR) ? 1 : 0;
                int sc = h->inputCharToScancode(shift, 0, static_cast<int>(key), 0);
                *reinterpret_cast<char*>(Ptr(g_focusWidget, 40) + v5) = static_cast<char>(sc);
                *reinterpret_cast<char*>(Ptr(g_focusWidget, 40) + v5 + 1) = 0;
                g_pendingKey = 0;
            }
        } else {
            g_pendingKey = 0;
        }
    }

    (void)returnedEarly;

    // LABEL_7 — recompute caret pen for the (possibly new) focused record.
    if (g_focusWidget) {
        if ((Mem<u8>(g_focusWidget, 38) & 2) != 0)
            Mem<i32>(g_focusWidget, 296) = h->utilParseInt(
                reinterpret_cast<const char*>(Ptr(g_focusWidget, 40)));
        int w = h->propertyGet(reinterpret_cast<const char*>(Ptr(g_focusWidget, 40)),
                    Mem<i32>(g_focusWidget, 308));
        v1 = Mem<i32>(g_focusWidget, 4) + w;            // dword_75BEC8 = *(record+4)+w
        g_caretPenX = v1;
    }
    return v1;
}

// ===========================================================================
// 0x420db4 — VIBE_Widget_ProcessMouseDrag (per-frame drag/focus FSM)
// Reconstructed 1:1 from the decompile + disasm 0x420db4-0x42135d, including the
// mouse-down focus-GRAB block: when the cursor is over a type-'A' widget that owns
// a data record and the left button is held, focus transfers to that record, the
// owning group is walked to recover the focus index, a flag-0x40 record with no
// value auto-defaults to -1, and a flag-(38&1) edit record gets its caret laid out.
// The render/scroll edges go through hooks; deterministic state mutations are direct.
// ===========================================================================
int Widget_ProcessMouseDrag() {
    const GuiDialogs4Hooks* h = g_hooks;
    int v0 = g_focusIndex;                             // v0 = dword_75BEBC (preserved across early block)

    // Disarm block (0x420dc0): an armed drag with no held button / non-editable focus
    // is dropped, restoring the parked cursor-clamp origin.
    if (g_dragArmed && !g_pendingMouseUp
        && (!g_mouseDown || !g_focusWidget || (Mem<u8>(g_focusWidget, 38) & 2) == 0)) {
        g_d4_dragOriginX = g_d4_savedClampY0;                // dword_62D0C8 = dword_75BEB8
        g_d4_dragOriginY = g_d4_savedClampY1;                // dword_62D0D0 = dword_75BEC0
        g_dragArmed = 0;                               // dword_62D34C = 0
        if (g_focusWidget) {
            int slot = Mem<i32>(g_focusWidget, 304);
            int owner = g_caretBaseW;                   // ecx = dword_62D33C
            g_focusWidget = 0;                          // dword_62D328 = 0
            W(slot).at<i32>(40) = 0;
            if (owner != -1) {                          // restore clamp + clear owner
                g_d4_dragOriginX = g_d4_savedClampY0;
                g_caretBaseW = -1;                      // dword_62D33C = -1
                g_d4_dragOriginY = g_d4_savedClampY1;
            }
        }
    }

    int v3 = g_pendingMouseX >> 16;                    // dword_672210>>16
    if (!g_mouseDown) g_focusFlag = 0;                 // dword_62D348 = 0
    g_focusIndex = v0;                                 // dword_75BEBC = v0

    if (!g_focusWidget && g_caretWidget != -1) {
        Widget_DestroyByTypeBridge(g_caretWidget, -1, v3);
        g_caretWidget = -1;                            // dword_62D350 = v4 (call ret; -1 model)
    }

    // -----------------------------------------------------------------------
    // Mouse-down focus GRAB (0x420eb2): cursor over a type-'A' widget.
    // -----------------------------------------------------------------------
    if (g_hitTestSlot4 != -1) {
        Widget& hit = W(g_hitTestSlot4);
        if (hit.type() == 65) {                        // *(slot+24) == 'A'
            std::uintptr_t v6 = WidgetDataPtr(g_hitTestSlot4); // *(slot+12) data record
            if (g_hitTestSlot4 == Mem<i32>(v6, 304)    // dword_62D22C == *(v6+304)
                && g_mouseDown && v6 != g_focusWidget && !g_focusFlag) {
                int v7 = g_focusIndex;                  // ebp = dword_75BEBC
                if (g_focusWidget)                      // restore old highlight to focusFlag
                    W(Mem<i32>(g_focusWidget, 304)).at<i32>(40) = g_focusFlag;
                W(Mem<i32>(v6, 304)).at<i32>(40) = 1;   // new record highlighted
                g_focusValuePrev = 0;                   // dword_75BED0 = 0
                g_focusValueAcc = g_d4_dragOriginX - v3;   // dword_75BED4 = dword_62D0C8 - v3
                g_focusValue = Mem<i32>(v6, 296);       // dword_75BECC = *(v6+296)
                g_focusWidget = v6;                     // dword_62D328 = v6

                // Group walk to recover the focus index.
                i32 group = W(Mem<i32>(v6, 304)).at<i32>(44); // v10 = *(slot+44)
                int v11 = 0;
                if (group) {
                    for (int i = 0; ; i += 4) {
                        int n = MemR<i32>(group, 26) >> 16;
                        if (v11 >= n) break;
                        if (Mem<i32>(g_focusWidget, 304)
                            == Mem<i32>(Mem<i32>(group, 24) + i, 0))
                            v7 = v11;
                        ++v11;
                    }
                }

                // flag-0x40 record with no value auto-defaults to -1.
                if ((Mem<u8>(g_focusWidget, 38) & 0x40) != 0
                    && !Mem<i32>(g_focusWidget, 24)
                    && !Mem<i32>(g_focusWidget, 28)) {
                    int active = 0;
                    for (int j = 0; j != 18; j += 3)
                        if (g_groupSlots[j] != -1) ++active;
                    if (!active && !Mem<i32>(g_focusWidget, 296))
                        Mem<i32>(g_focusWidget, 296) = -1;
                }

                g_focusIndex = v7;                      // dword_75BEBC = v7
                if ((Mem<u8>(g_focusWidget, 38) & 1) != 0) {  // edit record: caret
                    int w = h->propertyGet(
                                reinterpret_cast<const char*>(Ptr(g_focusWidget, 40)),
                                Mem<i32>(g_focusWidget, 308));
                    g_caretPenX = Mem<i32>(g_focusWidget, 4) + w; // dword_75BEC8
                    g_caretPenY = Mem<i32>(g_focusWidget, 8);     // dword_75BEC4
                    i16 caretY = static_cast<i16>(g_caretPenY - 7);
                    int caretYHi = g_caretPenY >> 16;             // eax = dword_75BEC4+2 sar 16
                    if (g_caretWidget == -1) {
                        // ebx = dword_62D2C8 + 8 (dropped: thunk hook is 2-arg, as in SetFocus)
                        g_caretWidget = h->widgetCreateObjectThunk(caretYHi, caretY);
                    } else {
                        Widget_LayoutBoundsBridge(g_caretPenX, caretY, g_caretWidget);
                    }
                } else if (g_caretWidget != -1) {
                    Widget_DestroyByTypeBridge(g_caretWidget,
                                               static_cast<int>(g_focusWidget), v3);
                    g_caretWidget = -1;
                }
            }
        }
    }

    int v18 = g_focusIndex;                            // v18 = dword_75BEBC
    if (g_mouseDown && g_focusWidget) g_focusFlag = 1; // dword_62D348 = 1

    // Slider step accumulation when the focused field is a held slider.
    if (g_focusWidget && g_hitTestSlot4 == Mem<i32>(g_focusWidget, 304)
        && (Mem<u8>(g_focusWidget, 38) & 2) != 0 && g_mouseWheelUp) {
        int v = Mem<i32>(g_focusWidget, 296);
        if (Mem<i32>(g_focusWidget, 24) > v && v != -1
            && Mem<i32>(g_focusWidget, 312) != -1)
            Mem<i32>(g_focusWidget, 296) += h->sliderComputeStep(static_cast<int>(g_focusWidget));
        v18 = g_focusIndex;
        // flag-0x40 / no value auto-default (second occurrence, 0x421142).
        if (!Mem<i32>(g_focusWidget, 24) && !Mem<i32>(g_focusWidget, 28)) {
            int active = 0;
            for (int k = 0; k != 18; k += 3)
                if (g_groupSlots[k] != -1) ++active;
            if (!active && !Mem<i32>(g_focusWidget, 296)
                && (Mem<u8>(g_focusWidget, 38) & 0x40) != 0)
                Mem<i32>(g_focusWidget, 296) = -1;
        }
    }
    g_focusIndex = v18;                                // dword_75BEBC = v18

    // Release: clear focus, destroy caret.
    if (g_mouseRelease) {
        if (g_focusWidget) {
            if ((Mem<u8>(g_focusWidget, 38) & 1) != 0)
                h->inputClearMouseButtons(16);
            int slot = Mem<i32>(g_focusWidget, 304);
            g_focusWidget = 0;
            W(slot).at<i32>(40) = 0;
            g_focusFlag = 0;
            if (g_caretWidget != -1) {
                Widget_DestroyByTypeBridge(g_caretWidget, g_mouseRelease, 0);
                g_caretWidget = -1;
            }
        }
    }

    if (g_focusWidget && !g_mouseDown) {
        g_mouseWheelUp = 0;
        g_focusValuePrev = 1;
    }

    h->scrollbarDragThumb();                            // VIBE_Scrollbar_DragThumb
    (void)Widget_HandleKeyInput();                      // chained edit handling

    // Icon spin when an iconic field is held.
    if (g_focusWidget && (Mem<u8>(g_focusWidget, 38) & 0x10) != 0
        && g_hitTestSlot4 == Mem<i32>(g_focusWidget, 304) && g_mouseWheelUp) {
        int next = Mem<i32>(g_focusWidget, 328) + 1;
        int last = Mem<i32>(g_focusWidget, 332) + Mem<i32>(g_focusWidget, 324) - 1;
        Mem<i32>(g_focusWidget, 328) = next;
        if (last < next) Mem<i32>(g_focusWidget, 328) = Mem<i32>(g_focusWidget, 324);
        h->inputSetIconTextById(Mem<i32>(g_focusWidget, 0), Mem<i32>(g_focusWidget, 328));
    }
    h->sliderUpdateFromMouse(0, 0);

    if (g_hitTestSlot4 == -1) return 0;
    Widget& w = W(g_hitTestSlot4);
    if ((w.radioFlag() & 0x40) == 0) return 0;          // +444 & 0x40

    // Copy the held tooltip widget's text (+184) into the global tooltip buffer
    // (g_tooltipText / byte_75BE38, owned by gui_dialogs3), verbatim 2-byte stride.
    char* dst = g_tooltipText;
    const char* src = reinterpret_cast<const char*>(&w.at<char>(184));
    while (true) {
        char c = *src;
        dst[0] = *src;
        if (!c) break;
        char c2 = src[1];
        src += 2;
        dst[1] = c2;
        dst += 2;
        if (!c2) break;
    }
    return 0;
}

// ===========================================================================
// 0x41fd48 — VIBE_Widget_HoverUpdate (per-frame hover + wheel scroll)
// Reconstructed 1:1 from the decompile + disasm 0x41fd48-0x4200f6:
//   * hover-latch head (deadzone +/-5, engage/disengage on dword_672238),
//   * the cursor "rolling point" shift (word_75BF44|dword_75BF46|word_75BF4A),
//   * the State_Finalize sweep over the whole window table (g_windows, gate
//     +13&2 && +640==1 && word+636), forwarding *(win+634)>>16,
//   * wheel-scroll forwarding through the REAL Window_Scroll sibling (@0x41a024)
//     for both the hovered-scroll window (dword_75BF08) and the wheel window
//     (dword_62D294), and the deferred State_Finalize(dword_62D248) tail return.
// ===========================================================================
int Widget_HoverUpdate() {
    const GuiDialogs4Hooks* h = g_hooks;
    int v0 = -1;                                       // esi = -1

    // Cursor coordinates: X = misaligned dword at 75BF46+2 >>16, Y = 75BF46 >>16.
    auto curX = [] { return MemR<i32>(reinterpret_cast<std::uintptr_t>(g_cursorPtBuf), 4) >> 16; };
    auto curY = [] { return MemR<i32>(reinterpret_cast<std::uintptr_t>(g_cursorPtBuf), 2) >> 16; };

    bool gotoLabel7 = false;
    if (g_hoverPrev != g_hoverLast                     // dword_75BF40 != dword_75BEE4
        || curX() < g_hoverX - 5 || curY() < g_hoverY - 5
        || curX() > g_hoverX + 5 || curY() > g_hoverY + 5
        || g_mouseDown) {                              // dword_672220
        g_hoverSlot = -1;                              // dword_75BF3C = -1
        g_hoverTick = g_frameTick;                     // dword_75BED8 = dword_62EB3C
        g_hoverX = curX(); g_hoverEngaged = 0; g_hoverY = curY();
    } else if (g_hoverSlot != -1) {
        gotoLabel7 = true;
    }

    if (!gotoLabel7) {
        if (g_hoverPrev != -1 && g_mouseDownHover) {   // dword_75BF40 != -1 && dword_672238
            g_hoverSlot = g_hoverPrev;
            g_hoverX = curX(); g_hoverEngaged = 1; g_hoverY = curY();
        }
    }
    // LABEL_7
    if (g_hoverSlot != -1 && g_hoverEngaged && !g_mouseDownHover) {
        g_hoverSlot = -1;
        g_hoverTick = g_frameTick;
        g_hoverX = curX(); g_hoverY = curY(); g_hoverEngaged = 0;
        h->inputClearMouseButtons(16);
    }
    g_hoverLast = g_hoverPrev;                          // dword_75BEE4 = dword_75BF40

    // Cursor rolling-point shift (0x41fe52): LOWORD(75BF46)=word_75BF4A;
    // word_75BF44 = HIWORD(75BF46).
    {
        u16 feedY, hiX;
        std::memcpy(&feedY, g_cursorPtBuf + 6, 2);     // word_75BF4A
        std::memcpy(&hiX,   g_cursorPtBuf + 4, 2);     // HIWORD(dword_75BF46)
        std::memcpy(g_cursorPtBuf + 2, &feedY, 2);     // LOWORD(dword_75BF46) = word_75BF4A
        std::memcpy(g_cursorPtBuf + 0, &hiX,   2);     // word_75BF44 = HIWORD(75BF46)
    }

    if (g_stateFinalizeReq != -1)                      // dword_62D248 != -1
        v0 = g_stateFinalizeReq;

    // State_Finalize sweep over the entire window table (stride 952 == sizeof(Window)).
    for (int i = 0; i < kMaxWindows; ++i) {            // edx: 67EB80 .. unk_695080
        Window& win = g_windows[i];
        if ((win.at<u8>(13) & 2) != 0 && win.at<i32>(640) == 1 && win.at<u16>(636))
            h->stateFinalize(win.at<i32>(634) >> 16);  // *(win+634)>>16
    }

    int* result = nullptr;                             // eax (return value carrier)
    bool doneScrollA = false;

    // Hovered-scroll window wheel forwarding (dword_75BF08).
    if (g_hoverScrollWin != -1) {                      // dword_75BF08 != -1
        Window& w3 = g_windows[g_hoverScrollWin];      // &dword_67EB80[238*idx]
        if (g_mouseDown) {                             // dword_672220
            if (w3.at<i32>(940) != -1) {               // v3[235] reset235
                int v4 = w3.at<i32>(936);              // v3[234] reset234
                if (v4 != -1 && !g_focusWidget) {      // && !dword_62D328
                    int v5 = w3.at<i32>(940);          // v3[235]
                    if (g_hitTestSlot4 == v5) {
                        // v6 = &windows[*(pool + 740*v5 + 476)]
                        Window& v6 = g_windows[W(v5).at<i32>(476)];
                        int v7 = v6.at<i32>(592);      // v6[148] scrollOffset
                        if (!(v6.at<u8>(608) != v7 && v7)) {
                            Window_Scroll(0, v6.at<i32>(612), v6.at<i32>(0)); // (0, v6[153], *v6)
                        }
                        doneScrollA = true;
                    } else if (g_hitTestSlot4 == v4) {
                        Window& v13 = g_windows[W(v4).at<i32>(476)];
                        int v14 = v13.at<i32>(592);    // v13[148]
                        if (v13.at<u8>(608) == v14 || !v14)
                            Window_Scroll(0, -v13.at<i32>(612), v13.at<i32>(0)); // (0, -v13[153], *v13)
                        doneScrollA = true;
                    }
                }
            }
        }
    }
    (void)doneScrollA;

    // Wheel window forwarding (dword_62D294).
    if (g_scrollWheelWin != -1) {                      // LABEL_29
        Window& wr = g_windows[g_scrollWheelWin];      // &dword_67EB80[238*idx]
        if (!(g_wheelUp == g_wheelDn || !wr.at<i32>(612) || g_focusWidget)) {
            if (g_wheelUp) {                           // dword_672254
                result = reinterpret_cast<int*>(
                    static_cast<std::intptr_t>(Window_Scroll(0, -wr.at<i32>(612), wr.at<i32>(0))));
            } else if (g_wheelDn) {                    // dword_672250
                result = reinterpret_cast<int*>(
                    static_cast<std::intptr_t>(Window_Scroll(0, wr.at<i32>(612), wr.at<i32>(0))));
            }
        }
    }

    // LABEL_37: deferred State_Finalize tail.
    if (v0 != -1)
        return h->stateFinalize(v0);
    return static_cast<int>(reinterpret_cast<std::intptr_t>(result));
}

// ===========================================================================
// 0x518efc — VIBE_Widget_AddPersonRow  (INTEGRATION ANCHOR)
// Builds a 3-cell market row: two gfx cells + a money-formatted price label.
// Wires the REAL world::MoneyFormatWithSeparators + gui::Object_AddTextLabel.
// ===========================================================================
// NOTE on parameters (recovered from the call site at 0x5192b5 and the body
// disasm): the original is __userpurge with a1@ax, a2@edx, a3@cx, a4@ebx, a5
// (stack). a2 (here `packed`) is a packed dword: its LOW word is the cell-row
// base y, its HIGH word (a2>>16) is the per-row gfx base AND the market-item id.
// a1 (`x`) is the row x. a3 (`y2`/cx) is ignored by the body (clobbered). a4 is
// the window slot. a5 is the price rate.
int Widget_AddPersonRow(i16 x, int packed, i16 y2, int winSlot, unsigned char rate) {
    const GuiDialogs4Hooks* h = g_hooks;
    (void)y2;                                           // a3@cx is not used by the body

    i16 yLow = static_cast<i16>(packed & 0xFFFF);       // *(_WORD *)var_13 == LOWORD(a2)
    int gfxBase = packed >> 16;                         // var_18 = a2 >> 16 (arithmetic)

    // Cell 1: fixed gfx 0x4CA (1226) at (x=a1, y=a2>>16).  0x518f1e
    Object_AddToWindow(winSlot, static_cast<i16>(gfxBase),
                       static_cast<i16>(x), 0x4CA);
    // Cell 2: gfx (a2>>16)+0xCE at (x=a1+2, y=LOWORD(a2)+2).  0x518f4a -> ebp
    int mid = Object_AddToWindow(winSlot, static_cast<i16>(yLow + 2),
                                 static_cast<i16>(x + 2), gfxBase + 0xCE);
    // Cell 3: fixed gfx 0x6B8 (1720) at (x=a1+3, y=LOWORD(a2)+53).  0x518f6e
    Object_AddToWindow(winSlot, static_cast<i16>(yLow + 53),
                       static_cast<i16>(x + 3), 0x6B8);
    W(mid).at<i32>(72) = 1;                              // +0x48 = 1  (0x518f96)

    // Price: LookupCachedMarketPrice(a2>>16, a5); ConvertX (truncate via x87 RC);
    // fistp -> the truncated integer. (int)trunc(p) == fistp(trunc(p)).  0x518fb0
    double price = h->lookupCachedMarketPrice(gfxBase, rate);
    int amount = static_cast<int>(h->coordConvertX(price));
    std::string text = world::MoneyFormatWithSeparators(amount, rate); // a5 passed verbatim

    // Label at (x=a1+7, y=LOWORD(a2)+58) in the current window (dword_62D230).
    int lbl = Object_AddTextLabel(static_cast<i16>(x + 7),
                                  static_cast<i16>(yLow + 58),
                                  g_currentWindowId, text.c_str());
    // Unconditional (no >=0 guard in the binary).  0x518ffa..0x519008
    Widget& lw = W(lbl);
    lw.w() = 36;                                        // word +0x14 = 0x24 (36)
    lw.at<i16>(112) = 67;                               // word +0x70 = 0x43 ('C')
    lw.at<i32>(92) = 1;                                 // dword +0x5C = 1
    return mid;
}

// ===========================================================================
// Render leaves — the deterministic state-byte selection is translated; the
// actual blits go through hooks.
// ===========================================================================

// 0x4137bc — VIBE_Widget_DrawCheckbox
int Widget_DrawCheckbox(std::uintptr_t widgetPtr) {
    const GuiDialogs4Hooks* h = g_hooks;
    if (!Mem<u8>(widgetPtr, 184) || (Mem<u8>(widgetPtr, 444) & 0x40) != 0)
        return static_cast<int>(widgetPtr);

    int state = 8;
    if (Mem<i32>(widgetPtr, 76)) state = 9;
    if (Mem<i32>(widgetPtr, 92)) state |= 0x10;
    if (Mem<i32>(widgetPtr, 64)) state |= 4;
    if ((Mem<u8>(widgetPtr, 444) & 8) != 0) state |= 0x20;

    int off = Mem<i32>(widgetPtr, 456);
    int gx = (off == -1) ? (MemR<i32>(widgetPtr, 14) >> 16)
                         : off + (MemR<i32>(widgetPtr, 14) >> 16);
    int span = Mem<i32>(widgetPtr, 460);                // v1[115]
    int gy = (span == -1) ? (Mem<i32>(widgetPtr, 16) >> 16) - 16
                          : span + (Mem<i32>(widgetPtr, 16) >> 16);
    int st = Mem<i32>(widgetPtr, 440);                  // v1[110]
    if (!st) st = g_defaultFont + 1;                    // dword_62D2B0 + 1
    h->stateFinalize(st);

    // 0x413860: flag 0x20 => textured-quad blit of the widget face through the
    // active render context (dword_62D268) into the frame blob (dword_62D210).
    if ((Mem<u8>(widgetPtr, 444) & 0x20) != 0) {
        h->decompressFinalize(static_cast<int>(g_frameBlob));
        double inv = 1.0 / static_cast<double>(MemR<u32>(g_renderContext, 116));
        double v = static_cast<double>(Mem<i32>(widgetPtr, 452)) * inv; // v8[113]
        double u = inv * static_cast<double>(Mem<i32>(widgetPtr, 448)); // v8[112]
        h->renderDrawTexturedQuad(
            static_cast<int>(g_renderContext),
            Mem<i32>(widgetPtr, 448) / 2 + (MemR<i32>(widgetPtr, 14) >> 16),
            Mem<i32>(widgetPtr, 452) / 2 + (Mem<i32>(widgetPtr, 4) >> 16),
            0, static_cast<float>(u), static_cast<float>(v), 0.5f);
        h->decompressStateBlob(static_cast<int>(g_frameBlob), 0);
    }

    if ((Mem<u8>(widgetPtr, 444) & 4) != 0)
        return h->animApply((MemR<i32>(widgetPtr, 14) >> 16) - 48,
                            (Mem<i32>(widgetPtr, 20) >> 16) + (Mem<i32>(widgetPtr, 16) >> 16),
                            Mem<i32>(widgetPtr, 448) + 96, static_cast<int>(g_frameBlob),
                            reinterpret_cast<const char*>(Ptr(widgetPtr, 184)), state);
    return h->animApply(gx - 48, gy, Mem<i32>(widgetPtr, 448) + 96, static_cast<int>(g_frameBlob),
                        reinterpret_cast<const char*>(Ptr(widgetPtr, 184)), state);
}

// 0x40ecb0 — VIBE_Widget_DrawScrollThumb
int Widget_DrawScrollThumb(std::uintptr_t widgetPtr, int surf) {
    const GuiDialogs4Hooks* h = g_hooks;
    int st = h->stateUpdate(g_thumbStateId);
    std::uintptr_t t0 = h->coordTransform(st, 0);
    h->coordTransform(st, 1);
    h->coordTransform(st, 2);
    std::uintptr_t t3 = h->coordTransform(st, 3);
    int y = Mem<i32>(widgetPtr, 32) - 100 - Mem<u16>(t0, 10);
    int xc = (MemR<i32>(widgetPtr, 14) >> 16) - 32;
    // 0x40ed49: `mov ecx, ds:dword_69FFB8+2; sar ecx,16` reads the MISALIGNED int at
    // (&g_screenClipW + 2) = bytes [hiword(clipW) | loword(clipExt)<<16]; >>16 yields
    // the sign-extended low word of g_screenClipExt.  The 4th arg is g_screenClipExt>>16.
    int clipPacked = static_cast<i32>(static_cast<i16>(g_screenClipExt));
    h->coordPush(0, 0, clipPacked, g_screenClipExt >> 16);
    h->animBasic(xc, y, st, surf, 0);
    h->animBasic(xc, y + 100 + Mem<u16>(t0, 10), st, surf, 1);
    h->animBasic(xc, y + Mem<u16>(t0, 10), st, surf, 2);
    h->stateGetCurrent(xc - 12, y, Mem<u16>(t0, 10) + 100 + Mem<u16>(t0, 10), 64);
    int ty = (g_pendingMouseX >> 16) - Mem<u16>(t3, 10) / 2;
    int tx = Mem<u16>(t0, 6) / 2 + xc - Mem<u16>(t3, 6) / 2;
    h->animBasic(tx, ty, st, surf, 3);
    g_scrollThumbX = tx; g_scrollThumbY = ty;
    h->stateFinalize(g_defaultFont + 1);
    char anim[24];
    h->animStateUpdate(Mem<i32>(widgetPtr, 296), anim, 0xA);
    return h->animApply(g_scrollThumbX, g_scrollThumbY, Mem<u16>(t3, 6), surf, anim, 8);
}

// 0x4121c4 — VIBE_Widget_DrawScrollBar
int Widget_DrawScrollBar(int widgetSlot, int surf) {
    const GuiDialogs4Hooks* h = g_hooks;
    unsigned frameBase = 0;
    int glyph = 8;
    Widget& w = W(widgetSlot);

    if ((w.radioFlag() & 2) != 0 && !g_focusWidget) {
        bool isFocusedOrSet = (g_hitTestSlot4 == widgetSlot) || w.at<i32>(36);
        if (isFocusedOrSet && !w.at<i32>(56)) {
            if ((w.radioFlag() & 1) != 0) w.at<i32>(40) = 1;
        } else if ((w.radioFlag() & 1) != 0) {
            w.at<i32>(40) = 0;
        }
    }
    if ((w.radioFlag() & 2) == 0 && w.at<i32>(68) && !w.at<i32>(56))
        w.at<i32>(40) = w.at<i32>(36);

    if (w.at<i32>(40)) glyph = 10;
    else if (w.at<i32>(76)) glyph = 9;

    h->stateFinalize(w.ld<i32>(110) >> 16);
    int handle = w.dataPtr();                            // v16 = *(v2+12)
    std::uintptr_t t0 = h->coordTransform(handle, 0);
    std::uintptr_t t1 = h->coordTransform(handle, 1);
    std::uintptr_t t2 = h->coordTransform(handle, 2);
    int trackLen = (w.ld<i32>(18) >> 16) - Mem<u16>(t0, 6) - Mem<u16>(t1, 6);
    if (w.at<i32>(40)) frameBase = 3;

    h->animBasic(w.ld<i32>(14) >> 16, w.at<i32>(16) >> 16, handle, surf, frameBase);
    h->animBasic((w.ld<i32>(14) >> 16) + (w.ld<i32>(18) >> 16) - Mem<u16>(t1, 6),
                 w.at<i32>(16) >> 16, handle, surf, frameBase + 1);
    int x0 = (w.ld<i32>(14) >> 16) + Mem<u16>(t0, 6);
    h->coordPush(x0 - 1, g_drawClipTop, g_drawClipBottom, x0 + trackLen);
    unsigned step = Mem<u16>(t2, 6);
    if (step) {
        // 0x412361 loop: for(i=0; (double)i < (double)(trackLen/step) + 0.5; ++i)
        // == iterate i = 0 .. (trackLen/step) inclusive (one MORE than int trunc).
        double bound = static_cast<double>(trackLen / static_cast<int>(step)) + 0.5;
        for (int i = 0; static_cast<double>(i) < bound; ++i) {
            int seg = static_cast<int>(step) * i;
            h->animBasic(Mem<u16>(t0, 6) + (w.ld<i32>(14) >> 16) + seg,
                         w.at<i32>(16) >> 16, handle, surf, frameBase + 2);
        }
    }
    return h->animApply(w.ld<i32>(14) >> 16,
                        (w.at<i32>(16) >> 16) - g_defaultCtrlH / 2 + Mem<u16>(t0, 10) / 2 + 1,
                        w.ld<i32>(18) >> 16, surf,
                        reinterpret_cast<const char*>(&w.at<char>(120)), glyph);
}

// 0x4186d8 — VIBE_Window_RenderContent  (a1@eax = backing widget record, a2@edx = surf)
int Window_RenderContent(std::uintptr_t a1, void* surf) {
    const GuiDialogs4Hooks* h = g_hooks;
    int a2 = static_cast<int>(reinterpret_cast<std::intptr_t>(surf));

    // Convenience for the 16.16 coord reads.  +2 is misaligned (MemR).
    const int cX = MemR<i32>(a1, 2) >> 16;   // *(a1+2)>>16
    const int cY = Mem<i32>(a1, 4) >> 16;    // *(a1+4)>>16
    const int cW = MemR<i32>(a1, 6) >> 16;   // *(a1+6)>>16
    const int cH = Mem<i32>(a1, 8) >> 16;    // *(a1+8)>>16

    if ((Mem<u8>(a1, 13) & 4) != 0)                                  // 0x4186ea
        h->surfaceColorFill(cX, cY, cH, cW, a2);                     // (x,y,h,w,surf)

    if ((Mem<u8>(a1, 13) & 4) == 0 && (Mem<u8>(a1, 12) & 1) == 0     // 0x418716
        && g_renderPhaseFlag != 1 && Mem<i32>(a1, 624) < 2
        && (Mem<u8>(a1, 13) & 0x10) != 0)
        h->resultHandlerInteraction(0, 0, cH, cW, Mem<i32>(a1, 32), cX, cY, a2);

    if (Mem<i32>(a1, 624) == 2 && (Mem<u8>(a1, 13) & 0x10) != 0      // 0x418762
        && g_renderPhaseFlag != 1)
        h->resultHandlerInteraction(cX, cY, cH, cW, a2, 0, 0, Mem<i32>(a1, 32));

    const int v31 = cX + Mem<i32>(a1, 920);                          // 0x4187a8
    const int v32 = cY + Mem<i32>(a1, 924);                          // 0x4187c2
    const int v3 = cW - (Mem<i32>(a1, 920) + Mem<i32>(a1, 928));     // 0x4187e5
    const int v4 = cH - (Mem<i32>(a1, 924) + Mem<i32>(a1, 932));     // 0x4187f5

    auto blitTexturedQuad = [&](std::uintptr_t ctx) {
        h->decompressFinalize(static_cast<int>(g_frameBlob));
        double inv = 1.0 / static_cast<double>(MemR<u32>(ctx, 116));
        double vv = static_cast<double>(v4) * inv;
        double uu = inv * static_cast<double>(v3);
        h->renderDrawTexturedQuad(static_cast<int>(ctx), v3 / 2 + v31, v4 / 2 + v32, 0,
                                  static_cast<float>(uu), static_cast<float>(vv),
                                  MemR<float>(a1, 916));
        h->decompressStateBlob(static_cast<int>(g_frameBlob), 0);
    };

    if ((Mem<u8>(a1, 12) & 1) != 0 && Mem<i32>(a1, 624) == 2         // 0x418818
        && g_renderPhaseFlag != 1) {
        if (g_renderContext && static_cast<std::uintptr_t>(a2) == g_frameBlob)
            blitTexturedQuad(g_renderContext);
        else
            h->renderWithSurfaceContext(a2);
        if ((Mem<u8>(a1, 13) & 0x10) != 0)                          // 0x4188c2
            h->resultHandlerInteraction(cX, cY, cH, cW, a2, 0, 0, Mem<i32>(a1, 36));
    }

    if ((Mem<u8>(a1, 12) & 1) != 0 && g_renderPhaseFlag == 1) {     // 0x418913
        if (g_renderContext && static_cast<std::uintptr_t>(a2) == g_frameBlob)
            blitTexturedQuad(g_renderContext);
        else
            h->renderWithSurfaceContext(a2);
    }

    if (Mem<i32>(a1, 912) && static_cast<std::uintptr_t>(a2) == g_frameBlob) // 0x4189ce
        blitTexturedQuad(static_cast<std::uintptr_t>(Mem<i32>(a1, 912)));

    if ((Mem<u8>(a1, 12) & 2) != 0)                                 // 0x418a5d
        h->surfaceRectOutline(cX, cY, cH - 1, cW - 1, 0xFF, 0xFF, 0xFF, a2);

    if (Mem<i32>(a1, 904) != -1) {                                  // 0x418a9b
        int v39 = h->stateUpdate(Mem<i32>(a1, 904));
        if (v39) {
            // save clip, push window clip, tiled-fill the background frames.
            int sv_l = g_drawClipLeft, sv_t = g_drawClipTop;        // 0x418cf4
            int sv_r = g_drawClipRight, sv_b = g_drawClipBottom;
            h->coordPush(cX, cY, cY + cH, cW + cX);                 // 0x418d53
            std::uintptr_t v43 = g_animTableBase + 84 * Mem<i32>(a1, 904); // 0x418d79
            int frameH = Mem<i32>(v43, 80) >> 16;
            int v40;
            if (Mem<i32>(a1, 584) < 0)                              // 0x418d8b
                v40 = cY - frameH - (Mem<i32>(a1, 584) % frameH);
            else
                v40 = cY - (Mem<i32>(a1, 584) % frameH);
            // 0x418dbe: outer for(i; (double)i < (double)(cH/frameH)+1.5; ++i)
            double yBound = static_cast<double>(cH / frameH) + 1.5; // dbl_610F0C
            for (int i = 0; static_cast<double>(i) < yBound; ++i) {
                int frameW = MemR<i32>(v43, 78) >> 16;              // 0x418e15
                for (int j = 0; j < cW / frameW; ++j) {            // 0x418e21
                    int x = j * frameW + cX;
                    h->animBasic(x, i * (Mem<i32>(v43, 80) >> 16) + v40, v39, a2, 0);
                }
            }
            h->coordPush(sv_l, sv_t, sv_b, sv_r);                  // Coord_Push(v27,v30,v29,v28)
        }
    }

    if (Mem<i32>(a1, 908)) {                                        // 0x418ab4
        int v11 = Mem<i32>(g_tileAnimRec, 8);
        int v36 = Mem<i32>(g_tileAnimRec, 4);
        h->entityAnimationUpdate(cX, cY, cH, cW, surf);
        int v12 = (Mem<i32>(a1, 584) < 0) ? (cY - v11) : cY;       // 0x418b0a
        int v13 = v12 - (Mem<i32>(a1, 584) % v11);
        int v35 = (Mem<i32>(a1, 600) < 0)                          // 0x418b39
                      ? (cX - v36 - (Mem<i32>(a1, 600) % v36))
                      : (cX - (Mem<i32>(a1, 600) % v36));
        int v34 = cH / v11 + 2;                                    // 0x418b86
        int v41 = cW / v36 + 2;                                    // 0x418b94
        int v37 = v13;
        for (int v38 = 0; v38 < v34; ++v38) {                      // 0x418ba4
            int v15 = v35;
            for (int v14 = 0; v14 < v41; ++v14) {                  // 0x418bbf
                h->resultHandlerInteraction(0, 0, Mem<i32>(g_tileAnimRec, 8),
                                            Mem<i32>(g_tileAnimRec, 4),
                                            static_cast<int>(g_tileAnimRec),
                                            v15, v37, a2);
                v15 += v36;
            }
            v37 += v11;
        }
        // 0x418c3a: trailing AnimationUpdate over the misaligned screen-clip extent.
        h->entityAnimationUpdate(0, 0, static_cast<i16>(g_screenClipExt),
                                 g_screenClipExt >> 16, surf);
    }

    int result = static_cast<int>(a1);
    std::uintptr_t child = static_cast<std::uintptr_t>(Mem<i32>(a1, 40));
    if (child) {                                                    // 0x418c43
        return h->resultFinalize(0, 0,
            Mem<i32>(child, 8), Mem<i32>(child, 4),
            static_cast<int>(child), cX, cY, a2, 1);
    }
    return result;
}

// ===========================================================================
// 0x5279dc — VIBE_Window_MainWndProc (Win32 top-level window procedure)
// ===========================================================================
long Window_MainWndProc(int a1, int a2, void* hWnd, unsigned msg,
                        unsigned long wParam, long lParam) {
    const GuiDialogs4Hooks* h = g_hooks;
    (void)a1;

    if (msg < 0x14) {
        if (msg >= kWmPaint) {                          // >= 15
            if (msg <= kWmPaint) {                      // == 15 WM_PAINT
                h->validateRect(hWnd);
                return 0;
            }
            if (msg == kWmQueryNewPalette)              // == 16
                {} // falls to DefWindowProc below (the original's v6 path)
        } else if (msg == kWmDestroy) {                 // == 2
            h->postQuitMessage(0);
            return 0;
        }
        return h->defWindowProc(hWnd, msg, wParam, lParam);
    }
    if (msg <= kWmEraseBkgnd) {                          // == 20 WM_ERASEBKGND
        h->validateRect(hWnd);
        return 1;
    }
    if (msg >= 0x209) {
        if (msg > 0x209) {
            if (msg >= 0x218) {
                if (msg > 0x218) {
                    if (msg == kWmReacquireAudio) {     // 2023
                        h->audioReacquireDigital(wParam, hWnd, 0x7E7);
                        return 0;
                    }
                } else if (!wParam) {                   // msg == 0x218 (WM_MOUSEACTIVATE-ish)
                    return kWndProcMagicAccept;          // 1112363332
                }
            }
            return h->defWindowProc(hWnd, msg, wParam, lParam);
        }
        h->validateRect(hWnd);                           // msg == 0x209
        return 1;
    }
    if (msg != kWmActivate)                              // != 28
        return h->defWindowProc(hWnd, msg, wParam, lParam);

    // WM_ACTIVATE
    if (wParam && g_wndDeactivated) {                    // reactivate
        if (g_wndAudioActive) {
            while (!h->renderIsSurfaceLost()) {}
            while (g_renderRetryFlag && !h->renderRetTrue()) {}
            h->gameObjectDispatchInteractions();
            int blob = static_cast<int>(g_frameBlob);
            if (h->decompressStateBlob(blob, 0)) {
                h->resultHandlerInteraction(0, 0, Mem<i32>(g_frameBlob, 8), Mem<i32>(g_frameBlob, 4),
                                 g_frameBlob2, 0, 0, blob);
                h->gameLogicInteractions(reinterpret_cast<int*>(g_frameBlob));
                h->decompressFinalize(blob);
                h->renderPresentFrame(nullptr);
                h->resultHandlerInteraction(0, 0, Mem<i32>(g_frameBlob, 8), Mem<i32>(g_frameBlob, 4),
                                 g_frameBlob2, 0, 0, blob);
            }
            g_audioPausedFlag = 0;
            h->audioReacquireAll(static_cast<int>(reinterpret_cast<std::intptr_t>(hWnd)), 0x7E7);
            h->inputAcquireMouse(1, 0);
        }
        g_wndDeactivated = 0;
        return 0;
    }
    if (wParam || g_wndDeactivated == 1)
        return 0;
    g_wndDeactivated = 1;                                // deactivate
    if (g_wndAudioActive) {
        h->inputAcquireMouse(0, a2);
        h->audioReleaseAll();
        g_audioPausedFlag = 1;
    }
    h->setWindowPos(hWnd, reinterpret_cast<void*>(1), 0, 0, 0, 0, 3);
    return 0;
}

} // namespace guild::gui
