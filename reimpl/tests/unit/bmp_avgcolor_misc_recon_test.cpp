// Golden tests for VIBE_Bmp_GetAverageColor kernel (bmp_avgcolor_misc_recon).
#include "tests/framework/test.h"
#include "render/bmp_avgcolor_misc_recon.h"

using namespace guild;
using guild::render::Bmp_GetAverageColor;

TEST(MiscReconBmpAvg, WeightedAverageGolden) {
    // 4 pixels: indices [0,0,1,2]; palette e0=(10,20,30) e1=(40,50,60) e2=(70,80,90).
    u8 palette[256 * 3] = {0};
    palette[0] = 10; palette[1] = 20; palette[2] = 30;
    palette[3] = 40; palette[4] = 50; palette[5] = 60;
    palette[6] = 70; palette[7] = 80; palette[8] = 90;
    u8 indices[4] = {0, 0, 1, 2};
    u8 out[3] = {0xAA, 0xAA, 0xAA};
    u8 ok = Bmp_GetAverageColor(indices, 4, palette, out);
    CHECK_EQ(ok, static_cast<u8>(1));
    // acc0 = (10*2 + 40 + 70)/4 = 32.5 -> trunc 32 -> out[2]
    // acc1 = (20*2 + 50 + 80)/4 = 42.5 -> 42 -> out[1]
    // acc2 = (30*2 + 60 + 90)/4 = 52.5 -> 52 -> out[0]
    CHECK_EQ(out[0], static_cast<u8>(52));
    CHECK_EQ(out[1], static_cast<u8>(42));
    CHECK_EQ(out[2], static_cast<u8>(32));
}

TEST(MiscReconBmpAvg, SolidColourEqualsThatColour) {
    u8 palette[256 * 3] = {0};
    palette[15] = 100; palette[16] = 150; palette[17] = 200; // entry 5
    u8 indices[9];
    for (int i = 0; i < 9; ++i) indices[i] = 5;
    u8 out[3] = {0};
    CHECK_EQ(Bmp_GetAverageColor(indices, 9, palette, out), static_cast<u8>(1));
    CHECK_EQ(out[2], static_cast<u8>(100));
    CHECK_EQ(out[1], static_cast<u8>(150));
    CHECK_EQ(out[0], static_cast<u8>(200));
}

TEST(MiscReconBmpAvg, RejectsEmptyOrNull) {
    u8 palette[256 * 3] = {0};
    u8 idx[1] = {0};
    u8 out[3] = {0};
    CHECK_EQ(Bmp_GetAverageColor(idx, 0, palette, out), static_cast<u8>(0));
    CHECK_EQ(Bmp_GetAverageColor(nullptr, 4, palette, out), static_cast<u8>(0));
    CHECK_EQ(Bmp_GetAverageColor(idx, 1, nullptr, out), static_cast<u8>(0));
    CHECK_EQ(Bmp_GetAverageColor(idx, 1, palette, nullptr), static_cast<u8>(0));
}
