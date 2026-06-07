// tests/unit/play_terrain_render_test.cpp — UNIT: tile geometry + lighting golden
// vectors for guild::play::TerrainRenderer over a synthetic 8x8 heightfield.
//
// Pins the deterministic geometry+lighting core (BuildTerrainVertex, which drives
// the REAL render::BuildTileVertex inner loop) and the LOD / subdivision counts
// (render::SelectTileMeshLod / TileSubdivCount) the renderer walks the tiles with.
#include "test.h"

#include "play/terrain_render.h"
#include "render/terrain_render.h"   // SelectTileMeshLod / TileSubdivCount

using namespace guild;

namespace {

// A deterministic 8x8 (tileSpan==1) field: a height ramp and a type ramp, with
// exactly one shadowed cell at (0,0) (high bit set).
play::Heightfield Make8x8() {
    play::Heightfield hf;
    hf.size = 8; hf.tileSpan = 1;
    hf.heights.assign(64, 0);
    hf.types.assign(64, 0);
    for (int y = 0; y < 8; ++y)
        for (int x = 0; x < 8; ++x) {
            int i = y * 8 + x;
            hf.heights[i] = (u8)(10 + x * 20 + y * 5);
            u8 t = (u8)((x + y) * 4);
            if (x == 0 && y == 0) t |= 0x80;   // shadowed
            hf.types[i] = t;
        }
    return hf;
}

} // namespace

// --- lighting math (ShadeFromRgb luma) -------------------------------------
TEST(PlayTerrainRenderUnit, ShadeLuma) {
    // luma = (R*77 + G*150 + B*29) >> 8
    CHECK_EQ((int)play::ShadeFromRgb(0, 0, 0), 0);
    CHECK_EQ((int)play::ShadeFromRgb(255, 255, 255), 255);
    CHECK_EQ((int)play::ShadeFromRgb(120, 80, 40), 87);   // (9240+12000+1160)>>8
    // greyscale luma == the channel value
    CHECK_EQ((int)play::ShadeFromRgb(80, 80, 80), 80);
}

// --- per-vertex geometry + lighting golden vectors -------------------------
TEST(PlayTerrainRenderUnit, VertexGolden) {
    play::Heightfield hf = Make8x8();
    play::TerrainView view = play::TerrainView::MakeTopDown(8, 1, 128, 96);
    CHECK(hf.valid());

    // (0,0): shadowed cell. h=10, type high-bit set, l=0.
    //   R = 1.0*0 + (-30 shadow) + 40 ambient = 10 ; shade=10.
    //   world.y = 0.25*10 = 2.5 ; world.x=0 ; world.z=0.
    {
        play::TileVertex v = play::BuildTerrainVertex(hf, view, 0, 0);
        CHECK_EQ((int)v.r, 10); CHECK_EQ((int)v.g, 10); CHECK_EQ((int)v.b, 10);
        CHECK_EQ((int)v.shade, 10);
        CHECK(v.worldY > 2.49f && v.worldY < 2.51f);
        CHECK(v.worldX == 0.0f && v.worldZ == 0.0f);
    }

    // (3,2): lit cell. h=80, type=20, l=40. R = 1.0*40 + 40 = 80 ; shade=80.
    //   world = (3, 0.25*80=20, 2).
    {
        play::TileVertex v = play::BuildTerrainVertex(hf, view, 3, 2);
        CHECK_EQ((int)v.r, 80); CHECK_EQ((int)v.shade, 80);
        CHECK(v.worldX == 3.0f && v.worldZ == 2.0f);
        CHECK(v.worldY > 19.99f && v.worldY < 20.01f);
        // top-down screen: originX(7.68) + 3*scaleX(14.08) = 49.92
        CHECK(v.screenX > 49.91f && v.screenX < 49.93f);
        CHECK(v.screenY > 26.87f && v.screenY < 26.89f);
    }

    // (7,7): h=46+... = 10+140+35 = wait: 10 + 7*20 + 7*5 = 185? type=(14)*4=56,l=112.
    //   R = 112 + 40 = 152 ; shade=152.
    {
        play::TileVertex v = play::BuildTerrainVertex(hf, view, 7, 7);
        CHECK_EQ((int)v.r, 152); CHECK_EQ((int)v.shade, 152);
        CHECK(v.worldX == 7.0f && v.worldZ == 7.0f);
    }

    // (1,1): h=10+20+5=35, type=8,l=16. R = 16 + 40 = 56 ; shade=56.
    {
        play::TileVertex v = play::BuildTerrainVertex(hf, view, 1, 1);
        CHECK_EQ((int)v.r, 56); CHECK_EQ((int)v.shade, 56);
        CHECK(v.worldY > 8.74f && v.worldY < 8.76f);  // 0.25*35 = 8.75
    }
}

// --- wrap (torus) sampling: sample size == sample 0 -------------------------
TEST(PlayTerrainRenderUnit, TorusWrap) {
    play::Heightfield hf = Make8x8();
    play::TerrainView view = play::TerrainView::MakeTopDown(8, 1, 128, 96);
    // sample (8, 0) wraps (mask&8 == 0) to cell (0,0) for height/type, but the
    // WORLD position uses the un-wrapped index 8 -> the right seam vertex.
    play::TileVertex w = play::BuildTerrainVertex(hf, view, 8, 0);
    play::TileVertex o = play::BuildTerrainVertex(hf, view, 0, 0);
    CHECK_EQ((int)w.r, (int)o.r);          // same cell sampled
    CHECK_EQ((int)w.shade, (int)o.shade);
    CHECK(w.worldX == 8.0f);               // but advanced world x
}

// --- LOD + subdivision counts the walk uses --------------------------------
TEST(PlayTerrainRenderUnit, LodAndSubdiv) {
    // tiny per-tile world step => coarse field => smallest LOD clamp lands at 1..4.
    int lodSmall = render::SelectTileMeshLod(0.01f);    // huge 50/scaleX -> clamp 4
    CHECK_EQ(lodSmall, 4);
    int lodBig = render::SelectTileMeshLod(1000.0f);    // ~0 -> clamp 1
    CHECK_EQ(lodBig, 1);

    // subdivision: base = tileSpan/lod + 1; the col/row==7 seam subtracts 4/lod.
    CHECK_EQ((int)render::TileSubdivCount(/*span*/8, /*lod*/1, /*col*/0, /*row*/0, true), 9);
    CHECK_EQ((int)render::TileSubdivCount(8, 1, 7, 0, true), 9 - 4);   // last col stitch
    CHECK_EQ((int)render::TileSubdivCount(8, 2, 0, 0, true), 8 / 2 + 1); // 5
    CHECK_EQ((int)render::TileSubdivCount(8, 2, 7, 0, true), 5 - 2);     // 4/2 stitch
}
