#include "test.h"

#include "render/terrain_mesh.h"
#include "render/terrain.h"          // real sibling: TileGrid + TileIsUniform
#include "render/tile_geometry.h"    // real sibling: BuildTileVertex / Vertex
#include "render/heightmap.h"        // real sibling: Heightmap + TileToWorld

#include <cstdint>
#include <vector>

using namespace guild;
using namespace guild::render;

// =============================================================================
// Cross-module: drive the new terrain-mesh build passes against the REAL terrain /
// tile-geometry / heightmap siblings — no stubs.
// =============================================================================

namespace {

// A flat-then-bump terrain-type grid: mostly type 5, with one 13 cell, so the
// uniform-region passes have both uniform and non-uniform blocks.
std::vector<u8> TypeGrid(int size, int bumpX, int bumpY, u8 bumpVal) {
    std::vector<u8> t(size * size, 5);
    t[bumpY * size + bumpX] = bumpVal;
    return t;
}

} // namespace

// MarkUniformTiles must agree, cell-for-cell, with the real TileIsUniform sibling:
// re-deriving each span-1 block's flag independently and comparing.
TEST(TerrainMeshIT, MarkUniformAgreesWithTileIsUniform) {
    const int size = 8, mask = 7;
    auto types = TypeGrid(size, 3, 4, 13);
    std::vector<u8> p0(64, 0), p1(16, 0), p2(4, 0);
    u8* planes[3] = {p0.data(), p1.data(), p2.data()};
    MarkUniformTiles(types.data(), size, mask, planes, 0, nullptr);

    // span 1: TileIsUniform scans the INCLUSIVE [x,x+1]x[y,y+1] 2x2 region. The flag
    // plane must agree, cell-for-cell, with the real sibling's verdict (not trivially
    // all-set, since the 2x2 neighbourhoods near the type-13 cell are non-uniform).
    TileGrid g{size, mask, types.data()};
    for (int y = 0; y < size; ++y) {
        for (int x = 0; x < size; ++x) {
            bool uni = TileIsUniform(&g, x, 1, y);          // real sibling oracle
            CHECK_EQ((p0[y * size + x] & 0x40) != 0, uni);
        }
    }
    // span 2: the 2x2 block covering (2..3,4..5) contains the type-13 cell -> NOT
    // uniform; cross-check via the real sibling at the block's origin.
    {
        const int span = 2, blocks = size / span;
        for (int by = 0; by < blocks; ++by)
            for (int bx = 0; bx < blocks; ++bx) {
                bool uni = TileIsUniform(&g, bx * span, span, by * span);
                int planeIdx = by * blocks + bx;
                CHECK_EQ((p1[planeIdx] & 0x40) != 0, uni);
            }
    }
}

// The slope-pass elevation summary must bound what TileToWorld would sample: for a
// flat heightmap the per-tile min==max==the flat height, and a real Heightmap built
// on the same bytes yields that exact world height at any tile corner.
TEST(TerrainMeshIT, ElevationSummaryMatchesHeightmapSample) {
    const int size = 8, mask = 7, tileSpan = 1;
    const u8 flatH = 80;
    std::vector<u8> heights(size * size, flatH);

    TileElevationSummary s{};
    SummarizeTileElevations(heights.data(), nullptr, size, mask, tileSpan, &s);
    for (int t = 0; t < 64; ++t) {
        CHECK_EQ(s.minHeight[t], flatH);
        CHECK_EQ(s.maxHeight[t], flatH);
    }

    // Build a real Heightmap on the same bytes and confirm TileToWorld's sampled
    // height equals flatH*scaleY + originY (the summary bounds that constant).
    Heightmap hm{};
    hm.originX = 0.0f; hm.originY = 1.0f; hm.originZ = 0.0f;
    hm.scaleX = 1.0f; hm.scaleY = 0.5f; hm.scaleZ = 1.0f;
    hm.size = size;
    hm.heights = heights.data();

    float w[3];
    bool ok = TileToWorld(&hm, /*tileX*/3, /*tileY*/2, w);   // real sibling
    CHECK(ok);
    CHECK_EQ(w[1], (float)flatH * 0.5f + 1.0f);              // == s.maxHeight bound
    CHECK(s.maxHeight[2 * 8 + 3] == flatH);                  // tile (col3,row2)
}

// ComputeTileVertices feeds the real BuildTileVertex sibling: the XZ corner the mesh
// builder emits is the world position; lifting it by a height sample reproduces what
// BuildTileVertex computes from the same height axis + accumulator.
TEST(TerrainMeshIT, TileVertexAgreesWithBuildTileVertex) {
    TileBuildParams p{};
    p.axisU[0] = 1.0f; p.axisU[2] = 0.0f;
    p.axisRow[2] = 1.0f;
    p.axisHeight[0] = 0.25f; p.axisHeight[1] = 1.0f; p.axisHeight[2] = -0.5f;
    p.origin[0] = 4.0f; p.origin[1] = 2.0f; p.origin[2] = 6.0f;

    const u8 cornerH = 40;
    float out[24];
    ComputeTileVertices(p, /*tileSpan*/2, cornerH, /*gridHeight*/0,
                        /*col*/1, /*row*/1, out);

    // Corner A (col,row) XZ-before-height = origin + col*2*axisU + row*2*axisRow.
    // The mesh builder lifted it by cornerH along axisHeight. Reproduce that lift via
    // the real BuildTileVertex using A's XZ-only position as the accumulator.
    const float accA[3] = {
        p.origin[0] + 2.0f * p.axisU[0],            // col*tileSpan*axisU.x, col=1,span=2
        p.origin[1],                                // axisU.y/axisRow.y = 0 here
        p.origin[2] + 2.0f * p.axisRow[2],          // row*tileSpan*axisRow.z
    };
    TileLightParams lp{};
    lp.heightAxis[0] = p.axisHeight[0];
    lp.heightAxis[1] = p.axisHeight[1];
    lp.heightAxis[2] = p.axisHeight[2];
    Vertex v{};
    BuildTileVertex(v, lp, accA, cornerH, /*typeByte*/0);   // real sibling

    // out[0..2] is corner A lifted by cornerH (height group 1, first vertex).
    CHECK_EQ(out[0], v.x);
    CHECK_EQ(out[1], v.y);
    CHECK_EQ(out[2], v.z);
}

// Full dirty -> mark -> summarize flow keeps the Floor dirty bit asserted across the
// passes (each pass OR's bit0), matching the original's per-pass dirty stamp.
TEST(TerrainMeshIT, DirtyBitFlowsThroughPasses) {
    std::uint8_t lodStates[64];
    std::uint32_t flags = 0;
    flags = InvalidateTiles(flags, lodStates, nullptr);
    CHECK_EQ(flags & 1u, 1u);

    const int size = 4, mask = 3;
    auto types = TypeGrid(size, 1, 1, 9);
    std::vector<std::uint8_t> p0(16, 0), p1(4, 0), p2(1, 0);
    std::uint8_t* planes[3] = {p0.data(), p1.data(), p2.data()};
    flags = MarkUniformTiles(types.data(), size, mask, planes, flags, nullptr);
    CHECK_EQ(flags & 1u, 1u);
    for (int i = 0; i < 64; ++i) CHECK_EQ(lodStates[i], (std::uint8_t)0xFF);
}
