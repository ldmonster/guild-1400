// guild::gui — gui_widgetn implementation (P6 / Wave 29 GUI coverage slice).
// 1:1 reconstructions of the deferred VIBE_Widget_* / VIBE_StatusText_* /
// VIBE_Scroll_* / VIBE_InfoPanel_* leaves. See gui_widgetn.h for provenance.

#include "gui/gui_widgetn.h"

#include "gui/object.h"          // g_widgets (dword_69FFB4) — real model
#include "gui/form_lifecycle.h"  // Form_Destroy, Widget_DestroyByType (real siblings)

#include <cstdint>
#include <cstring>

namespace guild::gui {

// ===========================================================================
// Module-owned globals.
// ===========================================================================
std::int32_t g_statusTextMarker[kStatusTextCountN] = {0}; // dword_11B5158 column
int g_infoPanelForm = -1;                                  // dword_631768
int g_infoPanelChild[kInfoPanelChildren] = {-1, -1, -1, -1}; // dword_631774
int g_infoPanelHandle[10] = {-1, -1, -1, -1, -1, -1, -1, -1, -1, -1};

// dword_631614 — the screen-transition / quit latch (modeled locally, as every
// other reimpl module does; the live process shares one global).
int g_scrollQuitLatch = 0; // dword_631614 (mirror for this loop)

// Screen-size global dword_69FFBC (hiword = screen height, used as a fallback
// center origin / clip). The real value comes from the display-mode setup; for the
// headless model it is a settable, deterministic mirror.
std::uint32_t g_screenSizeDword = 0; // dword_69FFBC

// ===========================================================================
// Hooks (inert defaults).
// ===========================================================================
namespace {

void DefWidgetLayoutBounds(int, int, int) {}      // render-coupled; no-op headless
void DefScrollOpen(void*) {}
void DefScrollUpdateAnimation() {}
int  DefScrollClose() { return 0; }
int  DefGameLogicRunFrameLoop(int, int, const void*) { return 0; } // exit loop at once
int  DefReadMouseRelease() { return 0; }          // dword_672230

const GuiWidgetNHooks kDefaultHooks = {
    &DefWidgetLayoutBounds,
    &DefScrollOpen, &DefScrollUpdateAnimation, &DefScrollClose,
    &DefGameLogicRunFrameLoop, &DefReadMouseRelease,
};

const GuiWidgetNHooks* g_hooks = &kDefaultHooks;

} // namespace

const GuiWidgetNHooks* SetGuiWidgetNHooks(const GuiWidgetNHooks* hooks) {
    const GuiWidgetNHooks* prev = g_hooks;
    g_hooks = hooks ? hooks : &kDefaultHooks;
    return prev;
}
const GuiWidgetNHooks* GuiWidgetNHooks_Default() { return &kDefaultHooks; }
const GuiWidgetNHooks& GuiWidgetNHooksActive() { return *g_hooks; }

void ResetGuiWidgetN() {
    std::memset(g_statusTextMarker, 0, sizeof(g_statusTextMarker));
    g_infoPanelForm = -1;
    for (int i = 0; i < kInfoPanelChildren; ++i) g_infoPanelChild[i] = -1;
    for (int i = 0; i < 10; ++i) g_infoPanelHandle[i] = -1;
    g_scrollQuitLatch = 0;
    g_screenSizeDword = 0;
    g_hooks = &kDefaultHooks;
}

// ===========================================================================
// 0x412480 — VIBE_Widget_RefreshText.
//   v3 = &widget[idx];
//   if (widget[+88] != 0) {            // text-present flag
//     v4 = v3[11];                     // +44 parent record dword
//     if (v4)  LayoutBounds(parent.midX - text.halfW, widget[+18], idx);
//     else     LayoutBounds(screen.midX - text.halfW, widget[+18], idx);
//   }
// The text width is widget[+16].hiword (the original reads *(int*)(v3+18)>>16, i.e.
// the high 16 bits of the dword at +16 — the measured text width); halfW = width/2.
// The parent mid-x = parent[+2].hiword + (parent[+6]>>16)/2  (parent x + w/2).
// y = widget[+16].hiword? No — original uses HIWORD(v3[4]) == widget[+16] high word
// for the LayoutBounds y argument (the stored text y).
// ===========================================================================
int Widget_RefreshTextN(int widgetIdx) {
    Widget& w = g_widgets[widgetIdx];
    // +88 text-present flag.
    if (w.at<std::int32_t>(88) == 0)
        return 0;

    // text half-width: high 16 bits of dword at +16  (== widget +18 word, signed >>16
    // of the +18-aligned dword in the decompile: *(int*)((char*)v3 + 18) >> 16).
    std::int32_t textDword18;
    std::memcpy(&textDword18, w.raw + 18, sizeof(textDword18));
    int halfW = (textDword18 >> 16) / 2;

    // y argument: HIWORD(v3[4]) == high word of the dword at +16.
    std::int32_t dword16;
    std::memcpy(&dword16, w.raw + 16, sizeof(dword16));
    std::int16_t y = static_cast<std::int16_t>(dword16 >> 16);

    int parent = w.at<std::int32_t>(44);  // v3[11]
    int x;
    if (parent != 0) {
        // parent fields live in the same 740-stride widget array (the +44 link is a
        // widget index in this model). parent[+2].hiword + (parent[+6]>>16)/2.
        Widget& p = g_widgets[parent];
        std::int32_t pX, pW;
        std::memcpy(&pX, p.raw + 2, sizeof(pX));
        std::memcpy(&pW, p.raw + 6, sizeof(pW));
        x = (pX >> 16) + (pW >> 16) / 2 - halfW;
    } else {
        x = static_cast<int>(g_screenSizeDword >> 16) / 2 - halfW;
    }

    GuiWidgetNHooksActive().widgetLayoutBounds(x, y, widgetIdx);
    // Original returns the low byte of LayoutBounds; our model returns 1 to signal
    // "refreshed" (the layout call's al result is not folded by any caller we model).
    return 1;
}

// ===========================================================================
// 0x412530 — VIBE_Widget_SetTextColor.
//   *(_WORD *)(base + 740*idx + 20) = color;
//   return VIBE_Widget_RefreshText(idx);
// ===========================================================================
int Widget_SetTextColor(int widgetIdx, std::int16_t color) {
    g_widgets[widgetIdx].at<std::int16_t>(20) = color;  // +20
    return Widget_RefreshTextN(widgetIdx);
}

// ===========================================================================
// 0x4bcc30 — VIBE_StatusText_ClearTable.
//   for (result = 0; result != 1600; dword_11B5158[result]=0) result += 50;
//   return result * 4;
// Zeros indices 0,50,100,...,1550 (the 32 entries' marker column). Returns 6400.
// ===========================================================================
int StatusText_ClearTable() {
    int result = 0;
    for (; result != kStatusTextStrideN * kStatusTextCountN; result += kStatusTextStrideN) {
        // result/50 is the entry; in the flat model each entry's marker is one slot.
        g_statusTextMarker[result / kStatusTextStrideN] = 0;
    }
    return result * 4;
}

// ===========================================================================
// 0x537318 — VIBE_Scroll_RunAnimationLoop.
//   Scroll_Open(self);
//   while (GameLogic_RunFrameLoop(415687, a2, a3)) {
//     Scroll_UpdateAnimation();
//     if (dword_672230) dword_631614 = <uninit ecx>;  // latch quit
//   }
//   return Scroll_Close();
// The original's `dword_631614 = v4` writes an uninitialized register; the intent
// (matching every sibling loop) is to latch the transition. We model it as = 1.
// ===========================================================================
int Scroll_RunAnimationLoop(void* self, int a2, const char* a3) {
    const GuiWidgetNHooks& h = GuiWidgetNHooksActive();
    h.scrollOpen(self);
    while (h.gameLogicRunFrameLoop(415687, a2, a3)) {
        h.scrollUpdateAnimation();
        if (h.readMouseRelease())
            g_scrollQuitLatch = 1;  // dword_631614
    }
    return h.scrollClose();
}

// ===========================================================================
// 0x4b8438 — VIBE_InfoPanel_Destroy.
//   if (dword_631768 != -1) {
//     for (i=0;i<4;++i) if (dword_631774[i] != -1) {
//       Widget_DestroyByType(dword_631774[i], dword_631774[i], -1);
//       dword_631774[i] = -1;
//     }
//     Form_Destroy(dword_631768);
//     dword_631768 = -1;
//     dword_63176C=dword_631770=dword_63179C=dword_631798=dword_631794=
//       dword_63178C=dword_631790=dword_631788=dword_631784 = -1;
//   }
// ===========================================================================
void InfoPanel_Destroy() {
    if (g_infoPanelForm == -1)
        return;
    for (int i = 0; i < kInfoPanelChildren; ++i) {
        int wIdx = g_infoPanelChild[i];
        if (wIdx != -1) {
            Widget_DestroyByType(wIdx, wIdx, -1);
            g_infoPanelChild[i] = -1;
        }
    }
    Form_Destroy(g_infoPanelForm);
    g_infoPanelForm = -1;
    for (int i = 0; i < 10; ++i)
        g_infoPanelHandle[i] = -1;
}

} // namespace guild::gui
