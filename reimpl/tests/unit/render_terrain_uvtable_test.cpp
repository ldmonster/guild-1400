// =============================================================================
// Unit tests for the terrain UV-emission subsystem (gilde.exe 0x5b94cc /
// 0x5bf22c). Golden-pinned 1:1:
//   * the 24-float flt_13FE540 corner-inset UV table (BuildTerrainUvTable)
//   * the per-quad UV record select (TerrainQuadUvT0/T1 = poly+16/+56 image)
//   * the per-cell sub-texture id select (TerrainSubTexId, byte_13DCE58 gate)
//   * the seam-UV midpoint blend (TerrainSeamBlendUv, flt_628B48 = 0.5)
// =============================================================================
#include "render/terrain_uvtable.h"
#include "tests/framework/test.h"

using namespace guild;
using namespace guild::render;

// ----- the 24-float corner-inset table (decompiled @0x5b94cc a1[0..23]) -------
TEST(TerrainUvTable, CornerInsetsForMip64) {
    float t[kTerrainUvTableSize];
    BuildTerrainUvTable(t, 64);
    const float e = 1.0f / 64.0f;          // v63
    const float f = 1.0f - e - e;          // v50
    const float ef = e + f;
    // T0: (e,e+f) (e,e) (e+f,e)
    CHECK_EQ(t[0], e);  CHECK_EQ(t[1], ef);
    CHECK_EQ(t[2], e);  CHECK_EQ(t[3], e);
    CHECK_EQ(t[4], ef); CHECK_EQ(t[5], e);
    // T1: (e+f,e) (e+f,e+f) (e,e+f)
    CHECK_EQ(t[6], ef);  CHECK_EQ(t[7], e);
    CHECK_EQ(t[8], ef);  CHECK_EQ(t[9], ef);
    CHECK_EQ(t[10], e);  CHECK_EQ(t[11], ef);
}

// ----- per-quad UV record select (poly+16 = T0 floats 0..5, poly+56 = 6..11) --
TEST(TerrainUvTable, QuadUvRecordsT0T1) {
    float tbl[kTerrainUvTableSize];
    BuildTerrainUvTable(tbl, 64);
    float t0[kTriUvFloats], t1[kTriUvFloats];
    TerrainQuadUvT0(t0, tbl, /*subTexId*/0);
    TerrainQuadUvT1(t1, tbl, /*subTexId*/0);
    for (int i = 0; i < kTriUvFloats; ++i) {
        CHECK_EQ(t0[i], tbl[i]);                 // T0 = floats 0..5
        CHECK_EQ(t1[i], tbl[kTriUvFloats + i]);  // T1 = floats 6..11
    }
}

// subTexId * 0x60 (24-float) stride: a nonzero sub-id offsets into the record.
TEST(TerrainUvTable, SubTexIdStride) {
    CHECK_EQ(TerrainUvBaseIndex(0), 0);
    CHECK_EQ(TerrainUvBaseIndex(1), 24);
    CHECK_EQ(TerrainUvBaseIndex(3), 72);
    // Build a 3-record table and read record #2.
    float big[24 * 4] = {};
    for (int i = 0; i < 24 * 4; ++i) big[i] = (float)i;
    float t0[kTriUvFloats];
    TerrainQuadUvT0(t0, big, 2);
    for (int i = 0; i < kTriUvFloats; ++i) CHECK_EQ(t0[i], (float)(48 + i)); // 2*24
}

// ----- the per-cell sub-texture id selector (byte_13DCE58 gate @0x5c1f78) -----
TEST(TerrainUvTable, SubTexIdSelector) {
    // cellFlag without 0x40 -> always 0 (no sub-texture).
    CHECK_EQ(TerrainSubTexId(0x80, nullptr, 5), 0u);
    // 0x40 set but null table (the all-zero shipped image) -> 0.
    CHECK_EQ(TerrainSubTexId(0x40, nullptr, 5), 0u);
    // 0x40 set + a runtime table -> low 6 bits of byte_13DCE58[(quad&0xFF)].
    u8 tab[256] = {};
    tab[5] = 0xC3;   // 0xC3 & 0x3F == 0x03
    CHECK_EQ(TerrainSubTexId(0x40, tab, 5), 3u);
    // quad index masked by 0xFF (the engine's `and eax, 0FFh`).
    tab[7] = 0x11;
    CHECK_EQ(TerrainSubTexId(0x40, tab, 0x107), 0x11u);
}

// ----- the seam-UV midpoint blend (flt_628B48 = 0.5, @0x5bfd27 / @0x5c2348) ---
TEST(TerrainUvTable, SeamBlendTLBR) {
    // TL-BR: rec[4]=(rec[2]+rec[4])*0.5; rec[5]=(rec[3]+rec[5])*0.5.
    float rec[kTriUvFloats] = {0.1f, 0.2f, 0.3f, 0.4f, 0.7f, 0.8f};
    TerrainSeamBlendUv(rec, /*diagTLBR*/true);
    CHECK_EQ(rec[0], 0.1f); CHECK_EQ(rec[1], 0.2f);   // untouched
    CHECK_EQ(rec[2], 0.3f); CHECK_EQ(rec[3], 0.4f);   // untouched
    CHECK_EQ(rec[4], (0.3f + 0.7f) * 0.5f);
    CHECK_EQ(rec[5], (0.4f + 0.8f) * 0.5f);
}

TEST(TerrainUvTable, SeamBlendBLTR) {
    // BL-TR: rec[0]=(rec[0]+rec[2])*0.5; rec[1]=(rec[1]+rec[3])*0.5.
    float rec[kTriUvFloats] = {0.1f, 0.2f, 0.3f, 0.4f, 0.7f, 0.8f};
    TerrainSeamBlendUv(rec, /*diagTLBR*/false);
    CHECK_EQ(rec[0], (0.1f + 0.3f) * 0.5f);
    CHECK_EQ(rec[1], (0.2f + 0.4f) * 0.5f);
    CHECK_EQ(rec[2], 0.3f); CHECK_EQ(rec[3], 0.4f);   // untouched
    CHECK_EQ(rec[4], 0.7f); CHECK_EQ(rec[5], 0.8f);   // untouched
}

// The blend weight is exactly 0.5 (get_bytes 0x628B48 == 00 00 00 3f).
TEST(TerrainUvTable, SeamBlendWeightIsHalf) {
    CHECK_EQ(kSeamUvBlend, 0.5f);
}
