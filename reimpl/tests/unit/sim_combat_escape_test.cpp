// Unit tests for the combat tactical-AI tile search (src/sim/combat_escape.*).
// Golden vectors computed independently in Python (see the module report).
#include "test.h"

#include "sim/combat_escape.h"
#include "render/heightmap.h"

#include <cmath>
#include <vector>

using namespace guild;
using guild::render::Heightmap;

namespace {

// Build a flat 8x8 grid: world(x,z) = (x*10, 0, z*10). All tiles passable
// (type byte != 0 and != 13).
struct Grid {
    Heightmap hm{};
    std::vector<guild::u8> heights;
    std::vector<guild::u8> entries;   // 24-byte stride tile records

    Grid(int n = 8, float scale = 10.0f) {
        heights.assign(static_cast<size_t>(n) * n, 0);
        entries.assign(static_cast<size_t>(n) * n * 24, 1);  // type byte 1 = passable
        hm.originX = hm.originY = hm.originZ = 0.0f;
        hm.scaleX = hm.scaleZ = scale;
        hm.scaleY = 1.0f;
        hm.size = n;
        hm.heights = heights.data();
        hm.entries = entries.data();
    }
    void block(int x, int z, guild::u8 type) { entries[24 * (x + z * hm.size)] = type; }
};

// A trivial "unit" model: just a world position + alive flag.
struct Unit {
    float x, y, z;
    bool  alive;
};

// Wire a ThreatField over a flat roster of Unit pointers. allyIds/enemyIds index
// into `units` (or -1 for empty). FindUnitById returns &units[id].
guild::sim::ThreatField MakeField(const Grid& g, std::vector<Unit>& units,
                                  const std::vector<guild::i32>& allyIds,
                                  const std::vector<guild::i32>& enemyIds,
                                  std::vector<guild::i32>& allyStore,
                                  std::vector<guild::i32>& enemyStore,
                                  const void* self = nullptr) {
    allyStore = allyIds; allyStore.resize(16, -1);
    enemyStore = enemyIds; enemyStore.resize(16, -1);
    guild::sim::ThreatField f;
    f.heightmap = &g.hm;
    f.allyRoster = allyStore.data();
    f.enemyRoster = enemyStore.data();
    f.rosterCount = 16;
    f.self = self;
    f.findUnitById = [&units](guild::i32 id) -> const void* {
        if (id < 0 || id >= static_cast<guild::i32>(units.size())) return nullptr;
        return &units[static_cast<size_t>(id)];
    };
    f.unitAlive = [](const void* u) {
        return static_cast<const Unit*>(u)->alive;
    };
    f.unitWorldPos = [](const void* u, float out[3]) {
        const Unit* p = static_cast<const Unit*>(u);
        out[0] = p->x; out[1] = p->y; out[2] = p->z;
    };
    return f;
}

bool Near(double a, double b, double eps = 1e-4) { return std::fabs(a - b) < eps; }

} // namespace

// VIBE_Coord_Distance3D @0x4865ec — tile-to-tile world distance.
TEST(SimCombatEscape, CoordDistance3D) {
    Grid g;
    // (0,0)->(3,4): dx=30, dz=40 -> 50
    CHECK(Near(guild::sim::CoordDistance3D(&g.hm, 0, 0, 3, 4), 50.0));
    // (2,2)->(2,5): dz=30
    CHECK(Near(guild::sim::CoordDistance3D(&g.hm, 2, 2, 2, 5), 30.0));
    // identical tiles -> 0
    CHECK(Near(guild::sim::CoordDistance3D(&g.hm, 4, 4, 4, 4), 0.0));
}

// VIBE_Combat_DistanceObjectToTarget @0x486524 — planar (X,Z) unit->tile dist.
TEST(SimCombatEscape, DistanceObjectToTarget) {
    Grid g;  // world(x,z) = (10x, 0, 10z)
    // Unit at world (0, 5, 0); tile (3,4) -> world (30, *, 40). Y is ignored.
    float unit[3] = {0.0f, 5.0f, 0.0f};
    CHECK(Near(guild::sim::DistanceObjectToTarget(&g.hm, unit, 3, 4), 50.0));
    // Same X/Z column, different Y -> distance unchanged (Y term cancels).
    float unit2[3] = {30.0f, 999.0f, 40.0f};
    CHECK(Near(guild::sim::DistanceObjectToTarget(&g.hm, unit2, 3, 4), 0.0));
}

// VIBE_Combat_ComputeTileThreatScore @0x48ae54 — signed threat.
TEST(SimCombatEscape, ThreatScoreSign) {
    Grid g;
    std::vector<Unit> units = {
        {0.0f, 0.0f, 0.0f, true},      // 0: enemy at corner (0,0)
        {70.0f, 0.0f, 70.0f, true},    // 1: ally at corner (7,7)
    };
    std::vector<guild::i32> as, es;
    auto f = MakeField(g, units, {1}, {0}, as, es);

    // With a huge radius both units count. Tile (3,3): enemy dist =
    // |(30,0,30)-(0,0,0)| = 42.4264, ally dist = |(30,0,30)-(70,0,70)| = 56.5685.
    double sc = guild::sim::ComputeTileThreatScore(f, 3, 3, 1.0e6f);
    double exp = 70.0 / std::sqrt(30.0 * 30.0 + 30.0 * 30.0)
               - 70.0 / std::sqrt(40.0 * 40.0 + 40.0 * 40.0);
    CHECK(Near(sc, exp));

    // Near the enemy (1,1) -> positive (dangerous); near the ally (6,6) ->
    // negative (safe). Both within 1e6 radius.
    CHECK(guild::sim::ComputeTileThreatScore(f, 1, 1, 1.0e6f) > 0.0);
    CHECK(guild::sim::ComputeTileThreatScore(f, 6, 6, 1.0e6f) < 0.0);

    // Dead units contribute nothing.
    units[0].alive = false;
    units[1].alive = false;
    CHECK(Near(guild::sim::ComputeTileThreatScore(f, 3, 3, 1.0e6f), 0.0));
}

// Radius gate: a unit outside `radius` is excluded.
TEST(SimCombatEscape, ThreatScoreRadiusGate) {
    Grid g;
    std::vector<Unit> units = {{0.0f, 0.0f, 0.0f, true}};   // enemy at (0,0)
    std::vector<guild::i32> as, es;
    auto f = MakeField(g, units, {}, {0}, as, es);
    // Tile (1,0) is 10 units away. radius 5 -> excluded -> 0. radius 40 -> 7.0.
    CHECK(Near(guild::sim::ComputeTileThreatScore(f, 1, 0, 5.0f), 0.0));
    CHECK(Near(guild::sim::ComputeTileThreatScore(f, 1, 0, 40.0f), 7.0));
}

// Self-exclusion in the ally loop.
TEST(SimCombatEscape, ThreatScoreSelfExcluded) {
    Grid g;
    std::vector<Unit> units = {{10.0f, 0.0f, 0.0f, true}};   // ally == self at (1,0)
    std::vector<guild::i32> as, es;
    auto f = MakeField(g, units, {0}, {}, as, es, &units[0]);
    // The only ally is self -> excluded -> score 0 even though it's adjacent.
    CHECK(Near(guild::sim::ComputeTileThreatScore(f, 1, 0, 40.0f), 0.0));
}

// VIBE_Combat_FindSafestTileInRange @0x48b01c — minimise threat.
TEST(SimCombatEscape, SafestTile) {
    Grid g;
    std::vector<Unit> units = {
        {0.0f, 0.0f, 0.0f, true},      // enemy at (0,0)
        {70.0f, 0.0f, 70.0f, true},    // ally at (7,7)
    };
    std::vector<guild::i32> as, es;
    auto f = MakeField(g, units, {1}, {0}, as, es);

    guild::sim::TileSearchResult r;
    bool ok = guild::sim::FindSafestTileInRange(f, 3, 3, 2, r);
    CHECK(ok);
    // steps=2 -> radius 40. The centre (3,3) is the only visited tile where the
    // enemy (dist 42.4 > 40) does NOT count -> score 0; all closer ring tiles see
    // the enemy and score positive, so (3,3) (score 0) is the minimum.
    CHECK_EQ(r.tileX, 3);
    CHECK_EQ(r.tileZ, 3);
}

// VIBE_Combat_FindMostThreatenedTile @0x48b1b4 — maximise threat.
TEST(SimCombatEscape, MostThreatenedTile) {
    Grid g;
    std::vector<Unit> units = {
        {0.0f, 0.0f, 0.0f, true},      // enemy at (0,0)
        {70.0f, 0.0f, 70.0f, true},    // ally at (7,7)
    };
    std::vector<guild::i32> as, es;
    auto f = MakeField(g, units, {1}, {0}, as, es);

    guild::sim::TileSearchResult r;
    bool ok = guild::sim::FindMostThreatenedTile(f, 1, 1, 2, r);
    CHECK(ok);
    // Visited order includes (1,0) and (0,1), both at world dist 10 from the enemy
    // -> 70/10 = 7.0, the maximum; (1,0) is reached first so it wins (strict >).
    CHECK_EQ(r.tileX, 1);
    CHECK_EQ(r.tileZ, 0);
}

// The "found" gates: no enemies near -> most-threatened must report not-found
// (best <= 5.0); safest still finds the global minimum (untouched sentinel only
// if every tile is impassable).
TEST(SimCombatEscape, FoundGates) {
    Grid g;
    std::vector<Unit> units = {{0.0f, 0.0f, 0.0f, true}};  // lone far enemy at (0,0)
    std::vector<guild::i32> as, es;
    auto f = MakeField(g, units, {}, {0}, as, es);

    // Centre (6,6), steps 1: only tile (6,6) visited; enemy dist ~84.85 > radius
    // (1*10*2=20) -> score 0 -> not > 5.0 -> not found.
    guild::sim::TileSearchResult r;
    CHECK(!guild::sim::FindMostThreatenedTile(f, 6, 6, 1, r));

    // Block every tile -> safest finds nothing (sentinel unchanged).
    for (int z = 0; z < 8; ++z)
        for (int x = 0; x < 8; ++x)
            g.block(x, z, 0);   // type 0 = impassable
    guild::sim::TileSearchResult r2;
    CHECK(!guild::sim::FindSafestTileInRange(f, 3, 3, 2, r2));

    // Tile type 13 is also skipped: unblock one tile but set it to 13.
    g.block(3, 3, 13);
    guild::sim::TileSearchResult r3;
    CHECK(!guild::sim::FindSafestTileInRange(f, 3, 3, 2, r3));
}

// VIBE_Combat_EscapeTileCallback @0x48b350 — collector accepts only "sp_ESCAPE".
TEST(SimCombatEscape, EscapeCollect) {
    std::vector<const void*> out;
    int a, b, c;
    CHECK(guild::sim::EscapeTileCollect("sp_ESCAPE", &a, out));   // accept
    CHECK(guild::sim::EscapeTileCollect("SP_escape", &b, out));   // case-insensitive
    CHECK(guild::sim::EscapeTileCollect("sp_spawn", &c, out));    // reject (still true)
    CHECK_EQ(static_cast<int>(out.size()), 2);
    CHECK(out[0] == &a);
    CHECK(out[1] == &b);
}

// VIBE_Combat_FindNearestEscapeTile @0x48b384 — nearest node to the unit.
TEST(SimCombatEscape, NearestEscape) {
    // Three escape nodes; the unit sits at (5,0,5).
    struct Node { float x, y, z; };
    Node n0{0, 0, 0};
    Node n1{6, 0, 6};      // nearest to (5,5)
    Node n2{50, 0, 50};
    std::vector<const void*> nodes = {&n0, &n1, &n2};
    float unit[3] = {5, 0, 5};
    auto origin = [](const void* p, float out[3]) {
        const Node* n = static_cast<const Node*>(p);
        out[0] = n->x; out[1] = n->y; out[2] = n->z;
    };
    const void* best = guild::sim::FindNearestEscapeTile(unit, nodes, origin);
    CHECK(best == &n1);

    // Empty list -> nullptr.
    std::vector<const void*> none;
    CHECK(guild::sim::FindNearestEscapeTile(unit, none, origin) == nullptr);
}
