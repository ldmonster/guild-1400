#include "util/math_rng_float.h"
#include "util/coord.h" // ConvertX (truncate toward zero)
#include "crt/rand.h"

namespace guild::util {

namespace {

// --- Private MINSTD (Lehmer) shuffle generator for RandomUnitFloat -----------
// Original globals: dword_649BBC (seed/state), dword_649BC0 (last output),
// dword_13CEB58[32] (Bays-Durham shuffle table).
i32 g_minstdState = 0; // dword_649BBC  (<=0 forces lazy re-seed)
i32 g_minstdLast = 0;  // dword_649BC0  (0 forces lazy re-seed)
i32 g_minstdTable[32] = {0}; // dword_13CEB58[]

// Scaling constants recovered from gilde.exe data:
const double kUnitScale = 4.656612875245797e-10; // dbl_62674C == 1/2^31 (approx)
const double kUnitMax   = 0.99999988;            // dbl_626754 (clamp)
const float  kCrtScale  = 3.0518509447574615e-05f; // flt_62675C == 1/32767

// One MINSTD step with Schrage's method (avoids 32-bit overflow), exactly as the
// original: q=127773, a=16807, r=2836.
inline void MinstdStep() {
    i32 v = 16807 * (g_minstdState % 127773) - 2836 * (g_minstdState / 127773);
    g_minstdState = v + (v < 0 ? 0x7FFFFFFF : 0);
}

// --- Private 32-bit LCG for RandomRange (NOT the CRT generator) ---------------
u32 g_rangeState = 0; // dword_122F49C

} // namespace

// gilde.exe 0x58b744 — VIBE_Math_RandomUnitFloat
void RandomUnitFloatSeed(i32 seed) {
    g_minstdState = seed;
    g_minstdLast = 0; // force the lazy re-init path on next draw
}

double RandomUnitFloat() {
    if (g_minstdState <= 0 || !g_minstdLast) {
        // Lazy (re)initialisation: positive-ize the seed, then warm/fill the
        // 32-entry shuffle table. The original primes for 8 extra steps (v0=39..32)
        // before recording 32 outputs (v0=31..0 -> table[31..0]).
        g_minstdState = (-g_minstdState >= 1) ? -g_minstdState : 1;
        for (int v0 = 39, v1 = 39; v0 >= 0; --v0, --v1) {
            MinstdStep();
            if (v0 < 32)
                g_minstdTable[v1] = g_minstdState;
        }
        g_minstdLast = g_minstdTable[0];
    }
    MinstdStep();
    // index = (last >> 26), sign-adjusted (last is always >= 0 here so == last>>26).
    int idx = (g_minstdLast - ((g_minstdLast >> 31) << 26)) >> 26;
    g_minstdLast = g_minstdTable[idx & 31];
    double v = static_cast<double>(g_minstdLast) * kUnitScale;
    if (v > kUnitMax)
        // 0x58b835: clamp path stores the constant as a *float* (dword 3F7FFFFEh)
        // and `fld dword` promotes it back to double — i.e. (double)(float)0.99999988,
        // == 0.9999998807907104, NOT the raw double 0.99999988. Cast through float.
        return static_cast<float>(kUnitMax);
    return static_cast<float>(v);
}

// gilde.exe 0x58b870 — VIBE_Math_RandomScaledInt (__usercall eax=fn(n@eax))
//   v = RandomUnitFloat() * n;  ConvertX();  return (int)v;  (truncate toward 0)
int RandomScaledInt(int n) {
    double v = RandomUnitFloat() * static_cast<double>(n);
    v = ConvertX(v);
    return static_cast<int>(v);
}

// gilde.exe 0x58b910 — VIBE_Math_RandomFloatScaled
//   (double)(int)RandNext() * flt_62675C(=1/32767)
double RandomFloatScaled() {
    return static_cast<double>(crt::RandNext()) * kCrtScale;
}

// gilde.exe 0x58b98c — VIBE_Math_RandomSquaredUnit
//   r = RandNext() * (1/32767);  return r*r;
double RandomSquaredUnit() {
    int r = crt::RandNext();
    double x = static_cast<double>(r) * kCrtScale;
    return x * x;
}

// gilde.exe 0x58b92c — VIBE_Math_RandomSquaredSigned
//   v = (float)( RandNext() * (1/32767) * 2.0 + (-1.0) );  // == 2u-1, u in [0,1]
//   return (v>=0) ? v*(1*v) : v*(-1*v);                     // sign-preserving square
double RandomSquaredSigned() {
    float v = static_cast<float>(static_cast<double>(crt::RandNext())
                                 * static_cast<double>(kCrtScale) * 2.0 + -1.0);
    if (v >= 0.0f)
        return v * (1.0 * v);
    return v * (-1.0 * v);
}

// gilde.exe 0x58b8c4 — VIBE_Math_RandomSignedOffset (__usercall eax=fn(n@ax))
//   cx = 2*n + 1;  if ((u16)cx == 0) return -n;       // i.e. 2*n == 0xFFFF
//   return (RandNext() % (u16)cx) - n;                 // uniform over [-n, n]
int RandomSignedOffset(u16 n) {
    u16 span = static_cast<u16>(2 * n + 1);
    if (span == 0)
        return -static_cast<int>(n);
    int r = crt::RandNext();
    return (r % span) - static_cast<int>(n);
}

// gilde.exe 0x58bc00 — VIBE_Math_RandomRangeWithBase (__usercall al=fn(base@al))
//   if (base == 0)  return RandNext() % 42 + 21;       // [21, 62]
//   mod = ((u8)base < 42.0f) ? 6 : 11;  (flt_626770 == 42.0)
//   return base + RandNext() % mod;                    // [base, base+mod-1]
char RandomRangeWithBase(char base) {
    if (!base)
        return static_cast<char>(crt::RandNext() % 42 + 21);
    // 0x58bc0c: `xor eax,eax; mov al,bl; fild word ptr` -> the base is ZERO-extended
    // to a byte (u8) before the FPU load, so the `< 42.0` compare is UNSIGNED in
    // [0,255], not signed. A negative char (high bit set) compares as >= 42 -> mod 11.
    int mod = (static_cast<u8>(base) < 42) ? 6 : 11;
    int r = crt::RandNext();
    return static_cast<char>(r % mod + base);
}

// gilde.exe 0x538438 — VIBE_Math_RandomRange (__usercall eax=fn(n@eax))
//   state = 1103515245*state + 12345; return ((state>>16) % 0x7FFF) % n;
void RandomRangeSeed(u32 seed) {
    g_rangeState = seed;
}

u32 RandomRange(u32 n) {
    g_rangeState = 1103515245u * g_rangeState + 12345u;
    u32 hi = (g_rangeState >> 16) & 0xFFFFu;
    return hi % 0x7FFFu % n;
}

} // namespace guild::util
