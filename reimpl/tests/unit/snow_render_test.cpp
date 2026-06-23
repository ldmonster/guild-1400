// Golden-vector + render tests for the snow falling-flake renderer.
//
//   VIBE_Snow_Render            @0x42b5b0  (header interp + quad emission)
//   VIBE_Snow_UpdateScene tail  @0x42a2cc  (per-texture transparency rule)
//   VIBE_GameTime_GetSeasonFromDay @0x58339c (season = day % 4)
//
// All constants/bit-patterns recovered with get_bytes from gilde.exe and verified
// against the Hex-Rays decompile + raw disasm. See progress/snow-render-wave6.md.
#include "tests/framework/test.h"
#include "render/snow.h"
#include "render/surface.h"

#include <cmath>
#include <cstring>

using namespace guild;
using namespace guild::render;

namespace {
bool fclose(float a, float b) { return std::fabs(a - b) < 1e-4f; }
inline u32 fbits(float f) { union { float f; u32 i; } u{f}; return u.i; }
} // namespace

// ---------------------------------------------------------------------------
// Season gate: weather (snow) only spawns in winter (season 3 == day % 4 == 3).
// ---------------------------------------------------------------------------
TEST(SnowRenderSeason, SeasonFromDayModulo) {
    CHECK_EQ(SnowSeasonFromDay(0), 0);
    CHECK_EQ(SnowSeasonFromDay(3), 3);
    CHECK_EQ(SnowSeasonFromDay(7), 3);   // 7 % 4
    CHECK_EQ(SnowSeasonFromDay(11), 3);
    CHECK(SnowIsWinterDay(3));
    CHECK(SnowIsWinterDay(15));           // 15 % 4 == 3
    CHECK(!SnowIsWinterDay(4));           // spring
    CHECK(!SnowIsWinterDay(0));
}

// ---------------------------------------------------------------------------
// UpdateScene per-texture transparency rule:  v6 = !winterFlag && level > 8.
// ---------------------------------------------------------------------------
TEST(SnowResetScene, TexTransparencyRule) {
    // Not winter + high mip level -> transparent.
    CHECK(SnowResetSceneTexTransparency(/*winter*/0, /*level*/9));
    // Winter (flag set) suppresses transparency regardless of level.
    CHECK(!SnowResetSceneTexTransparency(1, 9));
    // Low level never transparent.
    CHECK(!SnowResetSceneTexTransparency(0, 8));
    CHECK(!SnowResetSceneTexTransparency(0, 0));
    // unsigned compare: level must be strictly > 8.
    CHECK(SnowResetSceneTexTransparency(0, 255));
}

// ---------------------------------------------------------------------------
// Header interpolation: per-frame dt = (now - lastUpdate) * 0.1 and lastUpdate
// advances to now.
// ---------------------------------------------------------------------------
TEST(SnowRenderHeader, DtAndLastUpdate) {
    SnowSystemHdr h;
    h.lastUpdateMs = 1000;
    h.cBeg = h.cEnd = 0; // inactive count window
    h.dBeg = h.dEnd = 0; // inactive dir window
    float dt = SnowRenderStepHeader(h, 1100);
    CHECK(fclose(dt, (1100 - 1000) * 0.1f)); // = 10.0
    CHECK_EQ(h.lastUpdateMs, 1100);
}

TEST(SnowRenderHeader, CountRampInterpolates) {
    SnowSystemHdr h;
    h.cFrom = 0;    // [2]
    h.cTo   = 100;  // [3]
    h.cBeg  = 0;    // [24]
    h.cEnd  = 1000; // [25]
    h.lastUpdateMs = 0;
    // now=500 (midpoint): cTo*(500-0)/1000 + (1000-500)*cFrom/1000
    //                   = 100*500/1000 + 500*0/1000 = 50.
    SnowRenderStepHeader(h, 500);
    CHECK_EQ(h.count, 50);
    // now past cEnd -> snap to cTo.
    h.lastUpdateMs = 0;
    SnowRenderStepHeader(h, 2000);
    CHECK_EQ(h.count, 100);
}

TEST(SnowRenderHeader, DirectionRampInterpolates) {
    SnowSystemHdr h;
    h.oldX = 1.0f; h.oldZ = 0.0f;  // [15]/[16]
    h.tgtX = 0.0f; h.tgtZ = 1.0f;  // [19]/[20]
    h.dBeg = 0; h.dEnd = 100;      // [26]/[27]
    h.lastUpdateMs = 0;
    // now=50 (midpoint): dirX = tgtX*ela/span + oldX*rem/span
    //                         = 0*50/100 + 1*50/100 = 0.5
    //                    dirZ = ela*tgtZ/span + rem*oldZ/span
    //                         = 50*1/100 + 50*0/100 = 0.5
    SnowRenderStepHeader(h, 50);
    CHECK(fclose(h.dirX, 0.5f));
    CHECK(fclose(h.dirZ, 0.5f));
    // past dEnd -> snap to target.
    h.lastUpdateMs = 0;
    SnowRenderStepHeader(h, 200);
    CHECK(fclose(h.dirX, 0.0f));
    CHECK(fclose(h.dirZ, 1.0f));
}

// ---------------------------------------------------------------------------
// Quad emission: cull, vertex count, exact bit patterns and coordinates.
// ---------------------------------------------------------------------------
TEST(SnowRenderQuads, EmitsThreeVertsPerVisibleFlake) {
    SnowFlake flakes[2];
    std::memset(flakes, 0, sizeof(flakes));
    // Flake 0 fully on-screen: segment (10,20)->(14,26) inside [0,0,100,100).
    flakes[0].pz = 0.0f;
    flakes[0].sx = 10.0f; flakes[0].sy = 20.0f;
    flakes[0].sx2 = 14.0f; flakes[0].sy2 = 26.0f;
    // Flake 1 culled: sx2 beyond x1.
    flakes[1].sx = 90.0f; flakes[1].sy = 20.0f;
    flakes[1].sx2 = 200.0f; flakes[1].sy2 = 26.0f;

    SnowSystem sys;
    sys.flakes = flakes; sys.count = 2; sys.capacity = 2;
    SnowViewport vp{0, 0, 100, 100};

    SnowVertex out[16];
    int n = SnowBuildQuads(sys, vp, out, 16);
    CHECK_EQ(n, 3); // only flake 0 visible -> 3 vertices

    // vcoord = (1 - pz) * 0.025 = 1 * 0.025 = 0.025
    float vc = 0.025f;
    // vertex 0: x=(sx+sx2)*0.5=12, y=sy=20, z=vc, tu=0.5, tv=0
    CHECK(fclose(out[0].x, 12.0f));
    CHECK(fclose(out[0].y, 20.0f));
    CHECK(fclose(out[0].z, vc));
    CHECK(fclose(out[0].u, 0.5f));
    CHECK(fclose(out[0].v, 0.0f));
    // vertex 1: x=sx2=14, y=sy2=26, tu=1, tv=1
    CHECK(fclose(out[1].x, 14.0f));
    CHECK(fclose(out[1].y, 26.0f));
    CHECK(fclose(out[1].u, 1.0f));
    CHECK(fclose(out[1].v, 1.0f));
    // vertex 2: x=sx=10, y=sy2=26, tu=0, tv=1
    CHECK(fclose(out[2].x, 10.0f));
    CHECK(fclose(out[2].y, 26.0f));
    CHECK(fclose(out[2].u, 0.0f));
    CHECK(fclose(out[2].v, 1.0f));

    // Exact D3D TLVERTEX bit patterns: rhw=1.0, diffuse=0x50646464, specular=0.
    for (int i = 0; i < 3; ++i) {
        CHECK_EQ(out[i].rhw, fbits(1.0f));      // 0x3F800000
        CHECK_EQ(out[i].color, 0x50646464u);    // snow diffuse
        CHECK_EQ(out[i].specular, 0u);
    }
}

TEST(SnowRenderQuads, RespectsOutputCap) {
    SnowFlake flakes[4];
    std::memset(flakes, 0, sizeof(flakes));
    for (int i = 0; i < 4; ++i) {
        flakes[i].pz = 0.0f;
        flakes[i].sx = 10.0f; flakes[i].sy = 20.0f;
        flakes[i].sx2 = 14.0f; flakes[i].sy2 = 26.0f;
    }
    SnowSystem sys; sys.flakes = flakes; sys.count = 4; sys.capacity = 4;
    SnowViewport vp{0, 0, 100, 100};
    SnowVertex out[5];
    // cap=5 -> can fit 1 flake (3 verts); next would need 6 > 5 -> stops.
    int n = SnowBuildQuads(sys, vp, out, 5);
    CHECK_EQ(n, 3);
}

TEST(SnowRenderQuads, EmptyWhenNoFlakes) {
    SnowSystem sys; sys.flakes = nullptr; sys.count = 0;
    SnowViewport vp{0, 0, 100, 100};
    SnowVertex out[4];
    CHECK_EQ(SnowBuildQuads(sys, vp, out, 4), 0);
}

// SnowVertex must be the engine's 32-byte TLVERTEX stride.
TEST(SnowRenderQuads, VertexStride) {
    CHECK_EQ((int)sizeof(SnowVertex), 32);
}

// ---------------------------------------------------------------------------
// Render-to-Surface: drive a seeded + integrated snow field through the real
// projection (SnowUpdateFlake) and the real quad builder (SnowBuildQuads), then
// rasterize each emitted triangle's screen segment onto a software Surface (the
// engine presents these as textured triangles via DrawPrimitive; here we prove
// the projected geometry lands inside the viewport and paints pixels).
// ---------------------------------------------------------------------------
TEST(SnowRenderSurface, FieldProjectsAndPaints) {
    const int W = 64, H = 64;
    Surface* surf = SurfaceCreate(W, H, 16);
    CHECK(surf != nullptr);
    SurfaceColorFill(surf, 0, 0, 0);

    // Seed a deterministic flake field, then run one integration step with an
    // identity-ish camera and a viewport matching the surface.
    const int N = 64;
    SnowFlake flakes[N];
    std::memset(flakes, 0, sizeof(flakes));
    SnowSystem sys; sys.flakes = flakes; sys.count = N; sys.capacity = N;
    SnowSeedFlakes(sys);

    SnowCamera cam{};
    cam.m[0] = 1.0f; cam.m[4] = 1.0f; cam.m[8] = 1.0f; // identity basis
    SnowViewport vp{0, 0, W, H};
    SnowUpdateFlake(sys, 1.0f, cam, vp);

    SnowVertex verts[N * 3];
    int n = SnowBuildQuads(sys, vp, verts, N * 3);
    // The seeded field should produce at least one on-screen flake triangle.
    CHECK(n > 0);
    CHECK_EQ(n % 3, 0);

    int painted = 0;
    for (int t = 0; t < n; t += 3) {
        // Triangle screen-space coords are vertex .x/.y (already projected).
        int x0 = (int)std::lround(verts[t + 0].x);
        int y0 = (int)std::lround(verts[t + 0].y);
        int x1 = (int)std::lround(verts[t + 1].x);
        int y1 = (int)std::lround(verts[t + 1].y);
        int x2 = (int)std::lround(verts[t + 2].x);
        int y2 = (int)std::lround(verts[t + 2].y);
        SurfaceDrawLine(surf, x0, y0, x1, y1, 240, 240, 255);
        SurfaceDrawLine(surf, x1, y1, x2, y2, 240, 240, 255);
        SurfaceDrawLine(surf, x2, y2, x0, y0, 240, 240, 255);
        ++painted;
    }
    CHECK(painted > 0);

    // Confirm pixels were actually set somewhere on the surface.
    int nonZero = 0;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            u8 rgb[3];
            SurfaceGetPixelRgb(surf, x, y, rgb);
            if (rgb[0] || rgb[1] || rgb[2]) ++nonZero;
        }
    CHECK(nonZero > 0);

    SurfaceDestroy(surf);
}
