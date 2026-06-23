#pragma once
// widget_layout.{h,cpp} — widget bounds layout + text-width + dirty-rect accumulate
// (gilde.exe).
//
//   0x413220  VIBE_Widget_LayoutBounds  — recompute a widget's screen bounds, clamp to
//                                          the parent's clip + the screen, recurse into
//                                          a window's child widgets, stamp anchor tables
//   0x4152cc  VIBE_Property_Get         — measure a text string's pixel width
//   0x40e728  VIBE_State_GetCurrent     — clamp + append a dirty screen rectangle
//   0x5d8b00  VIBE_Coord_Transform      — pure leaf: glyph/metric record offset
//
// Widget_LayoutBounds and State_GetCurrent are reached by the per-frame entity render
// (render_submit.cpp / VIBE_Object_Update / VIBE_GameLogic_Interactions) and the whole
// GUI window/scroll/markup machinery; Property_Get measures label widths. They walk the
// engine's 740-byte widget array (dword_69FFB4 -> guild::gui::g_widgets, REUSED) and a
// handful of layout globals.
//
// ConvertX/fixed-point note: every clip comparison in these functions does an arithmetic
// `>> 16` on a 16.16 fixed-point dword and an integer `<=`/`>=` compare — NO float is
// involved (no ConvertX / fistp), so there is no rounding/truncation subtlety here; the
// shifts are reproduced exactly (sign-propagating `>>` on signed int).

#include "guild/common/types.h"
#include "gui/types.h"     // guild::gui::Widget, kWidgetStrideBytes (REUSED)

namespace guild::gui {

using guild::i16;
using guild::i32;
using guild::u8;
using guild::u16;

// ---------------------------------------------------------------------------
// 0x5d8b00 — VIBE_Coord_Transform(rec@eax, idx@dx).
// ---------------------------------------------------------------------------
// A tiny pure leaf: if `rec` is non-null it adds the dword stored at byte offset
// (4*idx + 69) within `rec` to `rec` and returns the result; otherwise returns 0.
// (In the GUI text path `rec` is a font/state record and the table at +69 maps a
// glyph code to that glyph's metric sub-record offset.) Reconstructed against a raw
// byte view so the unaligned `+69` read is byte-exact to the original.
//   if (rec) rec += *(u32*)(rec + 4*idx + 69);
//   return rec;
i32 CoordTransform(const u8* base, i32 rec, u16 idx);

// ---------------------------------------------------------------------------
// 0x4152cc — VIBE_Property_Get(text@eax, font@edx).
// ---------------------------------------------------------------------------
// Measures the pixel width of `text` rendered in font/state `font`. The original:
//   n     = strlen(text) + 1;            // includes the NUL in the loop bound (n-1)
//   state = VIBE_State_Update(font);     // realise the font/state record (-> handle)
//   w = 0;
//   for (i = 0; i < n-1; ++i) {
//       c = text[i];
//       if (c != '~') {                  // '~' = a colour/escape marker: skip, no width
//           g = VIBE_Coord_Transform(state, (u8)c);   // glyph metric record
//           if (i && text[i-1] != ' ')  w -= *(u16*)(g + 22);   // kern: pull back
//           if (c == ' ') w += dword_62D274 + dword_62D270 + *(u16*)(g + 26);
//           else          w += dword_62D274 +              *(u16*)(g + 26);
//       }
//   }
//   return w + dword_62D274;
// dword_62D274 (=2 default) is the inter-letter spacing; dword_62D270 (=8) is the extra
// space-character width. The glyph record's +22 = left-bearing/kern pullback, +26 =
// advance width. These globals + the State_Update realise + the per-glyph metric read
// are injected so this TU is self-contained.
struct TextMetricEnv {
    // VIBE_State_Update(font) -> the realised font/state record handle. (See
    // gui_object_state.h StateUpdate; the caller forwards it here.) 0 == no font.
    i32 (*stateUpdate)(void* ctx, int font) = nullptr;
    // *(u16*)(glyphRecord + byteOff): the glyph's metric word at +22 (kern) / +26
    // (advance). `glyph` is the value VIBE_Coord_Transform returned.
    u16 (*glyphWord)(void* ctx, i32 glyph, int byteOff) = nullptr;
    // VIBE_Coord_Transform(state, ch) -> glyph metric record handle.
    i32 (*coordTransform)(void* ctx, i32 state, u8 ch) = nullptr;
    void* ctx = nullptr;
    i32 letterSpacing = 2;   // dword_62D274
    i32 spaceExtra    = 8;   // dword_62D270
};
// Returns the measured pixel width (>= letterSpacing; never negative for normal text).
i32 PropertyGet(const TextMetricEnv& env, const char* text, int font);

// ---------------------------------------------------------------------------
// 0x40e728 — VIBE_State_GetCurrent(x@eax, y@edx, h@ecx, w@ebx).
// ---------------------------------------------------------------------------
// Clamps the rectangle (x, y, w, h) to the active clip window, and — if it still has
// positive extent AND it pokes outside the inner "scroll-safe" window — appends it to
// the active dirty-rect bucket (16-byte rects: [x, y, w, h]). Returns the (possibly
// clamped) x. The original's __usercall packs the args as (x@eax, y@edx, h@ecx, w@ebx);
// note the ODD x-alignment fix: when x is odd it bumps w by 2, decrements x, then forces
// w even (`w &= ~1`) — a 2-pixel even-alignment the software blitter requires.
//
// The clip rect (dword_64A1B4/B8/BC/C0) and the inner scroll window (dword_69FF80/84/
// 88/8C) plus the active dirty-rect bucket are injected via DirtyRectState.
struct DirtyRect { i32 x, y, w, h; };   // 16-byte rect, [x,y,w,h] as appended

struct DirtyRectState {
    // The active bucket's rect buffer + live count (dword_62D2D0[bucket] / dword_62D2E0
    // [bucket]). Append stops at 512 (0x200) entries (the original's hard cap).
    DirtyRect* rects = nullptr;
    int        count = 0;            // dword_62D2E0[active]
    int        capacity = 512;       // the 0x200 cap

    // Clip window (dword_64A1B4=left, B8=top, BC=right, C0=bottom). Defaults are the
    // 640x480 screen the binary ships with (BC=0x280, C0=0x1E0).
    i32 clipLeft = 0;     // dword_64A1B4
    i32 clipTop  = 0;     // dword_64A1B8
    i32 clipRight = 640;  // dword_64A1BC
    i32 clipBottom = 480; // dword_64A1C0

    // Inner scroll-safe window (dword_69FF80=left, 84=top, 88=right, 8C=bottom). A rect
    // wholly inside this window is NOT added (it is already covered); a rect that pokes
    // outside is. Defaults 0 (whole screen dirty) match BSS.
    i32 scrollLeft = 0;   // dword_69FF80
    i32 scrollTop  = 0;   // dword_69FF84
    i32 scrollRight = 0;  // dword_69FF88
    i32 scrollBottom = 0; // dword_69FF8C
};
// Returns the clamped x (the original's eax result).
i32 StateGetCurrent(DirtyRectState& st, i32 x, i32 y, i32 h, i32 w);

// ---------------------------------------------------------------------------
// 0x413220 — VIBE_Widget_LayoutBounds(x@ax, y@dx, widget@ebx).
// ---------------------------------------------------------------------------
// Repositions widget `widget` to screen (x, y), recomputes its clip bounds against its
// parent's bounds (the +44 link) and the screen extent globals, then dispatches on its
// type byte (+24):
//   type '@' (0x40)  window-backing: copy its w/h (+20/+22) into the bound quad, then
//                    walk the owning window's child id list (dword_67EB80 window record,
//                    238-dword stride; child ids at *(window+24), count at window+26 hi)
//                    and recurse LayoutBounds on each, offset by the child's local x/y.
//   type 'A' (0x41)  3D/anim: stamp the +14 / +16 hi-words into the anchor tables
//                    dword_695084 / dword_695088 (87-dword stride, by ownerWindow +116).
//   type 4           stamp x/y into the corner anchor table word_140642D / word_140642F
//                    (17-word stride, by ownerWindow +116).
//   other            done.
// Returns the type byte (the original's `al`/`v17`).
//
// The widget array is guild::gui::g_widgets (REUSED). The owning Window record array
// (dword_67EB80) + the three anchor tables + the parent-bounds (+44) pointer + the two
// screen-extent globals (dword_69FFB8 packed top.hi-word/left, dword_69FFBC packed
// bottom/right) are injected via LayoutEnv (the caller owns the real globals; same
// decoupling render_submit.h uses).
//
// PARENT BOUNDS (the +44 link): an 8-byte 16.16 fixed-point quad the original reads at
// byte offsets +2/+4/+6/+8 of the linked record. `parentBoundsDword(handle, off)`
// returns *(i32*)(handle + off); `parentBoundsWord(handle, off)` returns *(u16*)(...).
struct LayoutEnv {
    // The owning Window record array base (dword_67EB80, 952-byte stride). The '@'
    // (window-backing) path reads the window's child id-list pointer (window+24),
    // child count (window+26 hi-word), child origin (window+4/+6) to offset recursion.
    // `windowChildId(win, n)` returns the n-th child widget id; `windowChildCount(win)`
    // the count; `windowOriginX/Y(win)` the child-origin words. `win` here is the
    // widget's ownerWindow field (+116).
    int (*windowChildCount)(void* ctx, int win) = nullptr;     // *(int*)(win+26) >> 16
    int (*windowChildId)(void* ctx, int win, int n) = nullptr; // *(*(int*)(win+24) + 4*n)
    // OLD child-origin, captured before the recursion stamps the new one: the original
    // takes v26 = *(int*)(win+2) >> 16 (origin X) and v25 = *(int*)(win+4) >> 16
    // (origin Y), then OVERWRITES the window's origin words (win+4 := a1, win+6 := a2)
    // and re-lays children relative to the new origin (a1,a2) minus the old (v26,v25).
    i32 (*windowOriginX)(void* ctx, int win) = nullptr;        // *(int*)(win+2) >> 16
    i32 (*windowOriginY)(void* ctx, int win) = nullptr;        // *(int*)(win+4) >> 16
    void (*windowStampOrigin)(void* ctx, int win, i16 x, i16 y) = nullptr; // win+4:=x,+6:=y

    // The parent-bounds (+44) link. `handle` is the widget's +44 dword value; 0 => no
    // parent (the original skips the whole clamp block when +44 == 0).
    i32 (*parentBoundsDword)(void* ctx, i32 handle, int off) = nullptr; // *(i32*)(h+off)
    u16 (*parentBoundsWord)(void* ctx, i32 handle, int off) = nullptr;  // *(u16*)(h+off)

    // Anchor tables. typeA: dword_695084[87*win] = x.hi, dword_695088[87*win] = y.hi.
    // type4: word_140642D[17*win] = x, word_140642F[17*win] = y.
    void (*anchorTypeA)(void* ctx, int win, i32 xHi, i32 yHi) = nullptr;
    void (*anchorType4)(void* ctx, int win, i16 x, i16 y) = nullptr;

    void* ctx = nullptr;

    // Screen extent globals. dword_69FFB8: low word = left bound; high word (offset +2)
    // = a top bound used by the '@'-path clamp. dword_69FFBC: low word = right bound,
    // high word (+2) = bottom bound. The original reads these as packed 16.16-ish; we
    // model the two halves explicitly. Defaults 0 match BSS (set at display init).
    i32 screenExtB8 = 0;   // dword_69FFB8 (low=left, hi-word @+2 = top extent)
    i32 screenExtBC = 0;   // dword_69FFBC (low=right, hi-word @+2 = bottom extent)
};
// Returns the resolved type byte (>= 0). `widget` indexes g_widgets.
u8 WidgetLayoutBounds(const LayoutEnv& env, i16 x, i16 y, int widget);

} // namespace guild::gui
