#include "test.h"
#include "crt/rand.h"
#include "util/math_random.h"
#include "util/coord.h"

using namespace guild;

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
