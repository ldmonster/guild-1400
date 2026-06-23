#pragma once
// =============================================================================
// guild::sim — OBJECT / ENTITY per-frame update + text/anim apply (wave-19).
//
// 1:1 reconstruction of the per-frame entity renderers dispatched by the frame
// loop's interaction walk (gilde.exe 0x4139a8 VIBE_GameLogic_Interactions, the
// type-switch at cases 0x40/0x41/0x43/0x44 — see play/gamelogic_recon.cpp):
//
//   gilde.exe 0x40eea0  VIBE_Object_Update            -> ObjectUpdate           (case 0x41)
//   gilde.exe 0x415b78  VIBE_Animation_Apply          -> AnimationApply         (case 0x43)
//   gilde.exe 0x418f34  VIBE_EntityChild_Process      -> EntityChildProcess     (case 0x40)
//   gilde.exe 0x41078c  VIBE_Entity_InteractionLogic  -> EntityInteractionLogic (case 0x44)
//
// These walk the live scene-node array (dword_69FFB4, 740-byte stride), the clip
// rect globals (dword_64A1B4..C0), the font/property/coord render leaves
// (Coord_Push/Transform/ConvertX, Animation_Basic/Advanced/Apply, Property_Get/
// Set, State_*) and emit draw commands. The render/scene edges are injected via
// a hooks struct (inert defaults) — the established tree pattern; the PURE logic
// reconstructed verbatim here is:
//
//   * VIBE_Object_Update: the digit-count loop, the weight->string formatting
//     ("%li g" / "%.1f kg" / "%.1f t" with dbl_610D74=0.001 / dbl_610D6C=1e-6),
//     the "%i%%" fill-fraction (dbl_610D7C=100.0, clamp >1.0, ConvertX truncate),
//     the count clamp (>=48, <= +86 cap) and the unicode title copy.
//   * VIBE_Animation_Apply: the word-wrap layout — greedy line packing into the
//     96-byte-strided line buffer, the '~'/'-' break-back, the centring/right-
//     align flags (8/0x10), per-glyph Coord_Transform advance + 'newline' wrap,
//     and the returned content height (max of accumulated dword_62D27C).
//   * VIBE_EntityChild_Process: the modulo-priority class (8/6/4/2 -> +608),
//     the scroll-thumb enable/disable (235/234 child handles), the percent label
//     (dbl_610F14=0.5, ConvertX), and the window-border 9-slice tiling.
//   * VIBE_Entity_InteractionLogic: the fill-bar geometry — fraction =
//     (v135/(v128-v131)) over [pos..filled], the round-to-1.0 floor, the
//     horizontal (flags&2) vs vertical (flags&1) bar fill, the segment tiling,
//     and the value/percent label formatting.
//
// ConvertX (gilde.exe 0x5c6b08) sets the x87 rounding to truncate-toward-zero
// (chop) and frndint's st0 -> TRUNCATION. Modeled by ConvertXTrunc() (= trunc).
// =============================================================================
#include "guild/common/types.h"
#include "util/coord.h"   // VIBE_Coord_ConvertX @0x5c6b08 (reused: guild::util::ConvertX)

#include <string>

namespace guild::sim {

using namespace guild;

// gilde.exe 0x5c6b08 — VIBE_Coord_ConvertX: x87 round-to-truncate of st0
// (RC=11 chop + frndint). REUSED from guild::util::ConvertX (util/coord.cpp);
// aliased here so the object_update golden tests can pin the truncation.
inline double ConvertXTrunc(double x) { return guild::util::ConvertX(x); }

// Float / double constants used by the four functions (get_bytes-verified).
namespace anim_const {
inline constexpr double kWeightTon   = 9.9999999747524271e-07; // dbl_610D6C (1e-6)
inline constexpr double kWeightKg    = 1.0000000000000000e-03; // dbl_610D74 (0.001)
inline constexpr double kPctScale100 = 100.0;                  // dbl_610D7C
inline constexpr double kHalf        = 0.5;                    // dbl_610D84/610E34/610F14
inline constexpr double kBarTwo      = 2.0;                    // dbl_610DFC
inline constexpr double kBarA        = -1.0;                   // dbl_610E04
inline constexpr double kBarB        = -193.0;                 // dbl_610E0C
inline constexpr double kBarC        = 219.0;                  // dbl_610E14
inline constexpr double kBarD        = -135.5;                 // dbl_610E1C
inline constexpr float  kBarE        = 24.0f;                  // flt_610E24
inline constexpr float  kBarF        = 196.0f;                 // flt_610E28
inline constexpr float  kBarG        = 220.0f;                 // flt_610E2C
inline constexpr float  kBarH        = 100.0f;                 // flt_610E30
}  // namespace anim_const

// ---------------------------------------------------------------------------
// gilde.exe 0x40f044 — the count-digits loop inside VIBE_Object_Update.
//   v10 = 0; if (v9>0) do { ++v10; v9/=10; } while (v10<10 && v9>0);
// Returns the decimal digit count of `value` (>0), capped at 10; 0 for value<=0.
// ---------------------------------------------------------------------------
int CountDigits(int value);

// ---------------------------------------------------------------------------
// gilde.exe 0x40f06d..0x40f3b5 — VIBE_Object_Update weight/amount formatting.
// flags = node+38; count = node+74; unit = node+79 (weight-per-unit).
//   bit 0x04 (mass): total = unit*count;
//       >=1e6 -> "%.1f t"  (total * 1e-6);
//       >=1e3 -> "%.1f kg" (total * 0.001);
//       else  -> "%li g"   (total);
//   bit 0x08 (zero-padded): build "%%0%ii " with `digits`, then format count*unit;
//   else  -> "%i" of count.
// `digits` is CountDigits(node+6). Returns the formatted display string.
// ---------------------------------------------------------------------------
std::string FormatAmount(u8 flags, int count, int unitWeight, int digits);

// ---------------------------------------------------------------------------
// gilde.exe 0x40f6b0..0x40f710 — VIBE_Object_Update build-progress percent.
//   v41 = *(node+480) [float fill, 0..1]; clamp to <=1.0;
//   v33 = clamp * dbl_610D7C (100); ConvertX (truncate); sprintf("%i%%", v33).
// Returns the "%i%%" string and (out) the truncated integer percent.
// ---------------------------------------------------------------------------
std::string FormatBuildPercent(float fill, int* outPercent = nullptr);

// ---------------------------------------------------------------------------
// gilde.exe 0x408..0x4108c8 — VIBE_Entity_InteractionLogic fill-bar fraction.
//   v5 = (double)max / (double)(full - empty);   [v135 / (v128 - v131)]
//   filled = (double)(cur - empty) * v5;         [(v132 - v131) * v5]
//   targetF= (double)(target - empty) * v5;      [(*(v4+140) - v131) * v5]
//   if (filled>0 && bits(filled)<1.0bits) filled=1.0;   // tiny -> snap to 1px
//   (same for targetF).
// Returns {filled, target} as the two float bar lengths (matching the engine's
// >0 && <1.0-bits snap-to-1).
// ---------------------------------------------------------------------------
struct BarFraction { float filled; float target; };
BarFraction ComputeBarFraction(int maxLen, int full, int empty, int cur, int target);

// ---------------------------------------------------------------------------
// gilde.exe 0x41186d / 0x41136b — Entity_InteractionLogic percent label.
//   bits&0x800: pct = cur * flt_610E30(100) / full + 0.5; ConvertX; "%i%%".
// Returns the "%i%%" string for a fill bar's percentage (truncated after +0.5).
// ---------------------------------------------------------------------------
std::string FormatBarPercent(int cur, int full, int* outPercent = nullptr);

// ---------------------------------------------------------------------------
// gilde.exe 0x418fe4..0x41945f — EntityChild_Process modulo-priority class.
//   v8 = child[153]; if (v8) { v8%8? : 8; v8%6? : 6; v8%4? : 4; !(v8%2)? 2 }
// The nested test stores 8/6/4/2 into child+608 for the first divisor that
// divides v8 evenly (in the order 8,6,4,2). Returns that class (0 if none/zero).
// ---------------------------------------------------------------------------
int ChildPriorityClass(int v);

// ===========================================================================
// Word-wrap layout (the core of VIBE_Animation_Apply, 0x415e11..0x416245).
// Lays `text` into lines no wider than `maxWidth` using the glyph advance
// `charAdvance(ch)` and a fixed inter-word gap `wordGap` (dword_62D270) and
// inter-glyph gap `glyphGap` (dword_62D274). Breaks on spaces; if a single word
// overflows it back-tracks to the last '~' or '-' (converting a trailing '~' to
// '-'); the '~' char is also a hard line break that is NOT drawn. Returns the
// produced lines (the 96-byte-strided v59 buffer rows). `lineAdvance` is the
// font line height (dword_69FFB0). This is the layout half; the per-glyph emit
// is the renderer's job (hooks).
// ===========================================================================
struct WrapResult {
    std::string lines;       // the concatenated lines
    int         lineCount;   // number of lines produced
    int         contentH;    // returned content height (dword_62D27C)
};
WrapResult WrapText(const std::string& text, int maxWidth,
                    int (*charAdvance)(int ch), int wordGap, int glyphGap,
                    int lineAdvance);

// ===========================================================================
// Wave-21 — additional fully-recovered renderer-body fragments.
//
// These extend the wave-19 pure cores with the rest of the each renderer's
// NON-render-leaf logic that IS faithfully recoverable from the live decompile
// (the integer math + control flow whose operands are all present). The
// register-spilled float fragments (the bare VIBE_Coord_ConvertX() calls that
// lost their __usercall arguments in 0x41078c's body) are NOT reconstructed —
// per rule 8 they stay in the renderer's hook seam, not faked here.
// ===========================================================================

// ---------------------------------------------------------------------------
// gilde.exe 0x40eef4..0x40ef12 — VIBE_Object_Update label-width clamp.
//   if (width < 48) width = 48;                 // 0x40eef4 minimum
//   v5 = node[86];                               // optional max cap
//   if (v5 > 0 && width >= v5) width = v5;       // 0x40ef0c clamp to cap
// Returns the clamped label cell width.  `maxCap`<=0 disables the cap.
// ---------------------------------------------------------------------------
int ObjectUpdateClampWidth(int width, int maxCap);

// ---------------------------------------------------------------------------
// gilde.exe 0x419076..0x4190c9 — VIBE_EntityChild_Process scroll-thumb gate.
// The original updates two scroll-bar child nodes (handles v4[235] / v4[234]):
//   node235 (0x419076 then 0x4190a9, the SECOND comparison wins):
//       = (page + step + pos16 >= rangeTop) ? 1 : 0;       // enabled flag
//   node234 (0x4190c7):  = (step != 0) ? 0 : 1;            // enabled flag
// (each flag is stored to the node's +56 and +76 fields.)
// Inputs map to the decompile:
//   thumbPos = v10[153]; pos16 = v10[2]>>16; rangeTop = v10[145];
//   page = v10[148]; step = v10[146].
// Returns the two final enable flags {node235, node234}.
// ---------------------------------------------------------------------------
struct ScrollArrowFlags { int node235; int node234; };
ScrollArrowFlags EntityChildScrollArrows(int thumbPos, int pos16, int rangeTop,
                                         int page, int step);

// ---------------------------------------------------------------------------
// gilde.exe 0x4190fd..0x41910c — VIBE_EntityChild_Process scroll percent.
//   v14 = (double)rangeTop / (double)thumbPos + 0.5;  // dbl_610F14 = 0.5
//   pct = (int)ConvertX(v14);                         // truncate
// Returns the integer percent label value (the v41 the original stores).
// ---------------------------------------------------------------------------
int EntityChildScrollPercent(int rangeTop, int thumbPos);

// ---------------------------------------------------------------------------
// gilde.exe 0x41924a / 0x4192c5 — VIBE_EntityChild_Process 9-slice tile counts.
//   horizontal tiles = ((width  - 16) >> 3);   // top/bottom edge runs
//   vertical   tiles = ((height - 16) >> 3);   // left/right edge runs
// Both clamped to >0 by the caller's `if (v18 > 0)` gate; we return the raw
// shifted counts (which may be <=0 -> no tiles), matching the original.
// ---------------------------------------------------------------------------
struct BorderTileCounts { int horiz; int vert; };
BorderTileCounts EntityChildBorderTiles(int width, int height);

}  // namespace guild::sim
