// End-to-end: a combat unit's defensive tile-placement decision flow across the
// tactical-AI tile search (src/sim/combat_escape.*). Models the order-tick
// "escape / advance" branch: build a battlefield, then have the AI (a) flee to
// the safest reachable tile, (b) advance to the most-threatened tile, and
// (c) fall back to a scripted escape node — exactly the leaves the order-tick
// state machine resolves (combat_orders.h states 7/escape, 0/attack-advance).
#include "test.h"

#include "sim/combat_escape.h"
#include "render/heightmap.h"

#include <cmath>
#include <vector>

using namespace guild;
using guild::render::Heightmap;

namespace {

struct Unit { float x, y, z; bool alive; };

struct Field16 {
    Heightmap hm{};
    std::vector<u8> heights, entries;
    Field16(int n = 16, float s = 8.0f) {
        heights.assign(static_cast<size_t>(n) * n, 0);
        entries.assign(static_cast<size_t>(n) * n * 24, 1);
        hm.originX = hm.originY = hm.originZ = 0.0f;
        hm.scaleX = hm.scaleZ = s; hm.scaleY = 1.0f;
        hm.size = n; hm.heights = heights.data(); hm.entries = entries.data();
    }
    void block(int x, int z, u8 t) { entries[24 * (x + z * hm.size)] = t; }
};

guild::sim::ThreatField MakeField(Field16& g, std::vector<Unit>& units,
                                  std::vector<i32>& allyStore,
                                  std::vector<i32>& enemyStore,
                                  const void* self) {
    guild::sim::ThreatField f;
    f.heightmap = &g.hm;
    f.allyRoster = allyStore.data();
    f.enemyRoster = enemyStore.data();
    f.rosterCount = 16;
    f.self = self;
    f.findUnitById = [&units](i32 id) -> const void* {
        if (id < 0 || id >= static_cast<i32>(units.size())) return nullptr;
        return &units[static_cast<size_t>(id)];
    };
    f.unitAlive = [](const void* u) { return static_cast<const Unit*>(u)->alive; };
    f.unitWorldPos = [](const void* u, float out[3]) {
        const Unit* p = static_cast<const Unit*>(u);
        out[0] = p->x; out[1] = p->y; out[2] = p->z;
    };
    return f;
}

} // namespace

TEST(SimCombatEscapeE2E, FleeAdvanceAndScriptedEscape) {
    Field16 g;  // 16x16, scale 8 -> world(x,z) = (8x, 0, 8z)

    // Battlefield: 3 enemies clustered near the south-west, 2 allies near the
    // unit. Roster ids index into `units`.
    std::vector<Unit> units = {
        {0.0f,   0.0f, 0.0f,   true},   // 0 enemy
        {8.0f,   0.0f, 0.0f,   true},   // 1 enemy
        {0.0f,   0.0f, 8.0f,   true},   // 2 enemy
        {96.0f,  0.0f, 96.0f,  true},   // 3 ally (== the acting unit, at tile 12,12)
        {88.0f,  0.0f, 96.0f,  true},   // 4 ally
    };
    const void* self = &units[3];
    std::vector<i32> allyStore  = {3, 4, -1, -1, -1, -1, -1, -1,
                                   -1, -1, -1, -1, -1, -1, -1, -1};
    std::vector<i32> enemyStore = {0, 1, 2, -1, -1, -1, -1, -1,
                                   -1, -1, -1, -1, -1, -1, -1, -1};
    auto field = MakeField(g, units, allyStore, enemyStore, self);

    // Sanity: the acting unit's tile (12,12) is far from all enemies -> a large
    // radius gives a NEGATIVE (safe) score (allies dominate, self excluded).
    double here = guild::sim::ComputeTileThreatScore(field, 12, 12, 1.0e6f);
    CHECK(here < 0.0);

    // (a) FLEE: search a radius-3 diamond around the unit for the safest tile.
    guild::sim::TileSearchResult flee;
    bool fled = guild::sim::FindSafestTileInRange(field, 12, 12, 3, flee);
    CHECK(fled);
    // The chosen tile must be passable and within the diamond (Manhattan <= 3*... )
    // — and at least as safe as the centre.
    double fleeScore = guild::sim::ComputeTileThreatScore(
        field, flee.tileX, flee.tileZ, 3.0 * 8.0 * 2.0);
    double centreScore = guild::sim::ComputeTileThreatScore(
        field, 12, 12, 3.0 * 8.0 * 2.0);
    CHECK(fleeScore <= centreScore);

    // (b) ADVANCE: from a forward staging tile near the enemy cluster (2,2),
    // the most-threatened tile is the one most pressured by enemies.
    guild::sim::TileSearchResult push;
    bool advanced = guild::sim::FindMostThreatenedTile(field, 2, 2, 3, push);
    CHECK(advanced);
    double pushScore = guild::sim::ComputeTileThreatScore(
        field, push.tileX, push.tileZ, 3.0 * 8.0 * 2.0);
    CHECK(pushScore > guild::sim::kThreatMinScore);  // > 5.0 gate

    // (c) SCRIPTED ESCAPE fallback: when the diamond is fully blocked, the AI
    // falls back to the nearest "sp_ESCAPE" scene node. Collect nodes, then pick.
    struct Node { float x, y, z; };
    Node corner{0, 0, 0};
    Node nearby{104, 0, 104};   // closest to the unit at (96,96)
    Node midfield{56, 0, 56};
    std::vector<const void*> escapeNodes;
    CHECK(guild::sim::EscapeTileCollect("sp_ESCAPE", &corner,   escapeNodes));
    CHECK(guild::sim::EscapeTileCollect("decor_01",  &nearby,   escapeNodes));  // rejected!
    CHECK(guild::sim::EscapeTileCollect("Sp_Escape", &nearby,   escapeNodes));  // accepted (ci)
    CHECK(guild::sim::EscapeTileCollect("sp_ESCAPE", &midfield, escapeNodes));
    CHECK_EQ(static_cast<int>(escapeNodes.size()), 3);

    float unitOrigin[3] = {96.0f, 0.0f, 96.0f};
    auto origin = [](const void* p, float out[3]) {
        const Node* n = static_cast<const Node*>(p);
        out[0] = n->x; out[1] = n->y; out[2] = n->z;
    };
    const void* dest = guild::sim::FindNearestEscapeTile(unitOrigin, escapeNodes, origin);
    CHECK(dest == &nearby);

    // Full-block escape path: block the whole flee diamond -> safest search fails,
    // triggering the scripted fallback in the real order tick.
    for (int z = 9; z <= 15; ++z)
        for (int x = 9; x <= 15; ++x)
            g.block(x, z, 0);
    guild::sim::TileSearchResult blocked;
    CHECK(!guild::sim::FindSafestTileInRange(field, 12, 12, 3, blocked));
}
