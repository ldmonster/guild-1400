#include "gui/markup_build.h"
#include "gui/widget_create.h"
#include "gui/window.h"
#include "gui/object.h"
#include "gui/text/markup.h"

#include <cstring>

namespace guild::gui {

int g_buttonRedGfx = -1; // VIBE_Property_Validate(aButtonRed), cached

void ResetMarkupBuild() { g_buttonRedGfx = -1; }

namespace {

// The original keeps the running text cursor in two globals while building:
//   dword_69FFA8 = pen x (absolute), dword_69FFAC = pen y (absolute).
// We mirror them as locals threaded through the build; the leaves take WINDOW-
// relative coords (they re-add the window origin), so we subtract it at each call.
struct BuildCursor {
    int penX = 0;     // dword_69FFA8
    int penY = 0;     // dword_69FFAC
    int lineH = 0;    // v199 (pending line-feed height from the last inline widget)
};

// _BUTTON_RED graphic id, resolved once (VIBE_Property_Validate(aButtonRed)).
int ButtonRedGfx() {
    if (g_buttonRedGfx == -1)
        g_buttonRedGfx = Property_Validate("_BUTTON_RED");
    return g_buttonRedGfx;
}

} // namespace

// gilde.exe 0x416720 — VIBE_Window_ParseMarkupAndBuild (object-build half).
//
// NOT-1:1 (hardening sweep, gui_03): the original is a 1961-instruction, 298-BB
// monolith with a FULLY INLINED markup state machine (pen globals dword_69FFA8/
// dword_69FFAC; inline handlers for $R/$L/$B/$Y/$T/$M/$F/$[/$]/$A/$C/$<…) and 23
// callees including VIBE_Object_AddButtonLabel(0x41b598), VIBE_Object_SetEditText
// (0x41b75c), VIBE_Input_SetIconTextById(0x40fb4c), VIBE_Window_Resize(0x41a0f8),
// VIBE_Window_NormalizeSpriteWidths(0x416658), VIBE_Window_LayoutScrollContent
// (0x41536c). This reconstruction is a STRUCTURAL APPROXIMATION, not a faithful
// translation. Known divergences vs the binary:
//   * Depends on guild::gui::text::TokenizeMarkup — no such separable tokenizer
//     exists in the original; the parse is inlined into 0x416720.
//   * The $t/$tt Input_AddFieldToWindow params here (8,0x10 / 16,0x82) do NOT
//     match the three real call sites (0x4176ff, 0x417810, 0x417a25), which
//     compute field geometry from pen positions and object metrics ([obj+4]>>16,
//     [obj+2]>>16) and pass different args (window, 1, ebx=0).
//   * The layout-only tokens ($R/$L/$B/$Y/$T/$M/$F/$</$=/literal text/%-codes)
//     are stubbed as no-ops; the original mutates pen/columns and blits glyphs.
// A faithful 1:1 requires reconstructing the inlined parser at 0x416720 together
// with text/markup.*. Flagged per Rule 8 (do not paper over with an analogue) —
// left as-is pending a dedicated reconstruction task; see progress/harden/gui_03.md.
std::vector<MarkupObject> BuildMarkupIntoWindow(
    int winSlot, const char* str, const std::vector<int>& pendingObjectIds) {

    std::vector<MarkupObject> out;
    if (winSlot < 0 || winSlot >= kMaxWindows)
        return out;
    Window& win = g_windows[winSlot];
    if (!win.enabled() || str == nullptr)
        return out;

    // Pen starts at the window content origin (winX, winY - scrollY), as the
    // original seeds dword_69FFA8/dword_69FFAC from v194 geometry minus v194[146].
    BuildCursor cur;
    cur.penX = win.x();                          // v202 baseline
    cur.penY = win.y() - win.scrollCur();        // v203 - v194[146]

    std::size_t nextPending = 0;                 // index into pendingObjectIds

    std::string tokErr;
    std::vector<guild::gui::text::MarkupToken> toks =
        guild::gui::text::TokenizeMarkup(str, &tokErr);

    using K = guild::gui::text::MarkupKind;

    for (const auto& t : toks) {
        switch (t.kind) {

        case K::Inline: { // "$i..."
            unsigned char sel = static_cast<unsigned char>(t.letter);
            if (sel == 'a' || sel == 'n') {
                // ---- red inline button sprite (_BUTTON_RED) ----
                int sprite = Widget_CreateSprite(
                    static_cast<i16>(cur.penX - win.x()),
                    static_cast<i16>(cur.penY - win.y()),
                    ButtonRedGfx(), /*mode=*/0);
                GameObject_AttachToWindow(sprite, winSlot);

                if (!t.text.empty())             // "$ia[label]" / "$in[label]"
                    Object_RecomputeSize(sprite); // size from the sprite's metrics/text

                if (sel == 'n')                  // "$in" -> radio flag
                    g_widgets[sprite].at<u8>(444) |= 0x10;

                // Advance the pen by the sprite width (+20); remember the line height
                // (+22 + 7) for the next $A/line-feed, exactly as v52/v199 do.
                cur.penX += g_widgets[sprite].w();
                cur.lineH = g_widgets[sprite].h() + 7;

                MarkupObject mo;
                mo.kind = MarkupObjectKind::RedButton;
                mo.widgetIdx = sprite;
                mo.selector = static_cast<char>(sel);
                mo.radio = (sel == 'n');
                out.push_back(mo);
            } else {
                // ---- embedded pending object id placed via Object_AddToWindow ----
                if (nextPending >= pendingObjectIds.size())
                    break;                       // no pending object: original skips
                int objId = pendingObjectIds[nextPending++];

                int idx = Object_AddToWindow(
                    winSlot,
                    static_cast<i16>(cur.penY - win.y()),
                    static_cast<i16>(cur.penX - win.x()),
                    objId);
                if (idx == -1)
                    break;
                Widget& c = g_widgets[idx];

                // selector switch (the v59 switch): set button/clickable flags.
                switch (sel) {
                    case 'c': c.at<i32>(72) = 0; c.at<i32>(68) = 1; break; // clickable
                    case 'i': c.at<i32>(72) = 0; c.at<i32>(68) = 0; break; // plain
                    case 'b': c.at<i32>(72) = 1; c.at<i32>(68) = 0; break; // toggle
                    case 't': case 'a': case 'n':
                        if (!t.text.empty())
                            Object_RecomputeSize(idx);
                        break;
                    default: break;
                }

                cur.penX += c.w();               // dword_69FFA8 += object width

                MarkupObject mo;
                mo.kind = MarkupObjectKind::Embedded;
                mo.widgetIdx = idx;
                mo.selector = static_cast<char>(sel);
                mo.objId = objId;
                out.push_back(mo);
            }
            break;
        }

        case K::EditField: { // "$t" numeric / "$tt" icon-text input field
            unsigned char sel = static_cast<unsigned char>(t.letter);
            int idx;
            if (sel == 't') {
                // "$tt": icon/text field -> Input_AddFieldToWindow(...,8,0x10)
                idx = Input_AddFieldToWindow(
                    cur.penX - win.x(), cur.penY - win.y(),
                    g_defaultCtrlH, /*value=*/8, /*flags=*/0x10, winSlot);
            } else {
                // "$t": numeric field -> Input_AddFieldToWindow(...,16,0x82)
                idx = Input_AddFieldToWindow(
                    cur.penX - win.x(), cur.penY - win.y(),
                    g_defaultCtrlH, /*value=*/16, /*flags=*/0x82, winSlot);
            }
            if (idx != -1) {
                cur.penX += g_widgets[idx].w();  // advance past the field
                MarkupObject mo;
                mo.kind = MarkupObjectKind::InputField;
                mo.widgetIdx = idx;
                mo.selector = static_cast<char>(sel);
                out.push_back(mo);
            }
            break;
        }

        // ---- Layout-only tokens: affect the pen / columns, create no widget. ----
        case K::LineFeed: {                      // "$A" advance N lines
            int n = (t.arg > 0 && t.arg < 9) ? t.arg : 1;
            int adv = cur.lineH ? cur.lineH : (g_defaultCtrlH * n);
            cur.lineH = 0;
            cur.penX = win.x();
            cur.penY += adv;
            break;
        }
        case K::Clear:                           // "$C" reset to content origin
            cur.penX = win.x();
            cur.penY = win.y() - win.scrollCur();
            break;
        case K::ColumnReset:                     // "$L"
        case K::ColumnRight:                     // "$R"
        case K::ColumnCenter:                    // "$B"
        case K::ColumnFull:                      // "$Y"
        case K::Tab:                             // "$T"
        case K::Embed:                           // "$M"
        case K::FontColor:                       // "$F" / "$FF"
        case K::BoundRight:                      // "$<"
        case K::BoundLeft:                       // "$="
        case K::BracketOpen:
        case K::BracketClose:
        case K::Text:                            // literal run: glyph-blit (deferred)
        case K::PercentCode:                     // inline value code (deferred)
        case K::Unknown:
        default:
            break;
        }
    }

    return out;
}

} // namespace guild::gui
