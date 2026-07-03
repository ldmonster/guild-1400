#include "gui/window_render.h"
#include "gui/window.h"
#include "gui/object.h"

#include <cstdint>

// GUILD_WEAK: weak "default edge" on ELF (test TUs override these); strong on
// PE/COFF (MinGW), which has no usable weak-definition support — there are no
// test overrides in the Windows app build, so the defaults must be strong.
#if defined(_WIN32)
#define GUILD_WEAK
#else
#define GUILD_WEAK __attribute__((weak))
#endif

namespace guild::gui {

// ---- Forward-declared edges: default no-op / neutral (tests override) ------
// All marked weak so a test TU can supply counting/behavioural definitions.
void GUILD_WEAK Widget_LayoutBounds(int /*x*/, int /*y*/, int /*widgetIdx*/) {}
void GUILD_WEAK Widget_RefreshText(int /*widgetIdx*/) {}
void GUILD_WEAK GameObject_DispatchInteractions() {}
int  GUILD_WEAK DecompressState_Blob() { return 0; }
void GUILD_WEAK GameLogic_Interactions() {}
void GUILD_WEAK Decompression_Finalize() {}
int  GUILD_WEAK Render_PresentFrame() { return 0; }

// Window scroll-state field byte offsets (the originals address v1[145..151]).
namespace {
inline constexpr int kOffScrollMaxY  = 580; // dword[145]
inline constexpr int kOffScrollY     = 584; // dword[146]
inline constexpr int kOffScrollPrevY = 588; // dword[147]
inline constexpr int kOffScrollVelY  = 592; // dword[148]
inline constexpr int kOffScrollVelX  = 596; // dword[149]
inline constexpr int kOffScrollX     = 600; // dword[150]
inline constexpr int kOffScrollPrevX = 604; // dword[151]
inline constexpr int kOffScrollStep  = 608; // byte
// Window clip words used to reflow a child (word indices 292/294/300/302 = scroll
// fields read as words for the LayoutBounds origin math).
inline constexpr int kOffWordScrollY     = 584; // *((WORD*)v1+292)
inline constexpr int kOffWordScrollPrevY = 588; // *((WORD*)v1+294)
inline constexpr int kOffWordScrollX     = 600; // *((WORD*)v1+300)
inline constexpr int kOffWordScrollPrevX = 604; // *((WORD*)v1+302)
} // namespace

// gilde.exe 0x4146ac — VIBE_Window_RenderUpdates ()
int Window_RenderUpdates() {
    GameObject_DispatchInteractions();      // 0x40e6c0
    int pending = DecompressState_Blob();   // 0x423500
    if (pending) {
        GameLogic_Interactions();           // 0x4139a8
        Decompression_Finalize();           // 0x4235dc
        return Render_PresentFrame();        // 0x4349e4
    }
    return pending;
}

// gilde.exe 0x4163bc — VIBE_Window_ApplyScrollOffset (winSlot@eax)
int Window_ApplyScrollOffset(int winSlot) {
    Window& w = g_windows[winSlot];                      // &dword_67EB80[238*a1]
    const int step = w.raw[kOffScrollStep];              // *((BYTE*)v1 + 608)

    // ---- Vertical: step scrollY by `step` toward velocity, clamp at 0..maxY. ----
    int velY = w.at<i32>(kOffScrollVelY);                // v1[148]
    if (velY < 0) {
        w.at<i32>(kOffScrollY) -= step;                  // v1[146] -= step
        int v = step + velY;                             // v15 = step + v1[148]
        w.at<i32>(kOffScrollVelY) = v;                   // v1[148] = v15
        if (v > 0) {                                     // overshot: fold remainder back
            w.at<i32>(kOffScrollVelY) = 0;
            w.at<i32>(kOffScrollY) += v;                 // v1[146] = v + v1[146]
        }
        if (w.at<i32>(kOffScrollY) < 0) {                // clamp at top
            w.at<i32>(kOffScrollY)    = 0;
            w.at<i32>(kOffScrollVelY) = 0;
        }
    } else if (velY > 0) {
        w.at<i32>(kOffScrollY) += step;                  // v1[146] += step
        int v = velY - step;                             // v5 = v1[148] - step
        w.at<i32>(kOffScrollVelY) = v;                   // v1[148] = v5
        if (v < 0) {                                     // overshot
            w.at<i32>(kOffScrollVelY) = 0;
            w.at<i32>(kOffScrollY) += v;                 // v1[146] = v5 + v1[146]
        }
        int maxY = w.at<i32>(kOffScrollMaxY);            // v1[145]
        if (w.at<i32>(kOffScrollY) > maxY) {             // clamp at bottom
            w.at<i32>(kOffScrollVelY) = 0;
            w.at<i32>(kOffScrollY)    = maxY;
        }
    }

    // ---- Horizontal: same step (no clamp on the negative-overshoot fast path). ----
    int velX = w.at<i32>(kOffScrollVelX);                // v1[149]
    if (velX < 0) {
        w.at<i32>(kOffScrollX) -= step;                  // v1[150] -= step
        int v = step + velX;                             // v20 = step + v1[149]
        w.at<i32>(kOffScrollVelX) = v;                   // v1[149] = v20
        if (v > 0) {
            w.at<i32>(kOffScrollVelX) = 0;
            w.at<i32>(kOffScrollX) += v;                 // v1[150] = v20 + v1[150]
        }
    } else if (velX > 0) {
        w.at<i32>(kOffScrollX) += step;                  // v1[150] += step
        w.at<i32>(kOffScrollVelX) = velX - step;         // v1[149] = v8 - step
    }

    // ---- If the applied scroll changed since last frame, reflow the children. ----
    if (w.at<i32>(kOffScrollY) != w.at<i32>(kOffScrollPrevY) ||
        w.at<i32>(kOffScrollX) != w.at<i32>(kOffScrollPrevX)) {
        // Start index skips the background (flag 0x8) and scrollbar (flag 0x20) objects.
        int start = (w.flags() & 0x8) != 0;              // v10 = (flags&8)!=0
        if ((w.flags() & 0x20) != 0)
            start = ((w.flags() & 0x8) != 0) + 2;        // v10 = ... + 2
        i32* list = WindowChildList(winSlot);            // v1[6]
        for (int k = start; k < w.objCount(); ++k) {
            Widget& c = g_widgets[list[k]];              // dword_69FFB4 + 740*list[k]
            // 0x416631: nx = word@604(prevX) - word@600(scrollX) + child.x(+16)
            //           ny = child.y(+18) + word@588(prevY) - word@584(scrollY)
            int nx = w.at<i16>(kOffWordScrollPrevX) - w.at<i16>(kOffWordScrollX) + c.x();
            int ny = c.y() + w.at<i16>(kOffWordScrollPrevY) - w.at<i16>(kOffWordScrollY);
            Widget_LayoutBounds(nx, ny, list[k]);        // VIBE_Widget_LayoutBounds(...)
            if (c.type() == 17)                          // *(v23+24) == 17 -> mark redraw
                c.at<i32>(116) = 2;                      // *(v23+116) = 2
        }
    }

    // Snapshot the applied scroll as "previous".
    w.at<i32>(kOffScrollPrevY) = w.at<i32>(kOffScrollY);  // v1[147] = v1[146]
    int result = w.at<i32>(kOffScrollX);                  // v1[150]
    w.at<i32>(kOffScrollPrevX) = result;                  // v1[151] = result
    return result;
}

// gilde.exe 0x416658 — VIBE_Window_NormalizeSpriteWidths (winSlot@eax)
void Window_NormalizeSpriteWidths(int winSlot) {
    Window& w = g_windows[winSlot];                      // &dword_67EB80[238*a1]
    i32* list = WindowChildList(winSlot);                // v4[6]

    // Pass 1: find the widest sprite (type 9). The original reads `*(int*)(v5+18)>>16`,
    // a misaligned dword whose high word is the WIDTH word at +20 (== Widget::w()).
    int widest = 0;                                      // v1
    for (int k = 0; k < w.objCount(); ++k) {
        Widget& c = g_widgets[list[k]];                  // dword_69FFB4 + 740*list[k]
        if (c.type() == 9 && c.w() > widest)             // *(v5+24)==9 && *(v5+18)>>16 > v1
            widest = c.w();
    }
    int targetW = widest + 8;                            // v6 = v1 + 8
    if (targetW < 128)
        targetW = 128;                                   // LOWORD(v6) = 128
    i16 w12 = static_cast<i16>(targetW);                 // v12

    // Pass 2: stamp every sprite child with the common width + flag + refresh.
    for (int k = 0; k < w.objCount(); ++k) {
        int idx = list[k];
        Widget& c = g_widgets[idx];
        if (c.type() == 9) {                             // *(v10+24)==9
            c.at<i32>(88) = 1;                           // *(v10+88) = 1 (width overridden)
            c.w() = w12;                                 // *(... +20) = v12
            Widget_RefreshText(idx);                     // VIBE_Widget_RefreshText(idx)
        }
    }
}

} // namespace guild::gui
