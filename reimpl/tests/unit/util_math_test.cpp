#include "test.h"
#include "crt/rand.h"
#include "util/math.h"
#include "util/math_trig.h"
#include "util/math_rng_float.h"
#include <cmath>

using namespace guild;

namespace {
bool close(double a, double b, double eps = 1e-9) { return std::fabs(a - b) <= eps; }
} // namespace

// ---------------------------------------------------------------------------
// Scalar / range
// ---------------------------------------------------------------------------

TEST(Math, ClampValueRangeBasic) {
    int out = -1;
    // lo=10, mul=1 -> hi = 10 + 10000 = 10010
    CHECK_EQ(util::ClampValueRange(10, 5000, &out, 1), 1);
    CHECK_EQ(out, 5000);
    CHECK_EQ(util::ClampValueRange(10, 99999, &out, 1), 1);
    CHECK_EQ(out, 10010); // clamped to hi
    CHECK_EQ(util::ClampValueRange(10, 3, &out, 1), 1);
    CHECK_EQ(out, 10);    // clamped to lo
}

TEST(Math, ClampValueRangeRejectsBadArgs) {
    int out = 777;
    CHECK_EQ(util::ClampValueRange(0, 5, &out, 1), 0);
    CHECK_EQ(out, 777); // untouched
    CHECK_EQ(util::ClampValueRange(5, 5, &out, 0), 0);
    CHECK_EQ(out, 777);
}

TEST(Math, Distance2D) {
    // 3-4-5 triangle scaled by 2.9081632653
    CHECK(close(util::Distance2D(0, 0, 4, 3), 5.0 * 2.9081632653));
    CHECK(close(util::Distance2D(0, 0, 0, 0), 0.0));
}

TEST(Math, NormalizeAngleAndDecrement) {
    CHECK_EQ(util::NormalizeAngle(3.0), 3.0);
    CHECK_EQ(util::NormalizeAngle(-2.0), -3.0);   // negative -> add -1
    CHECK_EQ(util::DecrementAndAbs(2.0), 3.0);    // -Normalize(-2.0) = -(-3.0) = 3.0
}

TEST(Math, StoreAndZero) {
    double slot = 99.0;
    double r = util::StoreAndZero(5.5, &slot);
    CHECK_EQ(slot, 5.5);
    CHECK_EQ(r, 0.0);
}

TEST(Math, Atan2MatchesLibm) {
    const double pts[][2] = {{1, 1}, {-1, 1}, {1, -1}, {-1, -1}, {0, 1}, {3, 4}, {-7, 2}};
    for (auto& p : pts)
        CHECK(close(util::Atan2(p[0], p[1]), std::atan2(p[0], p[1])));
    CHECK(close(util::Atan2Unary(0.5), std::atan2(0.5, 1.0)));
}

TEST(Math, AcosGuarded) {
    for (double x = -0.99; x <= 0.99; x += 0.07)
        CHECK(close(util::AcosGuarded(x), std::acos(x)));
    CHECK_EQ(util::AcosGuarded(1.5), 0.0);          // x>1 saturates
    CHECK(close(util::AcosGuarded(-1.5), 3.14159265358979323846));
}

// ---------------------------------------------------------------------------
// Interpolation
// ---------------------------------------------------------------------------

TEST(Math, CatmullRomEndpoints) {
    // Recovered basis: segment runs between p0 and p1 (p2,p3 are tangent controls).
    // t=0 -> p0, t=1 -> p1 (golden values from the transcribed formula).
    CHECK(close(util::CatmullRomInterp(10, 20, 30, 40, 0.0f), 10.0));
    CHECK(close(util::CatmullRomInterp(10, 20, 30, 40, 1.0f), 20.0));
    CHECK(close(util::CatmullRomInterp(10, 20, 30, 40, 0.5f), 13.75));
}

TEST(Math, CubicBezierEndpoints) {
    float p0[2] = {0, 0}, p1[2] = {1, 2}, p2[2] = {3, 1}, p3[2] = {4, 4};
    float out[2];
    util::CubicBezierPoint(p0, p1, p2, p3, 0.0f, out);
    CHECK(close(out[0], 0.0, 1e-4) && close(out[1], 0.0, 1e-4));
    util::CubicBezierPoint(p0, p1, p2, p3, 1.0f, out);
    // t=1 -> p2 (the original's basis maps t^3 onto control p2)
    CHECK(close(out[0], 3.0, 1e-4) && close(out[1], 1.0, 1e-4));
}

TEST(Math, LerpClampedCoord) {
    // a..b = 0..10, span 10, d=5 -> 0 + 10/10*5 = 5, +0.5 -> 5 (round)
    CHECK_EQ(util::LerpClampedCoord(0, 10, 10, 5), 5);
    CHECK_EQ(util::LerpClampedCoord(10, 0, 10, 5), 5); // a,b swapped
    CHECK_EQ(util::LerpClampedCoord(0, 10, 10, 7), 7);
}

// ---------------------------------------------------------------------------
// Vector
// ---------------------------------------------------------------------------

TEST(Math, VectorLerp) {
    float a[3] = {0, 0, 0}, b[3] = {10, 20, 30}, out[3];
    util::VectorLerp(a, b, 0.5f, out);
    CHECK(close(out[0], 5.0, 1e-5) && close(out[1], 10.0, 1e-5) && close(out[2], 15.0, 1e-5));
}

TEST(Math, VectorNormalize) {
    float v[3] = {3, 4, 0};
    util::VectorNormalize(v);
    CHECK(close(v[0], 0.6, 1e-5) && close(v[1], 0.8, 1e-5) && close(v[2], 0.0, 1e-5));
    float z[3] = {0, 0, 0};
    util::VectorNormalize(z);
    CHECK_EQ(z[0], 0.0f);
    CHECK_EQ(z[1], 0.0f);
    CHECK_EQ(z[2], 0.0f);
}

TEST(Math, VectorWithinTolerance) {
    float a[3] = {1, 2, 3}, b[3] = {1.01f, 2.0f, 3.0f};
    CHECK(util::VectorWithinTolerance(a, b, 0.1f));
    CHECK(!util::VectorWithinTolerance(a, b, 0.001f));
}

TEST(Math, TriangleNormal) {
    float a[3] = {0, 0, 0}, b[3] = {1, 0, 0}, c[3] = {0, 1, 0}, n[3];
    util::TriangleNormal(a, b, c, n);
    // unit length
    double len = std::sqrt((double)n[0]*n[0] + (double)n[1]*n[1] + (double)n[2]*n[2]);
    CHECK(close(len, 1.0, 1e-4));
}

TEST(Math, VectorAngleBetween) {
    float a[3] = {1, 0, 0};
    float same[3] = {1, 0, 0};
    CHECK(close(util::VectorAngleBetween(a, same), 0.0, 1e-4));
    float opp[3] = {-1, 0, 0};
    CHECK(close(util::VectorAngleBetween(a, opp), -3.1415927, 1e-4));
}

TEST(Math, VectorAngleWrappedReturnsFinite) {
    float a[3] = {1, 0, 1}, b[3] = {-1, 0, 1};
    double r = util::VectorAngleWrapped(a, b);
    CHECK(std::isfinite(r));
}

TEST(Math, MaxVectorLength) {
    float vecs[8] = {3, 0, 0, /*pad*/ 0,  0, 0, 4, /*pad*/ 0}; // lengths 3 and 4
    CHECK(close(util::MaxVectorLength(vecs, 2), 4.0, 1e-4));
}

// ---------------------------------------------------------------------------
// 64-bit integer helpers
// ---------------------------------------------------------------------------

TEST(Math, Int64Helpers) {
    CHECK_EQ(util::UInt64Multiply(0x100000000ull, 3ull), 0x300000000ull);
    CHECK_EQ(util::Multiply64(7, 0, 6, 0), 42);
    CHECK_EQ(util::UnsignedLongLongDivide(1000ull, 7ull), 142ull);
    CHECK_EQ(util::UnsignedLongLongDivide(5ull, 0ull), 0ull); // div-by-0 guard
    CHECK_EQ(util::LongLongDivide(-1000, 7), (i64)(-142));
}

// ---------------------------------------------------------------------------
// Random-float family
// ---------------------------------------------------------------------------

TEST(MathRng, UnitFloatRangeAndDeterminism) {
    util::RandomUnitFloatSeed(-42);
    for (int i = 0; i < 50000; ++i) {
        double v = util::RandomUnitFloat();
        CHECK(v >= 0.0 && v <= 0.99999988);
    }
    // reseed determinism: same seed -> same first draws (golden from python model)
    util::RandomUnitFloatSeed(-42);
    const double golden[] = {0.4719730019569397, 0.5390093922615051,
                             0.7370190024375916, 0.18827413022518158,
                             0.9935095906257629, 0.5304490327835083};
    for (double g : golden)
        CHECK(close(util::RandomUnitFloat(), g, 1e-6));
}

TEST(MathRng, UnitFloatPositiveSeedsCollapse) {
    // Faithful quirk: ANY positive seed re-inits the MINSTD state to 1.
    util::RandomUnitFloatSeed(1);
    double a = util::RandomUnitFloat();
    util::RandomUnitFloatSeed(99999);
    double b = util::RandomUnitFloat();
    CHECK(close(a, b, 1e-9));
}

TEST(MathRng, ScaledIntInRange) {
    util::RandomUnitFloatSeed(-7);
    for (int i = 0; i < 10000; ++i) {
        int r = util::RandomScaledInt(100);
        CHECK(r >= 0 && r < 100);
    }
}

TEST(MathRng, ScaledIntGolden) {
    util::RandomUnitFloatSeed(-42);
    const int golden[] = {47, 53, 73, 18, 99, 53};
    for (int g : golden)
        CHECK_EQ(util::RandomScaledInt(100), g);
}

TEST(MathRng, FloatScaledRange) {
    crt::Srand(12345);
    for (int i = 0; i < 10000; ++i) {
        double v = util::RandomFloatScaled();
        CHECK(v >= 0.0 && v <= 1.0 + 1e-4);
    }
}

TEST(MathRng, FloatScaledDeterminism) {
    crt::Srand(12345);
    double first = util::RandomFloatScaled();
    crt::Srand(12345);
    CHECK(close(util::RandomFloatScaled(), first, 0.0));
}

TEST(MathRng, SquaredUnitRange) {
    crt::Srand(3);
    for (int i = 0; i < 10000; ++i) {
        double v = util::RandomSquaredUnit();
        CHECK(v >= 0.0 && v <= 1.0 + 1e-3);
    }
}

TEST(MathRng, SquaredSignedRange) {
    crt::Srand(5);
    for (int i = 0; i < 10000; ++i) {
        double v = util::RandomSquaredSigned();
        CHECK(v >= -1.0 - 1e-3 && v <= 1.0 + 1e-3);
    }
}

TEST(MathRng, SignedOffsetRange) {
    crt::Srand(11);
    for (int i = 0; i < 10000; ++i) {
        int r = util::RandomSignedOffset(5); // [-5, 5]
        CHECK(r >= -5 && r <= 5);
    }
    // n with 2n+1 == 0x10000 wrap is unreachable for u16; check the -n special.
    CHECK_EQ(util::RandomSignedOffset(0x8000), -0x8000); // 2*0x8000=0x10000, +1 -> 1; not special
}

TEST(MathRng, RandomRangeBoundsAndDeterminism) {
    util::RandomRangeSeed(1);
    for (int i = 0; i < 10000; ++i) {
        u32 r = util::RandomRange(6);
        CHECK(r < 6);
    }
    util::RandomRangeSeed(42);
    u32 a = util::RandomRange(1000);
    util::RandomRangeSeed(42);
    CHECK_EQ(util::RandomRange(1000), a);
}

TEST(MathRng, RangeWithBase) {
    crt::Srand(77);
    for (int i = 0; i < 5000; ++i) {
        char r = util::RandomRangeWithBase(0); // [21,62]
        CHECK(r >= 21 && r <= 62);
    }
    crt::Srand(77);
    for (int i = 0; i < 5000; ++i) {
        char r = util::RandomRangeWithBase(10); // base<42 -> [10,15]
        CHECK(r >= 10 && r <= 15);
    }
    crt::Srand(77);
    for (int i = 0; i < 5000; ++i) {
        char r = util::RandomRangeWithBase(50); // base>=42 -> [50,60]
        CHECK(r >= 50 && r <= 60);
    }
}
