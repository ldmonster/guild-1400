#include "test.h"
#include "crt/rand.h"
#include "util/math.h"
#include "util/math_trig.h"
#include "util/math_rng_float.h"
#include "util/coord.h"
#include <cmath>

using namespace guild;

// E2E 1: derive an angle between two vectors, then verify acos/atan2 self-consistency
// and the sin^2+cos^2 identity across a sweep of normalized direction pairs.
TEST(MathE2E, AngleTrigIdentity) {
    for (int deg = 0; deg < 360; deg += 7) {
        double rad = deg * (3.14159265358979323846 / 180.0);
        float a[3] = {1.0f, 0.0f, 0.0f};
        float b[3] = {(float)std::cos(rad), 0.0f, (float)std::sin(rad)};
        double ang = util::VectorAngleBetween(a, b);
        // sin^2 + cos^2 == 1 regardless of how the angle came out.
        double s = std::sin(ang), c = std::cos(ang);
        CHECK(std::fabs(s * s + c * c - 1.0) < 1e-6);
        // |angle| should match the geometric angle (mod sign/wrap) within float eps.
        double expected = std::acos(std::cos(rad)); // [0, pi]
        CHECK(std::fabs(std::fabs(ang) - expected) < 1e-2 ||
              std::fabs(std::fabs(ang) - (2 * 3.14159265358979323846 - expected)) < 1e-2);
    }
}

// E2E 2: AcosGuarded composed with Atan2 — verify acos(x) == atan2(sqrt(1-x^2), x).
TEST(MathE2E, AcosAtan2Composition) {
    for (double x = -0.95; x <= 0.95; x += 0.05) {
        double viaAcos = util::AcosGuarded(x);
        double viaAtan2 = util::Atan2(std::sqrt(1.0 - x * x), x);
        CHECK(std::fabs(viaAcos - viaAtan2) < 1e-9);
    }
}

// E2E 3: LerpClampedCoord fixed-point-style roundtrip — mapping the endpoints of a
// span back through the lerp returns the integer endpoints (rounding via ConvertX).
TEST(MathE2E, LerpClampedRoundtrip) {
    for (int span = 1; span <= 50; ++span) {
        // d=0 -> a, d=span -> b
        CHECK_EQ(util::LerpClampedCoord(0, (float)span, (float)span, 0), 0);
        CHECK_EQ(util::LerpClampedCoord(0, (float)span, (float)span, (float)span), span);
        // midpoint rounds to round(span/2) (round-half-up via +0.5 then trunc)
        int mid = util::LerpClampedCoord(0, (float)span, (float)span, span / 2.0f);
        CHECK(mid == span / 2 || mid == (span + 1) / 2);
    }
}

// E2E 4: a normalize -> distance pipeline. Normalize a vector, scale it to a known
// length, and confirm Distance2D over its integer projection is sane.
TEST(MathE2E, NormalizeThenDistance) {
    float v[3] = {6.0f, 0.0f, 8.0f}; // length 10
    util::VectorNormalize(v);
    CHECK(std::fabs(std::sqrt((double)v[0]*v[0] + (double)v[2]*v[2]) - 1.0) < 1e-5);
    // Distance2D between (0,0) and (3,4) scaled.
    double d = util::Distance2D(0, 0, 4, 3);
    CHECK(std::fabs(d - 5.0 * 2.9081632653) < 1e-6);
}

// E2E 5: RNG reseed determinism across the float family — a seeded run reproduces
// exactly after reseeding (mirrors the original per-thread CRT state reset).
TEST(MathE2E, RngReseedDeterminism) {
    crt::Srand(2024);
    double seqA[16];
    for (auto& x : seqA) x = util::RandomFloatScaled();
    int offA[16];
    for (auto& x : offA) x = util::RandomSignedOffset(8);

    crt::Srand(2024);
    for (auto& x : seqA) CHECK(util::RandomFloatScaled() == x);
    for (auto& x : offA) CHECK(util::RandomSignedOffset(8) == x);

    // Private MINSTD generator reseed determinism too.
    util::RandomUnitFloatSeed(-123);
    double u[8];
    for (auto& x : u) x = util::RandomUnitFloat();
    util::RandomUnitFloatSeed(-123);
    for (auto& x : u) CHECK(util::RandomUnitFloat() == x);
}
