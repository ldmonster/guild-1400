#include "test.h"

#include "render/terrain_scan.h"
#include "render/terrain_tile_query.h"
#include "render/sun_state.h"

#include "sim/actionqueue.h"  // guild::sim::g_gameTick
#include "shim_impl/disk_filesystem.h"

#include <cstdio>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::render;

namespace {

const char* RealGameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR"))
        return env;
    return "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";
}

// Build a NxN heightmap whose elevation is a separable ramp h(x,y)=x+y (clamped),
// so the per-row min/max are predictable.
std::vector<u8> RampHeights(int n) {
    std::vector<u8> h(n * n);
    for (int y = 0; y < n; ++y)
        for (int x = 0; x < n; ++x)
            h[y * n + x] = (u8)((x + y) & 0xFF);
    return h;
}

} // namespace

// End-to-end terrain height/lighting flow:
//   1. Scan every full row of a ramp heightmap and verify the running min/max
//      bounds equal the analytic envelope of the ramp.
//   2. Run a walkability search over a derived tile grid and confirm the nearest
//      walkable tile is reached.
//   3. Drive the sun-direction state machine and confirm it stamps the clock.
TEST(RenderTerrainScanE2E, RampRowEnvelope) {
    const int N = 16;
    auto heights = RampHeights(N);
    TerrainHeightGrid g{};
    g.width = N;
    g.heights = heights.data();

    // Accumulate the whole map's height range row by row (seeds 255/0).
    int lo = 255, hi = 0;
    for (int row = 0; row < N; ++row) {
        int r = ScanRowHeightRange(&g, &lo, 0, &hi, N - 1, row);
        CHECK_EQ(r, 0);
    }
    // Ramp h=x+y over [0,N) -> global min 0 (0,0), max 2*(N-1) (N-1,N-1).
    CHECK_EQ(lo, 0);
    CHECK_EQ(hi, 2 * (N - 1));

    // Single-row envelope: row r ranges from r (x=0) to r+N-1 (x=N-1).
    int loR = 255, hiR = 0;
    int rr = ScanRowHeightRange(&g, &loR, 0, &hiR, N - 1, 5);
    CHECK_EQ(rr, 0);
    CHECK_EQ(loR, 5);
    CHECK_EQ(hiR, 5 + N - 1);

    // A row scanned entirely off the grid leaves the accumulators untouched.
    int loC = 99, hiC = 7;
    int rc = ScanRowHeightRange(&g, &loC, 0, &hiC, N - 1, N + 4);  // row OOB
    CHECK_EQ(rc, 1);
    CHECK_EQ(loC, 99);
    CHECK_EQ(hiC, 7);
}

TEST(RenderTerrainScanE2E, WalkabilityThenSun) {
    // 6x6 walk grid: the center and a neighbour are type-13 (non-walkable); one
    // walkable tile sits a ring out at (row=4,col=4). Search from (centerX=3,
    // centerY=3). Golden result verified against the Python reference.
    const int S = 6;
    std::vector<u8> cells(24 * S * S, 0);
    auto put = [&](int row, int col, u8 t) { cells[24 * (row + col * S)] = t; };
    put(3, 3, 13);   // center blocked (type 13)
    put(2, 3, 13);   // neighbour blocked
    put(4, 4, 1);    // the only walkable cell

    TileWalkGrid wg{};
    wg.size = S;
    wg.cells = cells.data();

    int fx = -1, fy = -1;
    bool ok = FindNearestWalkableTile(&wg, /*centerY*/3, &fx, /*centerX*/3, &fy);
    CHECK(ok);
    CHECK_EQ(fx, 4);   // col
    CHECK_EQ(fy, 4);   // row

    // Sun-direction state machine stamps the current game tick.
    guild::sim::g_gameTick = 555;
    guild::i32 t1 = SetSunDirection(0, 3);
    CHECK_EQ(t1, 555);
    CHECK_EQ(g_sunDirState, -1);
    CHECK_EQ(g_sunDirParam, 3);

    guild::sim::g_gameTick = 556;
    guild::i32 t2 = EnableSun();
    CHECK_EQ(t2, 556);
    CHECK_EQ(g_sunDirState, 1);
    CHECK_EQ(g_sunDirStamp, 556);
    CHECK_EQ(g_sunDirParam, 0);

    ResetGlobalState();
    CHECK_EQ(g_lightState54, 0);
    CHECK_EQ(g_lightState64, 0);
}

// Real-asset guard: the terrain-scan primitives have no on-disk dependency, but
// per the module e2e convention we provide an asset-guarded section that cleanly
// skips (records zero checks, returns) when the original game data is absent.
TEST(RenderTerrainScanE2E, RealAssetGuard) {
    guild::shim::DiskFileSystem fs(RealGameDir());
    if (!fs.exists("Resources/forms.BIN")) {
        std::printf("  [skip] RenderTerrainScanE2E.RealAssetGuard: "
                    "real game dir absent (%s)\n", RealGameDir());
        return;  // clean skip — no checks recorded
    }
    // Assets present: re-run the deterministic primitives as a smoke check so the
    // guarded path still asserts something when data is available.
    const int N = 8;
    auto heights = RampHeights(N);
    TerrainHeightGrid g{};
    g.width = N;
    g.heights = heights.data();
    int lo = 255, hi = 0;
    for (int row = 0; row < N; ++row)
        ScanRowHeightRange(&g, &lo, 0, &hi, N - 1, row);
    CHECK_EQ(lo, 0);
    CHECK_EQ(hi, 2 * (N - 1));
}
