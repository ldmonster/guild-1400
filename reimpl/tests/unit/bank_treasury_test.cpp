// Unit tests for VIBE_Office_ComputeCityTaxRates (gilde.exe 0x5011dc).
// Golden vectors computed independently in python (see report).
#include "test.h"
#include "world/bank_treasury.h"

#include <vector>

using namespace guild;
using namespace guild::world;

// --- CityTaxHalveOfficeLevel: signed /2 toward zero (0x501245..0x501250) ---
TEST(BankTreasuryTax, HalveOfficeLevelTruncTowardZero) {
    CHECK_EQ(CityTaxHalveOfficeLevel(0), 0);
    CHECK_EQ(CityTaxHalveOfficeLevel(1), 0);
    CHECK_EQ(CityTaxHalveOfficeLevel(2), 1);
    CHECK_EQ(CityTaxHalveOfficeLevel(3), 1);
    CHECK_EQ(CityTaxHalveOfficeLevel(10), 5);
    CHECK_EQ(CityTaxHalveOfficeLevel(100), 50);
    // Negative office bytes round toward zero, NOT toward -inf:
    CHECK_EQ(CityTaxHalveOfficeLevel(static_cast<i8>(-1)), 0);
    CHECK_EQ(CityTaxHalveOfficeLevel(static_cast<i8>(-3)), -1);
    CHECK_EQ(CityTaxHalveOfficeLevel(static_cast<i8>(-100)), -50);
}

// --- Seed table recovered from dword_4FFA70 ---
TEST(BankTreasuryTax, SeedTableConstants) {
    const u8 expected[16] = {0x18, 0x27, 0x33, 0x3F, 0x45, 0x4B, 0x12, 0x39,
                             0x3F, 0x06, 0x0C, 0x2D, 0x33, 0x1E, 0x45, 0x4B};
    for (int i = 0; i < 16; ++i)
        CHECK_EQ(kCityTaxSeedRates[i], expected[i]);
}

// --- CityTaxRateFor: per-city rate cell (0x50123f..0x501263) ---
TEST(BankTreasuryTax, RateForNoBias) {
    // base=3, level 0: kSeedRates[3..7] = 3F 45 4B 12 39 = 63 69 75 18 57
    CHECK_EQ(CityTaxRateFor(3, 0, 0), static_cast<i8>(63));
    CHECK_EQ(CityTaxRateFor(3, 1, 0), static_cast<i8>(69));
    CHECK_EQ(CityTaxRateFor(3, 2, 0), static_cast<i8>(75));
    CHECK_EQ(CityTaxRateFor(3, 3, 0), static_cast<i8>(18));
    CHECK_EQ(CityTaxRateFor(3, 4, 0), static_cast<i8>(57));
}

TEST(BankTreasuryTax, RateForWrapsModulo16) {
    // base=14, level 0: slots 14,15,0,1 -> 45 4B 18 27 = 69 75 24 39
    CHECK_EQ(CityTaxRateFor(14, 0, 0), static_cast<i8>(69));
    CHECK_EQ(CityTaxRateFor(14, 1, 0), static_cast<i8>(75));
    CHECK_EQ(CityTaxRateFor(14, 2, 0), static_cast<i8>(24));
    CHECK_EQ(CityTaxRateFor(14, 3, 0), static_cast<i8>(39));
}

TEST(BankTreasuryTax, RateForWithBias) {
    // base=0, levels [10,20,0,100,-40], slots 0..4 = 18 27 33 3F 45 = 24 39 51 63 69
    //   24 - 10/2=5  -> 19
    //   39 - 20/2=10 -> 29
    //   51 - 0       -> 51
    //   63 - 100/2=50-> 13
    //   69 - (-40)/2=-20 -> 89
    CHECK_EQ(CityTaxRateFor(0, 0, 10),               static_cast<i8>(19));
    CHECK_EQ(CityTaxRateFor(0, 1, 20),               static_cast<i8>(29));
    CHECK_EQ(CityTaxRateFor(0, 2, 0),                static_cast<i8>(51));
    CHECK_EQ(CityTaxRateFor(0, 3, 100),              static_cast<i8>(13));
    CHECK_EQ(CityTaxRateFor(0, 4, static_cast<i8>(-40)), static_cast<i8>(89));
}

// --- ComputeCityTaxRates: full pass ---
TEST(BankTreasuryTax, ComputePassFillsBytesAndReturnsLast) {
    i32 cities[5] = {1, 2, 3, 4, 5};
    i8 out[5] = {0};
    // No resolver installed -> inert default (level 0 everywhere). base=3.
    CityTaxSetOfficeLevelResolver(nullptr, nullptr);
    int ret = ComputeCityTaxRates(3, cities, 5, out);
    CHECK_EQ(out[0], static_cast<i8>(63));
    CHECK_EQ(out[1], static_cast<i8>(69));
    CHECK_EQ(out[2], static_cast<i8>(75));
    CHECK_EQ(out[3], static_cast<i8>(18));
    CHECK_EQ(out[4], static_cast<i8>(57));
    // return value is the LAST full (untruncated) biased int == 57 here.
    CHECK_EQ(ret, 57);
}

TEST(BankTreasuryTax, ComputeEmptyReturnsMaskedBase) {
    // count<=0: loop never runs; returns base & 0xFFFF, no writes.
    CHECK_EQ(ComputeCityTaxRates(7, nullptr, 0, nullptr), 7);
    CHECK_EQ(ComputeCityTaxRates(0x1000A, nullptr, 0, nullptr), 0x0A);
    CHECK_EQ(ComputeCityTaxRates(5, nullptr, -3, nullptr), 5);
}

// --- resolver injection ---
namespace {
i8 ConstLevelResolver(i32 /*cityId*/, void* ctx) {
    return static_cast<i8>(*static_cast<int*>(ctx));
}
}

TEST(BankTreasuryTax, ComputeUsesInstalledResolver) {
    i32 cities[5] = {10, 20, 30, 40, 50};
    i8 out[5] = {0};
    int level = 100;  // half = 50 bias on every city
    CityTaxSetOfficeLevelResolver(&ConstLevelResolver, &level);
    int ret = ComputeCityTaxRates(0, cities, 5, out);
    // base=0 seeds slots 0..4 = 24 39 51 63 69; minus 50 each:
    CHECK_EQ(out[0], static_cast<i8>(24 - 50));  // -26
    CHECK_EQ(out[1], static_cast<i8>(39 - 50));  // -11
    CHECK_EQ(out[2], static_cast<i8>(51 - 50));  // 1
    CHECK_EQ(out[3], static_cast<i8>(63 - 50));  // 13
    CHECK_EQ(out[4], static_cast<i8>(69 - 50));  // 19
    CHECK_EQ(ret, 19);
    CityTaxSetOfficeLevelResolver(nullptr, nullptr);  // restore inert default
}

TEST(BankTreasuryTax, RateForByteWrapOnLargeNegativeBias) {
    // seed 0x4B=75 at slot 5, level i8 -120 -> half=-60 -> 75-(-60)=135 -> wraps to -121.
    CHECK_EQ(CityTaxRateFor(5, 0, static_cast<i8>(-120)), static_cast<i8>(-121));
}
