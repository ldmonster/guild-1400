// Wave-20 W20-SKYCAM golden tests:
//   VIBE_Sky_BuildDomeMesh     @0x5ef980  (render/sky_dome.{h,cpp})
//   VIBE_Heightmap_Create      @0x5c63a0  (render/heightmap_create.{h,cpp})
//   VIBE_Floor_ReloadTextures  @0x5bd2d8  (render/floor_reload.{h,cpp})
//   VIBE_Camera_ComputeWorldTarget @0x4c0864 (play/camera_target.{h,cpp})
//   VIBE_Coord_Push            @0x5d8ae8  (play/camera_target.{h,cpp})
#include "tests/framework/test.h"

#include "render/sky_dome.h"
#include "render/heightmap_create.h"
#include "render/floor_reload.h"
#include "render/floorgfx_recon.h"  // FloorTextureMipName (reused suffix table)
#include "play/camera_target.h"
#include "util/coord.h"

#include <cmath>
#include <cstring>

using namespace guild;
using namespace guild::render;
using namespace guild::play;

namespace {
bool approx(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }
} // namespace

// ---------------------------------------------------------------------------
// Sky dome — constants are bit-exact (verified get_global_value).
// ---------------------------------------------------------------------------
TEST(skydome, constants_bit_exact) {
    CHECK(kSkyColStep == 0.14285714924335480f); // flt_62C148 = 0x3E124925 (1/7)
    CHECK(kSkyRowStep == 0.20000000298023224f); // flt_62C14C = 0x3E4CCCCD (0.2)
    CHECK_EQ(kSkyDomeRows, 6);
    CHECK_EQ(kSkyDomeCols, 8);
}

TEST(skydome, rebuild_false_writes_nothing) {
    SkyDomeInputs in{};
    SkyDomeVertex out[48];
    std::memset(out, 0xAB, sizeof out);
    int n = BuildDome(in, /*rebuild=*/false, out);
    CHECK_EQ(n, 0);
}

TEST(skydome, grid_dimensions_and_uv_accumulators) {
    // A simple sky rect so uStep/vStep are clean: x [0,70], y [0,50].
    //   uStep = (70-0)*(1/7) = 10 ; vStep = (50-0)*0.2 = 10.
    SkyDomeInputs in{};
    // Corner vectors: pick distinct directions so atan2 varies but stays finite.
    in.c30[0] = 1; in.c30[1] = 0; in.c30[2] = 1;   // row-start hi
    in.c10[0] = 0; in.c10[1] = 0; in.c10[2] = 1;   // row-end   hi
    in.c20[0] = 1; in.c20[1] = 0; in.c20[2] = 0;   // row-start lo
    in.c00[0] = 0; in.c00[1] = 0; in.c00[2] = -1;  // row-end   lo
    in.rectXmin = 0; in.rectXmax = 70;
    in.rectYmin = 0; in.rectYmax = 50;

    SkyDomeVertex out[48];
    int n = BuildDome(in, true, out);
    CHECK_EQ(n, 48);                                // 6 rows * 8 cols

    const float uStep = (float)((double)70 * (double)kSkyColStep); // == 10
    const float vStep = (float)((double)50 * (double)kSkyRowStep); // == 10
    CHECK(approx(uStep, 10.0f, 1e-3f));
    CHECK(approx(vStep, 10.0f, 1e-3f));

    // Verify U resets per row and accumulates by uStep; V is row-constant.
    for (int r = 0; r < 6; ++r) {
        float vAcc = (float)0 + (float)r * vStep; // approx; recompute exactly below
        // exact vAcc the loop produces (additive accumulation):
        float vExact = 0.0f; for (int k = 0; k < r; ++k) vExact += vStep;
        for (int c = 0; c < 8; ++c) {
            const SkyDomeVertex& vx = out[r * 8 + c];
            float uExact = 0.0f; for (int k = 0; k < c; ++k) uExact += uStep;
            CHECK(approx(vx.u, uExact));
            CHECK(approx(vx.v, vExact));
            CHECK(vx.one0 == 1.0f);
            CHECK(vx.one1 == 1.0f);
            (void)vAcc;
        }
    }
}

TEST(skydome, longitude_is_atan2_of_normalized_dir) {
    // With c30=c10 and c20=c00 the row lerp is degenerate; the col lerp moves
    // from edgeStart to edgeEnd. Pin one cell's longitude against atan2 directly.
    SkyDomeInputs in{};
    in.c30[0] = 0; in.c30[1] = 0; in.c30[2] = 2;   // edgeStart dir +Z
    in.c10[0] = 0; in.c10[1] = 0; in.c10[2] = 2;
    in.c20[0] = 3; in.c20[1] = 0; in.c20[2] = 0;   // edgeEnd dir +X
    in.c00[0] = 3; in.c00[1] = 0; in.c00[2] = 0;
    in.rectXmin = 0; in.rectXmax = 7; in.rectYmin = 0; in.rectYmax = 5;

    SkyDomeVertex out[48];
    BuildDome(in, true, out);

    // Row 0 (tRow=0): edgeStart=(0,0,2), edgeEnd=(3,0,0). For col c, tCol=c/7,
    // dir = lerp -> normalize -> atan2(x, z).
    for (int c = 0; c < 8; ++c) {
        float t = (float)((double)c * (double)kSkyColStep);
        float dx = (3.0f - 0.0f) * t + 0.0f;
        float dz = (0.0f - 2.0f) * t + 2.0f;
        float len = std::sqrt(dx * dx + dz * dz);
        float nx = dx, nz = dz;
        if (len != 0.0f) { nx = dx / len; nz = dz / len; }
        float expect = (float)std::atan2((double)nx, (double)nz);
        CHECK(approx(out[c].longitude, expect, 1e-4f));
    }
}

// ---------------------------------------------------------------------------
// Heightmap_Create — alloc/dispatch wrapper.
// ---------------------------------------------------------------------------
namespace {
int g_allocCalls = 0;
unsigned long g_lastSizes[4];
void* CountingAlloc(unsigned long size, const char* /*tag*/) {
    if (g_allocCalls < 4) g_lastSizes[g_allocCalls] = size;
    ++g_allocCalls;
    return ::operator new[]((size_t)size, std::nothrow);
}
int g_buildCalls = 0;
void CountingBuild(Heightmap*, int) { ++g_buildCalls; }
} // namespace

TEST(heightmap_create, zero_size_returns_null) {
    SetHeightmapAllocator(nullptr);
    SetHeightmapBuilder(nullptr);
    CHECK(Create(0, 0, 2) == nullptr);
}

TEST(heightmap_create, positive_size_allocs_all_and_builds) {
    g_allocCalls = 0; g_buildCalls = 0;
    SetHeightmapAllocator(&CountingAlloc);
    SetHeightmapBuilder(&CountingBuild);

    const int size = 16;
    Heightmap* hm = Create(/*srcAsset*/123, size, /*flag*/2);
    CHECK(hm != nullptr);
    CHECK_EQ(hm->size, size);
    CHECK_EQ((int)hm->flag2d, 2);
    CHECK(hm->heights != nullptr);
    CHECK(hm->entries != nullptr);
    // alloc 0: record 0x30 ; alloc 1: heights size*size ; alloc 2: entries 24*size*size
    CHECK_EQ((int)g_allocCalls, 3);
    // record alloc: 0x30 in the 32-bit binary; sizeof(Heightmap) on this host
    // (native 8-byte pointers inflate the struct — buffer sizes are unaffected).
    CHECK_EQ((unsigned long)g_lastSizes[0], (unsigned long)sizeof(Heightmap));
    CHECK_EQ((int)g_lastSizes[1], size * size);
    CHECK_EQ((int)g_lastSizes[2], 24 * size * size);
    CHECK_EQ(g_buildCalls, 1);

    SetHeightmapAllocator(nullptr);
    SetHeightmapBuilder(nullptr);
}

TEST(heightmap_create, negative_size_nulls_entries_but_allocs_heights) {
    // 1:1 quirk: sz = abs(size) sizes the buffers; the signed `size <= 0` branch
    // nulls entries; OOM guard uses the original size. negative -> entries null.
    g_allocCalls = 0; g_buildCalls = 0;
    SetHeightmapAllocator(&CountingAlloc);
    SetHeightmapBuilder(&CountingBuild);

    Heightmap* hm = Create(0, -8, 1);
    CHECK(hm != nullptr);
    CHECK_EQ(hm->size, 8);                  // abs(-8)
    CHECK(hm->heights != nullptr);          // heights still allocated (sz*sz)
    CHECK(hm->entries == nullptr);          // size <= 0 -> entries = 0
    CHECK_EQ((int)g_allocCalls, 2);         // record + heights only
    CHECK_EQ(g_buildCalls, 1);              // build still runs (heights present)

    SetHeightmapAllocator(nullptr);
    SetHeightmapBuilder(nullptr);
}

// ---------------------------------------------------------------------------
// Camera_ComputeWorldTarget — flag-branched projection.
// ---------------------------------------------------------------------------
TEST(camera_target, constants_bit_exact) {
    CHECK(kCamHalf == 0.5f);      // flt_61E510
    CHECK(kCamOffsetY == 6.0f);   // flt_61E514
    CHECK(kCamOffsetX == 10.0f);  // flt_61E518
    CHECK(kCamOne == 1.0f);       // flt_62D224
}

TEST(camera_target, ortho_path_arithmetic_shift_17) {
    CameraTargetGlobals g{};
    g.packedX = 0x12340000;  // >>17 -> arithmetic
    g.packedY = -0x00020000; // negative: arithmetic shift keeps sign
    ComputeWorldTarget(g, /*mask*/0x00);  // no 0x40 -> ortho path
    CHECK_EQ(g.targetX, 0x12340000 >> 17);
    CHECK_EQ(g.targetY, (i32)(-0x00020000) >> 17);
}

TEST(camera_target, persp_plain_truncates) {
    CameraTargetGlobals g{};
    g.viewHalfX = 100.0f; g.viewHalfY = 50.0f;
    g.centerX = 7.9f; g.centerY = -3.9f;
    // px = 100*0.5 + 7.9 = 57.9 -> trunc 57 ; py = 50*0.5 - 3.9 = 21.1 -> trunc 21
    ComputeWorldTarget(g, 0x40);  // 0x40 set, 0x100 clear
    // A4 = trunc(py) ; A0 = trunc(px)
    CHECK_EQ(g.targetX, 21);   // py truncated
    CHECK_EQ(g.targetY, 57);   // px truncated
}

TEST(camera_target, persp_negative_truncates_toward_zero) {
    CameraTargetGlobals g{};
    g.viewHalfX = -3.0f; g.viewHalfY = -3.0f;
    g.centerX = 0.5f; g.centerY = 0.5f;
    // px = -1.5 + 0.5 = -1.0 -> trunc -1 ; py = -1.5 + 0.5 = -1.0 -> trunc -1
    // Use values that exercise toward-zero: make px = -1.9 -> -1
    g.viewHalfX = -4.8f; g.centerX = 0.0f; // px = -2.4 -> trunc -2
    g.viewHalfY = -4.8f; g.centerY = 0.0f; // py = -2.4 -> trunc -2
    ComputeWorldTarget(g, 0x40);
    CHECK_EQ(g.targetX, -2);
    CHECK_EQ(g.targetY, -2);
}

TEST(camera_target, persp_offset_submode) {
    CameraTargetGlobals g{};
    g.viewHalfX = 100.0f; g.viewHalfY = 50.0f;
    g.centerX = 0.0f; g.centerY = 0.0f;
    // px = 50 ; py = 25. offset: A4 = trunc(py - 6) = 19 ; A0 = trunc(px + 10) = 60
    ComputeWorldTarget(g, 0x40 | 0x100);
    CHECK_EQ(g.targetX, 19);
    CHECK_EQ(g.targetY, 60);
}

// ---------------------------------------------------------------------------
// Coord_Push — store order + return value.
// ---------------------------------------------------------------------------
TEST(coord_push, stores_in_binary_order_and_returns_a) {
    CoordState s{};
    i32 r = CoordPush(s, 11, 22, 33, 44);
    CHECK_EQ(r, 11);
    CHECK_EQ(s.a, 11);  // dword_64A1B4
    CHECK_EQ(s.b, 22);  // dword_64A1B8
    CHECK_EQ(s.c, 33);  // dword_64A1BC
    CHECK_EQ(s.d, 44);  // dword_64A1C0
}

// ---------------------------------------------------------------------------
// Floor_ReloadTextures — iteration / name-build (decode boundary mocked).
// ---------------------------------------------------------------------------
namespace {
int g_loadCalls = 0;
std::string g_lastPath;
bool MockLoad(const char* path, u8*, int flags) {
    ++g_loadCalls;
    g_lastPath = path;
    CHECK_EQ(flags, 17);
    return true;
}
std::string MockBuild(const std::string& name) { return "RES:" + name; }

const char* SlotName(void*, int slot) {
    static char buf[8][16];
    std::snprintf(buf[slot], sizeof buf[slot], "floor%d", slot);
    return buf[slot];
}
u8 g_surfacePixels[1];
u8* SurfaceAll(void*, int, int) { return g_surfacePixels; }
u8* SurfaceNone(void*, int, int) { return nullptr; }
int g_invalidateCalls = 0;
void MockInvalidate(void*) { ++g_invalidateCalls; }
} // namespace

TEST(floor_reload, null_floor_early_out) {
    FloorTileAccess acc{};
    CHECK_EQ(ReloadTextures(nullptr, acc), 0);
}

TEST(floor_reload, eight_slots_three_mips_all_loaded) {
    g_loadCalls = 0; g_invalidateCalls = 0;
    SetFloorBmpLoader(&MockLoad);
    SetFloorBmpPathBuilder(&MockBuild);

    FloorTileAccess acc{};
    acc.slotTemplateName = &SlotName;
    acc.tileSurfaceBuffer = &SurfaceAll;
    acc.invalidate = &MockInvalidate;

    int floorObj = 1; // any non-null
    int attempted = ReloadTextures(&floorObj, acc);
    CHECK_EQ(attempted, 8 * 3);          // 8 slots * 3 mip layers
    CHECK_EQ(g_loadCalls, 8 * 3);
    CHECK_EQ(g_invalidateCalls, 1);
    // last load: slot 7, mip 2 -> "floor7_high_2"
    CHECK(g_lastPath == "RES:floor7_high_2");

    SetFloorBmpLoader(nullptr);
    SetFloorBmpPathBuilder(nullptr);
}

TEST(floor_reload, missing_surface_skips_load) {
    g_loadCalls = 0; g_invalidateCalls = 0;
    SetFloorBmpLoader(&MockLoad);
    SetFloorBmpPathBuilder(&MockBuild);

    FloorTileAccess acc{};
    acc.slotTemplateName = &SlotName;
    acc.tileSurfaceBuffer = &SurfaceNone;   // no surface -> never loads
    acc.invalidate = &MockInvalidate;

    int floorObj = 1;
    int attempted = ReloadTextures(&floorObj, acc);
    CHECK_EQ(attempted, 0);
    CHECK_EQ(g_loadCalls, 0);
    CHECK_EQ(g_invalidateCalls, 1);

    SetFloorBmpLoader(nullptr);
    SetFloorBmpPathBuilder(nullptr);
}

TEST(floor_reload, mip_suffix_matches_table) {
    // The 3 suffixes come from kFloorMipSuffix ("", "_high_1", "_high_2").
    CHECK(FloorTextureMipName("x", 0) == "x");
    CHECK(FloorTextureMipName("x", 1) == "x_high_1");
    CHECK(FloorTextureMipName("x", 2) == "x_high_2");
}
