// End-to-end terrain tile-lighting flow: build a synthetic 8x8 terrain region with
// a light source, stamp a dynamic light circle, compute per-tile illumination,
// mip-downsample the elevation grid, animate water (vertices + texture), and verify
// the whole light grid + water positions against a python reference.
#include "tests/framework/test.h"

#include "render/tile_lighting.h"
#include "render/tile_visibility.h"
#include "render/water_anim.h"
#include "render/floorwater.h"

#include <cmath>
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::render;

TEST(RenderTileLightingE2E, TerrainRegionLightFlow) {
    const i32 size = 8;
    // --- build the synthetic terrain type grid (all type 0 == light source 0) ---
    u8 types[64];
    std::memset(types, 0, sizeof(types));
    types[3 * 8 + 3] = 0x80;   // punch a hole at (3,3)

    // --- stamp a dynamic light circle at (4,4), radius 2, r2cap 5 ---
    StampLightCircle(types, size, 4, 4, 2, 5);

    // 13 cells now carry the 0x80 bit (hole + circle; the hole sits in the circle).
    int holes = 0;
    for (int i = 0; i < 64; ++i)
        if (types[i] & 0x80) ++holes;
    CHECK_EQ(holes, 13);

    // --- compute per-tile illumination over the whole grid ---
    TileIlluminationTable tbl;
    u8 vals[8] = {5, 6, 7, 8, 9, 10, 11, 12};
    std::memcpy(tbl.value, vals, 8);

    u8 grid[64];
    long sum = 0;
    int illum5 = 0;
    for (int y = 0; y < size; ++y)
        for (int x = 0; x < size; ++x) {
            u8 v = ComputeTileIllumination(types, size, x, y, tbl);
            grid[y * size + x] = v;
            sum += v;
            if (v == 5) ++illum5;
        }
    // python golden:
    CHECK_EQ(illum5, 51);          // 64 - 13 lit/hole cells
    CHECK_EQ((int)sum, 255);       // 51 * 5
    CHECK_EQ((int)grid[0], 5);     // (0,0) lit by source 0
    CHECK_EQ((int)grid[4 * 8 + 4], 0);  // (4,4) was stamped -> hole -> 0

    // --- build an elevation grid and mip-downsample it (LOD pyramid level 1) ---
    u8 elev[64];
    for (int i = 0; i < 64; ++i)
        elev[i] = BuildTileElevationByte((u8)(i % 16), 1.0f, 0.0f, 0.0f, 1.0f);
    u8 mip[16];
    i32 m = MipDownsample(elev, size, mip);
    CHECK_EQ(m, 4);
    // top-left 2x2 of elev is {0,1,8,9} -> (0+1+8+9)/4 = 4
    CHECK_EQ((int)mip[0], (0 + 1 + 8 + 9) / 4);

    // --- LOD selection + seam stitch across two neighbouring tiles ---
    u8 pend = 0; int ctr = 0;
    u8 lodFar  = ComputeLodLevel(0, true, 5.0f, 2.0f, 1.0f, 0.0f, 100.0f, 2, 0,
                                 &pend, &ctr);
    pend = 0; ctr = 0;
    u8 lodNear = ComputeLodLevel(0, true, 5.0f, 2.0f, 1.0f, 0.0f, 1.0f, 2, 0,
                                 &pend, &ctr);
    CHECK_EQ((int)lodFar, 4);
    CHECK_EQ((int)lodNear, 1);
    // a LOD-4 tile bordering its LOD-1 left neighbour gets stitched down to LOD 2.
    CHECK_EQ((int)StitchTileLod(lodFar, lodNear, 0, 0), 2);
}

TEST(RenderTileLightingE2E, WaterMeshAnimation) {
    // --- one animated water mesh: advance its texture frame + wave vertices ---
    // texture group: 4 members, animation speed selector 1, groupId 7.
    auto findMember = [](i32 groupId, u8 frame) -> u32 {
        return (u32)(1000 + groupId * 100 + frame);
    };
    u32 time = 100;
    u32 curMember = 1700;   // groupId 7 frame 0 == 1000+700+0
    u32 newMember = AnimateWaterTexture(time, curMember, 7, 4, 0x21, findMember);
    // frame = (100/15) % 4 = 2  ->  1000 + 700 + 2 = 1702
    CHECK_EQ(newMember, 1702u);

    // --- wave vertex grid for this mesh ---
    float amp[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    float phase[4] = {0.1f, 0.2f, 0.3f, 0.4f};
    double t = 2.0 * 3.141592653589793;
    float out[64];
    AnimateWaterWaveGrid(out, amp, phase, t);
    // verify the first cell's 4-vec against the python reference.
    CHECK(std::fabs(out[0] - 0.0998334140f) < 1e-5f);
    CHECK(std::fabs(out[1] - 1.9601331949f) < 1e-5f);
    CHECK(std::fabs(out[2] - 1.7954164743f) < 1e-5f);
    CHECK(std::fabs(out[3] - (-1.2293314934f)) < 1e-5f);

    // a faster animation speed (selector 10 -> divisor 1) advances every tick.
    CHECK_EQ((int)WaterTextureFrameIndex(45, 10, 7), 3);   // 45 % 7
}
