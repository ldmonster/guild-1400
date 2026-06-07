#pragma once
#include "guild/common/types.h"

// =============================================================================
// guild::render — the 1024-entry light-falloff lookup table.
//
//   0x5c88f8  VIBE_Light_InitFalloffTable   (fills flt_140510C[0..1023])
//   0x5f0b9c  VIBE_Math_AcosGuarded         (x87 helper used by the recurrence)
//
// VIBE_Light_InitFalloffTable builds a 1024-entry float table addressed by the
// engine's light-distance index. Recovered from the disassembly at 0x5c88f8:
//
//   fld  flt_628CB8        ; A = 0x3F22F983 = 2/pi  (0.63661975...)
//   fld  flt_628CB4        ; B = 0x3A800000 = 1/1024 (0.0009765625)
//   for (i = 0; i < 1024; ++i)
//       x       = (double)i * B            ; i/1024, in [0, 1023/1024]
//       s       = AcosGuarded(x, B)        ; (see below) == asin(x)
//       table[i]= 1.0 - s * A              ; 1 - asin(i/1024)*(2/pi)
//
// VIBE_Math_AcosGuarded (0x5f0b9c) despite its IDA name computes **asin(x)**:
//   t = 1 - x*x
//   if (t == 0)   -> 0 or pi   (the |x|==1 guard; never hit here, max x<1)
//   else          -> (pi/2) - atan2(sqrt(1-x*x), x)
//                  == (pi/2) - acos(x)
//                  == asin(x)
// (tbyte_64A7D4 = 0x3FFF C90FDAA22168C000 = pi/2, exactly.)
//
// Since 0 <= i/1024 < 1 for every entry, the degenerate |x|==1 branch is never
// taken, so table[i] = 1.0 - asin(i/1024) * (2/pi) for all i. The result is a
// smooth falloff curve: table[0] = 1.0, decreasing monotonically toward ~0 as
// asin(i/1024)*(2/pi) approaches 1.
//
// The original stores the 5 trailing animation/state dwords (dword_64A054/58/5C/
// 60/64) from the two arguments; those are global engine state unrelated to the
// table and are documented but not modelled here (the report lists them).
// =============================================================================
namespace guild::render {

// Recovered constants (get_bytes; bit-exact float bit patterns).
constexpr double kFalloffStep  = 0.0009765625;          // flt_628CB4 = 1/1024
constexpr double kFalloffScale = 0.63661974668502808;   // flt_628CB8 = 2/pi

// Number of entries in the falloff table.
constexpr int kFalloffEntries = 1024;

// gilde.exe 0x5f0b9c — VIBE_Math_AcosGuarded. Computes asin(x) (see header note).
// For |x| >= 1 the original returns 0 (x>=1) or pi (x<=-1); reproduced here.
double AcosGuarded(double x);

// gilde.exe 0x5c88f8 — VIBE_Light_InitFalloffTable. Fills `out` (>= 1024 floats)
// with table[i] = 1.0 - asin(i/1024) * (2/pi). The intermediate asin and the
// final subtract are done in double precision (x87), then truncated to float on
// store (fstp dword), matching the original's `fstp ds:flt_140510C[ecx]`.
void InitFalloffTable(float out[kFalloffEntries]);

} // namespace guild::render
