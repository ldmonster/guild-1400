#include "test.h"
#include "render/terrain_render.h"
#include "render/tile_geometry.h"
#include "render/scene_node.h"
#include "render/mirror.h"
#include <cstring>

using namespace guild;
using namespace guild::render;

// ---------------------------------------------------------------------------
// LOD selection (VIBE_Heightmap_BuildTerrainMesh clamp + VIBE_Floor_RenderTerrain
// subdivision). Golden values computed in Python against the exact arithmetic.
// ---------------------------------------------------------------------------
TEST(TerrainRenderLod, SelectMeshLodClampRange) {
    // lod = clamp(trunc(trunc(50/scaleX + 0.5)/4), 1, 4)
    CHECK_EQ(SelectTileMeshLod(5.0f), 2);    // 50/5+0.5=10.5 -> 10/4=2
    CHECK_EQ(SelectTileMeshLod(12.5f), 1);   // 4.5 -> 4/4=1
    CHECK_EQ(SelectTileMeshLod(25.0f), 1);   // 2.5 -> 2/4=0 -> clamp 1
    CHECK_EQ(SelectTileMeshLod(50.0f), 1);   // 1.5 -> 1/4=0 -> clamp 1
    CHECK_EQ(SelectTileMeshLod(100.0f), 1);  // 1.0 -> clamp 1
    CHECK_EQ(SelectTileMeshLod(200.0f), 1);  // 0.75 -> 0 -> clamp 1
    CHECK_EQ(SelectTileMeshLod(3.0f), 4);    // 17.166 -> 17/4=4 -> clamp 4
    CHECK_EQ(SelectTileMeshLod(2.0f), 4);    // 25.5 -> 25/4=6 -> clamp 4
}

TEST(TerrainRenderLod, SelectMeshLodFromBounds) {
    // scaleX = (maxX-minX)/(size-1.75)
    CHECK_EQ(SelectTileMeshLodFromBounds(0.0f, 256.0f, 32), 1);   // sx=8.46 -> 6.4 -> 1
    CHECK_EQ(SelectTileMeshLodFromBounds(0.0f, 1024.0f, 32), 1);  // sx=33.8 -> 1.97 -> 1
    // A small map (size 32 over a tiny world) yields the finest LOD.
    CHECK_EQ(SelectTileMeshLodFromBounds(0.0f, 64.0f, 32), 4);    // sx=2.116 -> 24.1 -> 6 -> clamp 4
}

TEST(TerrainRenderLod, TileSubdivCountEdgeStitch) {
    // base = tileSpan/lod + 1; last col/row (idx==7) subtracts 4/lod.
    CHECK_EQ(TileSubdivCount(16, 1, 3, 3, true), 17);  // 16/1+1=17, idx!=7
    CHECK_EQ(TileSubdivCount(16, 2, 7, 3, true), 7);   // 16/2+1=9, idx==7 -> 9-(4/2)=7
    CHECK_EQ(TileSubdivCount(16, 4, 7, 3, true), 4);   // 16/4+1=5, idx==7 -> 5-(4/4)=4
    CHECK_EQ(TileSubdivCount(16, 1, 7, 3, true), 13);  // 16/1+1=17, idx==7 -> 17-(4/1)=13
    // row axis: idx==7 only when rowIndex==7.
    CHECK_EQ(TileSubdivCount(16, 2, 3, 7, false), 7);
    CHECK_EQ(TileSubdivCount(16, 2, 3, 3, false), 9);
    CHECK_EQ(TileSubdivCount(16, 0, 0, 0, true), 0);   // guard: lod==0
}

// ---------------------------------------------------------------------------
// Tile vertex build + per-vertex RGB lighting (VIBE_Floor_RenderTerrain inner loop).
// ---------------------------------------------------------------------------
TEST(TerrainRenderTile, ClampLightByte) {
    CHECK_EQ((int)ClampLightByte(100.5f), 100);
    CHECK_EQ((int)ClampLightByte(300.0f), 255);
    CHECK_EQ((int)ClampLightByte(255.9f), 255);
    CHECK_EQ((int)ClampLightByte(0.0f), 0);
}

TEST(TerrainRenderTile, BuildTileVertexWorldAndLight) {
    TileLightParams p{};
    p.heightAxis[0] = 0.0f; p.heightAxis[1] = 0.5f; p.heightAxis[2] = 0.0f;
    p.lightScale[0] = 1.0f; p.lightScale[1] = 1.0f; p.lightScale[2] = 1.0f;
    p.ambient[0] = 10.0f;   p.ambient[1] = 20.0f;   p.ambient[2] = 30.0f;
    p.shadowBias[0] = -50.0f; p.shadowBias[1] = -50.0f; p.shadowBias[2] = -50.0f;
    float acc[3] = {100.0f, 200.0f, 300.0f};

    Vertex v{};
    // typeByte=50 (high bit clear): l=(50&0x7f)*2=100; R=1*100+10=110, G=120, B=130
    u32 packed = BuildTileVertex(v, p, acc, /*height*/40, /*type*/50);
    CHECK_EQ(v.x, 100.0f);
    CHECK_EQ(v.y, 0.5f * 40.0f + 200.0f);   // 220
    CHECK_EQ(v.z, 300.0f);
    CHECK_EQ((int)v.lightIdx, 110);  // R @+66
    CHECK_EQ((int)v._pad41, 120);    // G @+65
    CHECK_EQ((int)v.color0, 130);    // B @+64
    CHECK_EQ(packed, (u32)130 | (120u << 8) | (110u << 16));

    // typeByte with high bit set (shadowed): 0x80|50 -> type&0x7f=50, l=100,
    // R=1*100 - 50 + 10 = 60, G=70, B=80
    Vertex v2{};
    BuildTileVertex(v2, p, acc, 40, 0x80 | 50);
    CHECK_EQ((int)v2.lightIdx, 60);
    CHECK_EQ((int)v2._pad41, 70);
    CHECK_EQ((int)v2.color0, 80);
}

// ---------------------------------------------------------------------------
// Mirror reflection (VIBE_Mirror_ClipPolygonToPlanes reflect).
// ---------------------------------------------------------------------------
TEST(MirrorReflect, ReflectPointAcrossPlane) {
    MirrorPlane mY{0.0f, 1.0f, 0.0f, 0.0f};   // y = 0
    float in[3] = {1.0f, 5.0f, 2.0f};
    float out[3];
    ReflectPointAcrossPlane(in, mY, out);
    CHECK_EQ(out[0], 1.0f);
    CHECK_EQ(out[1], -5.0f);   // mirrored about y=0
    CHECK_EQ(out[2], 2.0f);

    MirrorPlane mY3{0.0f, 1.0f, 0.0f, 3.0f};  // y = 3
    ReflectPointAcrossPlane(in, mY3, out);
    CHECK_EQ(out[1], 1.0f);    // 5 reflected about y=3 -> 1

    // Reflecting twice returns the original.
    float back[3];
    ReflectPointAcrossPlane(out, mY3, back);
    CHECK_EQ(back[1], 5.0f);
}

TEST(MirrorReflect, ClipReflectedBoxCullAndBounds) {
    // Mirror clip planes: inside when n·P >= d. Use a single plane x >= 0.
    MirrorClipPlane planes[1] = {{1.0f, 0.0f, 0.0f, 0.0f}};
    // 8 corners all at x = -1 (fully outside x>=0) -> culled.
    float outBox[8 * 3];
    for (int c = 0; c < 8; ++c) { outBox[c*3+0] = -1.0f; outBox[c*3+1] = 0.0f; outBox[c*3+2] = 1.0f; }
    float near = 1e10f, far = 0.0f;
    CHECK(!ClipReflectedBox(outBox, 3, planes, 1, &near, &far));

    // Mixed: one corner inside (x=2) -> visible; bounds expand over z.
    float inBox[8 * 3];
    for (int c = 0; c < 8; ++c) { inBox[c*3+0] = -1.0f; inBox[c*3+1] = 0.0f; inBox[c*3+2] = (float)(c+1); }
    inBox[0] = 2.0f;  // first corner inside
    near = 1e10f; far = 0.0f;
    CHECK(ClipReflectedBox(inBox, 3, planes, 1, &near, &far));
    CHECK_EQ(near, 1.0f);   // min z
    CHECK_EQ(far, 8.0f);    // max z
}
