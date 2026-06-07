#include "test.h"

#include "render/terrain_mesh.h"
#include "render/terrain.h"
#include "render/heightmap.h"

#include "shim_impl/disk_filesystem.h"

#include <cstdio>
#include <cstdint>
#include <vector>

using namespace guild;
using namespace guild::render;

namespace {

const char* RealGameDir() {
    if (const char* env = std::getenv("GUILD_GAME_DIR"))
        return env;
    return "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";
}

// A small terraced heightmap: a low plateau, a ramp, and a high plateau, used as a
// realistic terrain-build input.
std::vector<u8> TerracedHeights(int size) {
    std::vector<u8> h(size * size);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            u8 v;
            if (x < size / 4)            v = 20;
            else if (x < size / 2)       v = (u8)(20 + (x - size / 4) * 8);
            else                         v = 100;
            h[y * size + x] = v;
        }
    return h;
}

} // namespace

// =============================================================================
// End-to-end terrain mesh build flow: invalidate -> uniform-flag -> elevation
// summary -> tessellate the whole 8x8 tile grid into world vertices, then sanity
// check the geometry against the heightmap the vertices were built from.
// =============================================================================
TEST(TerrainMeshE2E, BuildWholeGrid) {
    const int size = 8, mask = 7, tileSpan = 1;
    auto heights = TerracedHeights(size);

    // 1. Mark every tile dirty.
    std::uint8_t lodStates[64];
    std::uint32_t flags = InvalidateTiles(0, lodStates, nullptr);
    CHECK_EQ(flags & 1u, 1u);

    // 2. Terrain-type uniform flags (use the height bytes as types for the smoke run).
    std::vector<std::uint8_t> p0(64, 0), p1(16, 0), p2(4, 0);
    std::uint8_t* planes[3] = {p0.data(), p1.data(), p2.data()};
    MarkUniformTiles(heights.data(), size, mask, planes, flags, nullptr);

    // 3. Per-tile elevation summary.
    TileElevationSummary summ{};
    SummarizeTileElevations(heights.data(), nullptr, size, mask, tileSpan, &summ);
    for (int t = 0; t < 64; ++t)
        CHECK(summ.minHeight[t] <= summ.maxHeight[t]);

    // 4. Tessellate every tile. A planar tile mapping: axisU on +x, axisRow on +z,
    // axisHeight on +y, origin at world (0,1,0).
    TileBuildParams p{};
    p.axisU[0] = 1.0f;
    p.axisRow[2] = 1.0f;
    p.axisHeight[1] = 0.5f;
    p.origin[1] = 1.0f;

    // A real Heightmap on the same bytes to cross-check sampled corner heights.
    Heightmap hm{};
    hm.originX = 0.0f; hm.originY = 1.0f; hm.originZ = 0.0f;
    hm.scaleX = 1.0f; hm.scaleY = 0.5f; hm.scaleZ = 1.0f;
    hm.size = size; hm.heights = heights.data();

    int builtTiles = 0;
    for (int row = 0; row < size; ++row) {
        for (int col = 0; col < size; ++col) {
            const u8 cornerH = heights[row * size + col];
            float out[24];
            ComputeTileVertices(p, tileSpan, cornerH, /*gridHeight*/cornerH, col, row, out);

            // Corner A world position must match the heightmap's TileToWorld sample
            // at (col,row): x=col, z=row, y = cornerH*0.5 + 1.
            float w[3];
            bool ok = TileToWorld(&hm, col, row, w);
            CHECK(ok);
            CHECK_EQ(out[0], (float)col);              // A.x = col*axisU.x
            CHECK_EQ(out[2], (float)row);              // A.z = row*axisRow.z
            CHECK_EQ(out[1], w[1]);                    // A.y lift == TileToWorld height
            // Height group 2 reuses the XZ corners (only y differs by lift).
            CHECK_EQ(out[12], out[0]);
            CHECK_EQ(out[14], out[2]);
            ++builtTiles;
        }
    }
    CHECK_EQ(builtTiles, 64);
}

// Real-asset guard: cleanly skip (zero checks) when the original game data is
// absent; when present, re-run the deterministic build as a smoke check.
TEST(TerrainMeshE2E, RealAssetGuard) {
    guild::shim::DiskFileSystem fs(RealGameDir());
    if (!fs.exists("Resources/forms.BIN")) {
        std::printf("  [skip] TerrainMeshE2E.RealAssetGuard: real game dir absent (%s)\n",
                    RealGameDir());
        return;  // clean skip — no checks recorded
    }
    const int size = 8, mask = 7, tileSpan = 1;
    auto heights = TerracedHeights(size);
    TileElevationSummary summ{};
    SummarizeTileElevations(heights.data(), nullptr, size, mask, tileSpan, &summ);
    // Low plateau tiles read 20; high plateau tiles read 100.
    CHECK_EQ(summ.minHeight[0], (std::uint8_t)20);
    CHECK_EQ(summ.maxHeight[7 * 8 + 7], (std::uint8_t)100);
}
