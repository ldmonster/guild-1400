#include "test.h"

// Golden-vector unit tests for the light-falloff LUT (src/render/falloff_lut.cpp).
// gilde.exe 0x5c88f8 VIBE_Light_InitFalloffTable / 0x5f0b9c VIBE_Math_AcosGuarded.
// Constants from get_bytes: flt_628CB4 = 0x3A800000 = 1/1024,
// flt_628CB8 = 0x3F22F983 = 2/pi. table[i] = 1 - acos(i/1024)*(2/pi).
// (Harden fix @0x5f0b9c: AcosGuarded is ACOS — VIBE_Math_Atan2's inner fxch
// makes the fpatan compute asin(x), subtracted from pi/2. Old asin-based
// goldens replaced.) Expected values computed with an independent Python
// reference (double acos, float store) matched to the x87 arithmetic.
#include "render/falloff_lut.h"

#include <cmath>

using namespace guild;
using namespace guild::render;

namespace {
bool feq(double a, double b) { return std::fabs(a - b) <= 1e-6 * (1.0 + std::fabs(b)); }
}

// ---- AcosGuarded computes acos(x) (as the IDA name says) -------------------
TEST(RenderFalloff, AcosGuardedIsAcos) {
    CHECK(feq(AcosGuarded(0.0), M_PI / 2.0));
    CHECK(feq(AcosGuarded(0.5), std::acos(0.5)));
    CHECK(feq(AcosGuarded(0.25), std::acos(0.25)));
    CHECK(feq(AcosGuarded(0.999), std::acos(0.999)));
    // |x| == 1 guard: 0 for x>=1, pi for x<=-1 (never hit by the table).
    CHECK(feq(AcosGuarded(1.0), 0.0));
    CHECK(feq(AcosGuarded(-1.0), M_PI));
}

// ---- InitFalloffTable: 1 - acos(i/1024)*(2/pi) ----------------------------
TEST(RenderFalloff, TableGolden) {
    static float t[kFalloffEntries];
    InitFalloffTable(t);

    // Constants are exact.
    CHECK(feq(kFalloffStep, 1.0 / 1024.0));
    CHECK(feq(kFalloffScale, 2.0 / M_PI));
    // Recovered bit-exact (get_bytes): table size 1024; flt_628CB4 = 0x3A800000 =
    // 1/1024 exactly; flt_628CB8 = 0x3F22F983 = the FLOAT-truncated 2/pi the binary
    // loads (== 0.63661974668502808 as a double, NOT the exact 2/pi).
    CHECK_EQ(kFalloffEntries, 1024);
    CHECK_EQ(kFalloffStep, 0.0009765625);                 // 0x3A800000, exact
    CHECK_EQ(kFalloffScale, 0.63661974668502808);         // 0x3F22F983 promoted to double
    CHECK(kFalloffScale != 2.0 / M_PI);                   // it is the truncated float

    // Golden entries (Python reference, float-stored; acos form).
    // old->new (asin->acos fix @0x5f0b9c): t[0] 1.0 -> ~4.03e-8, t[512]
    // 0.666667 -> 0.333333, t[1023] 0.028137 -> 0.971863.
    CHECK(feq(t[0],    4.034205858260975e-08));
    CHECK(feq(t[1],    0.0006217394256964326));
    CHECK(feq(t[256],  0.1608612835407257));
    CHECK(feq(t[512],  0.3333333730697632));
    CHECK(feq(t[768],  0.5398930907249451));
    CHECK(feq(t[1023], 0.9718628525733948));
}

// ---- The curve is a monotone RISE from ~0 (grazing) toward ~0.97 (facing) --
TEST(RenderFalloff, MonotoneRise) {
    static float t[kFalloffEntries];
    InitFalloffTable(t);
    CHECK(feq(t[0], 4.034205858260975e-08));
    bool monotone = true;
    float mn = t[0], mx = t[0];
    for (int i = 1; i < kFalloffEntries; ++i) {
        if (t[i] < t[i - 1] - 1e-7f) monotone = false;
        if (t[i] < mn) mn = t[i];
        if (t[i] > mx) mx = t[i];
    }
    CHECK(monotone);
    CHECK(mn >= 0.0f);
    CHECK(mx < 1.0f);          // never reaches 1 (max x = 1023/1024 < 1)
    CHECK(feq(mx, 0.9718628525733948));
}

// =============================================================================
// WAVE-10 HARDENING — boundary indices and exact-fill bounds.
// InitFalloffTable writes EXACTLY kFalloffEntries floats: probe index 0, 1, the
// last (1023), and prove it never writes index 1024 (the OOB slot). The "out of
// range index" the engine could feed the table is clamped by the caller (the
// distance index is masked to 10 bits); the table itself is a pure fill.
// =============================================================================
TEST(RenderFalloff, FillBoundsExact) {
    // Over-allocate one guard slot past the table and pre-stamp it; the fill must
    // leave it untouched (no write to out[1024]).
    static float t[kFalloffEntries + 1];
    const float kGuard = -123.5f;
    t[kFalloffEntries] = kGuard;
    InitFalloffTable(t);
    CHECK_EQ(t[kFalloffEntries], kGuard);           // index 1024 NOT written
    // Boundary entries (acos form).
    CHECK(feq(t[0], 4.034205858260975e-08));         // i=0: acos(0)=pi/2 -> ~0
    CHECK(feq(t[1], 0.0006217394256964326));
    CHECK(feq(t[kFalloffEntries - 1], 0.9718628525733948)); // i=1023: last entry
    CHECK(t[kFalloffEntries - 1] < 1.0f);           // strictly below 1 (x<1)
}

// AcosGuarded at the guard boundaries (|x| == 1) and just inside — the table
// never feeds |x|>=1 but the guard must be safe (no NaN from sqrt of a tiny
// negative when x is exactly 1).
TEST(RenderFalloff, AcosGuardedBoundaries) {
    CHECK(feq(AcosGuarded(1.0), 0.0));              // x >= 1 -> 0
    CHECK(feq(AcosGuarded(-1.0), M_PI));            // x <= -1 -> pi
    // largest table argument: 1023/1024, just under 1.
    double xmax = 1023.0 / 1024.0;
    double a = AcosGuarded(xmax);
    CHECK(a == a);                                  // not NaN
    CHECK(feq(a, std::acos(xmax)));
}
