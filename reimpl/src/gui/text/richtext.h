#pragma once
// Guild text module — the rich-string FORMAT/PARSE engine (string production).
//
// VIBE_Text_RenderRichString @0x59d6e8 (8882 B, 1172 xrefs) and its near-twin
// VIBE_Text_RenderFormattedMessage @0x59f99c (7296 B, 422 xrefs) are giant
// goto-laden dispatchers that:
//   1. Copy the format string (or a text-DB entry resolved by id) into a working
//      buffer.
//   2. Walk the buffer scanning for the three escape introducers
//        '%' (0x25)  inline value codes
//        '$' (0x24)  substitution / markup tokens (font, title, $[..], $M, $T)
//        '{' (0x7b)  random-text tokens {rN}
//      and REWRITE the buffer IN PLACE, splicing each code's expansion over the
//      code text (VIBE_Util_MemMove + qmemcpy).
//   3. Hand the fully-expanded string to the glyph blitter / child-object
//      emitter (icon batching at window slot +161, Z-order, surface blit).
//
// Step 3 (glyph blitting + child-object emission) belongs to the render surface
// and the markup builder; this module reconstructs steps 1-2 — the actual
// STRING PRODUCTION for the self-contained `%`-codes, sub-specifiers, and {rN}
// random tokens. The output is the expanded plain string (with embedded icon
// control bytes) that the original would have blitted.
//
// Recovered format-code table (the `v7` switch in RenderRichString, where the
// char follows the '%' introducer; an optional decimal digit before the letter
// is the field index `v209`, -1 when absent):
//
// Recovered byte-exactly from the disasm dispatch tree (al = code letter after
// the optional field digit). The module owns these self-contained leaves:
//   %%       -> emit byte 0x16            (literal-percent placeholder glyph) [0x25, line 625]
//   %i       -> grouped decimal int with '.' thousands separators            [0x69, line 1200]
//   %a       -> count icon "%i%c" (icon byte 0x14) when preceded by '%'      [0x61, line 969]
//   %S / %T  -> money string (coin icon 0x11), VIBE_Money_FormatWithSeparators [0x53/0x54, line 374]
//   %D       -> date "<season> <year>"    (PackToRecord + season-name lookup) [0x44, line 549]
//
// Deferred (v215 case/gender state machine + VIBE_String_GetDelimitedField over
// runtime text tables dword_8C36B0/dword_8C379C/dword_8C3790/dword_8C37A8, data
// not in tree): %r/%E/%F/%U/%b/%e/%B/%C and the %f fixed-point float path, the
// %N item-label leaf, the %W font/window select, and the {rN}/%r random pick.
// Earlier notes mislabeled the table as %m=money, %T=date, %s=string; the binary
// uses %S/%T=money, %D=date and has no standalone %s/%m/%c code.
//
//   Case/gender sub-specifiers that set the `v215` state for the FOLLOWING %s
//   (consumed inline): 'b'(0x62) 'U'(0x55) 'E'(0x45) 'B'(0x42) 'e'(0x65)
//   'a'(0x61) — these select singular/plural + lower/Title case of the next
//   substituted noun. See report for the v215 state table.
//
//   Markup introduced by '$' ($A, $M, $T, $[..], _FONT, _BUTTON_RED) and the
//   icon/child-object batching are produced by markup.{h,cpp} and the render
//   surface; see the deferred list in the report.

#include "gui/text/format.h"
#include "gui/text/textdb.h"

#include <string>
#include <vector>

namespace guild::gui::text {

// A produced value for one `%`-code, holding the expansion text and the count
// of source characters the code occupied in the format string (so the driver
// can splice it). `field` is the optional leading digit (-1 when absent).
struct CodeExpansion {
    std::string text;   // the substituted string (may contain icon control bytes)
    bool handled;       // false => this code is a deferred/markup branch
};

// Format-string driver. Expands the self-contained `%`-codes and {rN} tokens in
// `fmt` against the variadic-style argument list `args` (consumed left to right)
// and the text database `db`, returning the produced plain string (with embedded
// icon control bytes 0x11/0x14/0x16). This mirrors the in-place rewrite of
// RenderRichString for the codes this module owns; markup/`$` tokens are left
// verbatim for the markup builder to consume.
//
// `args` are pre-boxed values; the driver pulls the right type per code:
//   %i/%c -> int   %m -> money(amount, divisor=1)   %T -> GameTimeSource
//   %s    -> const char* (or a text-DB id when the value is a small index)
struct Arg {
    enum Kind { Int, Str, Money, Date } kind;
    i32 i = 0;            // Int / Money amount
    i32 divisor = 1;      // Money divisor
    std::string s;        // Str
    GameTimeSource date{}; // Date

    static Arg MakeInt(i32 v) { Arg a; a.kind = Int; a.i = v; return a; }
    static Arg MakeStr(std::string v) { Arg a; a.kind = Str; a.s = std::move(v); return a; }
    static Arg MakeMoney(i32 amount, i32 div = 1) { Arg a; a.kind = Money; a.i = amount; a.divisor = div; return a; }
    static Arg MakeDate(GameTimeSource d) { Arg a; a.kind = Date; a.date = d; return a; }
};

// Expand the format codes this module owns. `seasonNames` supplies the four
// season strings for %T (index = season 0..3); when null, the numeric season is
// printed. Unknown `$`/markup is copied through unchanged.
std::string RenderRichString(const char* fmt,
                             const std::vector<Arg>& args,
                             const TextDb* db = nullptr,
                             const char* const* seasonNames = nullptr);

// Resolve a localized message by id then expand it (RenderFormattedMessage path):
// looks up db->Text(msgId) and runs RenderRichString on it. Returns "" if the id
// is out of range (the original returns early / writes nothing).
std::string RenderFormattedMessage(const TextDb& db,
                                   int msgId,
                                   const std::vector<Arg>& args,
                                   const char* const* seasonNames = nullptr);

} // namespace guild::gui::text
