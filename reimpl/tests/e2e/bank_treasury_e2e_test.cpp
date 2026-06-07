// E2E: a full "sync buildings and offices" tax-rate reseed pass
// (VIBE_Scene_SyncBuildingAndOffices -> VIBE_Office_ComputeCityTaxRates, 0x5011dc),
// driving the office-level resolver across a small set of cities and verifying
// the byte buffer the original would commit, plus the determinism of the pass.
#include "test.h"
#include "world/bank_treasury.h"

#include <vector>

using namespace guild;
using namespace guild::world;

namespace {
// A tiny stand-in office table: cityId -> office-definition +2 byte. Models the
// GetEntryByCity -> GetDefinition walk the resolver abstracts.
struct OfficeTable {
    int CityToLevel(i32 cityId) const {
        switch (cityId) {
            case 100: return 0;    // no office -> no bias
            case 200: return 8;    // half = 4
            case 300: return 30;   // half = 15
            case 400: return 200;  // i8 200 == -56 -> half = -28 (raises rate)
            default:  return 0;
        }
    }
};

OfficeTable g_table;

i8 TableResolver(i32 cityId, void* /*ctx*/) {
    return static_cast<i8>(g_table.CityToLevel(cityId));
}
}  // namespace

TEST(BankTreasuryTaxE2E, FullReseedPassMatchesGolden) {
    CityTaxSetOfficeLevelResolver(&TableResolver, nullptr);

    i32 cities[4] = {100, 200, 300, 400};
    i8 rates[4] = {0};
    // base=5 -> seed slots 5,6,7,8 = 4B 12 39 3F = 75 18 57 63
    int ret = ComputeCityTaxRates(5, cities, 4, rates);

    // city 100: 75 - half(0)=0   -> 75
    // city 200: 18 - half(8)=4   -> 14
    // city 300: 57 - half(30)=15 -> 42
    // city 400: 63 - half(i8 200 = -56) = 63 - (-28) = 91
    CHECK_EQ(rates[0], static_cast<i8>(75));
    CHECK_EQ(rates[1], static_cast<i8>(14));
    CHECK_EQ(rates[2], static_cast<i8>(42));
    CHECK_EQ(rates[3], static_cast<i8>(91));
    CHECK_EQ(ret, 91);  // last full biased value

    CityTaxSetOfficeLevelResolver(nullptr, nullptr);
}

TEST(BankTreasuryTaxE2E, PassIsDeterministicForFixedBase) {
    CityTaxSetOfficeLevelResolver(&TableResolver, nullptr);
    i32 cities[4] = {100, 200, 300, 400};
    i8 a[4] = {0}, b[4] = {0};
    ComputeCityTaxRates(9, cities, 4, a);
    ComputeCityTaxRates(9, cities, 4, b);
    for (int i = 0; i < 4; ++i)
        CHECK_EQ(a[i], b[i]);
    CityTaxSetOfficeLevelResolver(nullptr, nullptr);
}

TEST(BankTreasuryTaxE2E, RngOverloadProducesInRangeBytes) {
    // The RandomModulo(16)-driven overload: base is unknown, but EVERY produced
    // byte must equal one of the (seed - bias) cells for SOME rotation, and the
    // vector length must match the city count. With the inert default resolver
    // (level 0) each byte is simply a member of the seed table.
    CityTaxSetOfficeLevelResolver(nullptr, nullptr);
    i32 cities[6] = {1, 2, 3, 4, 5, 6};
    std::vector<i8> rates = ComputeCityTaxRates(cities, 6);
    CHECK_EQ(static_cast<int>(rates.size()), 6);
    for (i8 r : rates) {
        bool inTable = false;
        for (int i = 0; i < 16; ++i)
            if (static_cast<i8>(kCityTaxSeedRates[i]) == r) inTable = true;
        CHECK(inTable);
    }
}

TEST(BankTreasuryTaxE2E, EmptyCityListReturnsRollUntouched) {
    // A pass with no cities (e.g. a not-yet-populated world) just returns the
    // masked roll and writes nothing.
    CHECK_EQ(ComputeCityTaxRates(11, nullptr, 0, nullptr), 11);
    std::vector<i8> none = ComputeCityTaxRates(nullptr, 0);
    CHECK_EQ(static_cast<int>(none.size()), 0);
}
