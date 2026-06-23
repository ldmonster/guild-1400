// Unit tests for the terrain tile-lighting / visibility / water-anim modules:
//   render/tile_lighting.{h,cpp}  render/tile_visibility.{h,cpp}
//   render/water_anim.{h,cpp}     (+ render/floorwater.cpp wave grid)
// Golden vectors computed with python3 (see the module report).
#include "tests/framework/test.h"

#include "render/tile_lighting.h"
#include "render/tile_visibility.h"
#include "render/water_anim.h"
#include "render/floorwater.h"
#include "render/heightmap.h"   // Heightmap (BuildLitTileGeometry target)

#include <algorithm>
#include <cmath>
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::render;

// ---------------------------------------------------------------------------
// StampLightCircle — marks the expected lit cells (golden, python).
// ---------------------------------------------------------------------------
TEST(RenderTileLighting, StampCircleMarksCells) {
    u8 types[64] = {0};
    // size 8, centre (4,4), radius 2, r2cap 5 -> 13 lit cells (python).
    StampLightCircle(types, 8, 4, 4, 2, 5);
    int golden[] = {20, 27, 28, 29, 34, 35, 36, 37, 38, 43, 44, 45, 52};
    int count = 0;
    for (int i = 0; i < 64; ++i)
        if (types[i] & 0x80) ++count;
    CHECK_EQ(count, 13);
    for (int g : golden)
        CHECK((types[g] & 0x80) != 0);
    // a cell just outside the circle is untouched.
    CHECK_EQ((int)(types[0] & 0x80), 0);
}

TEST(RenderTileLighting, StampCircleSingleCellWhenRadiusTiny) {
    u8 types[16] = {0};
    StampLightCircle(types, 4, 2, 1, 1, 999);   // r<=1 -> single centre cell
    int count = 0, idx = -1;
    for (int i = 0; i < 16; ++i)
        if (types[i] & 0x80) { ++count; idx = i; }
    CHECK_EQ(count, 1);
    CHECK_EQ(idx, 4 * 1 + 2);   // size*cy + cx
}

// ---------------------------------------------------------------------------
// ComputeTileIllumination — per-tile lookup (golden).
// ---------------------------------------------------------------------------
TEST(RenderTileLighting, TileIlluminationLookup) {
    u8 types[16] = {0, 1, 2, 3, 0x80, 5, 6, 7, 1, 1, 1, 1, 2, 2, 2, 2};
    TileIlluminationTable tbl;
    u8 vals[8] = {10, 11, 12, 13, 14, 15, 16, 17};
    std::memcpy(tbl.value, vals, 8);

    CHECK_EQ((int)ComputeTileIllumination(types, 4, 0, 0, tbl), 10);  // tb=0
    CHECK_EQ((int)ComputeTileIllumination(types, 4, 1, 0, tbl), 11);  // tb=1
    CHECK_EQ((int)ComputeTileIllumination(types, 4, 0, 1, tbl), 0);   // tb=0x80 -> hole
    CHECK_EQ((int)ComputeTileIllumination(types, 4, 5, 0, tbl), 11);  // wrap mask&5=1
}

// ---------------------------------------------------------------------------
// MipDownsample — 2x2 box average (golden).
// ---------------------------------------------------------------------------
TEST(RenderTileLighting, MipDownsampleBoxAverage) {
    u8 src[16] = {10, 20, 30, 40, 50, 60, 70, 80,
                  90, 100, 110, 120, 130, 140, 150, 160};
    u8 dst[4] = {0};
    i32 m = MipDownsample(src, 4, dst);
    CHECK_EQ(m, 2);
    CHECK_EQ((int)dst[0], 35);   // (10+20+50+60)/4
    CHECK_EQ((int)dst[1], 55);   // (30+40+70+80)/4
    CHECK_EQ((int)dst[2], 115);  // (90+100+130+140)/4
    CHECK_EQ((int)dst[3], 135);  // (110+120+150+160)/4
}

// ---------------------------------------------------------------------------
// BuildTileElevationByte (golden).
// ---------------------------------------------------------------------------
TEST(RenderTileLighting, ElevationByte) {
    CHECK_EQ((int)BuildTileElevationByte(100, 0.5f, 10.0f, 5.0f, 2.0f), 110);
    CHECK_EQ((int)BuildTileElevationByte(200, 0.25f, 0.0f, 0.0f, 1.0f), 50);
}

// ---------------------------------------------------------------------------
// StampSlopeLight — slope normal -> falloff intensity (golden).
// ---------------------------------------------------------------------------
TEST(RenderTileLighting, SlopeLightRampX) {
    // slope only in +x; light dir (-0.5,-0.5,0); identity-ramp LUT; fscale 100.
    u8 heights[16] = {0, 2, 4, 6, 0, 2, 4, 6, 0, 2, 4, 6, 0, 2, 4, 6};
    std::vector<float> lut(1024);
    for (int i = 0; i < 1024; ++i) lut[i] = (i / 1023.0f) * 64.0f;
    SlopeLightParams p{};
    p.normalScaleX = 1.0f; p.normalScaleY = 1.0f; p.normalScaleZ = 1.0f;
    p.lightDir[0] = -0.5f; p.lightDir[1] = -0.5f; p.lightDir[2] = 0.0f;
    p.falloffScale = 100.0f;
    u8 accum[16] = {0};
    StampSlopeLight(accum, heights, 4, p, lut.data());
    // python golden: columns 1,2 lit to 127; columns 0,3 unlit (wrap flips slope).
    int golden[16] = {0, 127, 127, 0, 0, 127, 127, 0,
                      0, 127, 127, 0, 0, 127, 127, 0};
    for (int i = 0; i < 16; ++i)
        CHECK_EQ((int)accum[i], golden[i]);
}

// ---------------------------------------------------------------------------
// QuadPolyVisible — full 16-pattern truth table (disasm-exact).
// ---------------------------------------------------------------------------
TEST(RenderTileLighting, QuadPolyTruthTable) {
    // index m: bit0=h0,bit1=h1,bit2=h2,bit3=h3. 0=Unchanged,1=Visible,2=Hidden.
    // Golden derived by SIMULATING the disasm goto-cascade @0x5bc982..0x5bcb30
    // instruction-by-instruction (HARDEN render_08, register map bl=h0, dl(==cl)=h1,
    // dh=h2, bh=h3). The PRIOR golden [0,2,1,0,1,0,2,2,2,1,0,1,0,1,2,0] was a
    // divergence (collapsed-guard mistranslation); this is the binary-exact table.
    int golden[16] = {0, 2, 2, 1, 1, 0, 0, 1, 1, 0, 0, 1, 2, 2, 2, 0};
    for (int m = 0; m < 16; ++m) {
        bool h0 = m & 1, h1 = m & 2, h2 = m & 4, h3 = m & 8;
        CHECK_EQ((int)QuadPolyVisible(h0, h1, h2, h3), golden[m]);
    }
}

// ---------------------------------------------------------------------------
// ComputeLodLevel (golden).
// ---------------------------------------------------------------------------
TEST(RenderTileVisibility, LodLevelDistance) {
    u8 pend = 0; int ctr = 0;
    // far tile -> LOD 4
    CHECK_EQ((int)ComputeLodLevel(0, true, 5.0f, 2.0f, 1.0f, 0.0f, 100.0f, 1, 0,
                                  &pend, &ctr), 4);
    // near tile -> LOD 1
    CHECK_EQ((int)ComputeLodLevel(0, true, 5.0f, 2.0f, 1.0f, 0.0f, 1.0f, 1, 0,
                                  &pend, &ctr), 1);
    // forced via flags
    CHECK_EQ((int)ComputeLodLevel(0x04, true, 0, 0, 1, 0, 0, 0, 0, &pend, &ctr), 1);
    CHECK_EQ((int)ComputeLodLevel(0x10, true, 0, 0, 1, 0, 0, 0, 0, &pend, &ctr), 4);
    // no entries -> 0
    CHECK_EQ((int)ComputeLodLevel(0, false, 0, 0, 1, 0, 0, 0, 0, &pend, &ctr), 0);
}

TEST(RenderTileVisibility, LodLevelDebounce) {
    // prevLod 2, new metric forces 4, but debounce holds prev until 8 frames pass.
    u8 pend = 0; int ctr = 0;
    for (int i = 0; i < 8; ++i) {
        u8 r = ComputeLodLevel(0, true, 5.0f, 2.0f, 1.0f, 0.0f, 100.0f, 1, 2,
                               &pend, &ctr);
        CHECK_EQ((int)r, 2);   // holds previous for the first 8 frames
    }
    // 9th frame (counter becomes 9 > 8) commits the pending LOD.
    u8 r = ComputeLodLevel(0, true, 5.0f, 2.0f, 1.0f, 0.0f, 100.0f, 1, 2,
                           &pend, &ctr);
    CHECK_EQ((int)r, 4);
}

// ---------------------------------------------------------------------------
// StitchTileLod (golden).
// ---------------------------------------------------------------------------
TEST(RenderTileVisibility, StitchSeam) {
    CHECK_EQ((int)StitchTileLod(4, 1, 0, 0), 2);   // left 2:1 seam -> 2
    CHECK_EQ((int)StitchTileLod(1, 0, 4, 0), 2);   // up seam -> 2
    CHECK_EQ((int)StitchTileLod(1, 0, 0, 1), 2);   // min LOD = 1<<1 = 2 -> max
    CHECK_EQ((int)StitchTileLod(4, 4, 4, 0), 4);   // no seam, no min clamp
    CHECK_EQ((int)StitchTileLod(0, 1, 4, 2), 0);   // unset LOD untouched
}

// ---------------------------------------------------------------------------
// ComputeTileCenterRadius — 8-corner average + |centre| (oracle).
// ---------------------------------------------------------------------------
TEST(RenderTileVisibility, CenterRadius) {
    // 8 corners, each 20 floats; put x,y,z at [0],[1],[2].
    float corners[160] = {0};
    for (int i = 0; i < 8; ++i) {
        corners[i * 20 + 0] = (float)(i + 1);   // x: 1..8 sum=36
        corners[i * 20 + 1] = 0.0f;
        corners[i * 20 + 2] = 2.0f;             // z: sum=16
    }
    float c[3];
    float r = ComputeTileCenterRadius(corners, c);
    CHECK(std::fabs(c[0] - 36.0f * 0.125f) < 1e-5f);   // 4.5
    CHECK(std::fabs(c[1] - 0.0f) < 1e-5f);
    CHECK(std::fabs(c[2] - 16.0f * 0.125f) < 1e-5f);   // 2.0
    float exp = std::sqrt(4.5f * 4.5f + 2.0f * 2.0f);
    CHECK(std::fabs(r - exp) < 1e-4f);
}

// ---------------------------------------------------------------------------
// Water texture animation — frame index (golden) + injected member lookup.
// ---------------------------------------------------------------------------
TEST(RenderWaterAnim, FrameIndex) {
    CHECK_EQ((int)WaterTextureFrameIndex(100, 1, 4), 2);   // 100/15 % 4
    CHECK_EQ((int)WaterTextureFrameIndex(100, 5, 3), 0);   // 100/8  % 3
    CHECK_EQ((int)WaterTextureFrameIndex(45, 10, 7), 3);   // 45/1   % 7
}

TEST(RenderWaterAnim, AnimateTextureGuards) {
    auto findMember = [](i32 groupId, u8 frame) -> u32 {
        return (u32)((groupId << 16) | frame);   // encode for verification
    };
    // memberCount 0 -> no change
    CHECK_EQ(AnimateWaterTexture(100, 0xABCDu, 7, 0, 0x01, findMember), 0xABCDu);
    // speed selector 0 -> no change
    CHECK_EQ(AnimateWaterTexture(100, 0xABCDu, 7, 4, 0x00, findMember), 0xABCDu);
    // active: groupId 7, flags114 low nibble 1, members 4, time 100 -> frame 2
    u32 got = AnimateWaterTexture(100, 0xABCDu, 7, 4, 0x21, findMember);
    CHECK_EQ(got, (u32)((7 << 16) | 2));
}

// ---------------------------------------------------------------------------
// Water vertex wave grid (render/floorwater.cpp) — golden positions.
// ---------------------------------------------------------------------------
TEST(RenderWaterAnim, WaveGridPositions) {
    float amp[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    float phase[4] = {0.1f, 0.2f, 0.3f, 0.4f};
    double t = 2.0 * 3.141592653589793;
    float out[64];
    AnimateWaterWaveGrid(out, amp, phase, t);
    // python golden (float32):
    CHECK(std::fabs(out[0] - 0.0998334140f) < 1e-5f);
    CHECK(std::fabs(out[1] - 1.9601331949f) < 1e-5f);
    CHECK(std::fabs(out[2] - 1.7954164743f) < 1e-5f);
    CHECK(std::fabs(out[3] - (-1.2293314934f)) < 1e-5f);
    CHECK(std::fabs(out[20] - 0.5536835194f) < 1e-5f);
    CHECK(std::fabs(out[21] - (-0.5099398494f)) < 1e-5f);
    CHECK(std::fabs(out[22] - 2.8281161785f) < 1e-5f);
    CHECK(std::fabs(out[23] - 3.4585192204f) < 1e-5f);
}

// =============================================================================
// WAVE-10 HARDENING: degenerate/edge memory-safety coverage for the tile-lighting
// cores (StampSlopeLight LUT bound, BuildLitTileGeometry degenerate heights, the
// illumination 15-pattern table bounds, MipDownsample min size, the circle-stamp
// border, illumination size==1). Run under ASAN+UBSAN.
// =============================================================================

// StampSlopeLight falloff-LUT index clamp (WAVE-10 OOB FIX regression). The dot
// `d = dot(normal, lightDir)` indexes the 1024-entry LUT as (int)(d*-1023). With a
// UNIT light dir (engine contract) d in [-1,0) -> idx in [0,1023]. A NON-unit light
// dir (or float rounding past -1) once drove idx to 1024+ -> heap-buffer-overflow
// (ASAN, tile_lighting.cpp:290). The clamp pins idx into the LUT; here a |L|=5 dir
// would index ~5115 -> must NOT read OOB.
TEST(TileLightingHardening, SlopeLightLutIndexClamp) {
    std::vector<u8> accum(64, 0), heights(64, 0);
    SlopeLightParams p{};
    p.normalScaleX = 1.0f; p.normalScaleY = 1.0f; p.normalScaleZ = 1.0f;
    p.lightDir[0] = 0.0f; p.lightDir[1] = -5.0f; p.lightDir[2] = 0.0f;  // |L| = 5
    p.falloffScale = 1.0f;
    std::vector<float> lut(1024, 0.5f);
    StampSlopeLight(accum.data(), heights.data(), 8, p, lut.data());  // no OOB
    // The clamp lands on lut[1023] = 0.5 -> lit = 0.5 -> (int)0.5 = 0 -> |= 0 (no-op).
    CHECK_EQ((int)accum[0], 0);
    // Exact-boundary unit dir d == -1 -> idx == 1023 (last valid entry, in-bounds).
    SlopeLightParams q = p; q.lightDir[1] = -1.0f;
    std::fill(accum.begin(), accum.end(), (u8)0);
    lut[1023] = 254.0f;  // distinct so we know the clamped index was used
    StampSlopeLight(accum.data(), heights.data(), 8, q, lut.data());
    CHECK_EQ((int)accum[0], 127);   // 1*254 -> clamp to 127
}

// MipDownsample at the minimum size: n==2 -> single 2x2 box average; n==0 -> no-op.
TEST(TileLightingHardening, MipDownsampleMinSize) {
    u8 src4[4] = {10, 20, 30, 40};
    u8 dst1[1] = {0};
    CHECK_EQ(MipDownsample(src4, 2, dst1), 1);
    CHECK_EQ((int)dst1[0], (10 + 20 + 30 + 40) >> 2);   // 25
    // n == 0 -> m == 0, no reads/writes.
    u8 dummy[1] = {7};
    CHECK_EQ(MipDownsample(dummy, 0, dummy), 0);
    CHECK_EQ((int)dummy[0], 7);
}

// BuildTileIlluminationTable: every slot exercises the 15-pattern strstr loop; the
// empty pattern (index 14) is the always-match tail, and the stored index is always
// in [0,14] (never OOB into value[]). Empty names default to 1.
TEST(TileLightingHardening, IlluminationTable15PatternBounds) {
    TileLightSource names[8];
    std::memset(names, 0, sizeof(names));
    std::strcpy(names[1].name, "SAND");          // -> pattern 2
    std::strcpy(names[2].name, "WASSER");        // -> pattern 10
    std::strcpy(names[3].name, "ZZZUNMATCHED");  // -> empty pattern tail (14)
    std::strcpy(names[4].name, "FELS");          // -> pattern 8
    TileIlluminationTable t = BuildTileIlluminationTable(names);
    CHECK_EQ((int)t.value[0], 1);    // empty slot default
    CHECK_EQ((int)t.value[1], 2);    // SAND
    CHECK_EQ((int)t.value[2], 10);   // WASSER
    CHECK_EQ((int)t.value[3], 14);   // unmatched -> empty pattern tail
    CHECK_EQ((int)t.value[4], 8);    // FELS
    for (int i = 0; i < 8; ++i) CHECK((int)t.value[i] <= 14);
}

// ComputeTileIllumination at size==1 (mask 0): every (x,y) folds to cell 0, no OOB.
// And the high-bit hole gate returns 0.
TEST(TileLightingHardening, IlluminationSizeOneAndHoleGate) {
    u8 types1[1] = {3};
    TileIlluminationTable il{};
    for (int i = 0; i < 8; ++i) il.value[i] = (u8)(i + 1);
    CHECK_EQ((int)ComputeTileIllumination(types1, 1, 99, 77, il), 4);  // value[3]
    u8 hole[1] = {0x80 | 2};
    CHECK_EQ((int)ComputeTileIllumination(hole, 1, 0, 0, il), 0);      // hole -> 0
}

// BuildLitTileGeometry with degenerate (flat / all-equal) heights across the three
// branches (r==1 equal-size, ratio>1 downsample, r>1 mip pyramid). Tight grids +
// ASAN guarantee the height/entries stores stay in-bounds; the null/zero guards
// short-circuit cleanly.
TEST(TileLightingHardening, BuildLitTileGeometryDegenerate) {
    TileIlluminationTable il{};
    for (int i = 0; i < 8; ++i) il.value[i] = 1;

    // Null/zero guards.
    {
        LitFloorView fl; Heightmap hm{};
        CHECK_EQ(BuildLitTileGeometry(fl, nullptr, il), 0u);     // null hm
        CHECK_EQ(BuildLitTileGeometry(fl, &hm, il), 0u);         // size<=0
    }
    // Branch r==1 (hm.size == floor.size), flat heights.
    {
        LitFloorView fl; fl.size = 4; fl.originY = 0; fl.scaleH = 1.0f;
        std::vector<u8> fh(16, 0), tg(16, 0); fl.heights = fh.data(); fl.texGrid = tg.data();
        Heightmap hm{}; hm.size = 4; hm.originY = 0; hm.scaleY = 1.0f;
        std::vector<u8> hh(16, 0), he(16 * 24, 0); hm.heights = hh.data(); hm.entries = he.data();
        CHECK_EQ(BuildLitTileGeometry(fl, &hm, il), 4u);
    }
    // Branch ratio>1 (floor.size > hm.size), flat heights.
    {
        LitFloorView fl; fl.size = 8; fl.originY = 0; fl.scaleH = 1.0f;
        std::vector<u8> fh(64, 3), tg(64, 0); fl.heights = fh.data(); fl.texGrid = tg.data();
        Heightmap hm{}; hm.size = 4; hm.originY = 0; hm.scaleY = 1.0f;
        std::vector<u8> hh(16, 0), he(16 * 24, 0); hm.heights = hh.data(); hm.entries = he.data();
        CHECK_EQ(BuildLitTileGeometry(fl, &hm, il), 4u);
    }
    // Branch r>1 mip pyramid (hm.size > floor.size), flat heights.
    {
        LitFloorView fl; fl.size = 8; fl.originY = 0; fl.scaleH = 1.0f;
        std::vector<u8> fh(64, 9), tg(64, 0); fl.heights = fh.data(); fl.texGrid = tg.data();
        Heightmap hm{}; hm.size = 32; hm.originY = 0; hm.scaleY = 1.0f;
        std::vector<u8> hh(32 * 32, 0), he(32 * 32 * 24, 0);
        hm.heights = hh.data(); hm.entries = he.data();
        CHECK_EQ(BuildLitTileGeometry(fl, &hm, il), 16u);   // last pyramid level S/r
    }
}

// StampLightCircle border coverage: r<=1 single-cell path at the grid corner, and
// the clamped r>1 span at a border centre (must not read/write outside size*size).
TEST(TileLightingHardening, StampLightCircleBorder) {
    std::vector<u8> types(64, 0);
    // r==0 single-cell at corner (7,7): stamps types[63] only.
    StampLightCircle(types.data(), 8, 7, 7, 0, 0);
    CHECK_EQ((int)(types[7 * 8 + 7] & 0x80), 0x80);
    // r==3 centred on the border (0,0): column/row spans clamp to [0, size-1].
    std::fill(types.begin(), types.end(), (u8)0);
    StampLightCircle(types.data(), 8, 0, 0, 3, 4);   // r2cap 4
    CHECK_EQ((int)(types[0] & 0x80), 0x80);          // centre lit
    // The far corner (7,7) is well outside r2cap -> untouched.
    CHECK_EQ((int)(types[7 * 8 + 7] & 0x80), 0);
}
