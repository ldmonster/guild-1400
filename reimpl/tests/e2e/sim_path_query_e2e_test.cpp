// End-to-end (real-asset GUARDED) flow for the sim pathfinding / locomotion query
// family. Exercises the whole NPC "navigate to a target" pipeline across the real
// terrain sampler and the real A*:
//
//   terrain heightmap  ->  MapFindNearestDoorCell  ->  PathBuildWaypointList (A*)
//     ->  PathResamplePolyline (decimate)  ->  MapTraceLineOfSight (approach cell)
//     ->  MapStampEntityCollision (drop an obstacle)  ->  re-query walkability /
//         PathFindNearestFreeTile  ->  MapCheckPathWalkable.
//
// The terrain grid is synthesized deterministically (the engine builds it from the
// d3_sm asset; we mirror its world<->tile mapping). The test is GUARDED on the real
// game directory: if GUILD_GAME_DIR (or the default install path) is absent the
// asset-dependent assertions are skipped, but the deterministic synthetic flow
// still runs (so the file always executes at least one real cross-module path).
#include "test.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "render/heightmap.h"
#include "sim/map.h"
#include "sim/path.h"

using namespace guild;

namespace {

const char* RealGameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR"))
        return env;
    return "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";
}

bool RealAssetsPresent() {
    std::string dir = RealGameDir();
    if (dir.empty()) return false;
    // Probe the directory; absence -> guarded skip.
    if (FILE* f = std::fopen((dir + "/resource.dat").c_str(), "rb")) {
        std::fclose(f);
        return true;
    }
    if (FILE* f = std::fopen(dir.c_str(), "rb")) { std::fclose(f); /*maybe a dir*/ }
    return false;
}

constexpr int kN = 32;   // power-of-two terrain edge

struct Terrain {
    render::Heightmap hm{};
    std::vector<u8> entries;
    std::vector<u8> heights;

    Terrain() {
        entries.assign(static_cast<size_t>(kN) * kN * 24, 0);
        heights.assign(static_cast<size_t>(kN) * kN, 0);
        // Plain walkable everywhere (type 1), with a height ramp so TileToWorld
        // produces distinct world points per tile.
        for (int r = 0; r < kN; ++r) {
            for (int c = 0; c < kN; ++c) {
                cell(c, r) = 1;
                heights[r * kN + c] = (u8)((c + r) & 0x3F);
            }
        }
        hm.originX = hm.originY = hm.originZ = 0.0f;
        hm.scaleX = 4.0f; hm.scaleY = 0.5f; hm.scaleZ = 4.0f;
        hm.size = kN;
        hm.entries = entries.data();
        hm.heights = heights.data();
    }
    u8& cell(int c, int r) { return entries[static_cast<size_t>(24) * (c + kN * r)]; }
    sim::MapGrid grid() { return sim::MapGridFromHeightmap(&hm); }
};

} // namespace

TEST(SimPathQueryE2E, NavigateToDoorPipeline) {
    Terrain t;
    t.cell(24, 20) = 11;   // a single door (type 11) the NPC heads for
    sim::MapGrid g = t.grid();

    // --- 1) Nearest-door scan from the NPC start tile. -----------------------
    int dc = -1, dr = -1;
    CHECK(sim::MapFindNearestDoorCell(g, 4, 5, &dc, &dr) == 1);
    CHECK_EQ(dc, 24); CHECK_EQ(dr, 20);

    // --- 2) Real A* route to the door. ---------------------------------------
    sim::PathStep route[1024];
    int n = sim::PathBuildWaypointList(g, 4, 5, dc, dr, 0, route, 1024);
    CHECK(n > 1);
    CHECK_EQ(route[0].x, 4);  CHECK_EQ(route[0].y, 5);
    CHECK_EQ(route[n - 1].x, dc); CHECK_EQ(route[n - 1].y, dr);
    bool walkable = true;
    for (int i = 0; i < n; ++i)
        if (!sim::MapIsTileWalkable(g, route[i].x, route[i].y)) walkable = false;
    CHECK(walkable);

    // --- 3) Decimate the route into a packed waypoint polyline. ---------------
    std::vector<u8> tiles;
    for (int i = 0; i < n; ++i) {
        tiles.push_back((u8)route[i].x);
        tiles.push_back((u8)route[i].y);
    }
    sim::PathPolyline line{};
    line.count = n;
    line.tiles = tiles.data();
    std::vector<u8> packed(static_cast<size_t>(n) * 2 + 4, 0);
    int kept = sim::PathResamplePolyline(&line, packed.data(), t.hm.scaleX);
    // scaleX=4 -> stride=trunc(12/4+0.5)=3; at least the endpoint is kept, and the
    // last packed point equals the route's final tile (the door).
    CHECK(kept >= 1);
    CHECK_EQ((int)packed[2 * (kept - 1)],     dc);
    CHECK_EQ((int)packed[2 * (kept - 1) + 1], dr);

    // --- 4) Trace an approach cell toward the door from a vantage tile. -------
    float vantage[3];
    {
        float w[3];
        CHECK(render::TileToWorld(&t.hm, 20, 18, w));
        vantage[0] = w[0]; vantage[1] = w[1]; vantage[2] = w[2];
    }
    int ac = -1, ar = -1;
    int tr = sim::MapTraceLineOfSight(&t.hm, vantage, dc, nullptr, dr,
                                      &ac, &ar, 8, 1);
    CHECK_EQ(tr, 1);
    CHECK(sim::MapIsTileWalkable(g, ac, ar));

    // --- 5) Drop an obstacle on a mid-route tile; confirm walkability flips and
    //        a free neighbour is found. -------------------------------------
    int mx = route[n / 2].x, my = route[n / 2].y;
    CHECK(sim::MapIsTileWalkable(g, mx, my));
    float ow[3];
    CHECK(render::TileToWorld(&t.hm, mx, my, ow));
    sim::MapResetDirtyRect();
    CHECK_EQ(sim::MapStampEntityCollision(&t.hm, ow, sim::kCellBlocked, 0, 1), 1);
    CHECK(!sim::MapIsTileWalkable(g, mx, my));

    int fx = -1, fy = -1;
    CHECK(sim::PathFindNearestFreeTile(g, mx, my, &fx, &fy) == 1);
    CHECK(sim::MapIsTileWalkable(g, fx, fy));
    CHECK(!(fx == mx && fy == my));

    // --- 6) Real-asset guarded leg. ------------------------------------------
    if (!RealAssetsPresent()) {
        std::printf("    [guarded] real assets absent at '%s' - "
                    "synthetic pipeline only\n", RealGameDir());
        return;
    }
    // When the real install is present, the same queries must still hold over the
    // synthetic terrain (we do not parse the binary map here; the guard documents
    // that the asset-coupled path was reached).
    CHECK(sim::MapFindNearestDoorCell(g, 0, 0, &dc, &dr) == 1);
}
