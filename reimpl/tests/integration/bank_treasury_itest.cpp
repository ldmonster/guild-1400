#include "test.h"

// Integration: drive bank_treasury's VIBE_Office_ComputeCityTaxRates against the
// REAL reconstructed office sibling pair VIBE_Office_GetEntryByCity (0x47efb4) +
// VIBE_Office_GetDefinition (0x47f008) — world/office.cpp, reading the live office
// holder table g_officeHolders (byte_B59848) and the office-def table. The
// CityOfficeLevelResolver hook IS exactly that GetEntryByCity->GetDefinition->
// def.byte2 chain in the live binary (the .cpp comment names both addresses); we
// install a resolver that performs that real chain over the real office globals,
// exactly as the engine wires the city-tax seeding, and assert the per-city rate
// bytes carry the office-def bias the real office table reports.
//
// (the seed-rotation roll uses the module's own recovered table; the RandomModulo
// convenience overload would draw the base from the real util RNG, but here we pin
// `base` so the office-table bias — the cross-module effect — is what's asserted.)
#include "world/bank_treasury.h"
#include "world/office.h"        // REAL siblings: g_officeHolders, GetEntryByCity, GetDefinition
#include "world/law_types.h"     // OfficeHolder

#include <cstring>

using namespace guild;
using namespace guild::world;

namespace {

// CityOfficeLevelResolver hook -> the REAL GetEntryByCity -> GetDefinition ->
// def.byte2 chain over the real office globals (no mock office table).
i8 RealOfficeLevelResolver(i32 cityId, void* /*ctx*/) {
    OfficeHolder h;
    if (!OfficeGetEntryByCity(static_cast<u8>(cityId), &h))
        return 0;                       // GetEntryByCity miss -> no bias (faithful)
    OfficeDef d;
    OfficeGetDefinition(h.holder, &d);  // GetDefinition(entry.field0)
    return static_cast<i8>((d.word0 >> 16) & 0xFF);  // def byte +2 (var_1E)
}

// Compute the same def.byte2 the resolver will, by replaying the real chain —
// used to build the expected rate vector independently of the live def-table data.
i8 ExpectedDefByte2(i32 cityId) { return RealOfficeLevelResolver(cityId, nullptr); }

} // namespace

// Seed the REAL office holder table with two occupied seats, install the real
// resolver, and verify ComputeCityTaxRates biases each city's seed rate by the
// def.byte2 the real office table yields — and that an unseeded city falls through
// to the real GetEntryByCity miss (zero bias).
TEST(BankTreasuryItest, CityTaxRatesBiasedByRealOfficeTable) {
    OfficeHolderTableReset();
    // Two real holder entries: city ids 5 and 6, distinct office types/holders so
    // their definitions differ. holder(+0) feeds GetDefinition.
    g_officeHolders[0].holder = 18;  // -> GetDefinition(18), def.byte2 = 4
    g_officeHolders[0].city   = 5;
    g_officeHolders[0].type   = 18;
    g_officeHolders[1].holder = 20;  // -> GetDefinition(20), def.byte2 = 5
    g_officeHolders[1].city   = 6;
    g_officeHolders[1].type   = 20;

    CityTaxSetOfficeLevelResolver(&RealOfficeLevelResolver, nullptr);

    // The seeded entries report a non-zero def.byte2 (the real office def table),
    // so the bias actually moves the rate — proving the cross-module read happened.
    CHECK(ExpectedDefByte2(18) != 0);
    CHECK(ExpectedDefByte2(20) != 0);

    // GetEntryByCity matches on the holder-id byte (+0), per the faithful scan.
    // So we query by the holder ids the entries carry.
    const i32 cities[3] = { 18, 20, 200 };   // 200 has no holder -> real miss
    i8 out[3] = {0, 0, 0};
    const int base = 4;   // pinned seed rotation (kCityTaxSeedRates[(4+i)%16])

    int last = ComputeCityTaxRates(base, cities, 3, out);

    // Build the expected vector via the SAME real office chain.
    for (int i = 0; i < 3; ++i) {
        i8 defByte2 = ExpectedDefByte2(cities[i]);
        int rate = static_cast<int>(kCityTaxSeedRates[(base + i) % 16]);
        i8 want = static_cast<i8>(rate - (static_cast<int>(defByte2) / 2));
        CHECK_EQ(out[i], want);
        // for the two seeded cities the office bias must have shifted the rate.
        if (i < 2) CHECK(out[i] != static_cast<i8>(rate));
    }

    // The unseeded city (200) hit the real GetEntryByCity miss -> zero bias, so its
    // rate is exactly the raw seed slot.
    CHECK_EQ(out[2], static_cast<i8>(kCityTaxSeedRates[(base + 2) % 16]));

    // ComputeCityTaxRates returns the last FULL (untruncated) biased value.
    {
        i8 defByte2 = ExpectedDefByte2(cities[2]);
        int rate = static_cast<int>(kCityTaxSeedRates[(base + 2) % 16]);
        CHECK_EQ(last, rate - (static_cast<int>(defByte2) / 2));
    }

    CityTaxSetOfficeLevelResolver(nullptr, nullptr);
}

// With NO holder seeded for the queried cities, the real GetEntryByCity misses on
// every city, so the resolver returns 0 and the rates are the raw seed table —
// exercising the office sibling's real miss path end to end.
TEST(BankTreasuryItest, AllCitiesMissRealOfficeTableYieldRawSeeds) {
    OfficeHolderTableReset();   // every entry vacant
    CityTaxSetOfficeLevelResolver(&RealOfficeLevelResolver, nullptr);

    const i32 cities[4] = { 100, 101, 102, 103 };
    i8 out[4] = {0, 0, 0, 0};
    const int base = 0;
    ComputeCityTaxRates(base, cities, 4, out);

    for (int i = 0; i < 4; ++i)
        CHECK_EQ(out[i], static_cast<i8>(kCityTaxSeedRates[(base + i) % 16]));

    CityTaxSetOfficeLevelResolver(nullptr, nullptr);
}

// Empty city list: the original never enters the loop and returns the masked base
// roll untouched (and never touches the office table) — the inert-default code path
// for the count<=0 guard.
TEST(BankTreasuryItest, EmptyCityListReturnsMaskedBase) {
    CityTaxSetOfficeLevelResolver(&RealOfficeLevelResolver, nullptr);
    int r = ComputeCityTaxRates(0x1234, nullptr, 0, nullptr);
    CHECK_EQ(r, 0x1234 & 0xFFFF);
    CityTaxSetOfficeLevelResolver(nullptr, nullptr);
}
