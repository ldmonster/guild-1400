// Golden-vector unit tests for the wave-5 TILE TEXTURE reconstructions:
//   0x5bd010  VIBE_Floor_LoadTexture           (slot resolution + mip path)
//   0x5ba1e8  VIBE_TextureCache_GetOrBuildTile (mask-wrapped sub-block + texelMask)
//   0x5c5530  VIBE_Heightmap_FloodFillTileType (single-pass 4-neighbour fill)
//   FloorTextureResolver                       (slot id -> slot name -> texture)
//
// Every expected value is computed directly from the gilde.exe decompile (the
// per-line addresses are cited in the implementation). Suite prefix: TileTex.
#include "test.h"

#include "render/floorgfx_recon.h"
#include "render/heightmap.h"   // FloodFillTileType @0x5c5530 (reused, not redefined)
#include "render/texture.h"
#include "render/texture_asset.h"
#include "render/tile_geometry.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::render;

// ===========================================================================
// 0x5bd010 — slot resolution
// ===========================================================================
TEST(TileTex, FloorTextureResolveSlot_Explicit) {
    char names[8][64];
    std::memset(names, 0, sizeof(names));
    // requested >= 0 -> returned verbatim (when < 8).
    CHECK_EQ(FloorTextureResolveSlot(names, 0), 0);
    CHECK_EQ(FloorTextureResolveSlot(names, 5), 5);
    CHECK_EQ(FloorTextureResolveSlot(names, 7), 7);
    // requested >= 8 -> -1 (the @0x5bd03a guard).
    CHECK_EQ(FloorTextureResolveSlot(names, 8), -1);
    CHECK_EQ(FloorTextureResolveSlot(names, 99), -1);
}

TEST(TileTex, FloorTextureResolveSlot_FirstEmptyScan) {
    char names[8][64];
    std::memset(names, 0, sizeof(names));
    // a3 < 0: all slots empty -> first empty is slot 0 (the do/while never enters).
    CHECK_EQ(FloorTextureResolveSlot(names, -1), 0);

    // Fill slots 0..2 -> first empty is slot 3.
    std::strcpy(names[0], "SAND");
    std::strcpy(names[1], "WIESE");
    std::strcpy(names[2], "WEG");
    CHECK_EQ(FloorTextureResolveSlot(names, -1), 3);

    // Fill all 8 -> the scan walks off the end: slot count reaches 8 -> -1.
    for (int i = 3; i < 8; ++i) std::strcpy(names[i], "X");
    CHECK_EQ(FloorTextureResolveSlot(names, -1), -1);
}

// ===========================================================================
// 0x5bd010 — mip-layer name build (suffix table dword_5B8C90)
// ===========================================================================
TEST(TileTex, FloorTextureMipName) {
    CHECK(FloorTextureMipName("SAND", 0) == "SAND");          // suffix "" (0x5B8C90)
    CHECK(FloorTextureMipName("SAND", 1) == "SAND_high_1");   // 0x5B8C9B
    CHECK(FloorTextureMipName("SAND", 2) == "SAND_high_2");   // 0x5B8CA6
    CHECK(FloorTextureMipName("SAND", 3) == "SAND");          // out of range -> base
    CHECK(std::strcmp(kFloorMipSuffix[0], "") == 0);
    CHECK(std::strcmp(kFloorMipSuffix[1], "_high_1") == 0);
    CHECK(std::strcmp(kFloorMipSuffix[2], "_high_2") == 0);
}

// ===========================================================================
// 0x5ba1e8 — mask-wrapped sub-block sample
// ===========================================================================
TEST(TileTex, TileCacheSampleSubBlock_NoWrap) {
    // 4x4 source grid, sample a 2x2 block (span=1) at (1,1) — no wrap.
    u8 src[16];
    for (int i = 0; i < 16; ++i) src[i] = (u8)i;   // src[y*4+x] = y*4+x
    u8 dst[4] = {0,0,0,0};
    TileCacheSampleSubBlock(src, 4, /*x0=*/1, /*y0=*/1, /*span=*/1, dst, /*dstStride=*/2);
    // rows y=1,2 cols x=1,2: src[5],src[6],src[9],src[10]
    CHECK_EQ((int)dst[0], 5);
    CHECK_EQ((int)dst[1], 6);
    CHECK_EQ((int)dst[2], 9);
    CHECK_EQ((int)dst[3], 10);
}

TEST(TileTex, TileCacheSampleSubBlock_Wraps) {
    // Sample a 2x2 block (span=1) at (3,3): cols 3,0  rows 3,0 (mask = 3, toroidal).
    u8 src[16];
    for (int i = 0; i < 16; ++i) src[i] = (u8)i;
    u8 dst[4] = {0,0,0,0};
    TileCacheSampleSubBlock(src, 4, 3, 3, 1, dst, 2);
    // row y=3 (mask&3=3 -> base 12): cols 3,0 -> src[15], src[12]
    // row y=0 (mask&4=0 -> base 0):  cols 3,0 -> src[3],  src[0]
    CHECK_EQ((int)dst[0], 15);
    CHECK_EQ((int)dst[1], 12);
    CHECK_EQ((int)dst[2], 3);
    CHECK_EQ((int)dst[3], 0);
}

// ===========================================================================
// 0x5db928 (+88) texelMask + 0x5ba1e8 width clamp
// ===========================================================================
TEST(TileTex, TileRecordTexelMaskAndWidthClamp) {
    // (w-1)|(w*w-1): for power-of-two w this is w*w-1.
    CHECK_EQ((int)TileRecordTexelMask(64), 64*64 - 1);
    CHECK_EQ((int)TileRecordTexelMask(8), 8*8 - 1);
    CHECK_EQ((int)TileRecordTexelMask(1), 0);
    // It equals texture.h TexelMask (the same +88 formula).
    CHECK_EQ((int)TileRecordTexelMask(32), (int)TexelMask(32));

    // Width clamp: shipped binary has all shifts/cap == 0 -> cap = min(span*64, 0) = 0.
    CHECK_EQ(TileCacheWidthClamp(/*span=*/8, /*cap=*/0, /*mip=*/0, /*capShift=*/0), 0);
    // With a non-degenerate global cap: min(span*64, cap<<capShift).
    CHECK_EQ(TileCacheWidthClamp(8, /*cap=*/1000, 0, 0), 512);   // 8*64=512 < 1000
    CHECK_EQ(TileCacheWidthClamp(8, /*cap=*/4, 0, 4), 64);       // 4<<4=64 < 512
    CHECK_EQ(TileCacheWidthClamp(8, /*cap=*/1000, /*mip=*/1, 0), 256); // 8*(64>>1)=256
}

// ===========================================================================
// 0x5c5530 — FloodFillTileType (the existing render/heightmap.cpp reconstruction;
// re-verified against the wave-5 decompile — reused here, not re-implemented).
// ===========================================================================
namespace {
// A Heightmap over a flat type-byte array (type at +0 of each 24-byte entry).
struct FloodGrid {
    int size;
    std::vector<u8> entries;            // size*size * 24-byte records
    Heightmap hm{};
    FloodGrid(int s, const std::vector<u8>& types)
        : size(s), entries((std::size_t)s * s * 24, 0) {
        for (int i = 0; i < s * s; ++i) entries[(std::size_t)i * 24] = types[i];
        std::memset(&hm, 0, sizeof(hm));
        hm.size = s;
        hm.entries = entries.data();
    }
    u8 at(int x, int y) const { return entries[(std::size_t)(y * size + x) * 24]; }
};
} // namespace

TEST(TileTex, FloodFillTileType_FillsEnclosed) {
    // 4x4 grid. Interior cells are rows/cols 1..2. A plus of FROM with the centre
    // interior cell (1,1) eligible: up=TO, down=FROM, left=TO, right=FROM -> flips.
    const int N = 4;
    const u8 FROM = 5, TO = 9;
    std::vector<u8> types(N * N, 1);   // background = 1 (neither from nor to)
    auto idx = [&](int x, int y){ return y * N + x; };
    types[idx(1,1)] = FROM;
    types[idx(1,0)] = TO;    // up
    types[idx(1,2)] = FROM;  // down
    types[idx(0,1)] = TO;    // left
    types[idx(2,1)] = FROM;  // right
    FloodGrid g(N, types);

    int ret = FloodFillTileType(&g.hm, FROM, TO);
    CHECK_EQ(ret, N - 1);                       // returns size-1
    CHECK_EQ((int)g.at(1, 1), TO);              // (1,1) flipped to TO
    // (2,1) is interior + FROM, but its right neighbour (3,1) is background 1 -> stays.
    CHECK_EQ((int)g.at(2, 1), FROM);
}

TEST(TileTex, FloodFillTileType_NoEligibleCells) {
    const int N = 4;
    std::vector<u8> types(N * N, 7);            // all one type, no FROM cells
    FloodGrid g(N, types);
    int ret = FloodFillTileType(&g.hm, /*from=*/5, /*to=*/9);
    CHECK_EQ(ret, N - 1);
    for (int i = 0; i < N * N; ++i)
        CHECK_EQ((int)g.entries[(std::size_t)i * 24], 7);   // unchanged
}

TEST(TileTex, FloodFillTileType_SinglePassPropagation) {
    // LIVE single-buffer behaviour: a flipped cell becomes a `to` neighbour for a
    // later cell in the SAME forward sweep. Everything FROM -> all interior cells
    // qualify and flip; the border stays FROM.
    const int N = 5;
    const u8 FROM = 3, TO = 8;
    std::vector<u8> types(N * N, FROM);
    FloodGrid g(N, types);
    FloodFillTileType(&g.hm, FROM, TO);
    for (int y = 1; y < N - 1; ++y)
        for (int x = 1; x < N - 1; ++x)
            CHECK_EQ((int)g.at(x, y), TO);
    CHECK_EQ((int)g.at(0, 0), FROM);
    CHECK_EQ((int)g.at(N - 1, N - 1), FROM);
}

// ===========================================================================
// FloorTextureResolver — typeByte -> slot -> texture record (cache-hit path)
// ===========================================================================
namespace {
// Seed a record into the cache's TextureSet directly (the FindActive hit path the
// resolver's LoadByName follows). Real BMP decode is exercised by the e2e.
int SeedRecord(TextureAssetCache& cache, const std::string& name, int w) {
    int slot = cache.set().CreateRecord(name, w);
    if (slot >= 0) {
        Texture& t = cache.set().records[(std::size_t)slot];
        for (std::size_t i = 0; i < t.texels.size(); ++i) t.texels[i] = (u8)(i & 0xFF);
    }
    return slot;
}
} // namespace

TEST(TileTex, ResolverMapsTypeByteToSlotTexture) {
    TextureAssetCache cache(32);
    // The resolver builds "*" + base + ".BMP" and calls LoadByName(path, base); the
    // FindActive(base) hit returns the seeded record. Seed under the BASE names.
    SeedRecord(cache, "SAND", 8);
    SeedRecord(cache, "WIESE", 16);

    char names[8][64];
    std::memset(names, 0, sizeof(names));
    std::strcpy(names[0], "SAND");
    std::strcpy(names[1], "WIESE");
    // names[2..7] empty.

    FloorTextureResolver r;
    r.Bind(names, &cache);
    CHECK(r.bound());

    // typeByte 0 -> slot 0 "SAND" record (width 8).
    const Texture* t0 = r.Resolve(0);
    CHECK(t0 != nullptr);
    CHECK(t0->name == "SAND");
    CHECK_EQ(t0->mipWidth, 8);

    // typeByte 1 -> slot 1 "WIESE" (width 16).
    const Texture* t1 = r.Resolve(1);
    CHECK(t1 != nullptr);
    CHECK(t1->name == "WIESE");
    CHECK_EQ(t1->mipWidth, 16);

    // empty slot -> nullptr (untextured fallback).
    CHECK(r.Resolve(2) == nullptr);

    // hole bit (0x80) -> nullptr regardless of low bits.
    CHECK(r.Resolve(0x80) == nullptr);
    CHECK(r.Resolve(0x81) == nullptr);

    // repeated resolve returns the SAME record (slot cache).
    CHECK(r.Resolve(0) == t0);
}

TEST(TileTex, ResolverHookTrampoline) {
    TextureAssetCache cache(8);
    SeedRecord(cache, "FELS", 4);
    char names[8][64];
    std::memset(names, 0, sizeof(names));
    std::strcpy(names[0], "FELS");

    FloorTextureResolver r;
    r.Bind(names, &cache);

    // No active resolver -> hook returns null.
    SetActiveFloorTextureResolver(nullptr);
    CHECK(FloorTextureResolver::GetTileTextureHook(0) == nullptr);

    // Install the resolver active -> the hook returns the resolved record.
    SetActiveFloorTextureResolver(&r);
    CHECK(ActiveFloorTextureResolver() == &r);
    const void* p = FloorTextureResolver::GetTileTextureHook(0);
    CHECK(p == static_cast<const void*>(r.Resolve(0)));
    CHECK(p != nullptr);
    SetActiveFloorTextureResolver(nullptr);  // restore
}

TEST(TileTex, ResolverNoCacheIsInert) {
    char names[8][64];
    std::memset(names, 0, sizeof(names));
    std::strcpy(names[0], "SAND");
    FloorTextureResolver r;
    r.Bind(names, nullptr);             // no cache
    CHECK(!r.bound());
    CHECK(r.Resolve(0) == nullptr);     // inert: untextured fallback
}
