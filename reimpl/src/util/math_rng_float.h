#pragma once
#include "guild/common/types.h"

// Random-float helpers from gilde.exe (VIBE_Math_Random*). Two generators are
// involved:
//   * guild::crt::RandNext  — the shared 16-bit CRT LCG (see crt/rand.h). Used by
//     RandomFloatScaled / RandomSquared* / RandomSignedOffset / RandomRangeWithBase.
//   * a private MINSTD (Lehmer, a=16807) shuffle generator with a 32-entry shuffle
//     table — used only by RandomUnitFloat (and RandomScaledInt on top of it).
// The MINSTD state/table live in globals in the original (dword_649BBC,
// dword_649BC0, dword_13CEB58[]); we model them as file-static here.
namespace guild::util {

// VIBE_Math_RandomUnitFloat @0x58b744. Bays-Durham-shuffled MINSTD producing a
// float in [0, 0.99999988]. Reseed by calling RandomUnitFloatSeed(seed).
double RandomUnitFloat();

// Seeds the private MINSTD generator (negative/zero forces the lazy re-init that
// the original performs when dword_649BBC <= 0). Test/determinism hook.
void RandomUnitFloatSeed(i32 seed);

// VIBE_Math_RandomScaledInt @0x58b870. (int)(RandomUnitFloat() * n) truncated.
int RandomScaledInt(int n);

// VIBE_Math_RandomFloatScaled @0x58b910. (double)RandNext() * (1/32767).
double RandomFloatScaled();

// VIBE_Math_RandomSquaredUnit @0x58b98c. r = RandNext()/32767; returns r*r.
double RandomSquaredUnit();

// VIBE_Math_RandomSquaredSigned @0x58b92c.
//   v = RandNext()/32767*2 - 1;  return v*|v| (sign-preserving square in [-1,1]).
double RandomSquaredSigned();

// VIBE_Math_RandomSignedOffset @0x58b8c4. Uniform random int in [-n, n] (span
//   2*n+1). Special-cased: if 2*n == 0xFFFF returns -n.
int RandomSignedOffset(u16 n);

// VIBE_Math_RandomRange @0x538438. Self-contained LCG (NOT the CRT one): advances
// a private 32-bit state and returns ((state>>16) % 0x7FFF) % n.
u32 RandomRange(u32 n);

// Seeds the RandomRange private LCG (test/determinism hook).
void RandomRangeSeed(u32 seed);

// VIBE_Math_RandomRangeWithBase @0x58bc00. if base==0 -> RandNext()%42 + 21;
//   else base + RandNext()%(base<42 ? 6 : 11). Returns a char like the original.
char RandomRangeWithBase(char base);

} // namespace guild::util
