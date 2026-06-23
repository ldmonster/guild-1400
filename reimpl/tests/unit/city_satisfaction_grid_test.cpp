// Unit tests (golden vectors) for the city district satisfaction/crime grid.
// gilde.exe VIBE_City_* cluster (world/city_satisfaction_grid).
#include "test.h"

#include <cstring>
#include <vector>

#include "world/city_satisfaction_grid.h"

using namespace guild;
using namespace guild::world;

namespace {

// Deterministic mock GridEnv: a fixed RNG sequence, a fixed player tile, a fixed
// heightmap divisor, and a settable resident list + crime weight.
struct MockGridEnv : GridEnv {
    std::vector<int> rngSeq;
    size_t rngIdx = 0;
    int divisor = 1;
    bool hasPlayer = true;
    float playerX = 4.0f, playerZ = 4.0f;   // raw world == raw tile here
    bool tileOk = true;
    std::vector<GridResident> residents;
    int weight = 1;
    int op86Calls = 0;
    u8  lastBody[40] = {0};

    int RandomModulo(u16 n) override {
        int v = rngSeq.empty() ? 0 : rngSeq[rngIdx % rngSeq.size()];
        ++rngIdx;
        return n ? (v % n) : 0;
    }
    bool HeightmapWorldToTile(float x, float z, int& tx, int& tz) override {
        if (!tileOk) return false;
        tx = static_cast<int>(x);
        tz = static_cast<int>(z);
        return true;
    }
    int HeightmapDivisor() override { return divisor; }
    int Residents(const GridResident** out) override {
        *out = residents.empty() ? nullptr : residents.data();
        return static_cast<int>(residents.size());
    }
    bool ResolvePlayerObject(float& x, float& z) override {
        if (!hasPlayer) return false;
        x = playerX; z = playerZ; return true;
    }
    int CrimeWeight(u8) override { return weight; }
    int RequestBuildOp86(const u8 body[40]) override {
        ++op86Calls;
        std::memcpy(lastBody, body, 40);
        return 86;
    }
};

// Reference ring cell list (matches the original clamp/visit order). These are
// the SOURCE cells the inner loop iterates (cx in [x-1,x+1] clamped, cy in
// [y-1,y+1] clamped).
std::vector<std::pair<int,int>> Ring(int x, int y) {
    std::vector<std::pair<int,int>> cells;
    int rowHi = x + 1; if (rowHi > 8) rowHi = 8;
    int colHi = y + 1; if (colHi > 8) colHi = 8;
    int rowLo = x - 1; if (rowLo < 0) rowLo = 0;
    if (rowLo <= rowHi)
        for (int cx = rowLo; cx <= rowHi; ++cx) {
            int colLo = y - 1; if (colLo < 0) colLo = 0;
            for (int cy = colLo; cy <= colHi; ++cy)
                cells.emplace_back(cx, cy);
        }
    return cells;
}

// gilde.exe 0x5780d2: the crime-AREA store target. The inner loop PRE-increments
// the cell pointer by 24 (one cell in y) before the store, so each source cell
// (cx,cy) writes the area field of cell (cx, cy+1). The golden vectors below
// assert against these shifted targets (verified against the disasm).
std::vector<std::pair<int,int>> AreaRing(int x, int y) {
    std::vector<std::pair<int,int>> cells;
    for (auto& c : Ring(x, y))
        cells.emplace_back(c.first, c.second + 1);
    return cells;
}

}  // namespace

// ---------------------------------------------------------------------------
// CoordWorldToCityTile: divides raw tile by the (mapSpan>>3) divisor.
// ---------------------------------------------------------------------------
TEST(CitySatGrid, WorldToTileDividesByDivisor) {
    MockGridEnv env;
    env.divisor = 4;
    int tx = -1, ty = -1;
    CHECK(CoordWorldToCityTile(env, 28.0f, 12.0f, tx, ty));
    CHECK_EQ(tx, 7);   // 28/4
    CHECK_EQ(ty, 3);   // 12/4
}

TEST(CitySatGrid, WorldToTileOffMapReturnsFalse) {
    MockGridEnv env;
    env.tileOk = false;
    int tx = 5, ty = 5;
    CHECK(!CoordWorldToCityTile(env, 1.0f, 1.0f, tx, ty));
}

// ---------------------------------------------------------------------------
// AddCrimeToGrid golden vector: centre +w, 3x3 ring crimeArea +w each.
// ---------------------------------------------------------------------------
TEST(CitySatGrid, AddCrimeCentreAndRing) {
    CityGridReset();
    MockGridEnv env;
    env.divisor = 1;
    env.playerX = 4.0f; env.playerZ = 4.0f;  // tile (4,4)
    env.weight = 3;

    int ox = -1, oy = -1;
    CityAddCrimeToGrid(env, /*lawId*/0, &ox, &oy);
    CHECK_EQ(ox, 4);
    CHECK_EQ(oy, 4);

    // Centre crime field == 3.
    CHECK_EQ(static_cast<int>(GridCrimeCentre(4, 4)), 3);
    // Every AREA cell (source cell shifted +1 in y by the +24 pre-increment)
    // == 3 (9 cells, none overlap themselves twice).
    for (auto& c : AreaRing(4, 4))
        CHECK_EQ(static_cast<int>(GridCrimeArea(c.first, c.second)), 3);
    // A cell outside the area ring is untouched.
    CHECK_EQ(static_cast<int>(GridCrimeArea(0, 0)), 0);
}

// ---------------------------------------------------------------------------
// Add then Remove restores the grid to zero (exact inverse).
// ---------------------------------------------------------------------------
TEST(CitySatGrid, AddRemoveCrimeRoundTripsToZero) {
    CityGridReset();
    MockGridEnv env;
    env.divisor = 1;
    env.playerX = 2.0f; env.playerZ = 5.0f;
    env.weight = 7;

    CityAddCrimeToGrid(env, 0, nullptr, nullptr);
    CityRemoveCrimeFromGrid(env, 0);

    for (int x = 0; x < 9; ++x)
        for (int y = 0; y < 9; ++y) {
            CHECK_EQ(static_cast<int>(GridCrimeArea(x, y)), 0);
            CHECK_EQ(static_cast<int>(GridCrimeCentre(x, y)), 0);
        }
}

// ---------------------------------------------------------------------------
// AddCrime with no player object: grid untouched, returns RNG y.
// ---------------------------------------------------------------------------
TEST(CitySatGrid, AddCrimeNoPlayerIsNoOp) {
    CityGridReset();
    MockGridEnv env;
    env.hasPlayer = false;
    env.rngSeq = {3, 5};   // first call -> x, second -> y
    int y = CityAddCrimeToGrid(env, 0, nullptr, nullptr);
    CHECK_EQ(y, 5);        // returns the second RNG draw (mod 8)
    CHECK_EQ(static_cast<int>(GridCrimeCentre(4, 4)), 0);
}

// ---------------------------------------------------------------------------
// Edge ring at (0,0): only 4 cells (2x2). Golden.
// ---------------------------------------------------------------------------
TEST(CitySatGrid, AddCrimeEdgeRingClamps) {
    CityGridReset();
    MockGridEnv env;
    env.divisor = 1;
    env.playerX = 0.0f; env.playerZ = 0.0f;
    env.weight = 2;
    CityAddCrimeToGrid(env, 0, nullptr, nullptr);
    // Source ring at (0,0) is the 2x2 block {(0,0),(0,1),(1,0),(1,1)}; the area
    // writes land at +1 in y: {(0,1),(0,2),(1,1),(1,2)} (0x5780d2 pre-increment).
    auto cells = AreaRing(0, 0);
    CHECK_EQ(static_cast<int>(cells.size()), 4);
    for (auto& c : cells)
        CHECK_EQ(static_cast<int>(GridCrimeArea(c.first, c.second)), 2);
}

// ---------------------------------------------------------------------------
// BuildSatisfactionGrid golden vector: one resident at tile (3,6).
//   v6 = scaled13/scale19 * weight18 = 10/2*4 = 20.
//   centre SatWeight(3,6) += v6*0.5 = 10.
//   The 3x3 ring (0x578a7f) reads +40/writes +16 of the next cell, which the +24
//   pre-increment collapses to SatWeight(cx,cy) += v6*0.5 and SatDenom(cx,cy) +=
//   v6 of the SOURCE cell. Cell (3,6) is in its own ring, so SatWeight(3,6) gets
//   another +10. Faithful result == 10 + 10 == 20 (verified against the disasm).
// ---------------------------------------------------------------------------
TEST(CitySatGrid, BuildSatisfactionWeightGolden) {
    CityGridReset();
    MockGridEnv env;
    env.divisor = 1;
    GridResident r;
    r.active = true; r.id = 1; r.sceneNode = 0x1000;
    r.typeByte = 5;            // eligible (not 0xB..0xD, not 0x10)
    r.scaled13 = 10; r.scale19 = 2; r.weight18 = 4;
    r.worldX = 3.0f; r.worldZ = 6.0f;   // tile (3,6)
    env.residents = {r};

    CityBuildSatisfactionGrid(env);
    CHECK(GridSatWeight(3, 6) > 19.999f && GridSatWeight(3, 6) < 20.001f);
}

// Ineligible type is skipped (no contribution).
TEST(CitySatGrid, BuildSatisfactionSkipsIneligible) {
    CityGridReset();
    MockGridEnv env;
    env.divisor = 1;
    GridResident r;
    r.active = true; r.id = 1; r.sceneNode = 0x1000;
    r.typeByte = 0x10;          // ineligible
    r.scaled13 = 10; r.scale19 = 2; r.weight18 = 4;
    r.worldX = 3.0f; r.worldZ = 6.0f;
    env.residents = {r};

    CityBuildSatisfactionGrid(env);
    CHECK_EQ(static_cast<int>(GridSatWeight(3, 6) * 1000), 0);
}

// Inactive / unplaced / id==-1 residents are skipped.
TEST(CitySatGrid, BuildSatisfactionSkipsInactive) {
    CityGridReset();
    MockGridEnv env;
    env.divisor = 1;
    GridResident a, b, c;
    a.active = false; a.id = 1; a.sceneNode = 1; a.typeByte = 5;
    a.scaled13 = 10; a.scale19 = 1; a.weight18 = 4; a.worldX = 1; a.worldZ = 1;
    b = a; b.active = true; b.id = -1;          // skipped: id == -1
    c = a; c.active = true; c.id = 2; c.sceneNode = 0;  // skipped: unplaced
    env.residents = {a, b, c};
    CityBuildSatisfactionGrid(env);
    CHECK_EQ(static_cast<int>(GridSatWeight(1, 1) * 1000), 0);
}

// ---------------------------------------------------------------------------
// Send commands: body assembly + RequestBuildOp86 forwarding.
// ---------------------------------------------------------------------------
TEST(CitySatGrid, SendSyncCommandBuildsBody) {
    MockGridEnv env;
    int rc = CitySendSyncCommand(env, /*a1*/11, /*clock*/0x1234, 22, 33, 44);
    CHECK_EQ(rc, 86);
    CHECK_EQ(env.op86Calls, 1);
    i32 v[10];
    std::memcpy(v, env.lastBody, 40);
    CHECK_EQ(v[0], 11);
    CHECK_EQ(v[2], 0x1234);   // clock low dword
    CHECK_EQ(v[3], 22);
    CHECK_EQ(v[4], 33);
    CHECK_EQ(v[6], 44);
    CHECK_EQ(v[9], 1768843636);  // sync tag
}

TEST(CitySatGrid, SendResetCommandBuildsBody) {
    MockGridEnv env;
    int rc = CitySendResetCommand(env, /*seq*/7);
    CHECK_EQ(rc, 86);
    i32 v[10];
    std::memcpy(v, env.lastBody, 40);
    CHECK_EQ(v[0], 1);            // reset flag
    CHECK_EQ(v[9], 1701732972);  // reset tag
}

// PlaceRandomCrime: tile within the requested district band.
TEST(CitySatGrid, PlaceRandomCrimeInBand) {
    MockGridEnv env;
    env.divisor = 4;            // span per district = 4
    env.rngSeq = {2, 1};        // tx offset 2, tz 1
    WorldPoint w;
    CHECK(CityPlaceRandomCrime(env, /*districtX*/3, w));
    // base = span*district = 12; tx = rnd(4)%4 + 12 = 14; tz = rnd(4)%4 = 1.
    CHECK_EQ(static_cast<int>(w.x), 14);
    CHECK_EQ(static_cast<int>(w.z), 1);
}
