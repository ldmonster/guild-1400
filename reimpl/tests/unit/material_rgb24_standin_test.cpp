#include "test.h"

// Unit tests — the LEGACY compat paths around a 24-bit BMP material
// (render/texture.h Rgb24MaterialStandIn*). NOTE (wave-4 verification): the
// @0x5da34c palettizer gap is CLOSED — the resolve layer now palettizes every
// 24-bit DecodedBmp through the reconstructed VIBE_Quant_BuildPalette
// @0x6029f0 (render/texture_palettize.h), so the live bind path never reaches
// either mode below. These pins keep the RAW (un-palettized, decode-layer)
// states exact for the kept compat machinery:
//
//   OFF (default): an INDEX-LESS 24-bit record is unbindable by the 8-bit
//       Texture record, so the poly renders the engine's LEVEL-SHADED 1x1
//       white default (VIBE_Texture_BindActive @0x5db564 slot==0 through the
//       textured leaf — render/meshlist.cpp RasterTri 16bpp branch). Every
//       covered pixel is the linear gray PackColor(L,L,L), L = max vertex
//       light byte (+66).
//
//   ON (the legacy stand-in): the poly draws through the in-tree affine RGB
//       kernel (play::RasterTexturedTriangleAffine). Every covered pixel is
//       the affinely-sampled TRUE 24-bit texel.
#include "play/object_mesh_render.h"
#include "render/colorformat.h"
#include "render/meshlist.h"
#include "render/surface.h"
#include "render/texture.h"
#include "render/texture_bin.h"
#include "render/bmp.h"

#include <cstring>
#include <vector>

using namespace guild;

namespace {

// 8x8 24-bit BMP: left half red(200,30,40), right half blue(30,60,220) —
// >256-colour-class content stands in for the shipped 24-bit city BMPs
// (e.g. Bjoern/hz_Dachleisten_Dunkel_AA.bmp, ~2000 distinct colours).
render::DecodedBmp Make24() {
    const int w = 8, h = 8;
    std::vector<u8> rgb((std::size_t)w * h * 3);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            std::size_t i = ((std::size_t)y * w + x) * 3;
            if (x < 4) { rgb[i] = 200; rgb[i + 1] = 30; rgb[i + 2] = 40; }
            else       { rgb[i] = 30;  rgb[i + 1] = 60; rgb[i + 2] = 220; }
        }
    std::vector<u8> file = render::BmpSave24Bit(w, h, rgb.data());
    return render::DecodeBmpBuffer(file);
}

// A screen triangle covering the left of a 32x32 target, UVs spanning the
// texture; light byte = 144 (a mid level).
void MakeTri(render::Vertex v[3], u8 light) {
    std::memset(v, 0, sizeof(render::Vertex) * 3);
    v[0].screenX = 2.0f;  v[0].screenY = 2.0f;  v[0].u = 0.05f; v[0].v = 0.05f;
    v[1].screenX = 28.0f; v[1].screenY = 4.0f;  v[1].u = 0.95f; v[1].v = 0.05f;
    v[2].screenX = 4.0f;  v[2].screenY = 28.0f; v[2].u = 0.05f; v[2].v = 0.95f;
    for (int i = 0; i < 3; ++i) v[i].lightIdx = light;
}

} // namespace

TEST(MaterialRgb24StandIn, OnModeDrawsTrueRgbThroughAffineKernel) {
    render::DecodedBmp bmp = Make24();
    CHECK(bmp.ok);
    CHECK_EQ(bmp.bpp, 24);
    CHECK(bmp.indices.empty());        // no palette indices -> unbindable record
    CHECK(!bmp.rgba.empty());

    render::Surface* fb = render::SurfaceCreate(32, 32, 16);
    render::SurfaceColorFill(fb, 0, 0, 0);

    render::Vertex v[3];
    MakeTri(v, 144);
    int written = play::RasterTexturedTriangleAffine(fb, &v[0], &v[1], &v[2], bmp);
    CHECK(written > 100);

    // PIN: interior pixels carry the SOURCE 24-bit colours (565-quantised).
    u8 px[3];
    render::SurfaceGetPixelRgb(fb, 6, 6, px);     // left half -> red family
    CHECK_EQ((int)px[0], 200 & 0xF8);
    CHECK_EQ((int)px[1], 30 & 0xFC);
    CHECK_EQ((int)px[2], 40 & 0xF8);
    render::SurfaceGetPixelRgb(fb, 22, 5, px);    // right half -> blue family
    CHECK_EQ((int)px[0], 30 & 0xF8);
    CHECK_EQ((int)px[1], 60 & 0xFC);
    CHECK_EQ((int)px[2], 220 & 0xF8);

    render::SurfaceDestroy(fb);
}

TEST(MaterialRgb24StandIn, OffModeDrawsLevelShadedWhiteDefault) {
    // The DEFAULT mode: the unbindable material falls through to the slot-4
    // textured default — the level-shaded white binding.
    CHECK(!render::Rgb24MaterialStandInEnabled());

    render::Surface* fb = render::SurfaceCreate(32, 32, 16);
    render::SurfaceColorFill(fb, 0, 0, 0);

    render::Vertex v[3];
    const u8 L = 144;
    MakeTri(v, L);
    render::Polygon tri{};
    tri.v0 = &v[0]; tri.v1 = &v[1]; tri.v2 = &v[2];
    tri.matIndex = 0;
    CHECK_EQ(render::SpanFillTexturedOpaque(fb, tri), 1);

    // PIN: every covered pixel is the linear gray PackColor(L,L,L) — the white
    // default at light level L (meshlist.cpp RasterTri 16bpp branch). Same
    // interior points as the ON-mode pin: gray here, true RGB there.
    const u32 gray = render::PackColor(fb->fmt, L, L, L);
    u8 exp[3] = {(u8)(L & 0xF8), (u8)(L & 0xFC), (u8)(L & 0xF8)};
    (void)gray;
    u8 px[3];
    render::SurfaceGetPixelRgb(fb, 6, 6, px);
    CHECK_EQ((int)px[0], (int)exp[0]);
    CHECK_EQ((int)px[1], (int)exp[1]);
    CHECK_EQ((int)px[2], (int)exp[2]);
    render::SurfaceGetPixelRgb(fb, 22, 5, px);
    CHECK_EQ((int)px[0], (int)exp[0]);
    CHECK_EQ((int)px[1], (int)exp[1]);
    CHECK_EQ((int)px[2], (int)exp[2]);

    // A corner outside the triangle stays the clear colour.
    render::SurfaceGetPixelRgb(fb, 31, 31, px);
    CHECK_EQ((int)px[0], 0);
    CHECK_EQ((int)px[2], 0);

    render::SurfaceDestroy(fb);
}

// --- wave-4: the white-default shade uses the AVG light selector --------------
// RasterizeMirrorTriangle @0x5f70bd sets the span palette row from the AVG of
// the three vertex +66 light bytes (dword_13FC5E0 = ((l0+l1+l2)/3) << 8) — not
// the max ("768 * max" is only the draw-list sort key, 0x5c5120). The white
// stand-in row is the linear gray of that AVG level.
TEST(MaterialRgb24StandIn, WhiteDefaultShadeUsesAvgLightNotMax) {
    render::Surface* fb = render::SurfaceCreate(32, 32, 16);
    render::SurfaceColorFill(fb, 0, 0, 0);

    render::Vertex v[3];
    MakeTri(v, 0);
    v[0].lightIdx = 30; v[1].lightIdx = 60; v[2].lightIdx = 90;  // avg 60, max 90
    render::Polygon tri{};
    tri.v0 = &v[0]; tri.v1 = &v[1]; tri.v2 = &v[2];
    tri.matIndex = 0;
    CHECK_EQ(render::SpanFillTexturedOpaque(fb, tri), 1);

    const u8 L = 60;                       // (30+60+90)/3
    u8 px[3];
    render::SurfaceGetPixelRgb(fb, 6, 6, px);
    CHECK_EQ((int)px[0], (int)(u8)(L & 0xF8));
    CHECK_EQ((int)px[1], (int)(u8)(L & 0xFC));
    CHECK_EQ((int)px[2], (int)(u8)(L & 0xF8));
    render::SurfaceDestroy(fb);
}
