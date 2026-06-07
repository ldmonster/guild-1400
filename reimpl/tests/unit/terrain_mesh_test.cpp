#include "test.h"
#include "render/terrain_mesh.h"

#include <cstdint>
#include <vector>

using namespace guild;
using namespace guild::render;

// ---------------------------------------------------------------------------
// ComputeTileVertices — exact golden vector (computed with python float32 oracle).
// Params chosen so every product/sum lands on an exact float: axisU=(2,0,.5),
// axisRow=(0,0,3), axisHeight=(0,1,0), origin=(10,-5,20); tileSpan=4, col=1, row=2,
// cornerHeight=30, gridHeight=200.
// ---------------------------------------------------------------------------
TEST(TerrainMesh, ComputeTileVerticesGolden) {
    TileBuildParams p{};
    p.axisU[0] = 2.0f;  p.axisU[1] = 0.0f;  p.axisU[2] = 0.5f;
    p.axisRow[0] = 0.0f; p.axisRow[1] = 0.0f; p.axisRow[2] = 3.0f;
    p.axisHeight[0] = 0.0f; p.axisHeight[1] = 1.0f; p.axisHeight[2] = 0.0f;
    p.origin[0] = 10.0f; p.origin[1] = -5.0f; p.origin[2] = 20.0f;

    float out[24];
    ComputeTileVertices(p, /*tileSpan=*/4, /*cornerHeight=*/30, /*gridHeight=*/200,
                        /*col=*/1, /*row=*/2, out);

    const float exp[24] = {
        18.0f, 25.0f, 46.0f,  26.0f, 25.0f, 48.0f,  26.0f, 25.0f, 60.0f,  18.0f, 25.0f, 58.0f,
        18.0f, 195.0f, 46.0f, 26.0f, 195.0f, 48.0f, 26.0f, 195.0f, 60.0f, 18.0f, 195.0f, 58.0f,
    };
    for (int i = 0; i < 24; ++i)
        CHECK_EQ(out[i], exp[i]);

    // The two height groups must share identical XZ (x at +0, z at +2); only y differs
    // because axisHeight only lifts y. Verify the corner XZ is reused across h1/h2.
    for (int v = 0; v < 4; ++v) {
        CHECK_EQ(out[v * 3 + 0], out[12 + v * 3 + 0]);   // x equal
        CHECK_EQ(out[v * 3 + 2], out[12 + v * 3 + 2]);   // z equal
        CHECK(out[12 + v * 3 + 1] > out[v * 3 + 1]);     // h2(200) lifts y above h1(30)
    }
}

// Corner topology: A=(col,row) B=(col+1,row) C=(col+1,row+1) D=(col,row+1).
// With axisU only on x and axisRow only on z, B is +tileSpan*axisU.x from A in x,
// and D is +tileSpan*axisRow.z from A in z.
TEST(TerrainMesh, ComputeTileVerticesTopology) {
    TileBuildParams p{};
    p.axisU[0] = 1.0f;  p.axisRow[2] = 1.0f;  // 1 unit per sample
    float out[24];
    ComputeTileVertices(p, /*tileSpan=*/2, 0, 0, /*col=*/0, /*row=*/0, out);
    // A at origin (0,0,0); B at x=+2 (2 samples * 1); D at z=+2.
    CHECK_EQ(out[0], 0.0f);   // A.x
    CHECK_EQ(out[3], 2.0f);   // B.x = (col+1)*span = 2
    CHECK_EQ(out[6], 2.0f);   // C.x
    CHECK_EQ(out[11], 2.0f);  // D.z = (row+1)*span = 2
    CHECK_EQ(out[2], 0.0f);   // A.z
}

// ---------------------------------------------------------------------------
// InvalidateTiles — sets dirty bit0 and 0xFF into all 64 LOD-state bytes.
// ---------------------------------------------------------------------------
TEST(TerrainMesh, InvalidateTiles) {
    std::uint8_t states[64];
    for (int i = 0; i < 64; ++i) states[i] = (std::uint8_t)i;
    std::uint32_t outFlags = 0;
    std::uint32_t r = InvalidateTiles(/*floorFlags=*/0x10, states, &outFlags);
    CHECK_EQ(r, 0x11u);            // bit0 OR'd in
    CHECK_EQ(outFlags, 0x11u);
    for (int i = 0; i < 64; ++i)
        CHECK_EQ(states[i], (std::uint8_t)0xFF);
    // bit0 already set stays set.
    CHECK_EQ(InvalidateTiles(1u, states, nullptr), 1u);
}

// ---------------------------------------------------------------------------
// MarkUniformTiles — span {1,2,4} uniform-block flag (0x40).
// ---------------------------------------------------------------------------
TEST(TerrainMesh, MarkUniformTilesAllSame) {
    const int size = 4, mask = 3;
    std::vector<std::uint8_t> types(size * size, 7);   // all identical -> uniform
    // 3 flag planes sized for the largest span's block count (size/1 = 16 cells).
    std::vector<std::uint8_t> p0(16, 0), p1(16, 0), p2(16, 0);
    std::uint8_t* planes[3] = {p0.data(), p1.data(), p2.data()};
    std::uint32_t outFlags = 0;
    std::uint32_t r = MarkUniformTiles(types.data(), size, mask, planes, 0, &outFlags);
    CHECK_EQ(r, 1u);               // dirty bit set
    CHECK_EQ(outFlags, 1u);
    // span 1: 16 blocks, each a single uniform cell -> all 0x40.
    for (int i = 0; i < 16; ++i) CHECK_EQ(p0[i] & 0x40, 0x40);
    // span 2: 4 blocks (2x2 of identical) -> 0x40.
    for (int i = 0; i < 4; ++i)   CHECK_EQ(p1[i] & 0x40, 0x40);
    // span 4: 1 block (whole grid identical) -> 0x40.
    CHECK_EQ(p2[0] & 0x40, 0x40);
}

TEST(TerrainMesh, MarkUniformTilesMixedClearsBit) {
    const int size = 4, mask = 3;
    std::vector<std::uint8_t> types(size * size, 0);
    types[5] = 9;                  // one differing cell (col1,row1)
    std::vector<std::uint8_t> p0(16, 0xFF), p1(16, 0xFF), p2(16, 0xFF);
    std::uint8_t* planes[3] = {p0.data(), p1.data(), p2.data()};
    MarkUniformTiles(types.data(), size, mask, planes, 0, nullptr);
    // span 1: TileIsUniform scans the INCLUSIVE [x0,x0+1]x[y0,y0+1] 2x2 region (span
    // is an offset, not a count). Blocks whose 2x2 neighbourhood touches the 9 cell
    // are non-uniform; the golden pattern was computed with the TileIsUniform oracle.
    const int span1[16] = {0,0,1,1, 0,0,1,1, 1,1,1,1, 1,1,1,1};
    for (int i = 0; i < 16; ++i)
        CHECK_EQ(p0[i] & 0x40, span1[i] ? 0x40 : 0x00);
    // span 4: the whole-grid 5x5-inclusive block is NOT uniform (has a 9) -> 0x40
    // cleared, other bits preserved.
    CHECK_EQ(p2[0] & 0x40, 0x00);
    CHECK_EQ(p2[0] & ~0x40, 0xFF & ~0x40);  // other bits untouched
    // span 2: block (col0,row0) covering cells (0..2,0..2) inclusive includes the 9
    // -> non-uniform.
    CHECK_EQ(p1[0] & 0x40, 0x00);
}

// ---------------------------------------------------------------------------
// SummarizeTileElevations — per-tile min/max over the (tileSpan+1)^2 block.
// ---------------------------------------------------------------------------
TEST(TerrainMesh, SummarizeTileElevationsRamp) {
    // 8x8 grid (so tile 0..7 each tileSpan=1 -> 2x2 sample block, mask=7).
    const int size = 8, mask = 7, tileSpan = 1;
    std::vector<std::uint8_t> h(size * size);
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x)
            h[y * size + x] = (std::uint8_t)(x + y);   // ramp 0..14
    TileElevationSummary s{};
    SummarizeTileElevations(h.data(), nullptr, size, mask, tileSpan, &s);
    // Tile (col,row) covers samples (col..col+1, row..row+1); min = col+row,
    // max = col+row+2.
    for (int row = 0; row < 8; ++row) {
        for (int col = 0; col < 8; ++col) {
            const int t = row * 8 + col;
            const int base = col + row;
            // edge tiles (col==7 / row==7) wrap via mask so the +1 sample folds to 0.
            std::uint8_t expMin, expMax;
            if (col < 7 && row < 7) { expMin = (std::uint8_t)base; expMax = (std::uint8_t)(base + 2); }
            else { /* wrapped — just assert min<=max and within range */ expMin = s.minHeight[t]; expMax = s.maxHeight[t]; }
            CHECK(s.minHeight[t] <= s.maxHeight[t]);
            if (col < 7 && row < 7) {
                CHECK_EQ(s.minHeight[t], expMin);
                CHECK_EQ(s.maxHeight[t], expMax);
            }
        }
    }
}

// Overlay grid: non-zero overlay bytes participate in min/max; zero overlay is ignored.
TEST(TerrainMesh, SummarizeTileElevationsOverlay) {
    const int size = 8, mask = 7, tileSpan = 1;
    std::vector<std::uint8_t> h(size * size, 100);     // flat 100
    std::vector<std::uint8_t> ov(size * size, 0);      // mostly ignored
    ov[0] = 250;                                        // raises max of tile(0,0)
    ov[9] = 0;                                          // zero -> ignored
    TileElevationSummary s{};
    SummarizeTileElevations(h.data(), ov.data(), size, mask, tileSpan, &s);
    CHECK_EQ(s.maxHeight[0], (std::uint8_t)250);        // overlay raised it
    CHECK_EQ(s.minHeight[0], (std::uint8_t)100);        // flat base
    // A tile with no non-zero overlay stays flat 100/100.
    CHECK_EQ(s.maxHeight[2 * 8 + 2], (std::uint8_t)100);
    CHECK_EQ(s.minHeight[2 * 8 + 2], (std::uint8_t)100);
}
