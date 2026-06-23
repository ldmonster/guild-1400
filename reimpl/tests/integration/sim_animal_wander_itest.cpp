// Integration tests for the animal wander helpers against the REAL sibling
// modules they call: guild::render::Heightmap (TileToWorld / WorldToTileWithHeight),
// guild::sim::MapTraceLineOfSight (the obstacle-aware spiral), guild::util RNG and
// the bone-chain transform. No mock map — a real Heightmap with non-unit scale and
// blocked tiles, so BuildWanderPath's clamp / LOS-fallback path is exercised end to
// end against translated code.
#include "sim/animal_wander.h"
#include "sim/map.h"
#include "render/heightmap.h"
#include "crt/rand.h"
#include "test.h"

#include <cstring>
#include <vector>

using namespace guild::sim;
using guild::u8;

namespace {

// Real Heightmap with configurable scale/origin; all tiles walkable unless blocked.
struct RealMap {
    guild::render::Heightmap hm{};
    std::vector<u8> entries;
    std::vector<u8> heights;
    int size;
    RealMap(int sz, float sx, float ox) : size(sz) {
        entries.assign(static_cast<size_t>(sz) * sz * 24, 1);
        heights.assign(static_cast<size_t>(sz) * sz, 0);
        std::memset(&hm, 0, sizeof(hm));
        hm.scaleX = sx; hm.scaleY = 1; hm.scaleZ = sx;
        hm.originX = ox; hm.originY = 0; hm.originZ = ox;
        hm.size = sz;
        hm.entries = entries.data();
        hm.heights = heights.data();
    }
    void block(int col, int row) { entries[24 * (col + row * size)] = 0; }
};

} // namespace

// BuildWanderPath round-trips through the REAL TileToWorld with non-unit scale:
// every produced world point must map back to a valid tile via the REAL
// WorldToTileWithHeight, and lie inside the clamp window [2,size-2].
TEST(SimAnimalWanderIT, WanderPointsRoundTripRealHeightmap) {
    RealMap m(64, 2.5f, -50.0f);   // scaleX=2.5, originX=-50
    guild::crt::Srand(4242);
    float start[3] = {0.0f, 0.0f, 0.0f};  // tile = trunc((0 - -50)/2.5) = 20
    float out[8 * 4] = {0};
    int n = Animal_BuildWanderPath(&m.hm, start, 8, out);
    CHECK_EQ(n, 8);
    for (int i = 0; i < 8; ++i) {
        int col = -1, row = -1; float h = 0;
        bool on = guild::render::WorldToTileWithHeight(&m.hm, out + i * 4, &col, &row, &h);
        CHECK(on);                       // every wander point is on the map
        CHECK(col >= 2 && col <= m.size - 2);
        CHECK(row >= 2 && row <= m.size - 2);
    }
}

// With the entire clamp window around the start blocked except a far ring, the real
// LOS spiral still resolves a walkable tile (never returns an unwalkable one).
TEST(SimAnimalWanderIT, WanderAvoidsBlockedViaRealLOS) {
    RealMap m(64, 1.0f, 0.0f);
    // Block the 9x9 block centred on tile (30,30): the RNG offset window is +-[4,5]
    // around the start tile, so the immediate clamp target is often blocked, forcing
    // the real spiral to step outward to a walkable cell.
    for (int c = 26; c <= 34; ++c)
        for (int r = 26; r <= 34; ++r)
            m.block(c, r);
    guild::crt::Srand(13);
    float start[3] = {30.0f, 0.0f, 30.0f};   // tile (30,30) (blocked centre)
    float out[6 * 4] = {0};
    Animal_BuildWanderPath(&m.hm, start, 6, out);
    for (int i = 0; i < 6; ++i) {
        int col = (int)out[i * 4 + 0];
        int row = (int)out[i * 4 + 2];
        u8 cell = m.entries[24 * (col + row * m.size)];
        // The chosen tile is either a walkable cell, or the fallback start tile
        // (which is blocked) — the LOS spiral returns walkable when one exists in
        // range, else falls back. With a walkable ring just outside the block, every
        // result here is walkable.
        CHECK(cell == 1);
    }
}

// FindHerdGrouping through real PointThroughBoneChain + VectorWithinTolerance:
// buildings sharing a pasture (within 6000 of the anchor) are grouped; far ones
// excluded; the anchor itself never groups with itself.
TEST(SimAnimalWanderIT, HerdGroupingRealTransform) {
    struct Frame { float f[140]; Frame() { std::memset(f, 0, sizeof(f)); } };
    Frame fr[6];
    // translations at frame[30..32]; near cluster {0,1,2,3}, far {4,5}.
    float xs[6] = {0, 1000, 2000, 5000, 90000, 95000};
    for (int i = 0; i < 6; ++i) fr[i].f[30] = xs[i];
    BuildingAnchor cand[6];
    for (long i = 0; i < 6; ++i) cand[i] = {fr[i].f, reinterpret_cast<void*>(i)};
    guild::crt::Srand(100);   // anchor = cand[RandomModulo(6)]
    // HARDEN (wave-10): the grouping loop stores a 3-float triple while
    // (group < 16), so the final triple can land at group==15 and write indices
    // 15,16,17 — the out buffer must hold 18 floats, not 16 (the prior size let
    // a full group scribble 2 floats past the end). The real engine's herd buffer
    // is the AnimalRec scratch (64 floats), so this only affected this helper test.
    float pts[18] = {0};
    int group = Animal_HerdGroupFrom(cand, 6, pts);
    // group is a multiple of 3 (x,y,z triples), at most 16 floats stored.
    CHECK(group % 3 == 0);
    CHECK(group <= 16);
    // Every gathered point's X is within 6000 of the anchor's X (real tolerance).
    // Recover the anchor index from the same RNG sequence is awkward; instead
    // assert all gathered X are in the near cluster's range [0,5000] (the only
    // mutually-within-6000 set), proving far buildings were excluded.
    for (int i = 0; i < group; i += 3) {
        CHECK(pts[i] >= 0.0f && pts[i] <= 5000.0f);
    }
}

// Cross-module: a spawned animal record drives a wander tick whose waypoints feed
// the REAL heightmap. Verifies the AI step + BuildWanderPath + heightmap chain.
TEST(SimAnimalWanderIT, AiStepFeedsRealHeightmap) {
    RealMap m(64, 1.0f, 0.0f);
    struct CaptureOps : IAnimalSceneOps {
        const guild::render::Heightmap* map;
        std::vector<std::pair<int,int>> tiles;
        int sounds = 0;
        const guild::render::Heightmap* SceneHeightmap() override { return map; }
        void IssueWanderAction(AnimalRec*, int c, int r, const char*) override {
            tiles.push_back({c, r});
        }
        void IssueSoundAction(AnimalRec*, int) override { ++sounds; }
    } ops;
    ops.map = &m.hm;
    SetAnimalSceneOps(&ops);

    AnimalRec rec{};
    rec.actor = 1; rec.kind = 4;   // sheep
    rec.x = 25.0f; rec.y = 0.0f; rec.z = 25.0f;
    guild::crt::Srand(77);
    Animal_UpdateSheep(&rec, 0);
    // Either it wandered (waypoints all on-map walkable) or made a sound.
    CHECK((ops.tiles.size() > 0) != (ops.sounds > 0));
    for (auto& t : ops.tiles) {
        CHECK(t.first >= 2 && t.first <= m.size - 2);
        CHECK(t.second >= 2 && t.second <= m.size - 2);
        u8 cell = m.entries[24 * (t.first + t.second * m.size)];
        CHECK(cell == 1);   // walkable
    }
    SetAnimalSceneOps(nullptr);
}
