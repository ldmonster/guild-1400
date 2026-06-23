#include "test.h"
#include "crt/rand.h"
#include "util/math_random.h"
#include "util/math_rng_float.h"
#include "util/coord.h"
#include <cstring>

using namespace guild;

// Reinterpret a double's exact value to a u64 bit pattern for byte-exact compares.
static u64 BitsD(double d) { u64 u; std::memcpy(&u, &d, 8); return u; }

// Golden vectors computed independently from the LCG spec (state*1103515245+12345,
// return (state>>16)&0x7FFF) — the classic POSIX example sequence.
TEST(Rand, Seed1Sequence) {
    crt::Srand(1);
    const int golden[] = {16838, 5758, 10113, 17515, 31051, 5627, 23010, 7419,
                          16212, 4086, 2749, 12767, 9084, 12060, 32225, 17543};
    for (int g : golden)
        CHECK_EQ(crt::RandNext(), g);
}

TEST(Rand, Seed12345Sequence) {
    crt::Srand(12345);
    const int golden[] = {21468, 9988, 22117, 3498, 16927, 16045, 19741, 12122};
    for (int g : golden)
        CHECK_EQ(crt::RandNext(), g);
}

TEST(Rand, RangeBounds) {
    crt::Srand(99);
    for (int i = 0; i < 100000; ++i) {
        int r = crt::RandNext();
        CHECK(r >= 0 && r <= 0x7FFF);
    }
}

TEST(Rand, StatePtrIsStable) {
    CHECK(crt::RandStatePtr() != nullptr);
    crt::Srand(0xDEADBEEF);
    CHECK_EQ(*crt::RandStatePtr(), 0xDEADBEEFu);
}

// Wave-11 hardening: the LCG multiply 1103515245u * state wraps in u32 (defined
// unsigned overflow). Exercise extreme seeds to confirm no UBSAN trap and that
// the result stays in [0, 0x7FFF].
TEST(Rand, ExtremeSeedsCleanWrap) {
    for (u32 seed : {0u, 1u, 0xFFFFFFFFu, 0x80000000u, 0x7FFFFFFFu}) {
        crt::Srand(seed);
        for (int i = 0; i < 1000; ++i) {
            int r = crt::RandNext();
            CHECK(r >= 0 && r <= 0x7FFF);
        }
    }
}

// Seed 0 produces a deterministic, reproducible stream (state starts at 0).
TEST(Rand, Seed0Reproducible) {
    crt::Srand(0);
    int a[8];
    for (int& x : a) x = crt::RandNext();
    crt::Srand(0);
    for (int x : a) CHECK_EQ(crt::RandNext(), x);
}

TEST(MathRandom, ModuloZeroReturnsZero) {
    CHECK_EQ(util::RandomModulo(0), 0);
}

TEST(MathRandom, ModuloInRange) {
    crt::Srand(7);
    for (int i = 0; i < 10000; ++i) {
        int r = util::RandomModulo(6);
        CHECK(r >= 0 && r < 6);
    }
}

TEST(MathRandom, ModuloMatchesFormula) {
    // RandomModulo(n) must equal RandNext()%n on the same stream.
    crt::Srand(555);
    int expected = crt::RandNext() % 100;
    crt::Srand(555);
    CHECK_EQ(util::RandomModulo(100), expected);
}

TEST(Coord, TruncTowardZero) {
    CHECK_EQ(util::ConvertX(3.9), 3.0);
    CHECK_EQ(util::ConvertX(-3.9), -3.0);
    CHECK_EQ(util::ConvertX(0.0), 0.0);
    CHECK_EQ(util::ConvertX(7.0), 7.0);
}

// --- Golden vectors for the CRT-LCG-backed float helpers (0x58b910/98c/8c4/c00) -
// Computed independently from the LCG spec (RandNext = (state>>16)&0x7FFF) and the
// recovered scale = float bit pattern 0x38000100 (== 1/32767 region, flt_62675C).

TEST(MathRandom, FloatScaledGolden) {
    // gilde.exe 0x58b910: (double)RandNext() * (float)0x38000100.
    crt::Srand(7);
    const double golden[] = {0.5970641188323498, 0.2992645036429167,
                             0.3316751606762409, 0.6919766832143068};
    for (double g : golden)
        CHECK_EQ(BitsD(util::RandomFloatScaled()), BitsD(g));
}

TEST(MathRandom, SquaredUnitGolden) {
    // gilde.exe 0x58b98c: x = RandNext()*scale; return x*x (ONE draw).
    crt::Srand(7);
    const double golden[] = {0.3564855619970503, 0.08955924314064129,
                             0.11000841220961023, 0.47883173011227315};
    for (double g : golden)
        CHECK_EQ(BitsD(util::RandomSquaredUnit()), BitsD(g));
}

TEST(MathRandom, SquaredUnitOneDraw) {
    // Confirm exactly one RandNext draw per call (stream parity with RandNext).
    crt::Srand(123);
    (void)util::RandomSquaredUnit();
    int after_one = crt::RandNext();
    crt::Srand(123);
    crt::RandNext();              // skip the one draw RandomSquaredUnit consumes
    CHECK_EQ(crt::RandNext(), after_one);
}

TEST(MathRandom, SquaredSignedRange) {
    // gilde.exe 0x58b92c: v = RandNext()*scale*2 - 1; sign-preserving square in [-1,1].
    crt::Srand(7);
    for (int i = 0; i < 5000; ++i) {
        double v = util::RandomSquaredSigned();
        CHECK(v >= -1.0 && v <= 1.0);
    }
    // One draw per call.
    crt::Srand(321);
    (void)util::RandomSquaredSigned();
    int after = crt::RandNext();
    crt::Srand(321);
    crt::RandNext();
    CHECK_EQ(crt::RandNext(), after);
}

TEST(MathRandom, SignedOffsetGolden) {
    // gilde.exe 0x58b8c4: span=2n+1; RandNext()%span - n; uniform in [-n, n].
    crt::Srand(7);
    const int golden[] = {3, 3, 1, -2, -1, 2}; // n=3, span=7
    for (int g : golden)
        CHECK_EQ(util::RandomSignedOffset(3), g);
    // Bounds for a larger span.
    crt::Srand(50);
    for (int i = 0; i < 5000; ++i) {
        int r = util::RandomSignedOffset(100);
        CHECK(r >= -100 && r <= 100);
    }
}

TEST(MathRandom, RangeWithBaseGolden) {
    // gilde.exe 0x58bc00.
    crt::Srand(7);
    const char b0[] = {55, 41, 53, 57}; // base==0 -> RandNext()%42 + 21
    for (char g : b0) CHECK_EQ(util::RandomRangeWithBase(0), g);

    crt::Srand(7);
    const char b10[] = {14, 12, 12, 10}; // base 10 (<42) -> mod 6
    for (char g : b10) CHECK_EQ(util::RandomRangeWithBase(10), g);

    crt::Srand(7);
    const char b50[] = {56, 55, 50, 53}; // base 50 (>=42) -> mod 11
    for (char g : b50) CHECK_EQ(util::RandomRangeWithBase(50), g);
}

TEST(MathRandom, RangeWithBaseNegativeIsUnsignedCompare) {
    // 0x58bc0c zero-extends the base byte before the `< 42.0` compare, so a negative
    // char (e.g. -1 == 0xFF == 255 unsigned) takes the mod-11 branch, NOT mod-6.
    crt::Srand(7);
    const char golden[] = {5, 4, -1, 2}; // (char)(-1 + RandNext()%11)
    for (char g : golden) CHECK_EQ(util::RandomRangeWithBase(-1), g);
}

TEST(MathRandom, RandomRangeGolden) {
    // gilde.exe 0x538438: private LCG; ((state>>16)&0xFFFF % 0x7FFF) % n.
    util::RandomRangeSeed(1);
    const u32 golden[] = {38, 59, 13, 16, 52, 28};
    for (u32 g : golden) CHECK_EQ(util::RandomRange(100), g);
}

// --- Golden vectors for the private MINSTD Bays-Durham shuffle (0x58b744/870) ----

TEST(MathRandom, UnitFloatGolden) {
    // gilde.exe 0x58b744: returns (double)(float)v, clamped to (double)(float)0.99999988.
    util::RandomUnitFloatSeed(1);
    const float golden[] = {0.4159993529319763f, 0.09196489304304123f,
                            0.7564104795455933f, 0.5297002196311951f,
                            0.9304364919662476f, 0.3835020661354065f,
                            0.653918981552124f,  0.06684223562479019f};
    for (float g : golden) {
        double r = util::RandomUnitFloat();
        // The function returns a float value carried in a double; compare as float.
        CHECK_EQ(static_cast<float>(r), g);
        CHECK(r >= 0.0 && r <= 0.9999998807907104);
    }
}

TEST(MathRandom, UnitFloatClampReturnsFloatCast) {
    // The clamp ceiling is (double)(float)0.99999988 == 0.9999998807907104,
    // NOT the raw double 0.99999988 (0x3FEFFFFFBF935359). Confirm the value can
    // never exceed the float-cast ceiling.
    util::RandomUnitFloatSeed(1);
    const double kFloatCeil = 0.9999998807907104; // (double)(float)0.99999988
    for (int i = 0; i < 20000; ++i)
        CHECK(util::RandomUnitFloat() <= kFloatCeil);
}

TEST(MathRandom, ScaledIntGolden) {
    // gilde.exe 0x58b870: trunc(RandomUnitFloat() * n).
    util::RandomUnitFloatSeed(1);
    const int golden[] = {415, 91, 756, 529, 930, 383, 653, 66};
    for (int g : golden) CHECK_EQ(util::RandomScaledInt(1000), g);
}
