// End-to-end test for the guild::render software rasterizer: rasterize a small
// textured quad (2 triangles) into a shared framebuffer and verify the composited
// result is BIT-EXACT against the python reference (render_raster_vectors.inc,
// kQuad), which composites the same two triangles with the same 16.16 math.
#include "render/raster.h"
#include "render/surface.h"
#include "test.h"

#include <cstring>
#include <vector>

#include "../unit/render_raster_vectors.inc"

using namespace guild::render;

TEST(RenderRasterE2E, TexturedQuadComposite) {
    const int W = 20, H = 20;
    Surface* s = SurfaceCreate(W, H, 8);
    std::memset(s->pixels, 0, (size_t)s->pitch * H);

    // Quad corners (screen x, y, light/shade byte):
    RasterVertex TL{2.0f, 2.0f, 40};
    RasterVertex TR{17.0f, 3.0f, 60};
    RasterVertex BR{16.0f, 17.0f, 200};
    RasterVertex BL{3.0f, 16.0f, 120};

    // Two triangles, drawn in order; the second composites over the first
    // (later writes win where they overlap the shared diagonal edge), exactly
    // as the reference does.
    RasterVertex triA[3] = {TL, TR, BR};
    RasterVertex triB[3] = {TL, BR, BL};
    int a = RasterizeTexturedTriangle(s, triA);
    int b = RasterizeTexturedTriangle(s, triB);
    CHECK(a != 0);
    CHECK(b != 0);

    // Compare against golden composite.
    std::vector<unsigned char> expect((size_t)W * H, 0);
    for (int i = 0; i < kQuad_count; ++i)
        expect[(size_t)kQuad[i].y * W + kQuad[i].x] = kQuad[i].v;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x)
            CHECK_EQ((int)s->pixels[(size_t)y * s->widthPx + x],
                     (int)expect[(size_t)y * W + x]);

    SurfaceDestroy(s);
}
