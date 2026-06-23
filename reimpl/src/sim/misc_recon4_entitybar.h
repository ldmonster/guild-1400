#pragma once
// misc_recon4_entitybar.{h,cpp} — VIBE_Entity_InteractionLogic prologue (gilde.exe)
//
//   0x41078c VIBE_Entity_InteractionLogic
//
// This 0x171f-byte function renders a widget's progress/fill bar (a 740-byte widget
// record at 740*idx + dword_69FFB4). The bulk of the body is render presentation:
// it makes ~40 calls into VIBE_Coord_Transform/ConvertX (0x5d8b00/0x5c6b08),
// VIBE_Animation_Basic/Advanced/Apply, VIBE_Velocity_Apply, VIBE_Result_Handler_*,
// VIBE_Property_Set/Get, VIBE_State_* and sprintf, branching on the widget flag
// word (+132). The Hex-Rays decompile of that body is heavily register-spilled: many
// VIBE_Coord_ConvertX() calls show NO captured arguments (the __usercall register
// args were lost), so the exact operands cannot be recovered from this decompile.
// Per project rule 8 (no cheap analogues, no half-translation) the RENDER BODY of
// 0x41078c is OMITTED — it cannot be faithfully reconstructed from the available
// decompile and would require recovering the lost register arguments first.
//
// What IS faithfully recoverable and self-contained is the PROLOGUE fill-fraction
// computation (0x410797..0x410920): reading the widget's value/min/max/total fields
// and turning them into the two clamped pixel widths the rest of the function draws.
// That arithmetic (integer field reads, the double divide, the >0 && <1.0 -> 1.0
// clamps) is exact and is reconstructed here, gated so it is independently testable.

#include "guild/common/types.h"

namespace guild::sim {

using f32 = float;
using f64 = double;

// The fields the prologue reads out of the 740-byte widget record:
//   +120 value     (v133)
//   +124 min       (v132)
//   +128 max       (v129)
//   +132 flags     (v149, u16)
//   +136 total     (v136)   == pixel span the bar fills
//   +140 value2    (used for the second width)
struct EntityBarFields {
    i32 value   = 0;   // +120
    i32 minVal  = 0;   // +124
    i32 maxVal  = 0;   // +128
    u16 flags   = 0;   // +132
    i32 total   = 0;   // +136
    i32 value2  = 0;   // +140
};

// Result of the prologue: the two clamped fill widths the body draws with.
//   v140 (.fillA) and v135 (.fillB), both stored as float in the original.
struct EntityBarFill {
    f32 fillA = 0.0f;  // *(float*)&v140
    f32 fillB = 0.0f;  // *(float*)&v135
};

// gilde.exe 0x410797..0x410920 — VIBE_Entity_InteractionLogic prologue.
// Reproduces exactly:
//   v5   = (double)total / (double)(max - min);
//   fillA = (double)(value  - min) * v5;
//   fillB = v5 * (double)(value2 - min);
//   if (fillA > 0 && fillA < 1.0) fillA = 1.0;   // bit pattern: v140 < 1065353216
//   if (fillB > 0 && fillB < 1.0) fillB = 1.0;
// (1065353216 == bit pattern of 1.0f; the original compared the float as a signed
//  int, which for positive floats is equivalent to "< 1.0f".)
EntityBarFill EntityBarComputeFill(const EntityBarFields& f);

} // namespace guild::sim
