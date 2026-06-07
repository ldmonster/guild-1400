#include "gui/gui_dialogs4.h"

#include "gui/object.h"         // g_widgets, Widget_AllocSlot
#include "gui/window.h"         // g_windows, g_currentWindowId, g_defaultFont
#include "gui/form.h"           // g_forms, g_currentFormId
#include "gui/widget_create.h"  // g_defaultCtrlH, g_screenClipW, g_screenClipExt, Object_AddTextLabel, Object_AddToWindow, Property_Get
#include "gui/window_render.h"  // Widget_LayoutBounds (REAL sibling — reused by the LayoutBounds bridge)
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
i32 g_hoverPrev, g_hoverLast, g_hoverSlot, g_hoverX, g_hoverY, g_hoverEngaged, g_hoverTick;
i32 g_thumbStateId, g_scrollThumbX, g_scrollThumbY;
i32 g_hitTestSlot4;
i32 g_pendingMouseX, g_mouseX, g_mouseY;
u8  g_pendingKey;
// g_mouseDown (dword_672220) is owned by input.cpp; referenced via the extern in our header.
i32 g_pendingMouseUp, g_mouseWheelUp, g_mouseRelease, g_mouseDownHover, g_mouseMovedFlag, g_frameTick;
unsigned char g_wndDeactivated;
i32 g_wndAudioActive, g_renderRetryFlag, g_frameBlob2, g_audioPausedFlag;
std::uintptr_t g_frameBlob;

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
inline std::uintptr_t Ptr(std::uintptr_t base, int off) {
    return reinterpret_cast<std::uintptr_t>(reinterpret_cast<char*>(base) + off);
}

// Widget pool element by slot, addressed as the original "dword_69FFB4 + 740*idx".
inline Widget& W(int slot) { return g_widgets[slot]; }

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
    g_hoverPrev = g_hoverLast = -1; g_hoverSlot = -1;
    g_hoverX = g_hoverY = g_hoverEngaged = g_hoverTick = 0;
    g_thumbStateId = 0; g_scrollThumbX = g_scrollThumbY = 0;
    g_hitTestSlot4 = -1;
    g_pendingMouseX = g_mouseX = g_mouseY = 0;
    g_pendingKey = 0;
    g_pendingMouseUp = g_mouseDown = g_mouseWheelUp = 0;
    g_mouseRelease = g_mouseDownHover = g_mouseMovedFlag = g_frameTick = 0;
    g_wndDeactivated = 0;
    g_wndAudioActive = g_renderRetryFlag = g_frameBlob2 = g_audioPausedFlag = 0;
    g_frameBlob = 0;
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
            int n = Mem<i32>(group, 26) >> 16;         // *(group+26)>>16
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
            if (g_caretWidget == -1) {
                ret = h->widgetCreateObjectThunk(g_caretPenX, caretX);
                g_caretWidget = ret;
            } else {
                ret = static_cast<unsigned char>(
                    Widget_LayoutBoundsBridge(g_caretPenX, caretX, g_caretWidget));
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
// Apply the pending keystroke to the focused edit field.
// ===========================================================================
int Widget_HandleKeyInput() {
    const GuiDialogs4Hooks* h = g_hooks;
    int ret = g_pendingMouseX >> 16;                   // dword_672210>>16
    if (!g_focusWidget || !g_pendingKey)               // !dword_62D328 || !byte_67225C
        return ret;

    unsigned key = g_pendingKey;
    ret = static_cast<unsigned char>(key);

    bool handledNav = false;
    if (key == kKeyEnter) {                             // 0x1C tab/enter navigate
        // (navigation walks the group; modeled minimally — clears focus on plain enter)
        g_focusWidget = 0;
        g_focusFlag = 0;
        g_pendingKey = 0;
        handledNav = true;
    } else if (key == kKeyBackspace) {                 // 0x0E delete last char
        ret = 0;
        char* buf = reinterpret_cast<char*>(Ptr(g_focusWidget, 40));
        std::size_t len = std::strlen(buf) + 1;
        if (static_cast<int>(len - 1) > 0) {
            ret = static_cast<unsigned char>(g_focusWidget);
            buf[len - 2] = 0;
        }
        g_pendingKey = 0;
        handledNav = true;
    } else if (key >= 2 && key <= 0x0B
               && (Mem<u8>(g_focusWidget, 38) & 2) != 0) {
        // numeric digit into a flag-2 (numeric) field
        h->animStateUpdate(Mem<i32>(g_focusWidget, 296),
                           reinterpret_cast<void*>(Ptr(g_focusWidget, 40)), 0xA);
    } else if ((Mem<u8>(g_focusWidget, 38) & 2) == 0) {
        // printable into a text field
        char* buf = reinterpret_cast<char*>(Ptr(g_focusWidget, 40));
        std::size_t len = std::strlen(buf) + 1;
        std::size_t pos = len - 1;
        int cap = (Mem<u8>(g_focusWidget, 38) & 1) != 0
                      ? Mem<i32>(g_focusWidget, 24)
                      : Mem<u8>(g_focusWidget, 36);
        if (static_cast<int>(len - 1) < cap) {
            int sc = h->inputCharToScancode(0, 0, static_cast<int>(key), 0);
            buf[pos] = static_cast<char>(sc);
            buf[pos + 1] = 0;
        }
        g_pendingKey = 0;
    }

    if (g_focusWidget) {                               // LABEL_7: recompute caret pen
        if ((Mem<u8>(g_focusWidget, 38) & 2) != 0)
            Mem<i32>(g_focusWidget, 296) = h->utilParseInt(
                reinterpret_cast<const char*>(Ptr(g_focusWidget, 40)));
        int w = h->propertyGet(reinterpret_cast<const char*>(Ptr(g_focusWidget, 40)),
                    Mem<i32>(g_focusWidget, 308));
        g_caretPenX = Mem<i32>(g_focusWidget, 4) + w;
        ret = g_caretPenX;
    }
    (void)handledNav;
    return ret;
}

// ===========================================================================
// 0x420db4 — VIBE_Widget_ProcessMouseDrag (per-frame drag/focus FSM)
// The render/scroll edges go through hooks; the deterministic state mutations
// (arm/disarm, clear focus on release, tooltip copy) are translated directly.
// ===========================================================================
int Widget_ProcessMouseDrag() {
    const GuiDialogs4Hooks* h = g_hooks;
    int mouseX = g_pendingMouseX >> 16;

    if (g_dragArmed && !g_pendingMouseUp
        && (!g_mouseDown || !g_focusWidget || (Mem<u8>(g_focusWidget, 38) & 2) == 0)) {
        g_dragArmed = 0;
        if (g_focusWidget) {
            int slot = Mem<i32>(g_focusWidget, 304);
            int base = g_caretBaseW;
            g_focusWidget = 0;
            W(slot).at<i32>(40) = 0;
            if (base != -1) g_caretBaseW = -1;
        }
    }

    if (!g_mouseDown) g_focusFlag = 0;

    if (!g_focusWidget && g_caretWidget != -1) {
        Widget_DestroyByTypeBridge(g_caretWidget, -1, mouseX);
        g_caretWidget = -1;                            // dword_62D350 = v4 (the call result; -1 model)
    }

    if (g_mouseDown && g_focusWidget) g_focusFlag = 1;

    // Slider step accumulation when the focused field is a held slider.
    if (g_focusWidget && g_hitTestSlot4 == Mem<i32>(g_focusWidget, 304)
        && (Mem<u8>(g_focusWidget, 38) & 2) != 0 && g_mouseWheelUp) {
        int v = Mem<i32>(g_focusWidget, 296);
        if (Mem<i32>(g_focusWidget, 24) > v && v != -1
            && Mem<i32>(g_focusWidget, 312) != -1)
            Mem<i32>(g_focusWidget, 296) += h->sliderComputeStep(static_cast<int>(g_focusWidget));
    }

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
// ===========================================================================
int Widget_HoverUpdate() {
    const GuiDialogs4Hooks* h = g_hooks;
    int result = -1;                                   // v0 = -1

    int mx = g_mouseX >> 16;                           // dword_75BF46>>16 hi-word usage
    int my = g_mouseY >> 16;

    if (g_hoverPrev != g_hoverLast
        || mx < g_hoverX - 5 || my < g_hoverY - 5
        || mx > g_hoverX + 5 || my > g_hoverY + 5
        || g_mouseMovedFlag) {
        g_hoverSlot = -1;
        g_hoverTick = g_frameTick;
        g_hoverX = mx; g_hoverEngaged = 0; g_hoverY = my;
    } else if (g_hoverSlot != -1) {
        goto label7;
    }

    if (g_hoverPrev != -1 && g_mouseDownHover) {
        g_hoverSlot = g_hoverPrev;
        g_hoverX = mx; g_hoverEngaged = 1; g_hoverY = my;
    }
label7:
    if (g_hoverSlot != -1 && g_hoverEngaged && !g_mouseDownHover) {
        g_hoverSlot = -1;
        g_hoverTick = g_frameTick;
        g_hoverX = mx; g_hoverY = my; g_hoverEngaged = 0;
        h->inputClearMouseButtons(16);
    }
    g_hoverLast = g_hoverPrev;

    if (g_hitTestSlot4 != -1) result = g_hitTestSlot4;
    return result < 0 ? -1 : result;
}

// ===========================================================================
// 0x518efc — VIBE_Widget_AddPersonRow  (INTEGRATION ANCHOR)
// Builds a 3-cell market row: two gfx cells + a money-formatted price label.
// Wires the REAL world::MoneyFormatWithSeparators + gui::Object_AddTextLabel.
// ===========================================================================
int Widget_AddPersonRow(i16 x, int gfxId, i16 y2, int winSlot, unsigned char rate) {
    const GuiDialogs4Hooks* h = g_hooks;

    // Cell 1: base gfx at y2.
    Object_AddToWindow(winSlot, y2, 0, gfxId);
    int yTop = y2;
    // Cell 2: gfx+2 a few px below.
    int mid = Object_AddToWindow(winSlot, static_cast<i16>(yTop + 2), 0, gfxId + 2);
    // Cell 3 (decorative): gfx+53.
    Object_AddToWindow(winSlot, static_cast<i16>(yTop + 53), 0, gfxId + 53);
    W(mid).at<i32>(72) = 1;                              // +72 = 1 (clickable)

    // Look up the cached market price for this item type, truncate, format.
    double price = h->lookupCachedMarketPrice(gfxId, rate);
    h->coordConvertX(price);
    int amount = static_cast<int>(price);
    std::string text = world::MoneyFormatWithSeparators(amount, rate ? rate : 1);

    int lbl = Object_AddTextLabel(static_cast<i16>(x + 7),
                                  static_cast<i16>(yTop + 58),
                                  g_currentWindowId, text.c_str());
    if (lbl >= 0) {
        Widget& lw = W(lbl);
        lw.w() = 36;                                    // +20 = 36
        lw.at<i16>(112) = 67;                           // +112 = 'C'
        lw.at<i32>(92) = 1;                             // +92 = 1
    }
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
    int gx = (off == -1) ? (Mem<i32>(widgetPtr, 14) >> 16)
                         : off + (Mem<i32>(widgetPtr, 14) >> 16);
    int span = Mem<i32>(widgetPtr, 460);                // v1[115]
    int gy = (span == -1) ? (Mem<i32>(widgetPtr, 16) >> 16) - 16
                          : span + (Mem<i32>(widgetPtr, 16) >> 16);
    int st = Mem<i32>(widgetPtr, 440);                  // v1[110]
    if (!st) st = g_defaultFont + 1;                    // dword_62D2B0 + 1
    h->stateFinalize(st);

    if ((Mem<u8>(widgetPtr, 444) & 4) != 0)
        return h->animApply((Mem<i32>(widgetPtr, 14) >> 16) - 48,
                            (Mem<i32>(widgetPtr, 20) >> 16) + (Mem<i32>(widgetPtr, 16) >> 16),
                            Mem<i32>(widgetPtr, 448) + 96, 0,
                            reinterpret_cast<const char*>(Ptr(widgetPtr, 184)), state);
    return h->animApply(gx - 48, gy, Mem<i32>(widgetPtr, 448) + 96, 0,
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
    int xc = (Mem<i32>(widgetPtr, 14) >> 16) - 32;
    h->coordPush(0, 0, g_screenClipW >> 16, g_screenClipExt >> 16);
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

    h->stateFinalize(w.at<i32>(110) >> 16);
    int handle = w.dataPtr();                            // v16 = *(v2+12)
    std::uintptr_t t0 = h->coordTransform(handle, 0);
    std::uintptr_t t1 = h->coordTransform(handle, 1);
    std::uintptr_t t2 = h->coordTransform(handle, 2);
    int trackLen = (w.at<i32>(18) >> 16) - Mem<u16>(t0, 6) - Mem<u16>(t1, 6);
    if (w.at<i32>(40)) frameBase = 3;

    h->animBasic(w.at<i32>(14) >> 16, w.at<i32>(16) >> 16, handle, surf, frameBase);
    h->animBasic((w.at<i32>(14) >> 16) + (w.at<i32>(18) >> 16) - Mem<u16>(t1, 6),
                 w.at<i32>(16) >> 16, handle, surf, frameBase + 1);
    int x0 = (w.at<i32>(14) >> 16) + Mem<u16>(t0, 6);
    h->coordPush(x0 - 1, g_drawClipTop, g_drawClipBottom, x0 + trackLen);
    unsigned step = Mem<u16>(t2, 6);
    if (step) {
        for (int i = 0; i < trackLen / static_cast<int>(step); ++i) {
            int seg = static_cast<int>(step) * i;
            h->animBasic(Mem<u16>(t0, 6) + (w.at<i32>(14) >> 16) + seg,
                         w.at<i32>(16) >> 16, handle, surf, frameBase + 2);
        }
    }
    return h->animApply(w.at<i32>(14) >> 16,
                        (w.at<i32>(16) >> 16) - g_defaultCtrlH / 2 + Mem<u16>(t0, 10) / 2 + 1,
                        w.at<i32>(18) >> 16, surf,
                        reinterpret_cast<const char*>(&w.at<char>(120)), glyph);
}

// 0x4186d8 — VIBE_Window_RenderContent
int Window_RenderContent(std::uintptr_t widgetPtr, void* surf) {
    const GuiDialogs4Hooks* h = g_hooks;
    int isurf = static_cast<int>(reinterpret_cast<std::intptr_t>(surf));

    if ((Mem<u8>(widgetPtr, 13) & 4) != 0)
        h->surfaceColorFill(Mem<i32>(widgetPtr, 2) >> 16, Mem<i32>(widgetPtr, 4) >> 16,
                            Mem<i32>(widgetPtr, 8) >> 16, Mem<i32>(widgetPtr, 6) >> 16, isurf);

    if ((Mem<u8>(widgetPtr, 12) & 2) != 0)
        h->surfaceRectOutline(Mem<i32>(widgetPtr, 2) >> 16, Mem<i32>(widgetPtr, 4) >> 16,
                              (Mem<i32>(widgetPtr, 8) >> 16) - 1, (Mem<i32>(widgetPtr, 6) >> 16) - 1,
                              0xFF, 0xFF, 0xFF, isurf);

    if (Mem<i32>(widgetPtr, 904) != -1) {
        int anim = h->stateUpdate(Mem<i32>(widgetPtr, 904));
        if (anim) {
            int x0 = Mem<i32>(widgetPtr, 2) >> 16;
            int y0 = Mem<i32>(widgetPtr, 4) >> 16;
            h->coordPush(x0, y0, y0 + (Mem<i32>(widgetPtr, 8) >> 16),
                         (Mem<i32>(widgetPtr, 6) >> 16) + x0);
            // tiled fill loop bound preserved structurally; body draws via hook.
            int tileW = Mem<i32>(widgetPtr, 6) >> 16;
            int tileH = Mem<i32>(widgetPtr, 8) >> 16;
            (void)tileW; (void)tileH;
            h->coordPush(0, 0, 0, 0);
        }
    }

    int result = static_cast<int>(widgetPtr);
    std::uintptr_t child = Mem<std::uintptr_t>(widgetPtr, 40);
    if (child) {
        return h->resultFinalize(0, 0,
            Mem<i32>(child, 8), Mem<i32>(child, 4),
            static_cast<int>(child), Mem<i32>(widgetPtr, 2) >> 16,
            Mem<i32>(widgetPtr, 4) >> 16, isurf, 1);
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
