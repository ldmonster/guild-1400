#include "test.h"

// =============================================================================
// Unit goldens — terrain-ground wave 4 reconstructions:
//   * BuildTerrainUvTable          (ComputeFilterWeights @0x5b94cc, 24 floats)
//   * BuildTileIlluminationTable   (ComputeTileIllumination @0x5c4718 build half)
//   * BuildLitTileGeometry         (@0x5c47dc, all three fill modes)
//   * BuildFloorGroundFromBlock    (0x5bd44c placement + 0x5e7d28 origin override)
//   * FloorGroundWorldY            (the floor-lattice ground sample)
//   * GroundFrame::Bind/Render     (the 0x5bf22c walk + 0x5AEC88 flush, headless)
// All expected values are hand-computed from the captured decompiles.
// =============================================================================
#include "play/city_view3d.h"        // BuildEngineFrustum / AimCamera
#include "play/terrain_render.h"
#include "render/heightmap.h"
#include "render/surface.h"
#include "render/terrain_uvtable.h"
#include "render/tile_lighting.h"

#include <cmath>
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::render;
using namespace guild::play;

// ---------------------------------------------------------------------------
// BuildTerrainUvTable @0x5b94cc — golden for the shipped mip tile size 64.
// e = 1/64 = 0.015625, f = 1 - 2e = 0.96875, e+f = 0.984375 (all exact in f32).
// ---------------------------------------------------------------------------
TEST(TerrainGround, UvTableGolden64) {
    float t[kTerrainUvTableSize];
    BuildTerrainUvTable(t, 64);
    const float e = 0.015625f, ef = 0.984375f;
    const float want[24] = {
        e, ef,  e, e,   ef, e,    // T0 (the TL-BR split, tri 0)
        ef, e,  ef, ef, e, ef,    // T1
        e, e,   ef, ef, e, ef,    // T2 (the alternate diagonal, tri 0)
        e, e,   ef, e,  ef, ef,   // T3
    };
    for (int i = 0; i < 24; ++i)
        CHECK(t[i] == want[i]);

    // mip size 4 (the SetMipFilterLevel @0x5b9e74 lower clamp): e=0.25, f=0.5.
    BuildTerrainUvTable(t, 4);
    CHECK(t[0] == 0.25f);
    CHECK(t[1] == 0.75f);
    CHECK(t[2] == 0.25f);
    CHECK(t[3] == 0.25f);
}

// ---------------------------------------------------------------------------
// BuildTileIlluminationTable @0x5c4718 — pattern-index goldens (the 0x5c4690
// table; AUGSBURG's shipped slot names among them).
// ---------------------------------------------------------------------------
TEST(TerrainGround, IlluminationTableBuild) {
    TileLightSource names[8] = {};
    std::strcpy(names[0].name, "SAND");           // pattern 2
    std::strcpy(names[1].name, "EINFACHER_WEG");  // contains WEG -> 11
    std::strcpy(names[2].name, "WIESE");          // 4
    std::strcpy(names[3].name, "FELS");           // 8
    std::strcpy(names[4].name, "WASSER");         // 10
    std::strcpy(names[5].name, "PRUNK_WEG");      // WEG -> 11
    std::strcpy(names[6].name, "XYZ");            // nothing -> empty pattern 14
    // names[7] empty -> the slot default 1

    TileIlluminationTable t = BuildTileIlluminationTable(names);
    CHECK_EQ((int)t.value[0], 2);
    CHECK_EQ((int)t.value[1], 11);
    CHECK_EQ((int)t.value[2], 4);
    CHECK_EQ((int)t.value[3], 8);
    CHECK_EQ((int)t.value[4], 10);
    CHECK_EQ((int)t.value[5], 11);
    CHECK_EQ((int)t.value[6], 14);   // pattern 14 == "" always matches
    CHECK_EQ((int)t.value[7], 1);    // empty name keeps the default

    // case-insensitive through the StrToUpper leg; ERDE/MOOR/PFLASTER/KIESEL/EIS.
    TileLightSource more[8] = {};
    std::strcpy(more[0].name, "erde");
    std::strcpy(more[1].name, "Moor");
    std::strcpy(more[2].name, "pflaster_dunkel");
    std::strcpy(more[3].name, "KIESELSTRAND");
    std::strcpy(more[4].name, "EISFELD");   // contains EIS (9) before FELS? scan
                                            // order is by PATTERN index: FELS=8
                                            // is checked BEFORE EIS=9 and
                                            // "EISFELD" does NOT contain "FELS"
                                            // ("SFELD" != "FELS") -> EIS == 9
    TileIlluminationTable t2 = BuildTileIlluminationTable(more);
    CHECK_EQ((int)t2.value[0], 3);
    CHECK_EQ((int)t2.value[1], 5);
    CHECK_EQ((int)t2.value[2], 6);
    CHECK_EQ((int)t2.value[3], 7);
    CHECK_EQ((int)t2.value[4], 9);

    // ComputeTileIllumination lookup: high-bit grid byte -> 0 (hole/unlit).
    u8 grid[4] = {0, 1, 0x82, 3};
    CHECK_EQ((int)ComputeTileIllumination(grid, 2, 0, 1, t), 0);   // 0x82 cell
    CHECK_EQ((int)ComputeTileIllumination(grid, 2, 1, 0, t), 11);  // slot 1
}

// ---------------------------------------------------------------------------
// gilde.exe 0x5c4690 — the 15 terrain-class match patterns (9-byte stride),
// recovered byte-for-byte via get_bytes(0x5c4690, 9*15). Direct byte-pin of the
// table the strstr loop scans (src/render/tile_lighting.cpp:24). Pattern index ==
// the stored illumination class; index 14 is the empty always-match tail.
// ---------------------------------------------------------------------------
TEST(TerrainGround, TerrainTypePatternsTableBytes) {
    const char* want[15] = {
        "_ill*", "_unk*", "SAND", "ERDE", "WIESE", "MOOR", "PFLASTER", "KIESEL",
        "FELS", "EIS", "WASSER", "WEG", "WEG", "_ill*", "",
    };
    for (int k = 0; k < 15; ++k)
        CHECK(std::strcmp(kTerrainTypePatterns[k], want[k]) == 0);
    // 9-byte stride: each entry is a fixed 9-char cell.
    CHECK_EQ((int)sizeof(kTerrainTypePatterns), 15 * 9);
    CHECK_EQ((int)sizeof(kTerrainTypePatterns[0]), 9);
}

// ---------------------------------------------------------------------------
// BuildLitTileGeometry @0x5c47dc — equal-size fill (r == 1).
//   elev = (int)(((i16)h * scaleH + originY - hmOriginY) * (1/hmScaleY))
// ---------------------------------------------------------------------------
TEST(TerrainGround, LitTileGeometryEqualSize) {
    const i32 n = 8;
    std::vector<u8> heights((size_t)n * n), grid((size_t)n * n);
    for (i32 i = 0; i < n * n; ++i) {
        heights[(size_t)i] = (u8)(i * 3);
        grid[(size_t)i] = (u8)(i & 7);
    }
    TileLightSource names[8] = {};
    std::strcpy(names[0].name, "SAND");    // class 2
    std::strcpy(names[1].name, "WIESE");   // class 4
    // slots 2..7 empty -> class 1
    TileIlluminationTable illum = BuildTileIlluminationTable(names);

    std::vector<u8> hmHeights((size_t)n * n, 0xCD);
    std::vector<u8> hmEntries(24u * (size_t)n * n, 0xCD);
    Heightmap hm{};
    hm.size = n;
    hm.originY = 10.0f;
    hm.scaleY = 2.0f;
    hm.heights = hmHeights.data();
    hm.entries = hmEntries.data();

    LitFloorView fl;
    fl.size = n;
    fl.heights = heights.data();
    fl.texGrid = grid.data();
    fl.originY = 4.0f;    // floor+148
    fl.scaleH = 1.5f;     // floor+196

    BuildLitTileGeometry(fl, &hm, illum);

    // cell 0: h=0 -> (0*1.5 + 4 - 10)/2 = -3 -> byte 0xFD (low byte of -3).
    CHECK_EQ((int)hmHeights[0], 0xFD);
    // cell 10: h=30 -> (45 + 4 - 10)/2 = 19.5 -> trunc 19.
    CHECK_EQ((int)hmHeights[10], 19);
    // entries: grid byte 0 -> class 2 (SAND), 1 -> 4 (WIESE), 2..7 -> 1.
    CHECK_EQ((int)hmEntries[24 * 0], 2);
    CHECK_EQ((int)hmEntries[24 * 1], 4);
    CHECK_EQ((int)hmEntries[24 * 2], 1);
    CHECK_EQ((int)hmEntries[24 * 9], 4);   // cell 9: grid 9&7 = 1 -> WIESE
}

// ---------------------------------------------------------------------------
// BuildLitTileGeometry @0x5c47dc — upsample (hm 2x the floor): the direct fill
// at stride r plus the in-place midpoint pyramid (avg4 + the /5 edge blends).
// ---------------------------------------------------------------------------
TEST(TerrainGround, LitTileGeometryUpsample) {
    const i32 n = 4, S = 8;             // r = 2
    std::vector<u8> heights((size_t)n * n), grid((size_t)n * n, 0);
    // identity Y mapping: scaleH=1, originY == hmOriginY, scaleY=1 -> elev == h.
    for (i32 i = 0; i < n * n; ++i)
        heights[(size_t)i] = (u8)(10 * i);
    TileLightSource names[8] = {};
    std::strcpy(names[0].name, "MOOR");  // class 5 for every cell (grid all 0)
    TileIlluminationTable illum = BuildTileIlluminationTable(names);

    std::vector<u8> hmHeights((size_t)S * S, 0);
    std::vector<u8> hmEntries(24u * (size_t)S * S, 0);
    Heightmap hm{};
    hm.size = S;
    hm.originY = 0.0f;
    hm.scaleY = 1.0f;
    hm.heights = hmHeights.data();
    hm.entries = hmEntries.data();

    LitFloorView fl;
    fl.size = n;
    fl.heights = heights.data();
    fl.texGrid = grid.data();
    fl.originY = 0.0f;
    fl.scaleH = 1.0f;

    BuildLitTileGeometry(fl, &hm, illum);

    // direct fill at stride 2: hm(0,0)=h(0,0)=0, hm(2,0)=h(1,0)=10, hm(0,2)=40.
    CHECK_EQ((int)hmHeights[0], 0);
    CHECK_EQ((int)hmHeights[2], 10);
    CHECK_EQ((int)hmHeights[2 * S], 40);
    // pyramid cell A=(0,0), r=2: A=0 B=hm(0,2)=40 C=hm(2,0)=10 D=hm(2,2)=50.
    //   avg4 = (0+40+10+50)>>2 = 25
    //   top mid  hm(1,0) = (25 + 2*(C+A))/5 = (25+20)/5 = 9
    //   left mid hm(0,1) = (25 + 2*(B+A))/5 = (25+80)/5 = 21
    //   centre   hm(1,1) = 25
    CHECK_EQ((int)hmHeights[1], 9);
    CHECK_EQ((int)hmHeights[S], 21);
    CHECK_EQ((int)hmHeights[S + 1], 25);
    // entries: every touched cell carries class 5 (the A copy).
    CHECK_EQ((int)hmEntries[24 * 1], 5);
    CHECK_EQ((int)hmEntries[24 * (S + 1)], 5);
}

// ---------------------------------------------------------------------------
// BuildLitTileGeometry @0x5c47dc — downsample (floor 2x the hm): ratio^2 box
// average of truncated elevations + 15-bin majority illumination vote.
// ---------------------------------------------------------------------------
TEST(TerrainGround, LitTileGeometryDownsample) {
    const i32 n = 8, S = 4;             // ratio = 2
    std::vector<u8> heights((size_t)n * n), grid((size_t)n * n, 0);
    for (i32 y = 0; y < n; ++y)
        for (i32 x = 0; x < n; ++x)
            heights[(size_t)y * n + x] = (u8)(x + y);
    // the top-left 2x2 block votes 3:1 for slot 1 (class 4) over slot 0 (class 2)
    grid[0] = 0; grid[1] = 1; grid[(size_t)n] = 1; grid[(size_t)n + 1] = 1;
    TileLightSource names[8] = {};
    std::strcpy(names[0].name, "SAND");    // 2
    std::strcpy(names[1].name, "WIESE");   // 4
    TileIlluminationTable illum = BuildTileIlluminationTable(names);

    std::vector<u8> hmHeights((size_t)S * S, 0);
    std::vector<u8> hmEntries(24u * (size_t)S * S, 0);
    Heightmap hm{};
    hm.size = S;
    hm.originY = 0.0f;
    hm.scaleY = 1.0f;
    hm.heights = hmHeights.data();
    hm.entries = hmEntries.data();

    LitFloorView fl;
    fl.size = n;
    fl.heights = heights.data();
    fl.texGrid = grid.data();
    fl.originY = 0.0f;
    fl.scaleH = 1.0f;

    BuildLitTileGeometry(fl, &hm, illum);

    // hm(0,0) = (h(0,0)+h(1,0)+h(0,1)+h(1,1))/4 = (0+1+1+2)/4 = 1.
    CHECK_EQ((int)hmHeights[0], 1);
    // hm(1,0) block {2,3,3,4} -> 3 ; hm(0,1) block {2,3,3,4} -> 3.
    CHECK_EQ((int)hmHeights[1], 3);
    CHECK_EQ((int)hmHeights[S], 3);
    // majority vote: 3 cells class 4 vs 1 cell class 2 -> 4.
    CHECK_EQ((int)hmEntries[0], 4);
    // an all-slot-0 block -> class 2 everywhere.
    CHECK_EQ((int)hmEntries[24 * 3], 2);
}

// ---------------------------------------------------------------------------
// BuildFloorGroundFromBlock — the 0x5bd44c placement + 0x5e7d28 origin override
// + the @0x5bd8ab.. texture-grid min-normalization.
// ---------------------------------------------------------------------------
static SceneFloorBlock MakeBlock(i32 n, bool withOrigin) {
    SceneFloorBlock b;
    b.ok = true;
    b.headerOk = true;
    b.floorPresent = true;
    b.gridN = (u32)n;
    b.cellScale = 2.0f;
    b.heightScale = 3.0f;
    b.heights.accepted = true;
    b.heights.elemSize = (u32)n;
    b.heights.count = (u32)n;
    b.heights.data.assign((size_t)n * n, 0);
    for (i32 i = 0; i < n * n; ++i)
        b.heights.data[(size_t)i] = (u8)(i & 0xFF);
    b.textureGrid.accepted = true;
    b.textureGrid.data.assign((size_t)n * n, 0);
    for (i32 i = 0; i < n * n; ++i)
        b.textureGrid.data[(size_t)i] = (u8)(3 + (i & 3));   // min 3, max 6
    b.typeNameCount = 8;
    b.typeNames[0] = "SAND";
    b.typeNames[1] = "WIESE";
    if (withOrigin) {
        b.hasOrigin = true;
        b.origin[0] = 11.0f; b.origin[1] = 22.0f; b.origin[2] = 33.0f;
    }
    return b;
}

TEST(TerrainGround, FloorGroundFromBlock) {
    FloorGround g = BuildFloorGroundFromBlock(MakeBlock(16, /*withOrigin=*/false));
    CHECK(g.valid());
    CHECK_EQ(g.size, 16);
    CHECK_EQ(g.tileSpan, 2);
    // DeriveFloorPlacement(16, 2, 3): originX = -16, originY = 3*-64 = -192,
    // originZ = 16; axes (2,0,0)/(0,0,-2)/(0,3,0).
    CHECK(g.origin[0] == -16.0f);
    CHECK(g.origin[1] == -192.0f);   // the verified flt_628AE0 = -64.0
    CHECK(g.origin[2] == 16.0f);
    CHECK(g.axisU[0] == 2.0f && g.axisV[2] == -2.0f && g.axisH[1] == 3.0f);
    // min-normalized grid: min 3 subtracted (cells become 0..3).
    CHECK_EQ((int)g.gridMin, 3);
    CHECK_EQ((int)g.gridMax, 6);
    CHECK_EQ((int)g.texGrid[0], 0);
    CHECK_EQ((int)g.texGrid[1], 1);
    CHECK_EQ(std::strcmp(g.typeNames[0].name, "SAND"), 0);
    CHECK_EQ(std::strcmp(g.typeNames[1].name, "WIESE"), 0);
    CHECK_EQ((int)g.typeNames[7].name[0], 0);

    // the @0x5e7d28 stream origin OVERWRITES floor +144/148/152.
    FloorGround g2 = BuildFloorGroundFromBlock(MakeBlock(16, /*withOrigin=*/true));
    CHECK(g2.valid());
    CHECK(g2.origin[0] == 11.0f && g2.origin[1] == 22.0f && g2.origin[2] == 33.0f);

    // the 0x5bd707 gate: no texture grid -> the engine frees the floor (invalid).
    SceneFloorBlock noTex = MakeBlock(16, false);
    noTex.textureGrid.accepted = false;
    noTex.textureGrid.data.clear();
    CHECK(!BuildFloorGroundFromBlock(noTex).valid());
}

TEST(TerrainGround, FloorGroundWorldYSample) {
    FloorGround g = BuildFloorGroundFromBlock(MakeBlock(16, /*withOrigin=*/false));
    CHECK(g.valid());
    // cell (0,0): world x = originX + 0*2 = -16, z = originZ + 0*-2 = 16.
    float y = 0;
    CHECK(FloorGroundWorldY(g, -16.0f, 16.0f, &y));
    // h(0,0) = 0 -> y = originY + 0*3 = -192.
    CHECK(y == -192.0f);
    // cell (5,2): world x = -16 + 5*2 = -6; z = 16 - 2*2 = 12; h = 2*16+5 = 37.
    CHECK(FloorGroundWorldY(g, -6.0f, 12.0f, &y));
    CHECK(y == -192.0f + 37.0f * 3.0f);
}

// ---------------------------------------------------------------------------
// GroundFrame — the 0x5bf22c walk + 0x5AEC88 flush, headless + deterministic.
// ---------------------------------------------------------------------------
TEST(TerrainGround, GroundFrameRenderHeadless) {
    // A 64-grid floor (tileSpan 8 — the realistic tile span class; tiny spans
    // legitimately tessellate to zero quads at coarse LODs).
    FloorGround g = BuildFloorGroundFromBlock(MakeBlock(64, /*withOrigin=*/false));
    CHECK(g.valid());
    CHECK_EQ(g.tileSpan, 8);

    GroundFrame gf;
    CHECK(gf.Bind(&g));
    CHECK(gf.bound());
    // the flt_13FE540 UV-table image was built at bind (mip size 64).
    CHECK(gf.uvTable()[0] == 0.015625f);

    const int W = 128, H = 96;
    render::Surface* fb = render::SurfaceCreate(W, H, 16);
    CHECK(fb != nullptr);
    render::SurfaceColorFill(fb, 0, 0, 64);

    // An angled engine camera ABOVE the whole field (heights reach -192 + 255*3
    // = 573), aimed at the centre (AimCamera — the documented host aim helper
    // over the engine camera model). WINDING (wave-5): the camera was pulled in
    // from y=900 to y=250 after the row-mirror hack was removed — the genuine
    // @0x5bf22c winding draws the floor in its true (un-mirrored) world position,
    // so the closer above-camera gives the field real on-screen coverage (it is
    // front-facing for the engine signed-area cull without any lattice flip).
    GroundViewParams vp;
    {
        CityCamera3D cam;
        const float eye[3] = {0.0f, 250.0f, -120.0f};
        const float tgt[3] = {0.0f, 0.0f, 0.0f};
        AimCamera(cam, eye, tgt);
        for (int k = 0; k < 3; ++k) { vp.eye[k] = cam.eye[k]; vp.rot[k] = cam.rot[k]; }
    }
    render::Frustum fr{};
    const float halfW = (float)W * 0.5f;
    BuildEngineFrustum((float)W, (float)H, halfW, 1.0f, 5000.0f, fr);
    float planes[6][4];
    for (int pi = 0; pi < 4; ++pi)
        for (int k = 0; k < 4; ++k)
            planes[pi][k] = fr.plane[pi][k];
    planes[4][0] = 0; planes[4][1] = 0; planes[4][2] = 1.0f;  planes[4][3] = 1.0f;
    planes[5][0] = 0; planes[5][1] = 0; planes[5][2] = -1.0f; planes[5][3] = -5000.0f;
    vp.frustum = &fr;
    vp.clipPlanes = planes;
    vp.clipPlaneCount = 6;
    vp.proj.xScale = halfW;  vp.proj.xOffset = halfW;
    vp.proj.yScale = -halfW; vp.proj.yOffset = (float)H * 0.5f;

    GroundRenderStats st = gf.Render(fb, vp, /*frameFlags=*/1);
    CHECK(st.tilesDrawn > 0);
    CHECK(st.appended > 0);
    CHECK(st.rasterTris > 0);
    CHECK(st.vertsBuilt > 0);

    int nonClear = 0;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            u8 px[3];
            render::SurfaceGetPixelRgb(fb, x, y, px);
            if (px[0] != 0 || px[1] != 0 || px[2] != 64) ++nonClear;
        }
    CHECK(nonClear > 100);   // the floor under the camera fills real pixels

    // Determinism: a second render over a re-cleared surface is byte-identical.
    std::vector<u8> snap1;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            u8 px[3];
            render::SurfaceGetPixelRgb(fb, x, y, px);
            snap1.push_back(px[0]); snap1.push_back(px[1]); snap1.push_back(px[2]);
        }
    render::SurfaceColorFill(fb, 0, 0, 64);
    GroundRenderStats st2 = gf.Render(fb, vp, 1);
    CHECK_EQ(st2.appended, st.appended);
    CHECK_EQ(st2.rasterTris, st.rasterTris);
    std::vector<u8> snap2;
    for (int y = 0; y < H; ++y)
        for (int x = 0; x < W; ++x) {
            u8 px[3];
            render::SurfaceGetPixelRgb(fb, x, y, px);
            snap2.push_back(px[0]); snap2.push_back(px[1]); snap2.push_back(px[2]);
        }
    CHECK(snap1 == snap2);

    render::SurfaceDestroy(fb);
}

// =============================================================================
// WAVE-10 HARDENING: degenerate/edge memory-safety coverage for the UV table at
// tiny mip sizes, FloodFillTileType over full/empty/checkerboard type grids, and
// AverageAreaHeight at grid corners. Run under ASAN+UBSAN.
// =============================================================================

// UV-table corner insets at the tiniest mip sizes (and the size==0 guard). At
// size 1 e==1, f==-1, e+f==0 (all in-bounds writes; the 24 floats are fully filled).
TEST(TerrainGroundHardening, UvTableTinyMips) {
    float t[kTerrainUvTableSize];
    BuildTerrainUvTable(t, 1);              // e = 1, f = 1-2 = -1, e+f = 0
    CHECK(t[2] == 1.0f);                    // T0.v1 = (e,e)
    CHECK(t[4] == 0.0f);                    // T0.v2.x = e+f
    BuildTerrainUvTable(t, 2);              // e = 0.5, f = 0, e+f = 0.5
    CHECK(t[2] == 0.5f);
    CHECK(t[4] == 0.5f);
    // size == 0 -> guarded to 1 (no division by zero); all 24 floats still written.
    for (int i = 0; i < 24; ++i) t[i] = -999.0f;
    BuildTerrainUvTable(t, 0);
    CHECK(t[2] == 1.0f);                    // behaves as size 1
    for (int i = 0; i < 24; ++i) CHECK(t[i] != -999.0f);
}

// FloodFillTileType over a FULL grid (all fromType): interior cells flip to toType;
// over an EMPTY grid (no fromType): nothing changes; over a CHECKERBOARD: no
// interior cell has all-4 neighbours fromType/toType so nothing flips. The fill
// only touches interior cells [1,n-1) -> the border row/col is never written (OOB
// guard). All reads stay inside n*n*24.
TEST(TerrainGroundHardening, FloodFillPatterns) {
    const int n = 8;
    auto makeHm = [&](std::vector<u8>& e) -> Heightmap {
        Heightmap hm{}; hm.size = n; hm.entries = e.data(); return hm;
    };
    // FULL grid of fromType=5; toType=9. Interior cells all flip.
    {
        std::vector<u8> e((size_t)n * n * 24, 0);
        for (int i = 0; i < n * n; ++i) e[(size_t)i * 24] = 5;
        Heightmap hm = makeHm(e);
        CHECK_EQ(FloodFillTileType(&hm, 5, 9), n - 1);
        // Interior cell (3,3) flipped; border cell (0,0) untouched.
        CHECK_EQ((int)e[(size_t)(3 * n + 3) * 24], 9);
        CHECK_EQ((int)e[0], 5);
    }
    // EMPTY (no fromType present) -> no change.
    {
        std::vector<u8> e((size_t)n * n * 24, 0);
        for (int i = 0; i < n * n; ++i) e[(size_t)i * 24] = 2;   // all type 2
        Heightmap hm = makeHm(e);
        FloodFillTileType(&hm, 5, 9);                            // fromType 5 absent
        for (int i = 0; i < n * n; ++i) CHECK_EQ((int)e[(size_t)i * 24], 2);
    }
    // CHECKERBOARD of 5 / 0: no interior 5-cell has all 4 neighbours in {5,9}.
    {
        std::vector<u8> e((size_t)n * n * 24, 0);
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
                e[(size_t)(y * n + x) * 24] = (u8)(((x + y) & 1) ? 5 : 0);
        Heightmap hm = makeHm(e);
        FloodFillTileType(&hm, 5, 9);
        for (int y = 0; y < n; ++y)
            for (int x = 0; x < n; ++x)
                CHECK_EQ((int)e[(size_t)(y * n + x) * 24], ((x + y) & 1) ? 5 : 0);
    }
    // Smallest grids: n==1 and n==2 -> interior loop empty, returns n-1, no OOB.
    {
        std::vector<u8> e1(1 * 24, 5);  Heightmap h1{}; h1.size = 1; h1.entries = e1.data();
        CHECK_EQ(FloodFillTileType(&h1, 5, 9), 0);
        std::vector<u8> e2(4 * 24, 5);  Heightmap h2{}; h2.size = 2; h2.entries = e2.data();
        CHECK_EQ(FloodFillTileType(&h2, 5, 9), 1);
    }
}

// AverageAreaHeight at the grid corners (the 8x8 box overlaps the edges, falling
// back to the bilinear sample for off-grid cells). No OOB read of heights[].
TEST(TerrainGroundHardening, AverageAreaHeightCorners) {
    const int n = 8;
    Heightmap hm{}; hm.size = n;
    std::vector<u8> h((size_t)n * n, 10);
    std::vector<u8> e((size_t)n * n * 24, 0);
    hm.heights = h.data(); hm.entries = e.data();
    hm.scaleX = 1.0f; hm.scaleY = 1.0f; hm.scaleZ = 1.0f;
    hm.originX = 0.0f; hm.originY = 0.0f; hm.originZ = 0.0f;
    // Flat height grid (all 10): every fold (bilinear or cell) is ~10.5/10 -> ~10.x.
    float corner0[3] = {0.5f, 0.0f, 0.5f};      // near tile (0,0)
    double a0 = AverageAreaHeight(&hm, corner0);
    CHECK(a0 > 9.0 && a0 < 11.0);
    float cornerN[3] = {7.5f, 0.0f, 7.5f};      // near tile (7,7)
    double aN = AverageAreaHeight(&hm, cornerN);
    CHECK(aN > 9.0 && aN < 11.0);
    // null/zero guards.
    CHECK_EQ(AverageAreaHeight(nullptr, corner0), 0.0);
    Heightmap z{}; z.size = 0;
    CHECK_EQ(AverageAreaHeight(&z, corner0), 0.0);
}
