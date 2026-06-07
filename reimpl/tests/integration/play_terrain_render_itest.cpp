// tests/integration/play_terrain_render_itest.cpp — INTEGRATION: rasterize a
// synthetic heightfield into a 128x96 software Surface through the REAL raster
// leaves (render::RasterizeTexturedTriangle / FillTexturedSpansShaded) and assert
// the ground actually drew (non-blank + known shaded pixels).
#include "test.h"

#include "play/terrain_render.h"
#include "render/surface.h"

using namespace guild;

// Render the synthetic floor into a real 8bpp surface and assert it filled.
TEST(PlayTerrainRenderItest, RasterizeSyntheticGround) {
    // The synthetic heightfield (the unit field, scaled up to a full 8x8 tile grid
    // with tileSpan==8 so each tile tessellates into real quads).
    play::Heightfield hf = play::Heightfield::MakeSynthetic(/*edge=*/64, /*seed=*/0);
    CHECK(hf.valid());
    CHECK_EQ(hf.size, 64);
    CHECK_EQ(hf.tileSpan, 8);

    play::TerrainView view = play::TerrainView::MakeTopDown(hf.size, hf.tileSpan, 128, 96);

    play::TerrainRenderStats st;
    render::Surface* fb =
        play::RenderTerrainToSurface(128, 96, hf, view, /*background=*/0, &st);
    CHECK(fb != nullptr);
    if (!fb) return;

    // The walk visited all 64 tiles and tessellated real quads / triangles.
    CHECK_EQ(st.tilesDrawn, 64);
    CHECK(st.quadsBuilt > 1000);
    CHECK(st.trisDrawn > 1000);

    // The ground drew a LARGE non-background fraction (no texture => shaded fill).
    int total = fb->width * fb->height;     // 128*96 = 12288
    CHECK(st.nonBlankPix > total / 2);      // > 50% painted
    CHECK_EQ(st.nonBlankPix, play::RenderTerrain(fb, hf, view, 0).nonBlankPix);

    // The shading varies (the affine span shade ramps): many distinct shade bytes.
    int hist[256] = {0};
    for (int i = 0; i < total; ++i) hist[fb->pixels[i]]++;
    int distinct = 0;
    for (int i = 1; i < 256; ++i) if (hist[i]) ++distinct;
    CHECK(distinct > 50);                    // a real shaded gradient, not one flat fill

    // A central pixel is well inside the drawn ground and non-zero.
    {
        int cx = 64, cy = 48;
        u8 p = fb->pixels[(size_t)cy * fb->widthPx + cx];
        CHECK(p != 0);
    }

    // Per-vertex shade range was populated from BuildTileVertex lighting.
    CHECK(st.minShade < st.maxShade);
    CHECK(st.maxShade > 100);

    render::SurfaceDestroy(fb);
}

// Inert texture hook keeps the renderer on the shaded fallback (device leaf path).
TEST(PlayTerrainRenderItest, InertTextureHookKeepsShadedPath) {
    static int calls = 0;
    calls = 0;
    play::TerrainRenderHooks hooks{};
    hooks.getTileTexture = [](u8) -> const void* { ++calls; return nullptr; };
    play::SetTerrainRenderHooks(&hooks);

    play::Heightfield hf = play::Heightfield::MakeSynthetic(32, 7);
    play::TerrainView view = play::TerrainView::MakeTopDown(hf.size, hf.tileSpan, 128, 96);
    play::TerrainRenderStats st;
    render::Surface* fb = play::RenderTerrainToSurface(128, 96, hf, view, 0, &st);
    CHECK(fb != nullptr);
    if (fb) {
        CHECK(st.nonBlankPix > 0);           // still drew via the shaded path
        render::SurfaceDestroy(fb);
    }
    play::SetTerrainRenderHooks(nullptr);    // restore inert default
}
