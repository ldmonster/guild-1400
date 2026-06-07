// Integration test: the city district crime/satisfaction grid driven ALONGSIDE
// the REAL world relation-matrix sibling (world/relation.cpp). A crime event
// raises the district crime accumulator (grid) AND lowers the perpetrator's
// standing toward the victim in the relation matrix — exercising both standing
// systems linked into one binary (no mocks for relation; the grid leaves use a
// deterministic mock env as designed).
#include "test.h"

#include <cstring>
#include <vector>

#include "world/city_satisfaction_grid.h"
#include "world/relation.h"   // REAL sibling: RelationGet / RelationSet

using namespace guild;
using namespace guild::world;

namespace {

struct ItestEnv : GridEnv {
    int divisor = 1;
    float px = 4.0f, pz = 4.0f;
    int weight = 1;

    int RandomModulo(u16 n) override { return n ? 0 : 0; }
    bool HeightmapWorldToTile(float x, float z, int& tx, int& tz) override {
        tx = static_cast<int>(x); tz = static_cast<int>(z); return true;
    }
    int HeightmapDivisor() override { return divisor; }
    int Residents(const GridResident** out) override { *out = nullptr; return 0; }
    bool ResolvePlayerObject(float& x, float& z) override { x = px; z = pz; return true; }
    int CrimeWeight(u8) override { return weight; }
};

}  // namespace

// A crime by person A against person B: bump the district grid via the real
// CityAddCrimeToGrid AND drop A->B standing via the real RelationSet/Get.
TEST(CitySatGridItest, CrimeRaisesGridAndLowersStanding) {
    CityGridReset();
    RelationReset();

    const int A = 3, B = 5;
    // Seed an initial positive standing.
    RelationSet(A, B, 40);
    CHECK_EQ(RelationGet(A, B), 40);

    ItestEnv env;
    env.divisor = 1;
    env.px = 4.0f; env.pz = 4.0f;   // crime at district (4,4)
    env.weight = 6;

    int ox = -1, oy = -1;
    CityAddCrimeToGrid(env, /*lawId*/2, &ox, &oy);
    CHECK_EQ(ox, 4);
    CHECK_EQ(oy, 4);

    // District grid recorded the crime.
    CHECK_EQ(static_cast<int>(GridCrimeCentre(4, 4)), 6);

    // The relation sibling: lower A->B standing by the same weight, faithfully
    // through the real matrix packing.
    RelationSet(A, B, RelationGet(A, B) - env.weight);
    CHECK_EQ(RelationGet(A, B), 34);

    // The matrix is asymmetric: B->A is still neutral (0), unaffected.
    CHECK_EQ(RelationGet(B, A), 0);
    // Self-standing is the fixed sentinel.
    CHECK_EQ(RelationGet(A, A), 127);
}

// Build a multi-district satisfaction grid, then resolve it alongside the real
// relation matrix to make sure the two flat backing stores don't alias / clash
// when linked together (distinct globals, distinct state).
TEST(CitySatGridItest, GridAndRelationStateAreIndependent) {
    CityGridReset();
    RelationReset();

    // Populate the relation matrix.
    for (int i = 0; i < 8; ++i)
        RelationSet(i, (i + 1) % 8, i * 5 - 10);

    // Independently add crimes across several districts.
    ItestEnv env;
    env.divisor = 1; env.weight = 1;
    for (int d = 0; d < 8; ++d) {
        env.px = static_cast<float>(d);
        env.pz = static_cast<float>(d);
        CityAddCrimeToGrid(env, 0, nullptr, nullptr);
    }

    // Each district centre carries exactly one crime.
    for (int d = 0; d < 8; ++d)
        CHECK_EQ(static_cast<int>(GridCrimeCentre(d, d)), 1);

    // The relation matrix is intact and independent.
    for (int i = 0; i < 8; ++i)
        CHECK_EQ(RelationGet(i, (i + 1) % 8), i * 5 - 10);
}

// Remove-after-add restores grid crime to zero while the relation sibling holds
// its own state — verifying the inverse op + cross-module isolation.
TEST(CitySatGridItest, RemoveRestoresGridIndependentOfRelations) {
    CityGridReset();
    RelationReset();
    RelationSet(1, 2, 99);

    ItestEnv env;
    env.divisor = 1; env.px = 3.0f; env.pz = 3.0f; env.weight = 8;
    CityAddCrimeToGrid(env, 0, nullptr, nullptr);
    CityRemoveCrimeFromGrid(env, 0);

    for (int x = 0; x < 9; ++x)
        for (int y = 0; y < 9; ++y)
            CHECK_EQ(static_cast<int>(GridCrimeCentre(x, y)) +
                     static_cast<int>(GridCrimeArea(x, y)), 0);

    CHECK_EQ(RelationGet(1, 2), 99);
}
