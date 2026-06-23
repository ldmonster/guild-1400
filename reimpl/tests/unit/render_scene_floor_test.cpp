// Unit tests for render/scene_floor.{h,cpp} — the scene-stream FLOOR block
// (full LoadFloorRegions grammar) + the city Heightmap build.
//   gilde.exe 0x5e7e38  Scene_LoadFromStream (header + floor-flag position)
//   gilde.exe 0x5e67c8  WorldIo_ReadObject   (object skip-walk grammar)
//   gilde.exe 0x5e78a8  WorldIo_LoadFloorRegions (the decompiled block grammar)
//   gilde.exe 0x5dcca0  Bio_ReadArrayQuick   ([elemSize][count][payload] framing)
//   gilde.exe 0x5bd44c  Floor_LoadFromHeightmap (placement / normalize mirrors)
//   gilde.exe 0x5c5610  Heightmap_BuildTerrainMesh (grid scales / originY)
#include "render/scene_floor.h"
#include "render/heightmap.h"
#include "render/scene_load.h"
#include "test.h"

#include <cmath>
#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::render;

namespace {

// Byte-builder for a minimal tag-0x3A6C00BB scene stream.
struct Buf {
    std::vector<u8> b;
    void u8v(u8 v) { b.push_back(v); }
    void u32v(u32 v) { for (int i = 0; i < 4; ++i) b.push_back((u8)(v >> (8 * i))); }
    void f32v(float v) { u32 u; std::memcpy(&u, &v, 4); u32v(u); }
    void vec3(float x, float y, float z) { f32v(x); f32v(y); f32v(z); }
    void str(const char* s) { while (*s) b.push_back((u8)*s++); b.push_back(0); }
};

// The full version-gated header for tag 0x3A6C00BB (camera, fog, 7-light rig
// with colour + 6 keyframes each) — the exact field sequence ParseSceneHeader
// reads (scene_load.h).
void PutHeaderBB(Buf& w) {
    w.u32v(0x3A6C00BBu);            // tag
    w.str("MegaCam");               // camera name
    w.f32v(1.0f);                   // ambient
    w.vec3(0, 0, 0);                // camPos
    w.vec3(0, 0, 1);                // camTarget        (>= B5)
    w.u32v(0);                      // camFlag          (>= B7)
    w.u32v(0);                      // fogColor         (>= B3)
    w.f32v(10.0f);                  // fogNear
    w.f32v(1000.0f);                // fogFar
    for (int i = 0; i < 7; ++i) {   // 7 lights         (>= BA)
        w.vec3(0, 0, 0);            // pos
        w.vec3(1, 1, 1);            // color            (>= B5)
        for (int k = 0; k < 6; ++k) { w.u32v((u32)k); w.f32v(0); w.f32v(0); }
    }
}

// One minimal kind-2 (dummy/locator) object record + empty event bindings —
// exercises the skip-walk (0x5e67c8 grammar) in front of the floor block.
void PutDummyObject(Buf& w, const char* name) {
    w.u8v(1);                       // present
    w.str(name);
    w.u32v(0);                      // ownerId  (>= B2)
    w.u32v(0);                      // +535     (>= AB)
    w.u32v(2);                      // kind = 2 (dummy)
    w.u8v(0);                       // explicit-kind byte (>= A6)
    w.vec3(1, 2, 3);                // +76 pos
    w.vec3(0, 0, 0);                // +132 euler
    w.u8v(0);                       // no +92/+144 block
    for (int i = 0; i < 10; ++i) {  // (>= AF) 10 x (vec3, vec3)
        w.vec3(0, 0, 0); w.vec3(0, 0, 0);
    }
    w.u8v(0);                       // no child
    w.u8v(0);                       // no sibling
    w.u32v(0);                      // event bindings: 0 (>= A7)
}

// A VIBE_Bio_ReadArrayQuick record: [elemSize][count][count*elemSize payload].
void PutArrayQuick(Buf& w, u32 elemSize, u32 count, const std::vector<u8>& payload) {
    w.u32v(elemSize);
    w.u32v(count);
    for (u8 v : payload) w.u8v(v);
}

std::vector<u8> Ramp(std::size_t n, int step) {
    std::vector<u8> v(n);
    for (std::size_t i = 0; i < n; ++i) v[i] = (u8)(step * (int)i);
    return v;
}

// One ver-0x3A6C00BB water-region record (the @0x5e79d3 loop body fields).
void PutWaterRegionBB(Buf& w, const char* texName) {
    w.str(texName);                 // texName
    w.u8v(0xA1);                    // texByteA (read, unused)
    w.u8v(0x35);                    // texByteB
    w.u8v(0x01);                    // texByteC
    w.u8v(0x12); w.u8v(0x34); w.u8v(0xFF); w.u8v(0xFE);  // the 4 flag bytes
    w.f32v(12.5f);                  // rec+20 (ver >= B6)
    w.u32v(0xDEADBEEFu);            // rec+12
    w.u32v(0x0BADF00Du);            // rec+16
    w.f32v(1); w.f32v(2); w.f32v(3); w.f32v(4);   // vecA rec+24
    w.f32v(5); w.f32v(6); w.f32v(7); w.f32v(8);   // vecB rec+40
    w.u32v(0x11223344u);            // tail dword (ver >= B9) -> low byte 0x44
}

} // namespace

// ---------------------------------------------------------------------------
// Heights record behind the object tree — REAL grammar: [name][N dword] then
// the ArrayQuick framing [elemSize][count][payload] (the old "three size
// dwords" were N + the two ArrayQuick length dwords).
// ---------------------------------------------------------------------------
TEST(RenderSceneFloor, ParsesHeightsRecordBehindObjectTree) {
    Buf w;
    PutHeaderBB(w);
    w.u32v(1);                      // objCount
    PutDummyObject(w, "dummy_TEST");
    w.u8v(1);                       // floor flag
    w.str("t_height");              // ctx+0 record name
    w.u32v(4);                      // N (ReadDwordSwapArgs @0x5e790c)
    PutArrayQuick(w, 4, 4, Ramp(16, 10));   // heights: elemSize=4, count=4

    SceneFloorHeights f = ParseSceneFloorHeights(w.b.data(), w.b.size());
    CHECK(f.ok);
    CHECK(f.name == "t_height");
    CHECK_EQ((int)f.sizeX, 4);      // N
    CHECK_EQ((int)f.sizeY, 4);      // ArrayQuick elemSize
    CHECK_EQ((int)f.third, 4);      // ArrayQuick count
    CHECK_EQ((int)f.heights.size(), 16);
    CHECK_EQ((int)f.heights[0], 0);
    CHECK_EQ((int)f.heights[5], 50);
    CHECK_EQ((int)f.heights[15], 150);
}

TEST(RenderSceneFloor, FloorFlagZeroMeansNoFloor) {
    Buf w;
    PutHeaderBB(w);
    w.u32v(0);                      // objCount = 0
    w.u8v(0);                       // floor flag = 0
    SceneFloorHeights f = ParseSceneFloorHeights(w.b.data(), w.b.size());
    CHECK(!f.ok);
    SceneFloorBlock blk = ParseSceneFloorBlock(w.b.data(), w.b.size());
    CHECK(blk.headerOk);
    CHECK(!blk.floorPresent);
    CHECK(!blk.ok);
}

TEST(RenderSceneFloor, TruncatedOrMalformedGridRejected) {
    // truncated height payload (ArrayQuick wants 64 bytes, only 10 present)
    {
        Buf w;
        PutHeaderBB(w);
        w.u32v(0);
        w.u8v(1);
        w.str("t_height");
        w.u32v(8);
        PutArrayQuick(w, 8, 8, Ramp(10, 1));     // 10 of 64 payload bytes
        CHECK(!ParseSceneFloorHeights(w.b.data(), w.b.size()).ok);
        CHECK(!ParseSceneFloorBlock(w.b.data(), w.b.size()).ok);
    }
    // ArrayQuick count mismatch (count != N) — the @0x5dccdb gate: the engine
    // seeks past the payload and gets a NULL array.
    {
        Buf w;
        PutHeaderBB(w);
        w.u32v(0);
        w.u8v(1);
        w.str("t_height");
        w.u32v(8);                               // N = 8
        PutArrayQuick(w, 8, 4, Ramp(32, 1));     // count 4 != N -> rejected
        SceneFloorHeights f = ParseSceneFloorHeights(w.b.data(), w.b.size());
        CHECK(!f.ok);
        CHECK(f.heights.empty());
    }
    // non-square framing (elemSize != N) — wrapper-level reject
    {
        Buf w;
        PutHeaderBB(w);
        w.u32v(0);
        w.u8v(1);
        w.str("t_height");
        w.u32v(8);
        PutArrayQuick(w, 4, 8, Ramp(32, 1));     // elemSize 4 != N
        CHECK(!ParseSceneFloorHeights(w.b.data(), w.b.size()).ok);
    }
    // non-power-of-two grid — wrapper-level reject
    {
        Buf w;
        PutHeaderBB(w);
        w.u32v(0);
        w.u8v(1);
        w.str("t_height");
        w.u32v(6);
        PutArrayQuick(w, 6, 6, Ramp(36, 1));
        CHECK(!ParseSceneFloorHeights(w.b.data(), w.b.size()).ok);
    }
    // bad header tag
    {
        Buf w;
        w.u32v(0x12345678u);
        CHECK(!ParseSceneFloorHeights(w.b.data(), w.b.size()).ok);
        CHECK(!ParseSceneFloorBlock(w.b.data(), w.b.size()).headerOk);
    }
}

// ---------------------------------------------------------------------------
// wave-10 HARDENING (ASAN+UBSAN): degenerate stream inputs to the floor parser.
// ---------------------------------------------------------------------------
TEST(RenderSceneFloor, EmptyAndTinyStreams) {
    // null / empty / sub-header buffers: no read past the end.
    CHECK(!ParseSceneFloorHeights(nullptr, 0).ok);
    CHECK(!ParseSceneFloorBlock(nullptr, 0).headerOk);
    std::vector<u8> tiny = {1, 2, 3};
    CHECK(!ParseSceneFloorHeights(tiny.data(), tiny.size()).ok);
    CHECK(!ParseSceneFloorBlock(tiny.data(), tiny.size()).headerOk);
    // a header but the stream ends right at the object count (truncated).
    {
        Buf w; PutHeaderBB(w);
        SceneFloorBlock b = ParseSceneFloorBlock(w.b.data(), w.b.size());
        CHECK(b.headerOk);
        CHECK(!b.ok);            // ran off the stream before the floor block
    }
}

TEST(RenderSceneFloor, DegenerateGridDimensions) {
    // N == 0: the ArrayQuick payload is 0 bytes; the wrapper rejects (not pow2).
    {
        Buf w; PutHeaderBB(w); w.u32v(0); w.u8v(1);
        w.str("t_height"); w.u32v(0);
        PutArrayQuick(w, 0, 0, {});      // elemSize=0,count=0
        SceneFloorHeights f = ParseSceneFloorHeights(w.b.data(), w.b.size());
        CHECK(!f.ok);                     // N=0 not pow2 (IsPow2(0)==false)
    }
    // N == 1 (power of two, square, tiny): accepted with a 1-byte payload.
    {
        Buf w; PutHeaderBB(w); w.u32v(0); w.u8v(1);
        w.str("t_height"); w.u32v(1);
        PutArrayQuick(w, 1, 1, {42});    // 1x1
        SceneFloorHeights f = ParseSceneFloorHeights(w.b.data(), w.b.size());
        CHECK(f.ok);
        CHECK_EQ((int)f.heights.size(), 1);
        CHECK_EQ((int)f.heights[0], 42);
    }
    // N == 8192 (> 4096 cap): the wrapper rejects oversized grids before the parse
    // would try to materialize a 64 MiB payload (we only frame the lengths, then
    // the payload framing reads as much as the stream provides; the grid-size cap
    // rejects regardless). Frame only the lengths + a short payload (count != N).
    {
        Buf w; PutHeaderBB(w); w.u32v(0); w.u8v(1);
        w.str("t_height"); w.u32v(8192);
        PutArrayQuick(w, 8192, 4, Ramp(16, 1));   // count 4 != 8192 -> seek + null
        SceneFloorHeights f = ParseSceneFloorHeights(w.b.data(), w.b.size());
        CHECK(!f.ok);
    }
    // power-of-two that exceeds the cap with a matching (but empty) frame: rejected
    // by f.sizeX > 4096 even if the ArrayQuick "accepted" (count == N == 0 here is
    // not pow2; use a real >4096 pow2 with a count mismatch handled above). Here we
    // confirm the cap explicitly with a constructed accepted-but-oversized block is
    // unreachable in a finite test; the count-mismatch path above covers the reject.
}

// ---------------------------------------------------------------------------
// FULL floor block golden — every 0x5e78a8 stream field at ver 0x3A6C00BB:
// heights + lightOffsets + water (flag, count, waterHeights, 2 records) +
// texture grid + 8 type names + the float scales + the trailing origin vec3.
// ---------------------------------------------------------------------------
TEST(RenderSceneFloor, FullBlockGoldenParse) {
    const u32 N = 4;
    Buf w;
    PutHeaderBB(w);
    w.u32v(0);                          // objCount = 0
    w.u8v(1);                           // floor flag
    w.str("t_height");                  // ctx+0
    w.u32v(N);                          // ctx+156 N
    PutArrayQuick(w, N, N, Ramp(N * N, 3));            // ctx+64 heights
    PutArrayQuick(w, N, 4 * N, Ramp(4 * N * N, 1));    // ctx+140 lightOffsets (>= BB)
    w.u8v(1);                           // waterFlag (>= AA)
    w.u32v(2);                          // ctx+152 waterRegionCount
    PutArrayQuick(w, N, N, Ramp(N * N, 2));            // ctx+68 waterHeights
    PutWaterRegionBB(w, "Wasser_Teich_Fluss_blau");
    PutWaterRegionBB(w, "");            // empty texName: no texture load @0x5e7a16
    w.str("t_texture");                 // ctx+72
    PutArrayQuick(w, N, N, {5, 6, 7, 8, 5, 6, 7, 8,
                            5, 6, 7, 8, 5, 6, 7, 8}); // ctx+136 textureGrid
    const char* kTypes[8] = {"SAND", "EINFACHER_WEG", "MOOS", "FELS",
                             "WASSER", "ACKER", "PRUNK_WEG", ""};
    for (int i = 0; i < 8; ++i) w.str(kTypes[i]);      // ctx+164 (8 slots, >= B0)
    w.f32v(200.0f);                     // ctx+144 cellScale  (a FLOAT dword)
    w.f32v(2.5f);                       // ctx+148 heightScale
    w.vec3(-400.0f, -100.0f, 400.0f);   // trailing origin (>= 0x3A6C000E)

    SceneFloorBlock b = ParseSceneFloorBlock(w.b.data(), w.b.size());
    CHECK(b.ok);
    CHECK(b.headerOk);
    CHECK(b.floorPresent);
    CHECK_EQ(b.ver, 0x3A6C00BBu);
    CHECK(b.name == "t_height");
    CHECK_EQ((int)b.gridN, (int)N);

    // heights
    CHECK(b.heights.accepted);
    CHECK_EQ((int)b.heights.elemSize, (int)N);
    CHECK_EQ((int)b.heights.count, (int)N);
    CHECK_EQ((int)b.heights.data.size(), (int)(N * N));
    CHECK_EQ((int)b.heights.data[5], 15);
    // lightOffsets: 4*N*N bytes
    CHECK(b.lightOffsets.accepted);
    CHECK_EQ((int)b.lightOffsets.count, (int)(4 * N));
    CHECK_EQ((int)b.lightOffsets.data.size(), (int)(4 * N * N));
    // water
    CHECK_EQ((int)b.waterFlag, 1);
    CHECK_EQ(b.waterRegionCount, 2);
    CHECK(b.waterHeights.accepted);
    CHECK_EQ((int)b.waterHeights.data.size(), (int)(N * N));
    CHECK_EQ((int)b.waterRegions.size(), 2);
    const SceneFloorWaterRegion& wr = b.waterRegions[0];
    CHECK(wr.texName == "Wasser_Teich_Fluss_blau");
    CHECK_EQ((int)wr.texByteA, 0xA1);
    CHECK_EQ((int)wr.texByteB, 0x35);
    CHECK_EQ((int)wr.texByteC, 0x01);
    CHECK_EQ((int)wr.flagBytes[0], 0x12);
    CHECK_EQ((int)wr.flagBytes[1], 0x34);
    CHECK_EQ((int)wr.flagBytes[2], 0xFF);
    CHECK_EQ((int)wr.flagBytes[3], 0xFE);
    // rec+8 = b0 | b1<<8 | ((b2&1)|((b3&1)<<1))<<16 = 0x12 | 0x3400 | 1<<16
    CHECK_EQ(wr.packedFlags, 0x00013412u);
    CHECK(wr.param20 == 12.5f);
    CHECK_EQ(wr.raw12, 0xDEADBEEFu);
    CHECK_EQ(wr.raw16, 0x0BADF00Du);
    CHECK(wr.vecA[0] == 1.0f && wr.vecA[3] == 4.0f);
    CHECK(wr.vecB[0] == 5.0f && wr.vecB[3] == 8.0f);
    CHECK_EQ((int)wr.tail340, 0x44);    // low byte of the tail dword
    CHECK(b.waterRegions[1].texName.empty());
    // texture grid + type names + scales + origin
    CHECK(b.textureName == "t_texture");
    CHECK(b.textureGrid.accepted);
    CHECK_EQ((int)b.textureGrid.data.size(), (int)(N * N));
    CHECK_EQ(b.typeNameCount, 8);
    for (int i = 0; i < 8; ++i) CHECK(b.typeNames[i] == kTypes[i]);
    CHECK(b.cellScale == 200.0f);
    CHECK(b.heightScale == 2.5f);
    CHECK(b.hasOrigin);
    CHECK(b.origin[0] == -400.0f);
    CHECK(b.origin[1] == -100.0f);
    CHECK(b.origin[2] == 400.0f);

    // The compatibility wrapper sees the same heights.
    SceneFloorHeights f = ParseSceneFloorHeights(w.b.data(), w.b.size());
    CHECK(f.ok);
    CHECK(f.heights == b.heights.data);
}

// ---------------------------------------------------------------------------
// Version gates of the body parser (ParseFloorRegions over a raw reader).
// ---------------------------------------------------------------------------
TEST(RenderSceneFloor, FloorVersionGates) {
    // ver < 0x3A6C0009 -> "Incompatible Floor-Versions... Sorry!" (@0x5e78bf)
    {
        Buf w;
        w.str("t_height");
        SceneReader r(w.b.data(), w.b.size());
        SceneFloorBlock b = ParseFloorRegions(r, 0x3A6C0008u);
        CHECK(!b.ok);
    }
    // ver 0x3A6C00AF (>= water 0xAA, < typeNames8 0xB0, < lightOffs 0xBB):
    // empty name skips the grids, waterFlag 0, SIX type names (loc_5E7DDF,
    // end = +0x180), float scales, origin vec3.
    {
        Buf w;
        w.str("");                      // name
        w.u8v(0);                       // waterFlag
        w.str("");                      // textureName
        for (int i = 0; i < 6; ++i) w.str(i == 0 ? "SAND" : "X");
        w.f32v(100.0f);                 // cellScale
        w.f32v(1.5f);                   // heightScale
        w.vec3(1, 2, 3);                // origin
        SceneReader r(w.b.data(), w.b.size());
        SceneFloorBlock b = ParseFloorRegions(r, 0x3A6C00AFu);
        CHECK(b.ok);
        CHECK_EQ(b.typeNameCount, 6);
        CHECK(b.typeNames[0] == "SAND");
        CHECK(b.typeNames[6].empty());
        CHECK(b.cellScale == 100.0f);
        CHECK(b.heightScale == 1.5f);
        CHECK(b.hasOrigin);
        CHECK(b.origin[2] == 3.0f);
        CHECK(!r.eof());
    }
    // ver 0x3A6C00B5 water record: rec+20 defaults to 0x427C0000 (63.0f,
    // @0x5e7dbf) and the tail byte to 0 (@0x5e7dd2); vecA NOT scaled (>= AC).
    {
        Buf w;
        w.str("");                      // name (skip grids)
        w.u8v(1);                       // waterFlag
        w.u32v(1);                      // count
        // (no waterHeights ArrayQuick? ver >= AD reads it — 0xB5 >= 0xAD)
        PutArrayQuick(w, 0, 0, {});     // waterHeights: expected N=0, count=0
        w.str("W");                     // texName
        w.u8v(1); w.u8v(2); w.u8v(3);   // texBytes
        w.u8v(4); w.u8v(5); w.u8v(6); w.u8v(7);  // flag bytes
        // NO rec+20 dword (ver < B6)
        w.u32v(11); w.u32v(22);         // rec+12 / rec+16
        for (int i = 0; i < 8; ++i) w.f32v((float)i);  // vecA + vecB
        // NO tail dword (ver < B9)
        w.str("");                      // textureName
        for (int i = 0; i < 8; ++i) w.str("T");        // 8 names (>= B0)
        w.f32v(1.0f); w.f32v(1.0f);     // scales
        w.vec3(0, 0, 0);                // origin
        SceneReader r(w.b.data(), w.b.size());
        SceneFloorBlock b = ParseFloorRegions(r, 0x3A6C00B5u);
        CHECK(b.ok);
        CHECK_EQ((int)b.waterRegions.size(), 1);
        CHECK(b.waterRegions[0].param20 == 63.0f);     // 0x427C0000
        CHECK_EQ((int)b.waterRegions[0].tail340, 0);
        CHECK(b.waterRegions[0].vecA[3] == 3.0f);      // unscaled (ver >= AC)
    }
}

// ---------------------------------------------------------------------------
// Pure-math mirrors of VIBE_Floor_LoadFromHeightmap @0x5bd44c.
// ---------------------------------------------------------------------------
TEST(RenderSceneFloor, FloorPlacementDerivation) {
    // originX = (double)(-N)*0.5*cellScale ; originY = heightScale*-64.0 ;
    // originZ = 0.5*N*cellScale ; axes ; tileSpan = N/8.
    // RECALIBRATION (terrain-ground wave 4): flt_628AE0 @0x628AE0 is the exact
    // dword 0xC2800000 == -64.0f (get_int: u32le 3263168512, double-confirmed by
    // the rdata float sweep). The earlier expectation pinned the -50.0 misread.
    FloorPlacement p = DeriveFloorPlacement(128, 50.0f, 2.0f);
    CHECK(p.originX == -3200.0f);       // -128 * 0.5 * 50
    CHECK(p.originY == -128.0f);        // 2 * -64
    CHECK(p.originZ == 3200.0f);
    CHECK(p.axisU[0] == 50.0f && p.axisU[1] == 0.0f && p.axisU[2] == 0.0f);
    CHECK(p.axisV[0] == 0.0f && p.axisV[1] == 0.0f && p.axisV[2] == -50.0f);
    CHECK(p.axisH[0] == 0.0f && p.axisH[1] == 2.0f && p.axisH[2] == 0.0f);
    CHECK_EQ(p.tileSpan, 16);
    // signed division (the @0x5bd7c1 sar/sbb idiom)
    CHECK_EQ(DeriveFloorPlacement(-16, 1.0f, 1.0f).tileSpan, -2);
}

TEST(RenderSceneFloor, TextureGridMinNormalization) {
    // @0x5bd8ab..0x5bdc95: min/max scan then every byte -= min.
    u8 g[8] = {5, 9, 7, 5, 6, 9, 8, 5};
    u8 mn = 0, mx = 0;
    NormalizeFloorTextureGrid(g, 8, &mn, &mx);
    CHECK_EQ((int)mn, 5);
    CHECK_EQ((int)mx, 9);
    CHECK_EQ((int)g[0], 0);
    CHECK_EQ((int)g[1], 4);
    CHECK_EQ((int)g[6], 3);
    // empty grid: scan stays at (min=255, max=0), nothing subtracted.
    NormalizeFloorTextureGrid(nullptr, 0, &mn, &mx);
    CHECK_EQ((int)mn, 255);
    CHECK_EQ((int)mx, 0);
}

// BuildCityHeightmapFromFloor: the 0x5c5610 scale derivation over the parsed
// grid + a world AABB, sampled back through the 1:1 TileToWorld (0x5c65d4) and
// WorldToTileWithHeight (0x5c6644).
TEST(RenderSceneFloor, BuildsHeightmapWithEngineScales) {
    SceneFloorHeights f;
    f.ok = true;
    f.name = "t_height";
    f.sizeX = f.sizeY = f.third = 4;
    f.heights = {0, 0, 0, 0,
                 0, 100, 100, 0,
                 0, 100, 255, 0,
                 0, 0, 0, 0};

    const float lo[3] = {-1000.0f, 50.0f, -2000.0f};
    const float hi[3] = {3000.0f, 550.0f, 2000.0f};
    Heightmap hm{};
    std::vector<u8> store;
    CHECK(BuildCityHeightmapFromFloor(f, lo, hi, hm, store));
    CHECK_EQ(hm.size, 4);
    CHECK(hm.heights == store.data());

    // DeriveGridScaleXZ (flt_628BA4 @0x628BA4 = 0xBFE00000 = -1.75):
    //   scaleX = (3000 - -1000) / (4 - 1.75) ; originX = -1000
    //   originZ = 2000 ; scaleZ = (-2000 - 2000) / (4 - 1.75)
    const float denom = 4.0f - 1.75f;
    CHECK(hm.originX == -1000.0f);
    CHECK(hm.originZ == 2000.0f);
    CHECK(std::abs(hm.scaleX - 4000.0f / denom) < 1e-3f);
    CHECK(std::abs(hm.scaleZ - (-4000.0f / denom)) < 1e-3f);
    // originY = minY + 1.0 (@0x5c5b95) ; scaleY = (maxY-minY) * flt_628BA8
    // with the EXACT dword 0x3B81848E (kTerrainScaleYNorm, ~1/253.0).
    CHECK(hm.originY == 51.0f);
    CHECK(hm.scaleY == (float)(500.0 * (double)kTerrainScaleYNorm));
    {   // the constant is the exact binary bit pattern
        u32 bits = 0;
        std::memcpy(&bits, &kTerrainScaleYNorm, 4);
        CHECK_EQ(bits, 0x3B81848Eu);
    }

    // The SceneFloorBlock overload produces the identical mapping.
    SceneFloorBlock blk;
    blk.ok = true;
    blk.gridN = 4;
    blk.heights.elemSize = blk.heights.count = 4;
    blk.heights.accepted = true;
    blk.heights.data = f.heights;
    Heightmap hm2{};
    std::vector<u8> store2;
    CHECK(BuildCityHeightmapFromFloor(blk, lo, hi, hm2, store2));
    CHECK(hm2.scaleY == hm.scaleY);
    CHECK(hm2.originY == hm.originY);
    CHECK(hm2.scaleX == hm.scaleX);

    // TileToWorld at the 255 cell: world.y = 255*scaleY + originY.
    float out[3] = {0, 0, 0};
    CHECK(TileToWorld(&hm, 2, 2, out));
    CHECK(std::abs(out[1] - (255.0f * hm.scaleY + hm.originY)) < 1e-3f);

    // WorldToTileWithHeight at that tile's world XZ lands on the tile and
    // returns a height in the byte range's world span.
    int tx = -1, tz = -1;
    float h = 0;
    const float probe[3] = {out[0], 0, out[2]};
    CHECK(WorldToTileWithHeight(&hm, probe, &tx, &tz, &h));
    CHECK_EQ(tx, 2);
    CHECK_EQ(tz, 2);
    CHECK(h >= hm.originY);
    CHECK(h <= 255.5f * hm.scaleY + hm.originY + 1.0f);
}
