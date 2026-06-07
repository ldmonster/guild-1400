#include "test.h"
#include "render/clip.h"
#include "render/meshlist.h"
#include "render/raster.h"
#include "render/surface.h"

#include <cstring>
#include <cmath>
#include <vector>

using namespace guild;
using namespace guild::render;

static void SetXYZ(Vertex& v, float x, float y, float z) { v.x = x; v.y = y; v.z = z; }
static void SetClipByte(Vertex& v, u8 b) { ((u8*)&v)[76] = b; }

// near plane: inside when z >= 1.0
static const ClipPlane kNear = {0.0f, 0.0f, 1.0f, 1.0f};

// ---------------------------------------------------------------------------
// E2E: build a small SORTED draw list with one fully-inside poly and one poly
// straddling the near plane, FLUSH it (clip -> dispatch -> rasterize) into an
// 8-bit framebuffer, and verify the clipped geometry + framebuffer pixels.
// ---------------------------------------------------------------------------
TEST(RenderClipE2E, FlushSortedListClipsAndRasterizes) {
    // Projection that maps model xy directly onto screen pixels for z that gives
    // a known triangle: choose scalars so that with z fixed the screen = a
    // pixel-sized triangle around the upper-left of a 64x64 buffer.
    // For the straddling poly, vertices live at z in [0,2]; after clip the
    // survivors are reprojected. We pick scalars so reprojected coords land in
    // the buffer and produce a non-empty span.
    ProjectScalars proj{20.0f, 20.0f, 20.0f, 20.0f};

    // --- inside poly (already projected, no clip needed) -------------------
    Vertex inTri[3];
    std::memset(inTri, 0, sizeof(inTri));
    // Pre-set projected screen coords (screenX=+0x10, screenY=+0x14) + light.
    inTri[0].screenX = 4.0f;  inTri[0].screenY = 4.0f;   ((u8*)&inTri[0])[66] = 200;
    inTri[1].screenX = 30.0f; inTri[1].screenY = 4.0f;   ((u8*)&inTri[1])[66] = 200;
    inTri[2].screenX = 16.0f; inTri[2].screenY = 28.0f;  ((u8*)&inTri[2])[66] = 200;
    SetXYZ(inTri[0], 4.0f, 4.0f, 5.0f);
    SetXYZ(inTri[1], 30.0f, 4.0f, 5.0f);
    SetXYZ(inTri[2], 16.0f, 28.0f, 5.0f);
    // clip bytes have NO frustum bits set (in-frustum) -> direct path.
    Polygon inPoly{}; inPoly.v0 = &inTri[0]; inPoly.v1 = &inTri[1]; inPoly.v2 = &inTri[2];

    // --- straddling poly (needs clip) --------------------------------------
    Vertex stTri[3];
    std::memset(stTri, 0, sizeof(stTri));
    SetXYZ(stTri[0], 0.5f, 0.5f, 2.0f);  SetClipByte(stTri[0], 0x01);
    SetXYZ(stTri[1], 2.0f, 0.5f, 2.0f);  SetClipByte(stTri[1], 0x01);
    SetXYZ(stTri[2], 1.0f, 2.0f, 0.0f);  SetClipByte(stTri[2], 0x01);
    Polygon stPoly{}; stPoly.v0 = &stTri[0]; stPoly.v1 = &stTri[1]; stPoly.v2 = &stTri[2];

    // Sorted draw list: entry[0] sort key smaller, entry[1] larger. The flush
    // walks back-to-front (idx = count-1 .. 0). Both must be drawn.
    std::vector<DrawListEntry> entries(2);
    entries[0] = DrawListEntry{0x00000001u, &inPoly};
    entries[1] = DrawListEntry{0x00000002u, &stPoly};
    MeshList list{entries.data(), 2};

    SpanDispatch disp;
    ClipContext ctx{1, &kNear};
    ClipScratch sc;
    std::memset(&sc, 0, sizeof(sc));

    Surface* fb = SurfaceCreate(64, 64, 8);
    CHECK(fb != nullptr);
    std::memset(fb->pixels, 0, (size_t)fb->pitch * fb->height);

    int drawn = RasterizeMeshList(list, fb, disp, ctx, proj, sc);
    CHECK_EQ(drawn, 2);

    // --- verify clipped geometry of the straddling poly --------------------
    // tri straddles z>=1: v0(z2),v1(z2) inside ; v2(z0) outside -> 4 survivors.
    // python golden:
    //   v0=(0.5,0.5,2) v1=(2,0.5,2)  edge v1->v2 interp at z=1: (1.5,1.25,1)
    //   edge v2->v0 interp at z=1: (0.75,1.25,1)
    CHECK_EQ(sc.outCount, 4);
    // pool vertex 0 = interp on v1->v2
    CHECK(std::fabs(sc.newVerts[0].x - 1.5f)  < 1e-5f);
    CHECK(std::fabs(sc.newVerts[0].y - 1.25f) < 1e-5f);
    CHECK(std::fabs(sc.newVerts[0].z - 1.0f)  < 1e-5f);
    // pool vertex 1 = interp on v2->v0
    CHECK(std::fabs(sc.newVerts[1].x - 0.75f) < 1e-5f);
    CHECK(std::fabs(sc.newVerts[1].y - 1.25f) < 1e-5f);
    CHECK(std::fabs(sc.newVerts[1].z - 1.0f)  < 1e-5f);

    // survivors reprojected: v0(0.5,0.5,2): r=0.5 -> sx=20*0.5*0.5+20=25 ; sy=25
    CHECK(std::fabs(stTri[0].screenX - 25.0f) < 1e-3f);
    CHECK(std::fabs(stTri[0].screenY - 25.0f) < 1e-3f);
    // v1(2,0.5,2): sx=20*2*0.5+20=40 ; sy=25
    CHECK(std::fabs(stTri[1].screenX - 40.0f) < 1e-3f);
    CHECK(std::fabs(stTri[1].screenY - 25.0f) < 1e-3f);
    // pool v0 (1.5,1.25,1): r=1 -> sx=20*1.5+20=50 ; sy=20*1.25+20=45
    CHECK(std::fabs(sc.newVerts[0].screenX - 50.0f) < 1e-3f);
    CHECK(std::fabs(sc.newVerts[0].screenY - 45.0f) < 1e-3f);

    // --- verify framebuffer pixels -----------------------------------------
    // The inside triangle (light 200) covers a centroid region; sample a pixel
    // well inside it and confirm it got the shade value 200 written.
    // Centroid ~ ((4+30+16)/3, (4+4+28)/3) = (16.6, 12).
    bool anyDrawn = false;
    for (int y = 0; y < 64 && !anyDrawn; ++y)
        for (int x = 0; x < 64; ++x)
            if (fb->pixels[y * fb->pitch + x] != 0) { anyDrawn = true; break; }
    CHECK(anyDrawn);

    // The inside triangle is affine-shaded with constant light 200, so its
    // interior pixels equal 200.
    CHECK_EQ((int)fb->pixels[12 * fb->pitch + 16], 200);

    SurfaceDestroy(fb);
}

// E2E: an all-inside list never invokes the clipper (outCount stays 0) and the
// direct dispatch still rasterizes every entry.
TEST(RenderClipE2E, AllInsideUsesDirectPath) {
    Vertex tri[3];
    std::memset(tri, 0, sizeof(tri));
    tri[0].screenX = 4;  tri[0].screenY = 4;  ((u8*)&tri[0])[66] = 111;
    tri[1].screenX = 28; tri[1].screenY = 4;  ((u8*)&tri[1])[66] = 111;
    tri[2].screenX = 16; tri[2].screenY = 26; ((u8*)&tri[2])[66] = 111;
    SetXYZ(tri[0], 4, 4, 5); SetXYZ(tri[1], 28, 4, 5); SetXYZ(tri[2], 16, 26, 5);
    Polygon poly{}; poly.v0 = &tri[0]; poly.v1 = &tri[1]; poly.v2 = &tri[2];

    DrawListEntry e{0x04000000u, &poly};  // key>>24 == 4 -> opaque slot
    MeshList list{&e, 1};
    SpanDispatch disp;
    ClipContext ctx{1, &kNear};
    ProjectScalars proj{1, 0, 1, 0};
    ClipScratch sc; std::memset(&sc, 0, sizeof(sc));

    Surface* fb = SurfaceCreate(48, 32, 8);
    std::memset(fb->pixels, 0, (size_t)fb->pitch * fb->height);
    int drawn = RasterizeMeshList(list, fb, disp, ctx, proj, sc);
    CHECK_EQ(drawn, 1);
    CHECK_EQ(sc.outCount, 0);                 // clipper never ran
    CHECK_EQ((int)fb->pixels[10 * fb->pitch + 16], 111);
    SurfaceDestroy(fb);
}
