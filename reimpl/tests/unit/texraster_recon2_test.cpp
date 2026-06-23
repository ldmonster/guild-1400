// =============================================================================
// Golden-vector unit tests for texraster_recon2 (texture surface/cache, span
// patchers, light orchestration). Self-contained; drives the reconstructed logic
// through its hooks. Unique suite prefix TexRaster2Recon*.
// =============================================================================
#include "tests/framework/test.h"
#include "render/texraster_recon2_texsurf.h"
#include "render/texraster_recon2_texcache.h"
#include "render/texraster_recon2_spanpatch.h"
#include "render/texraster_recon2_light.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::render;

// ---------------------------------------------------------------------------
// Palette unpack: 0x00RRGGBB -> [R,G,B] for 256 entries.
// ---------------------------------------------------------------------------
TEST(TexRaster2ReconPalette, UnpackRGB) {
    u32 dws[256];
    for (int i = 0; i < 256; ++i) dws[i] = 0x00112233u + (u32)(i << 24); // alpha varies (ignored)
    u8 out[768] = {0};
    UnpackDdrawPalette(dws, out);
    for (int i = 0; i < 256; ++i) {
        CHECK_EQ(out[i * 3 + 0], 0x33); // R = byte0
        CHECK_EQ(out[i * 3 + 1], 0x22); // G = (x&0xFF00)>>8
        CHECK_EQ(out[i * 3 + 2], 0x11); // B = (x&0xFF0000)>>16
    }
}

TEST(TexRaster2ReconPalette, UnpackSpecificColors) {
    u32 dws[256] = {0};
    dws[0] = 0x00FF0000u; // pure blue channel in bits16-23
    dws[1] = 0x0000FF00u; // green
    dws[2] = 0x000000FFu; // red
    u8 out[768] = {0};
    UnpackDdrawPalette(dws, out);
    CHECK_EQ(out[0], 0x00); CHECK_EQ(out[1], 0x00); CHECK_EQ(out[2], 0xFF);
    CHECK_EQ(out[3], 0x00); CHECK_EQ(out[4], 0xFF); CHECK_EQ(out[5], 0x00);
    CHECK_EQ(out[6], 0xFF); CHECK_EQ(out[7], 0x00); CHECK_EQ(out[8], 0x00);
}

// ---------------------------------------------------------------------------
// Free-surface cache match predicate + claim (LoadFromCache).
// mipKey = (32*flags)>>7 == bit2 of flags; dynKey = (16*flags)>>7 == bit3.
// ---------------------------------------------------------------------------
static int g_restoreCalled = 0;
static int g_palCalled = 0;
static char RestoreStubCDT(TexSurfRecord&, u8, bool, u32, char) { ++g_restoreCalled; return 7; }
static char PalStub(TexSurfRecord&, const u8*, u32) { ++g_palCalled; return 9; }

TEST(TexRaster2ReconCache, LoadFromCacheHitClaims) {
    g_restoreCalled = 0; g_palCalled = 0;
    TexSurfHooks h;
    h.createDynamicTexture = &RestoreStubCDT;
    h.createSurfacePalette = &PalStub;
    SetTexSurfHooks(h);

    static FreeSurfaceEntry pool[3];
    std::memset(pool, 0, sizeof(pool));
    DDrawSurface front{101}, pal{202};
    pool[1].front = &front; pool[1].palette = &pal;
    pool[1].deviceKey = 0xABCD; pool[1].mipKey = 1; pool[1].dynKey = 0;

    TexSurf().freeCacheCount = 2;
    TexSurf().freeCacheCap   = 3;
    TexSurf().freeCache      = pool;
    TexSurf().ignoreMipKey   = 0;

    TexSurfRecord rec;
    rec.deviceHandle = 0xABCD;
    rec.flags = 0x04; // bit2 set -> mipKey=1, bit3 clear -> dynKey=0

    char r = Texture_LoadFromCache(rec, 256, nullptr);
    CHECK_EQ(r, 9);                       // palette-attach path returned
    CHECK_EQ(g_restoreCalled, 0);         // did NOT fall back to restore
    CHECK_EQ(g_palCalled, 1);
    CHECK(rec.frontSurface == &front);
    CHECK(rec.paletteSurface == &pal);
    CHECK_EQ(pool[1].deviceKey, 0);       // slot marked empty
    CHECK_EQ(TexSurf().freeCacheCount, 1u);
}

TEST(TexRaster2ReconCache, LoadFromCacheMissFallsBack) {
    g_restoreCalled = 0; g_palCalled = 0;
    TexSurfHooks h;
    h.createDynamicTexture = &RestoreStubCDT;
    h.createSurfacePalette = &PalStub;
    SetTexSurfHooks(h);

    static FreeSurfaceEntry pool[2];
    std::memset(pool, 0, sizeof(pool));
    pool[0].deviceKey = 0x1111; // wrong device
    TexSurf().freeCacheCount = 1;
    TexSurf().freeCacheCap   = 2;
    TexSurf().freeCache      = pool;

    TexSurfRecord rec;
    rec.deviceHandle = 0x2222; // no match
    rec.flags = 0;
    char r = Texture_LoadFromCache(rec, 0, nullptr);
    CHECK_EQ(r, 7);                       // restore stub returned
    CHECK_EQ(g_restoreCalled, 1);
}

TEST(TexRaster2ReconCache, LoadFromCacheEmptyCacheRestores) {
    g_restoreCalled = 0;
    TexSurfHooks h; h.createDynamicTexture = &RestoreStubCDT;
    SetTexSurfHooks(h);
    TexSurf().freeCacheCount = 0;  // empty -> immediate restore
    TexSurfRecord rec; rec.flags = 0;
    char r = Texture_LoadFromCache(rec, 0, nullptr);
    CHECK_EQ(r, 7);
    CHECK_EQ(g_restoreCalled, 1);
}

// ---------------------------------------------------------------------------
// RestoreSurface: mip flag = (flags&8)?0:defaultMipFlag; (flags&4) selects path.
// ---------------------------------------------------------------------------
static bool g_lastMip = false;
static u32  g_lastA2 = 0;
static char g_lastA3 = 0;
static u8   g_lastFmt = 99;
static char CapCDT(TexSurfRecord&, u8 fmt, bool mip, u32 a2, char a3) {
    g_lastFmt = fmt; g_lastMip = mip; g_lastA2 = a2; g_lastA3 = a3; return 1;
}

TEST(TexRaster2ReconRestore, MipSuppressedByFlag8) {
    TexSurfHooks h; h.createDynamicTexture = &CapCDT; SetTexSurfHooks(h);
    TexSurf().defaultMipFlag = 1;
    TexSurfRecord rec; rec.flags = 0x08;  // bit3 -> mip forced 0
    Texture_RestoreSurface(rec, 0, 0);
    CHECK_EQ(g_lastMip, false);
}

TEST(TexRaster2ReconRestore, DynamicPathUsesZeroDims) {
    TexSurfHooks h; h.createDynamicTexture = &CapCDT; SetTexSurfHooks(h);
    TexSurf().defaultMipFlag = 0;
    TexSurfRecord rec; rec.flags = 0x04;  // bit2 -> dynamic format, (0,0)
    Texture_RestoreSurface(rec, 555, 7);
    CHECK_EQ(g_lastFmt, 1);               // dynamic fmt tag
    CHECK_EQ(g_lastA2, 0u);
    CHECK_EQ(g_lastA3, 0);
}

TEST(TexRaster2ReconRestore, PlainPathPassesDims) {
    TexSurfHooks h; h.createDynamicTexture = &CapCDT; SetTexSurfHooks(h);
    TexSurf().defaultMipFlag = 1;
    TexSurfRecord rec; rec.flags = 0;     // plain path, mip = defaultMipFlag
    Texture_RestoreSurface(rec, 555, 7);
    CHECK_EQ(g_lastFmt, 0);
    CHECK_EQ(g_lastMip, true);
    CHECK_EQ(g_lastA2, 555u);
    CHECK_EQ(g_lastA3, 7);
}

// ---------------------------------------------------------------------------
// ReleaseSurfaces: evict path vs release-each path; texel free always.
// ---------------------------------------------------------------------------
static int g_evicted = 0, g_released = 0, g_freed = 0;
static int EvictStub(TexSurfRecord&) { ++g_evicted; return 0; }
static int RelStub(DDrawSurface*) { ++g_released; return 0; }
static int FreeStub(void*) { ++g_freed; return 0; }

TEST(TexRaster2ReconRelease, EvictWhenCachedSpecial) {
    g_evicted = g_released = g_freed = 0;
    TexSurfHooks h; h.evictAndStore=&EvictStub; h.releaseSurface=&RelStub; h.freeDebug=&FreeStub;
    SetTexSurfHooks(h);
    TexSurf().surfaceCacheEnabled = 1;
    DDrawSurface a{1}, b{2};
    int texel = 0;
    TexSurfRecord rec;
    rec.tag = 42; rec.frontSurface = &a; rec.paletteSurface = &b; rec.texelMem = &texel;
    Texture_ReleaseSurfaces(rec);
    CHECK_EQ(g_evicted, 1);
    CHECK_EQ(g_released, 0);
    CHECK(rec.frontSurface == nullptr);
    CHECK(rec.paletteSurface == nullptr);
    CHECK_EQ(g_freed, 1);                 // texel freed
    CHECK(rec.texelMem == nullptr);
}

TEST(TexRaster2ReconRelease, ReleaseEachWhenNotCached) {
    g_evicted = g_released = g_freed = 0;
    TexSurfHooks h; h.evictAndStore=&EvictStub; h.releaseSurface=&RelStub; h.freeDebug=&FreeStub;
    SetTexSurfHooks(h);
    TexSurf().surfaceCacheEnabled = 0;    // cache disabled -> release-each path
    DDrawSurface a{1}, b{2};
    TexSurfRecord rec;
    rec.tag = 42; rec.frontSurface = &a; rec.paletteSurface = &b; rec.texelMem = nullptr;
    Texture_ReleaseSurfaces(rec);
    CHECK_EQ(g_evicted, 0);
    CHECK_EQ(g_released, 2);              // both surfaces released
    CHECK_EQ(g_freed, 0);                // no texel
}

// ---------------------------------------------------------------------------
// ReleaseSurface guard: flag bit1 / cache-disabled -> early 0; re-upload gate.
// ---------------------------------------------------------------------------
static int g_uploaded = 0;
static void UpStub(TexSurfRecord&) { ++g_uploaded; }

TEST(TexRaster2ReconReleaseOne, EarlyOutOnFlag2) {
    g_released = 0; g_uploaded = 0;
    TexSurfHooks h; h.releaseSurface=&RelStub; h.uploadToSurface=&UpStub; SetTexSurfHooks(h);
    TexSurf().surfaceCacheEnabled = 1;
    TexSurfRecord rec; rec.flags = 0x02;  // bit1 -> early 0
    char r = Texture_ReleaseSurface(rec);
    CHECK_EQ(r, 0);
    CHECK_EQ(g_released, 0);
    CHECK_EQ(g_uploaded, 0);
}

TEST(TexRaster2ReconReleaseOne, ReuploadWhenMaskSetAndNotSpecial) {
    g_released = 0; g_uploaded = 0;
    TexSurfHooks h; h.releaseSurface=&RelStub; h.uploadToSurface=&UpStub; SetTexSurfHooks(h);
    TexSurf().surfaceCacheEnabled = 1;
    DDrawSurface a{1};
    TexSurfRecord rec; rec.flags = 0; rec.frontSurface = &a; rec.wrapMask = 0xFFF; rec.tag = 1;
    Texture_ReleaseSurface(rec);
    CHECK_EQ(g_released, 1);
    CHECK(rec.frontSurface == nullptr);
    CHECK_EQ(g_uploaded, 1);             // wrapMask && tag!=42 -> re-upload
}

// ---------------------------------------------------------------------------
// SetBasePath default VFS normalize copies + sets basePathSet.
// ---------------------------------------------------------------------------
TEST(TexRaster2ReconBasePath, DefaultNormalizeCopies) {
    int r = Texture_SetBasePath("data/textures", 0);
    CHECK(r != 0);
    CHECK_EQ(TexBasePath().basePathSet, r);
    CHECK(std::strcmp(TexBasePath().baseDir, "data/textures") == 0);
}

// ---------------------------------------------------------------------------
// Span patchers: all five fan the SAME six globals into the params; only the
// variant tag differs.
// ---------------------------------------------------------------------------
TEST(TexRaster2ReconSpanPatch, AllVariantsShareSources) {
    u8  texels[4] = {1,2,3,4};
    u16 pal[2] = {0x1234, 0x5678};
    SpanPatchSources s;
    s.texBase = texels; s.palBase = pal;
    s.uStepFrac = 0x00010000; s.lightStart = 0x00400000;
    s.texelMask = 0x3FF; s.widthShift = 5;

    auto check = [&](SpanPatchVariantParams p, SpanVariant v) {
        CHECK(p.variant == v);
        CHECK(p.texBase == texels);
        CHECK(p.palBase == pal);
        CHECK_EQ(p.uStepFrac, 0x00010000);
        CHECK_EQ(p.lightStart, 0x00400000);
        CHECK_EQ(p.texelMask, 0x3FFu);
        CHECK_EQ(p.widthShift, 5);
    };
    check(PatchSpanConstantsMasked(s),      SpanVariant::Masked);
    check(PatchSpanConstantsBlend(s),       SpanVariant::Blend);
    check(PatchSpanConstantsBlendMasked(s), SpanVariant::BlendMasked);
    check(PatchSpanConstantsOr(s),          SpanVariant::Or);
    check(PatchSpanConstantsOrMasked(s),    SpanVariant::OrMasked);
}

// ---------------------------------------------------------------------------
// TextureCache_Setup: identity texture matrix layout (0/1 pattern).
// ---------------------------------------------------------------------------
TEST(TexRaster2ReconTexCache, SetupIdentityMatrix) {
    TexCacheHooks h; // all inert
    SetTexCacheHooks(h);
    TextureCache_Setup(0, 3, 7);
    const float* m = TexCache().texMatrix;
    const float expect[12] = {0,0,1,1, 0,1,0,0, 1,0,1,1};
    for (int i = 0; i < 12; ++i) CHECK_EQ(m[i], expect[i]);
    CHECK_EQ(TexCache().filterMode, 3);
    CHECK_EQ(TexCache().filterBlur, 7);
}

// ---------------------------------------------------------------------------
// TextureCache_DisposeAll: zeroes the record array, resets globals, returns
// the seeded frame stamp.
// ---------------------------------------------------------------------------
TEST(TexRaster2ReconTexCache, DisposeAllZeroesAndResets) {
    TexCacheHooks h; SetTexCacheHooks(h);   // default releaseEntry zeroes +64
    static std::vector<u8> recs(2 * 128, 0xAB);
    // Each record's +64 ref dword > 0 so the drain loop runs (then default
    // releaseEntry zeroes it, terminating).
    for (int i = 0; i < 2; ++i) *reinterpret_cast<i32*>(&recs[i*128 + 64]) = 1;
    TexCache().texRecBase = recs.data();
    TexCache().texRecCount = 2;
    TexCache().mipBase = nullptr; TexCache().mipCount = 0;
    TexCache_SetFrameStamp62EB38(0xDEAD);

    u32 stamp = TextureCache_DisposeAll();
    CHECK_EQ(stamp, 0xDEADu);
    CHECK_EQ((u32)TexCache().frameStamp, 0xDEADu);
    CHECK_EQ(TexCache().nextAnimId, 0u);
    // whole record array zeroed
    bool allZero = true;
    for (u8 b : recs) if (b != 0) { allZero = false; break; }
    CHECK(allZero);
}

// ---------------------------------------------------------------------------
// TextureCache_DisposeAll: the mip-block drain loop frees + zeros ALL N blocks
// (0x5d9c03: inc edx; cmp edx,dword_1406A60; jb — the loop runs for every entry,
// not N-1). Each 776-byte block's payload pointer (at +0) is freed and nulled.
// ---------------------------------------------------------------------------
TEST(TexRaster2ReconTexCache, DisposeAllFreesEveryMipBlock) {
    static int s_freed = 0; s_freed = 0;
    struct L { static void fd(void* p) { if (p) s_freed++; } };
    TexCacheHooks h; h.freeDebug = &L::fd; SetTexCacheHooks(h);

    TexCache().texRecBase = nullptr; TexCache().texRecCount = 0;

    const u32 N = 3;
    static std::vector<u8> blocks(N * 776, 0);
    // Distinct non-null payload pointers at each block's +0.
    static int payloads[3] = {0,0,0};
    for (u32 i = 0; i < N; ++i)
        *reinterpret_cast<void**>(&blocks[i * 776]) = &payloads[i];
    TexCache().mipBase = blocks.data();
    TexCache().mipCount = N;
    TexCache_SetFrameStamp62EB38(0x1);

    TextureCache_DisposeAll();

    // All N payloads freed (the last block must NOT be skipped).
    CHECK_EQ(s_freed, (int)N);
    // Every payload pointer cleared by the loop + the trailing memset.
    for (u32 i = 0; i < N; ++i)
        CHECK(*reinterpret_cast<void**>(&blocks[i * 776]) == nullptr);
    CHECK_EQ(TexCache().mipCount, 0u);
}

// ---------------------------------------------------------------------------
// TextureCache_Free: drains a '*'-tagged tile slot then frees the array.
// ---------------------------------------------------------------------------
TEST(TexRaster2ReconTexCache, FreeDrainsSpecialSlots) {
    static int s_freeCalls = 0; s_freeCalls = 0;
    struct L { static void fd(void*) { s_freeCalls++; } };
    TexCacheHooks h; h.freeDebug = &L::fd; SetTexCacheHooks(h);

    // 1 tile slot (68 bytes): slot[0] = ptr to a tile whose tag byte == 42 and
    // whose +64 ref starts > 0 (drained by default releaseEntry).
    static std::vector<u8> tile(68, 0);
    tile[0] = 42;                         // *a1 == 42
    *reinterpret_cast<i32*>(&tile[64]) = 1; // ref > 0
    static std::vector<u8> slots(68, 0);
    *reinterpret_cast<u8**>(&slots[0]) = tile.data();

    TexCache().tileBase = slots.data();
    TexCache().tileCount = 1;
    TextureCache_Free();
    CHECK_EQ(TexCache().tileCount, 0u);
    CHECK(TexCache().tileBase == nullptr);
    CHECK(s_freeCalls >= 1);              // array freed
    // slot pointer + flag cleared
    CHECK(*reinterpret_cast<u8**>(&slots[0]) == nullptr);
    CHECK_EQ(slots[64], 0);
}

// ---------------------------------------------------------------------------
// Light_CreateSunRays: exact flag-byte edits on the object record.
// ---------------------------------------------------------------------------
static u8 g_objbuf[600];
static int AttachStub(int, const int*, const char*, const void*) { return 1; }
static u8* ByteBaseStub(int h) { return h ? g_objbuf : nullptr; }

TEST(TexRaster2ReconLight, CreateSunRaysFlagEdits) {
    std::memset(g_objbuf, 0, sizeof(g_objbuf));
    g_objbuf[531] = 0xFF; g_objbuf[530] = 0x00; g_objbuf[529] = 0xFF;

    LightSceneHooks h{};
    h.objectAttachToUniverseNode = &AttachStub;
    h.objectByteBase = &ByteBaseStub;
    SetLightSceneHooks(h);
    LightScene().frameStamp = 0x1234;

    int r = Light_CreateSunRays(0, 0);
    CHECK_EQ(r, 1);
    CHECK_EQ(g_objbuf[535], 5);
    CHECK_EQ(*reinterpret_cast<int*>(&g_objbuf[536]), 1);
    CHECK_EQ(g_objbuf[531], 0xFB);       // 0xFF & 0xFB
    CHECK_EQ(g_objbuf[530], 0x0C);       // 0x00 | 0x0C
    CHECK_EQ(g_objbuf[529], 0xFD);       // 0xFF & 0xFD
    CHECK_EQ(LightScene().sunRaysObj, 1);
    CHECK_EQ(LightScene().sunRaysFlag1, 1);
    CHECK_EQ(LightScene().sunRaysStamp, 0x1234);
    CHECK_EQ(LightScene().sunRaysFlag2, 0);
}

// ---------------------------------------------------------------------------
// Light_EnableDaylight: frees existing octree, rebuilds with (7,8).
// ---------------------------------------------------------------------------
static int g_freedNode = 0; static unsigned g_buildA3 = 99;
static void FreeNodeStub(int n) { g_freedNode = n; }
static int  BuildOctStub(int, int, unsigned a3, unsigned) { g_buildA3 = a3; return 555; }

TEST(TexRaster2ReconLight, EnableDaylightRebuildsOctree) {
    g_freedNode = 0; g_buildA3 = 99;
    LightSceneHooks h{};
    h.freeNodeRecursive = &FreeNodeStub;
    h.buildOctreeForRegion = &BuildOctStub;
    SetLightSceneHooks(h);
    LightScene().octreeRoot = 321;
    int r = Light_EnableDaylight();
    CHECK_EQ(g_freedNode, 321);          // freed old root
    CHECK_EQ(g_buildA3, 7u);             // rebuilt region arg (7,8)
    CHECK_EQ(r, 555);
    CHECK_EQ(LightScene().octreeRoot, 555);
}

// ---------------------------------------------------------------------------
// Light_ApplyTorchEffects: reparents children with type>=5 when gate passes,
// then detaches each collected object.
// ---------------------------------------------------------------------------
static int g_reparentCount = 0; static int g_detachCount = 0;
static void ReparentStub(int, int) { ++g_reparentCount; }
static char DetachStub(int) { ++g_detachCount; return 3; }
// WalkAndInvoke stub fills buf[0]=objHandle, buf[32]=count=1.
static char WalkStub(int, void*, int(*)(), int, void* ctx) {
    int* buf = reinterpret_cast<int*>(ctx);
    buf[0] = 1000; buf[32] = 1; return 0;
}
static int  ChildHeadStub(int) { return 2000; }       // one child
static int  ChildNextStub(int) { return 0; }          // end of list
static i8   ChildTypeStub(int) { return 6; }          // >= 5 -> reparent
static int  GateTrue(int, const char*) { return 1; }

TEST(TexRaster2ReconLight, ApplyTorchEffectsReparentsAndDetaches) {
    g_reparentCount = 0; g_detachCount = 0;
    LightSceneHooks h{};
    h.walkAndInvoke = &WalkStub;
    h.objChildHead = &ChildHeadStub;
    h.objChildNext = &ChildNextStub;
    h.objChildType = &ChildTypeStub;
    h.gatePredicate = &GateTrue;
    h.objectReparentWithTransform = &ReparentStub;
    h.objectDetachAndRelease = &DetachStub;
    SetLightSceneHooks(h);

    char r = Light_ApplyTorchEffects(42);
    CHECK_EQ(g_reparentCount, 1);
    CHECK_EQ(g_detachCount, 1);
    CHECK_EQ(r, 3);
}
