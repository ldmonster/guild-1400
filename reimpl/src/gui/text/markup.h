#pragma once
// Guild text module — markup tokenizer.
//
// VIBE_Window_ParseMarkupAndBuild @0x416720 (8118 B) walks a marked-up string
// and BUILDS child Objects on a window (buttons, labels, edit fields, sprites,
// line breaks, columns). The grammar is introduced by '$' (0x24) for markup and
// '%' (0x25) for inline value codes; '$[' .. '$]' delimits a bracketed region.
//
// The widget-creation half of that function is owned by the GUI object/window/
// render modules (VIBE_Object_AddButtonLabel, VIBE_Object_AddToWindow,
// VIBE_Widget_CreateSprite, VIBE_Window_LayoutScrollContent, VIBE_Window_Resize,
// VIBE_Input_AddFieldToWindow ...) and is DEFERRED here (see report).
//
// This module reconstructs the TOKENIZER: the byte-exact `$`-letter -> token
// classification (the big `v15` switch), the `$[`/`$]` bracket handling with its
// "Missing ']'" error, and the `Unknown textparameter: %%%c` error for an
// unrecognized '%'-letter. The output is the token stream the builder consumes.
//
// Recovered token letters (the char immediately after '$', value = v15):
//   '['  0x5B  -> BracketOpen   (copies "$[" marker, asc_610EE8)
//   ']'  0x5D  -> BracketClose  (copies "$]" marker, asc_610EEC)
//   'M'  0x4D  -> Embed         (copies "$M",  aM)        -> $M message embed
//   'T'  0x54  -> Tab           (copies "$T",  aT_1)      -> $T column/tab
//   'L'  0x4C  -> ColumnReset   (v208=0; cursor x = left)
//   'R'  0x52  -> ColumnRight   (v208=64; right column)
//   'B'  0x42  -> ColumnCenter  (v208=128)
//   'Y'  0x59  -> ColumnFull    (v208=256)
//   'C'  0x43  -> Clear         (truncate current line, reset cursor)
//   'F'  0x46  -> FontColor     ("$FF" selects a color from dword_40DDB0; "$F"
//                                alone selects a font via dword_62D2B0+digit)
//   'A'  0x41  -> LineFeed       (advance N lines; digit prefix = N, default 1)
//   '<'  0x3C  -> BoundRight     (set right content bound from $M metric)
//   '='  0x3D  -> BoundLeft      (set left content bound from $M metric)
//   'i'  0x69  -> Inline        ('$ia'/'$in' => red button sprite, else widget)
//   't'  0x74  -> EditField     ('$t' / '$tt' edit/text field)
//   's'/'a'/'n'/'b'/'c' -> object-kind selectors for the preceding token
//   other      -> Unknown       ("Unknown textparameter: %%%c")
//
// A leading decimal digit before a letter is the token's numeric argument
// (`v207`, -1 when absent), e.g. "$3A" feeds 3 line-feeds.

#include "guild/common/types.h"

#include <string>
#include <vector>

namespace guild::gui::text {

enum class MarkupKind {
    Text,          // a run of literal characters
    BracketOpen,   // $[
    BracketClose,  // $]
    Embed,         // $M
    Tab,           // $T
    ColumnReset,   // $L
    ColumnRight,   // $R
    ColumnCenter,  // $B
    ColumnFull,    // $Y
    Clear,         // $C
    FontColor,     // $F  (or $FF color)
    LineFeed,      // $A  (count in `arg`)
    BoundRight,    // $<
    BoundLeft,     // $=
    Inline,        // $i  (red button when followed by 'a'/'n')
    EditField,     // $t
    PercentCode,   // a '%'-value code (letter in `letter`)
    Unknown,       // unrecognized '$'/'%' letter -> error
};

struct MarkupToken {
    MarkupKind kind;
    int arg = -1;       // leading decimal digit (-1 when absent)
    char letter = 0;    // the code/selector letter (e.g. 'i','a','t','s')
    std::string text;   // literal text (kind==Text) or token spelling
};

// Tokenize a marked-up string into the token stream the builder consumes.
// `error` (if non-null) receives the first error message the original would log
// ("Missing ']'" or "Unknown textparameter: %%<c>"), or stays empty.
std::vector<MarkupToken> TokenizeMarkup(const char* str, std::string* error = nullptr);

} // namespace guild::gui::text
