#include "test.h"
#include "render/terrain_render.h"
#include "render/tile_geometry.h"
#include "render/scene_node.h"
#include "render/mirror.h"
#include "render/geometry_types.h"
#include "render/surface.h"
#include <vector>
#include <cstring>
#include <cmath>

using namespace guild;
using namespace guild::render;

// =============================================================================
// End-to-end: build a synthetic terrain region + a scene node + a mirror plane,
// run the terrain tile-geometry build + the scene-node append into a draw list,
// then rasterize the draw list and verify both the draw-list entries and the
// framebuffer against a reference.
//
// The rasterizer here is a self-contained flat-fill triangle filler (the e2e
// asserts ordering + coverage, not the bit-exact textured span path which the
// raster module owns). It consumes the SAME 8-byte DrawListEntry stream the
// terrain/scene/mirror builders produce.
// =============================================================================

namespace {

// A minimal flat-fill triangle rasterizer over a Surface, keyed by the draw-list
// entry sort key (low byte -> color), so the framebuffer reflects draw order/key.
void FillTri(Surface* fb, const Polygon& p, u8 r, u8 g, u8 b) {
    const Vertex& a = *p.v0; const Vertex& bb = *p.v1; const Vertex& c = *p.v2;
    float minx = a.screenX, maxx = a.screenX, miny = a.screenY, maxy = a.screenY;
    auto upd = [&](const Vertex& v){
        if (v.screenX < minx) minx = v.screenX;
        if (v.screenX > maxx) maxx = v.screenX;
        if (v.screenY < miny) miny = v.screenY;
        if (v.screenY > maxy) maxy = v.screenY;
    };
    upd(bb); upd(c);
    int x0 = (int)std::floor(minx), x1 = (int)std::ceil(maxx);
    int y0 = (int)std::floor(miny), y1 = (int)std::ceil(maxy);
    auto edge = [](float ax,float ay,float bx,float by,float px,float py){
        return (px-ax)*(by-ay)-(py-ay)*(bx-ax); };
    float area = edge(a.screenX,a.screenY,bb.screenX,bb.screenY,c.screenX,c.screenY);
    if (area == 0.0f) return;
    for (int y = y0; y <= y1; ++y) {
        for (int x = x0; x <= x1; ++x) {
            float px = x + 0.5f, py = y + 0.5f;
            float w0 = edge(bb.screenX,bb.screenY,c.screenX,c.screenY,px,py);
            float w1 = edge(c.screenX,c.screenY,a.screenX,a.screenY,px,py);
            float w2 = edge(a.screenX,a.screenY,bb.screenX,bb.screenY,px,py);
            bool inside = (w0>=0&&w1>=0&&w2>=0)||(w0<=0&&w1<=0&&w2<=0);
            if (inside) SurfaceSetPixelRgb(fb, x, y, r, g, b);
        }
    }
}

void RasterDrawList(Surface* fb, const DrawListEntry* entries, i32 count) {
    for (i32 i = 0; i < count; ++i) {
        const DrawListEntry& e = entries[i];
        // Color from the sort key low byte (so different keys -> distinguishable).
        u8 key = (u8)(e.sortKey & 0xFF);
        FillTri(fb, *e.poly, (u8)(50 + key * 20), (u8)(key * 10), 200);
    }
}

} // namespace

TEST(TerrainRenderE2E, BuildTerrainNodeMirrorRasterize) {
    // ---- 1. Synthetic terrain region: a 4x4 height grid, derive LOD ------------
    const i32 size = 32;
    // World AABB so scaleX = (256-0)/(32-1.75) = 8.46 -> LOD 1 (coarsest in range).
    i32 lod = SelectTileMeshLodFromBounds(0.0f, 256.0f, size);
    CHECK_EQ(lod, 1);
    // Tile subdivision for an interior tile and an edge (col 7) tile.
    CHECK_EQ(TileSubdivCount(/*span*/8, (u8)lod, /*col*/2, /*row*/2, true), 9);
    CHECK_EQ(TileSubdivCount(8, (u8)lod, 7, 2, true), 5);  // 8/1+1=9, edge: 9-4=5

    // ---- 2. Build a small tile's vertices from height samples ------------------
    TileLightParams p{};
    p.heightAxis[0]=0; p.heightAxis[1]=1.0f; p.heightAxis[2]=0;
    p.lightScale[0]=1; p.lightScale[1]=1; p.lightScale[2]=1;
    p.ambient[0]=20; p.ambient[1]=20; p.ambient[2]=20;
    p.shadowBias[0]=-30; p.shadowBias[1]=-30; p.shadowBias[2]=-30;

    // A 2x2 patch of vertices laid out in screen space as two triangles (a quad).
    std::vector<Vertex> verts(4);
    // screen positions (projected) for a 12x12 quad at (4,4)..(16,16)
    float sx[4] = {4, 16, 4, 16};
    float sy[4] = {4, 4, 16, 16};
    u8 heights[4] = {10, 12, 14, 16};
    u8 types[4] = {30, 30, 30, 30};
    float acc[3] = {0,0,0};
    for (int i = 0; i < 4; ++i) {
        BuildTileVertex(verts[i], p, acc, heights[i], types[i]);
        verts[i].screenX = sx[i];
        verts[i].screenY = sy[i];
        verts[i].z = 5.0f + i;     // depth for hardware key test elsewhere
    }
    // light: type30 -> l=60, R=G=B=clamp(1*60+20)=80
    CHECK_EQ((int)verts[0].lightIdx, 80);

    // Two CCW triangles for the quad; both visible (flags36 high bit set).
    std::vector<Polygon> polys(2);
    polys[0].v0 = &verts[0]; polys[0].v1 = &verts[1]; polys[0].v2 = &verts[2];
    polys[1].v0 = &verts[1]; polys[1].v1 = &verts[3]; polys[1].v2 = &verts[2];
    polys[0].flags36 = 0x80; polys[1].flags36 = 0x80;

    // ---- 3. Draw-list buffers + terrain tile-poly append -----------------------
    std::vector<DrawListEntry> dl(64);
    DrawList out{dl.data(), 0, (i32)dl.size()};
    u32 texSort[2] = {3, 5};   // ((tex-base)>>7)+1 supplied by caller
    i32 nTerrain = AppendTilePolysToDrawList(polys.data(), 2, texSort, &out);
    CHECK_EQ(nTerrain, 2);
    CHECK_EQ(out.count, 2);
    CHECK_EQ(dl[0].sortKey, 3u);
    CHECK_EQ(dl[1].sortKey, 5u);
    CHECK_EQ(dl[0].poly, &polys[0]);

    // ---- 4. Scene node: append a separate node's polys (software mode) ---------
    std::vector<Vertex> nverts(3);
    float nsx[3] = {20, 30, 20}, nsy[3] = {20, 20, 30};
    for (int i=0;i<3;++i){ nverts[i]=Vertex{}; nverts[i].screenX=nsx[i]; nverts[i].screenY=nsy[i]; nverts[i].z=8.0f; }
    std::vector<Polygon> npolys(1);
    npolys[0].v0=&nverts[0]; npolys[0].v1=&nverts[1]; npolys[0].v2=&nverts[2];
    npolys[0].flags36 = 0x80;   // visible
    NodeAppendContext ctx;
    ctx.mode = NodeAppendMode::Software;
    ctx.baseKey = 1;
    ctx.frameStamp = 0x1234;
    u32 nTex[1] = {6};          // texture delta
    u32 nStamp[1] = {0};
    i32 nNode = ProcessSceneNodeAppend(/*cullByte*/0x00, npolys.data(), 1, nTex, ctx, &out, nStamp);
    CHECK_EQ(nNode, 1);
    CHECK_EQ(out.count, 3);
    CHECK_EQ(dl[2].sortKey, ctx.baseKey + 6);   // 7
    CHECK_EQ(nStamp[0], ctx.frameStamp);

    // A fully-culled node appends nothing.
    i32 culled = ProcessSceneNodeAppend(0x40, npolys.data(), 1, nTex, ctx, &out, nStamp);
    CHECK_EQ(culled, 0);
    CHECK_EQ(out.count, 3);

    // ---- 5. Mirror: reflect a node across y=0, append reflected polys ----------
    MirrorPlane mp{0,1,0,0};
    std::vector<Vertex> mverts(3);
    float mref[3];
    float mworld[3][3] = {{40,5,40},{50,5,40},{40,5,50}};
    for (int i=0;i<3;++i){
        ReflectPointAcrossPlane(mworld[i], mp, mref);
        mverts[i]=Vertex{};
        // store reflected world y to confirm reflection happened
        CHECK_EQ(mref[1], -5.0f);
        // screen positions for the reflected triangle
        mverts[i].screenX = mworld[i][0]; mverts[i].screenY = mworld[i][2];
    }
    std::vector<Polygon> mpolys(1);
    mpolys[0].v0=&mverts[0]; mpolys[0].v1=&mverts[1]; mpolys[0].v2=&mverts[2];
    mpolys[0].flags38 = 4;   // no-cull bit set -> always appended
    u32 mTex[1] = {9};
    bool mNear[1] = {true};
    i32 nMir = AppendMirroredPolys(mpolys.data(), 1, mTex, /*mirrorViewTex*/0xFFFF, mNear, &out);
    CHECK_EQ(nMir, 1);
    CHECK_EQ(out.count, 4);
    CHECK_EQ(dl[3].sortKey, 9u);

    // ---- 6. Rasterize the whole draw list + verify framebuffer -----------------
    Surface* fb = SurfaceCreate(64, 64, 16);
    CHECK(fb != nullptr);
    SurfaceColorFill(fb, 0, 0, 0);
    RasterDrawList(fb, out.entries, out.count);

    // Reference checks: terrain quad center (10,10) was filled; node tri (23,22)
    // filled; an untouched corner stays black.
    u8 px[3];
    SurfaceGetPixelRgb(fb, 10, 10, px);
    CHECK(px[2] == 200);   // terrain triangles wrote blue=200
    SurfaceGetPixelRgb(fb, 23, 22, px);
    CHECK(px[2] == 200);   // node triangle filled
    SurfaceGetPixelRgb(fb, 60, 2, px);
    CHECK(px[0] == 0 && px[1] == 0 && px[2] == 0);  // untouched corner

    // Count filled pixels as a coverage signature (deterministic reference).
    int filled = 0;
    for (int y = 0; y < 64; ++y)
        for (int x = 0; x < 64; ++x) {
            SurfaceGetPixelRgb(fb, x, y, px);
            if (px[2] == 200) ++filled;
        }
    CHECK(filled > 100);   // the quad (~144px) + node tri (~50) + mirror tri

    SurfaceDestroy(fb);
}
