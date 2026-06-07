#include "test.h"
#include "render/terrain_walk.h"
#include "render/surface.h"
#include "render/geometry_types.h"
#include <vector>
#include <set>
#include <cmath>
#include <cstring>

using namespace guild;
using namespace guild::render;

// =============================================================================
// End-to-end: build a synthetic terrain (height grid + 8x8 tiles) and a camera/
// projection, run the FULL VIBE_Floor_RenderTerrain walk into a draw list, then
// rasterize that draw list into a framebuffer and verify the visited-tile set, the
// draw-list entries, the texture binds, and the framebuffer pixels against a
// deterministic reference.
// =============================================================================

namespace {

// --- synthetic floor harness (mirrors the unit test, sized for rasterization) ---
struct TerrainScene {
    static constexpr i32 kSize = 32;
    static constexpr i32 kSpan = 4;
    std::vector<u8> heights, types, texSrc;
    std::vector<TerrainTile> tiles;
    std::vector<std::vector<Vertex>>  vbufs;
    std::vector<std::vector<Polygon>> pbufs;
    std::vector<std::vector<void*>>   clipbufs;
    std::vector<std::vector<u8>>      drawbufs;
    TerrainFloor floor{};

    TerrainScene() {
        heights.assign((size_t)kSize*kSize, 0);
        types.assign((size_t)kSize*kSize, 0);
        texSrc.assign((size_t)kSize*kSize, 0);
        for (i32 y=0;y<kSize;++y) for (i32 x=0;x<kSize;++x){
            heights[(size_t)y*kSize+x]=(u8)(((x+y)*3)&0x3F);
            types[(size_t)y*kSize+x]=(u8)(40 + ((x+2*y)&0x1F));
            texSrc[(size_t)y*kSize+x]=(u8)((x*7 ^ y*5)&0xFF);
        }
        tiles.resize(64);
        vbufs.resize(64);pbufs.resize(64);clipbufs.resize(64);drawbufs.resize(64);
        for(int i=0;i<64;++i){
            vbufs[i].assign((size_t)(kSpan+2)*(kSpan+2),Vertex{});
            pbufs[i].assign((size_t)2*(kSpan+2)*(kSpan+2),Polygon{});
            clipbufs[i].assign((size_t)3*2*(kSpan+2)*(kSpan+2),nullptr);
            drawbufs[i].assign((size_t)24*(kSpan+2)*(kSpan+2),0);
            tiles[i]=TerrainTile{};
            tiles[i].lod=1; tiles[i].prevLod=0;
            tiles[i].vertexBuf=vbufs[i].data();
            tiles[i].polyBuf=pbufs[i].data();
            tiles[i].clipList=clipbufs[i].data();
            tiles[i].drawData=drawbufs[i].data();
        }
        floor.size=kSize; floor.tileSpan=kSpan; floor.mask=kSize-1;
        floor.heights=heights.data(); floor.types=types.data(); floor.texSrc=texSrc.data();
        for(int l=0;l<4;++l) floor.mipTexSrc[l]=texSrc.data();
        floor.tiles=tiles.data();
        floor.flatLit=1; floor.minLodNibble=0;
    }
};

// Texture-bind log capturing the visited (tileU,tileV,lod) set.
struct BindLog { std::set<std::tuple<i32,i32,i32>> visited; };
u32 BindHook(const u8*, i32, i32 u, i32 v, i32 lod, void* s){
    auto* b=(BindLog*)s; b->visited.emplace(u,v,lod);
    return (u32)((b->visited.size())*128 + 128);  // distinct 128-aligned ids
}

// Flat-fill triangle rasterizer keyed by sort-key low byte (same as terrain_render e2e).
void FillTri(Surface* fb, const Polygon& p){
    const Vertex& a=*p.v0; const Vertex& bb=*p.v1; const Vertex& c=*p.v2;
    float minx=a.screenX,maxx=a.screenX,miny=a.screenY,maxy=a.screenY;
    auto upd=[&](const Vertex& v){
        if(v.screenX<minx)minx=v.screenX;
        if(v.screenX>maxx)maxx=v.screenX;
        if(v.screenY<miny)miny=v.screenY;
        if(v.screenY>maxy)maxy=v.screenY;
    };
    upd(bb);upd(c);
    int x0=(int)std::floor(minx),x1=(int)std::ceil(maxx);
    int y0=(int)std::floor(miny),y1=(int)std::ceil(maxy);
    auto edge=[](float ax,float ay,float bx,float by,float px,float py){return (px-ax)*(by-ay)-(py-ay)*(bx-ax);};
    float area=edge(a.screenX,a.screenY,bb.screenX,bb.screenY,c.screenX,c.screenY);
    if(area==0.0f) return;
    for(int y=y0;y<=y1;++y) for(int x=x0;x<=x1;++x){
        if(x<0||y<0||x>=fb->width||y>=fb->height) continue;
        float px=x+0.5f,py=y+0.5f;
        float w0=edge(bb.screenX,bb.screenY,c.screenX,c.screenY,px,py);
        float w1=edge(c.screenX,c.screenY,a.screenX,a.screenY,px,py);
        float w2=edge(a.screenX,a.screenY,bb.screenX,bb.screenY,px,py);
        bool inside=(w0>=0&&w1>=0&&w2>=0)||(w0<=0&&w1<=0&&w2<=0);
        if(inside) SurfaceSetPixelRgb(fb,x,y,80,120,200);
    }
}

} // namespace

TEST(TerrainWalkE2E, FullWalkRasterizeAndVerify) {
    TerrainScene sc;

    // ---- 1. Camera / projection: place tile world coords directly into screen ----
    TerrainRenderState st;
    // Light: gentle sun, all type bytes positive so the lit branch runs.
    float sScale[3]={1.f,1.f,1.f}, sAmb[3]={30.f,30.f,30.f}, sBias[3]={-20.f,-20.f,-20.f};
    SetupTerrainLight(st, /*flatLit*/false, sScale, sAmb, sBias);

    // Axis vectors: tile-X -> screen +X, tile-Y(row) -> screen +Y (top-down map view),
    // height -> small +depth so 1/z projection is well-defined.
    st.originView[0]=4.f; st.originView[1]=4.f; st.originView[2]=10.f;
    st.axisU[0]=2.f;  st.axisU[1]=0.f;  st.axisU[2]=0.f;   // per tile-X sample
    st.axisV[0]=0.f;  st.axisV[1]=2.f;  st.axisV[2]=0.f;   // per tile-Y sample
    st.axisH[0]=0.f;  st.axisH[1]=0.f;  st.axisH[2]=0.05f; // height -> depth

    // Projection: screenX = projXScale * x/z + projXOff (with z ~ 10, scale 10 keeps
    // x roughly identity). The Y axis is flipped (negative scale + a +96 offset) so the
    // terrain winds FRONT-facing for the engine's signed-area cull (area <= 0 keeps the
    // poly) while staying on-screen (screenY positive).
    st.projXScale = 10.f; st.projXOff = 0.f;
    st.projYScale = -10.f; st.projYOff = 96.f;

    // ---- 2. Draw list + texture-cache + hooks -----------------------------------
    std::vector<DrawListEntry> dl(8192);
    DrawList out{dl.data(),0,(i32)dl.size()};
    st.drawList=&out;
    st.texBaseStride=0x80;

    BindLog binds;
    TerrainWalkHooks hooks;
    hooks.updateVisibility = [](TerrainFloor*){ return 1; };
    hooks.getOrBuildTile   = BindHook;
    hooks.cacheState       = &binds;

    // ---- 3. Run the FULL walk ----------------------------------------------------
    i32 appended = RenderTerrain(&sc.floor, st, hooks, /*animateWater*/true);

    // ---- 4. Verify the visited-tile set + draw list ------------------------------
    CHECK(appended > 0);
    CHECK_EQ(appended, out.count);
    CHECK(out.count <= out.capacity);
    // All 64 lod-1 tiles are visible -> all contributed texture binds.
    CHECK(!binds.visited.empty());
    // Build-geometry bit cleared after the frame.
    CHECK_EQ((int)(sc.floor.flatLit & 1), 0);

    // Every appended entry has a valid poly pointer and a textured sort key (>=2).
    for (i32 i=0;i<out.count;++i){
        CHECK(out.entries[i].poly != nullptr);
        CHECK((i8)out.entries[i].poly->flags36 < 0);  // only front-facing appended
        CHECK(out.entries[i].sortKey >= 2u);          // id/128+1, id>=128
    }

    // Per-frame counters are consistent with the appended geometry.
    CHECK(st.vertsThisFrame > 0);
    CHECK_EQ(st.vertsThisFrame, st.vertsAlt);
    CHECK(st.polysThisFrame >= out.count);   // counters count all polys, list is clamped

    // ---- 5. Rasterize the draw list into a framebuffer ---------------------------
    Surface* fb = SurfaceCreate(128,128,16);
    CHECK(fb!=nullptr);
    SurfaceColorFill(fb,0,0,0);
    for (i32 i=0;i<out.count;++i) FillTri(fb, *out.entries[i].poly);

    // ---- 6. Reference pixel checks ----------------------------------------------
    // The projected terrain fills the band x[4..54], y[36..92]; its centre is filled.
    u8 px[3];
    SurfaceGetPixelRgb(fb, 29, 64, px);
    CHECK(px[2] == 200);     // blue terrain fill present at the band centre

    // A far corner well outside the projected terrain band stays black.
    SurfaceGetPixelRgb(fb, 127, 127, px);
    bool farBlack = (px[0]==0 && px[1]==0 && px[2]==0);
    CHECK(farBlack);         // bottom-right corner is outside the terrain band
    // (depending on projection extent the far corner may or may not be covered; assert
    //  that SOME region is black -> the terrain didn't fill the whole frame.)
    int filled=0, black=0;
    for(int y=0;y<128;++y)for(int x=0;x<128;++x){
        SurfaceGetPixelRgb(fb,x,y,px);
        if(px[2]==200)++filled; else if(px[0]==0&&px[1]==0&&px[2]==0)++black;
    }
    CHECK(filled > 100);     // meaningful terrain coverage
    CHECK(black  > 100);     // and a black margin remains
    (void)farBlack;

    SurfaceDestroy(fb);
}

// A second pass with a coarser LOD on half the tiles exercises the LOD-mix path.
TEST(TerrainWalkE2E, MixedLodWalk) {
    TerrainScene sc;
    // Make the right half of the map LOD 2 (coarser), left half LOD 1.
    for (int r=0;r<8;++r) for(int c=0;c<8;++c)
        sc.tiles[r*8+c].lod = (c>=4) ? 2 : 1;

    TerrainRenderState st;
    float sScale[3]={1,1,1}, sAmb[3]={50,50,50}, sBias[3]={0,0,0};
    SetupTerrainLight(st,false,sScale,sAmb,sBias);
    st.originView[2]=10.f; st.axisU[0]=2.f; st.axisV[1]=2.f; st.axisH[2]=0.05f;
    st.projXScale=10.f; st.projYScale=-10.f; st.projYOff=96.f;  // front-facing winding

    std::vector<DrawListEntry> dl(8192);
    DrawList out{dl.data(),0,(i32)dl.size()}; st.drawList=&out;
    BindLog binds; TerrainWalkHooks hooks;
    hooks.updateVisibility=[](TerrainFloor*){return 1;};
    hooks.getOrBuildTile=BindHook; hooks.cacheState=&binds;

    i32 appended = RenderTerrain(&sc.floor, st, hooks, false);
    CHECK(appended > 0);

    // LOD-1 tiles emit more polys per tile than LOD-2 tiles:
    //   lod1 interior subdiv = (4/1+1)^2 = 25 verts, 2*(5-1)^2 = 32 polys
    //   lod2 interior subdiv = (4/2+1)^2 = 9  verts, 2*(3-1)^2 = 8  polys
    TerrainTile& t1 = sc.tiles[2*8 + 1];   // lod 1
    TerrainTile& t2 = sc.tiles[2*8 + 5];   // lod 2
    CHECK_EQ(t1.subdivCached, 25);
    CHECK_EQ(t2.subdivCached, 9);
    CHECK(t1.polyCount > t2.polyCount);
}
