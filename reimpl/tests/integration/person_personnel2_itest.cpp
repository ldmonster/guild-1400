// Integration: drive person_personnel2's PersonComputeAssetWorth against a REAL
// reconstructed sibling — VIBE_Person_SumCurrencyHeld (inventory_wealth.cpp
// 0x59152c), the currency-stack aggregator. This is the live wiring: in the running
// game ComputeAssetWorth's currency term IS VIBE_Person_SumCurrencyHeld of the
// household head. The reconstructed SumCurrencyHeld takes a ContainerView (it scans
// the person's currency child stacks) rather than a raw record — which is precisely
// why person_personnel2 routes it through the PersonPersonnel2Hooks::SumCurrencyHeld
// virtual hook. Here we override that hook to adapt the head Person into a
// ContainerView and forward into the genuine inventory_wealth::PersonSumCurrencyHeld,
// so the asset-worth currency term is the REAL aggregator's verdict, end to end.
//
// ComputeAssetWorth also reads the REAL shared g_persons[] table (entity.cpp) to
// resolve the household head via the +0x27 family-slot word — another live sibling.
// The currency proto table + active player are seeded through the real Wealth* API.
#include "test.h"

#include "sim/person_personnel2.h"
#include "sim/inventory_wealth.h"   // REAL PersonSumCurrencyHeld + Wealth* API
#include "sim/entity.h"             // REAL g_persons[] (shared sibling table)
#include "sim/person.h"            // PersonGetByte/Word/Dword

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

constexpr i16 kCurrencyProto = 5;   // player 0's currency good id (proto table)

// The household head's currency stacks, aggregated through the REAL sibling.
std::vector<StockChild> g_headStacks;

// A hooks subclass whose SumCurrencyHeld forwards into the REAL aggregator.
struct RealWealthHooks : PersonPersonnel2Hooks {
    int SumCurrencyHeld(const Person* /*head*/) override {
        ContainerView cv;
        cv.children = g_headStacks;             // the head's currency child stacks
        return PersonSumCurrencyHeld(cv);       // REAL inventory_wealth aggregator
    }
};

void ClearPersons() {
    for (int i = 0; i < kPersonCapacity; ++i) {
        std::memset(&g_persons[i], 0, sizeof(Person));
        g_persons[i].marker = -1;
        g_personIds[i] = 0;
    }
}

// Seat the household head in slot `idx` with a given kind byte (+2).
void SeatHead(int idx, u8 kind) {
    g_persons[idx].marker = 0;
    auto* base = reinterpret_cast<u8*>(&g_persons[idx]);
    base[2] = kind;     // kind byte the < 10 gate keys on
}

void SeedWealth() {
    WealthSetCurrencyProtoTable({ kCurrencyProto });   // player 0 -> proto 5
    WealthSetActivePlayer(0);
    CHECK_EQ(static_cast<int>(WealthCurrencyProto(0)), kCurrencyProto);
}

// Build a person record with a family-slot word (+0x27) pointing at head slot `idx`.
struct PersonBuf {
    Person p;
    PersonBuf() { std::memset(&p, 0, sizeof p); }
    void SetFamilyWord(u16 v) {
        std::memcpy(reinterpret_cast<u8*>(&p) + 0x27, &v, 2);
    }
};

} // namespace

// includeBuildings=0: ComputeAssetWorth returns ONLY the currency term, which the
// REAL SumCurrencyHeld computes from the household head's matching-proto stacks.
TEST(PersonPersonnel2Itest, AssetWorthCurrencyFromRealSum) {
    ClearPersons();
    SeedWealth();
    RealWealthHooks hooks;
    SetPersonPersonnel2Hooks(&hooks);

    // Head in slot 12, kind 3 (< 10 -> currency counted). Holds 60+90 = 150 in
    // proto-5 stacks, plus a non-currency stack the REAL aggregator ignores.
    SeatHead(12, 3);
    g_headStacks = { {kCurrencyProto, 60}, {kCurrencyProto, 90}, {77, 1000} };

    PersonBuf rec;
    rec.SetFamilyWord(12);     // +0x27 -> head slot index

    int worth = PersonComputeAssetWorth(&rec.p, /*includeBuildings*/ 0);
    CHECK_EQ(worth, 150);      // real currency sum of the head

    SetPersonPersonnel2Hooks(nullptr);
}

// The kind-gate: when the household head's kind byte is >= 10, ComputeAssetWorth
// must NOT count its currency (the REAL aggregator is never consulted) -> 0.
TEST(PersonPersonnel2Itest, AssetWorthSkipsCurrencyWhenHeadKindHigh) {
    ClearPersons();
    SeedWealth();
    RealWealthHooks hooks;
    SetPersonPersonnel2Hooks(&hooks);

    SeatHead(20, 10);          // kind 10 -> NOT < 10, currency skipped
    g_headStacks = { {kCurrencyProto, 500} };

    PersonBuf rec;
    rec.SetFamilyWord(20);

    int worth = PersonComputeAssetWorth(&rec.p, 0);
    CHECK_EQ(worth, 0);

    SetPersonPersonnel2Hooks(nullptr);
}

// No valid family slot (+0x27 == 0xFFFF): currency term is skipped entirely.
TEST(PersonPersonnel2Itest, AssetWorthNoFamilyMeansZeroCurrency) {
    ClearPersons();
    SeedWealth();
    RealWealthHooks hooks;
    SetPersonPersonnel2Hooks(&hooks);

    g_headStacks = { {kCurrencyProto, 999} };

    PersonBuf rec;
    rec.SetFamilyWord(0xFFFF); // no household

    int worth = PersonComputeAssetWorth(&rec.p, 0);
    CHECK_EQ(worth, 0);

    SetPersonPersonnel2Hooks(nullptr);
}

// CheckDebtRatioCritical also routes its currency term through the SAME real hook:
// with a large reserve+currency the person is solvent (net > 0) -> not critical.
TEST(PersonPersonnel2Itest, DebtRatioSolventViaRealSum) {
    ClearPersons();
    SeedWealth();
    RealWealthHooks hooks;
    SetPersonPersonnel2Hooks(&hooks);

    g_headStacks = { {kCurrencyProto, 1000} };   // REAL sum = 1000

    PersonBuf rec;   // kind 0 (threshold 0.20); rec itself is the SumCurrencyHeld arg
    // net = reserve + sum = 0 + 1000 = 1000 > 0 -> solvent.
    bool critical = PersonCheckDebtRatioCritical(&rec.p, /*reserve*/ 0);
    CHECK_EQ(critical, false);

    SetPersonPersonnel2Hooks(nullptr);
}
