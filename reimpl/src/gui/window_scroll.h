#pragma once
// window_scroll.{h,cpp} — gilde.exe 0x41536c VIBE_Window_LayoutScrollContent
//
// This is the line-flush stage of the window markup renderer. The markup builder
// (VIBE_Window_ParseMarkupAndBuild @0x416720) accumulates one line's worth of
// "word" records into a pair of global parallel arrays (byte_672300 / dword_672402
// …, stride 262, count dword_62D254), then calls LayoutScrollContent to (a) measure
// the line's total pixel width, (b) compute the horizontal start pen depending on the
// alignment flags on the window (left / right / centered / justified), (c) draw each
// word with the bitmap-font glyph helpers while stepping around any child widgets that
// already occupy the row, and finally (d) reset the word count to 0.
//
// All of the layout/measure/justify/centre arithmetic is GENUINE engine behaviour and
// is reconstructed 1:1 (every `>> 16` is a signed arithmetic shift; the justify split
// and the centre split are signed `idiv`, matching the original). The actual glyph
// blits (VIBE_Animation_Basic/Advanced/Coord_Transform/Shape_*) are routed through the
// caller-supplied hooks in WinScrollEnv, which are inert in the headless build (rule 3
// render boundary) — exactly the pattern used by sim/misc_recon4_textlayout.
//
// PROVENANCE OF THE 1:1 ARGS (from the call sites in 0x416720):
//   eax (a1@ecx) = window slot index  -> WinScrollEnv::window(slot)
//   edx (a2)     = current row Y (a2)               -> param `y`
//   ebx (a3)     = caller context/colour handle (a3)-> param `ctx`
//   a4 (char)    = flush flag (1 normally, 0 on wrap)
//   a5 (int)     = alignment mode (0 / 64 / 128 / 256)
//   a6 (int)     = left margin / x-start
//   a7 (int)     = right margin / x-end
// Return = the window record's flags dword as it was on entry (v35), which the original
// saves and restores; observationally LayoutScrollContent leaves win flags unchanged.

#include "guild/common/types.h"

namespace guild::gui {

// One accumulated "word" record. Mirrors the parallel arrays the markup builder fills:
//   byte_672300[j]  ch0       (first byte of the token; '$' introduces a command)
//   byte_672301[j]  ch1       (command byte after '$')
//   *(i32*)(&dword_672402+j)  packed: HIWORD = word pixel width (`>>16`); the byte at
//                              offset +1 (flagsByte) carries bit6 (0x40) = "coloured".
//   byte_672400[j], byte_672401[j]  mask-colour pair (for the coloured-word path).
struct WinScrollWord {
    unsigned char ch0      = 0;   // byte_672300[j]
    unsigned char ch1      = 0;   // byte_672301[j]
    int           width    = 0;   // *(i32*)(&dword_672402+j) >> 16  (signed)
    unsigned char flagsByte= 0;   // *(u8*)(&dword_672402+j+1)
    unsigned char color0   = 0;   // byte_672400[j]
    unsigned char color1   = 0;   // byte_672401[j]
};

// A child-widget record as the layout needs it (dword_69FFB4 + 740*idx).
struct WinScrollChild {
    int  dead = 0;        // [25] (off 0x64): non-zero -> skip
    int  x    = 0;        // [4]>>16  (off 0x10)
    int  yw   = 0;        // [5]>>16  (off 0x14)  (width on the [4] axis / y span)
    int  base = 0;        // +14 (off 0x0E)>>16   (left edge along the pen axis)
    int  height = 0;      // +18 (off 0x12)>>16   (advance/height along the pen axis)
};

// A window record as the layout needs it (dword_67EB80 + 952*slot).
struct WinScrollWindow {
    int           flags   = 0;       // [3] (low byte = byte+12 sign; byte+13 = >>8)
    int           childCount = 0;    // *(i32*)(win+26)>>16
    const WinScrollChild* children = nullptr;  // resolved [6] child array
    int           childN  = 0;       // length of `children`
};

// Caller environment / inert render boundary. Everything the original reads from globals
// or draws through is provided here so the layout math is exercised headless.
struct WinScrollEnv {
    // Window record for the given slot index (a1). Must stay alive for the call.
    const WinScrollWindow* window = nullptr;

    // The accumulated word records and their count (== dword_62D254 on entry).
    const WinScrollWord* words = nullptr;
    int                  wordCount = 0;       // dword_62D254

    // Engine globals consumed by the math.
    int spaceWidth = 0;    // dword_62D270  (default 8)
    int tracking   = 0;    // dword_62D274  (default 2)
    int lineHeightAccum = 0; // dword_69FFB0 (added to a2 for the child-row clip test)
    int reentryGuard = 0;  // dword_62D288  (1 => early return, win flags untouched)

    // Glyph metrics: VIBE_Coord_Transform(font, ch) -> the advance (`*(u16*)(g+26)`).
    // Default returns 0 so a headless run still walks the control flow.
    int (*glyphAdvance)(unsigned char ch) = nullptr;

    // Inert glyph draw hooks. drawBasic mirrors VIBE_Animation_Basic and returns
    // non-zero when the glyph was actually drawn (so the pen advances) — the default
    // returns 1 so the measure/advance loop is fully exercised.
    int  (*drawBasic)(int x, int y, void* ctx, unsigned char ch) = nullptr;   // Animation_Basic
    void (*drawAdvanced)(int x, int y, void* ctx, unsigned char ch) = nullptr; // Animation_Advanced
    void (*setMaskColor)(unsigned char a, unsigned char b) = nullptr;          // Shape_SetMaskColor

    // Reported back for tests/diagnostics (not part of the original's return value).
    int penStart = 0;   // computed horizontal start (esi after alignment)
};

// gilde.exe 0x41536c — VIBE_Window_LayoutScrollContent. Lays out + draws the
// accumulated line; returns the window flags dword as on entry (the original's v35).
// On return env.wordCount is conceptually reset to 0 (dword_62D254 = 0); callers that
// own the global mirror that. `env.penStart` receives the computed start pen.
int Window_LayoutScrollContent(WinScrollEnv& env, int y, void* ctx,
                               unsigned char flushFlag, int alignMode,
                               int xStart, int xEnd);

} // namespace guild::gui
