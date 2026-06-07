// Unit / golden-vector tests for the reconstructed texture-record upload + bind
// layer (render/texture_upload.{h,cpp}) and the cache-reset + deterministic mip
// downsample helpers (render/texture_cache.cpp, render/texture_mip.cpp):
//
//   VIBE_Texture_BindActive          0x5db564
//   VIBE_Texture_ResetBinding        0x5db5f0
//   VIBE_Texture_PropagateToInstances0x5db1bc
//   VIBE_Texture_UploadToSurface     0x5db234
//   VIBE_Texture_UploadAllRecords    0x5db4b8
//   VIBE_Texture_RestoreIfLost       0x5dba74
//   VIBE_TextureCache_Reset          0x5b9f54
//   DownsampleIndex2x / BuildIndexMipChain (deterministic mip golden vectors)
//
// The GPU boundary is a counting mock ITextureSurface (no vendor calls).
#include "test.h"

#include "render/texture.h"
#include "render/texture_upload.h"
#include "render/texture_cache.h"
#include "render/texture_mip.h"

#include <vector>

using namespace guild;
using namespace guild::render;

// ---------------------------------------------------------------------------
// Counting mock for the GPU surface boundary. release() == DDraw vtable +8;
// loadAndUpload() == VIBE_Render_LoadAndStretchTexture. Records each call so a
// test can assert exactly which surfaces were released / created.
// ---------------------------------------------------------------------------
namespace {
struct MockSurface : ITextureSurface {
    int releases = 0;
    int loads = 0;
    u32 lastReleased = 0;
    int lastWidth = 0;
    bool lastStretch = false;
    u8 lastNoColorKey = 0, lastMipShift = 0;
    u32 nextId = 1000;

    i32 release(u32 id) override {
        ++releases;
        lastReleased = id;
        return 0;
    }
    u8 loadAndUpload(const char* /*path*/, int width, bool stretch,
                     u8 noColorKey, u8 mipShift, u32* srcOut, u32* dstOut) override {
        ++loads;
        lastWidth = width;
        lastStretch = stretch;
        lastNoColorKey = noColorKey;
        lastMipShift = mipShift;
        if (srcOut) *srcOut = nextId++;
        if (dstOut) *dstOut = nextId++;
        return (u8)mipShift;   // achieved shift == requested (mirrors +124)
    }
};

// Make a normal (master) active record of the given square width in a bank slot.
Texture* makeRecord(TextureBank& bank, int idx, int width, const char* name) {
    Texture& t = bank.records[(size_t)idx];
    t = Texture{};
    t.refCount = 1;
    t.name = name;
    t.slot = idx;            // master: +76 == own index (non-zero)
    t.paletteId = idx;       // distinct group
    TextureSetSize(t, width);
    return &t;
}
} // namespace

// ===========================================================================
// BindActive — publishes record geometry into the live binding.
// ===========================================================================
TEST(TexUpload, BindActivePublishesGeometry) {
    TextureBank bank(8);
    makeRecord(bank, 3, 64, "WALL");
    // Distinct palette + texel sentinel so we can confirm the pointers cross over.
    static u8 pal[8] = {1, 2, 3, 4, 5, 6, 7, 8};
    bank.records[3].palette = pal;
    bank.records[3].texels[0] = 0x5A;

    bank.BindActive(3);
    CHECK_EQ(bank.binding.boundIndex, 3);
    CHECK_EQ(bank.binding.slot, 3);
    CHECK_EQ(bank.binding.mipWidth, 64);
    CHECK_EQ((int)bank.binding.widthShift, 6);          // log2(64)
    CHECK(bank.binding.palBase == pal);
    CHECK(bank.binding.texBase == bank.records[3].texels.data());

    // Re-binding the same index is a no-op (early return: idx == dword_64A1F8).
    bank.binding.slot = 999;          // poison
    bank.BindActive(3);
    CHECK_EQ(bank.binding.slot, 999); // untouched -> early return hit
}

TEST(TexUpload, BindActiveCloneFollowsMaster) {
    TextureBank bank(8);
    Texture* master = makeRecord(bank, 2, 32, "BASE");
    static u8 mpal[4] = {9, 9, 9, 9};
    master->palette = mpal;

    // A clone record (flags bit1) whose +76 points at the master index 2.
    Texture& clone = bank.records[5];
    clone = Texture{};
    clone.refCount = 1;
    clone.flags = 2;          // bit1 == clone
    clone.slot = 2;           // master index
    clone.name = "CLONE";

    bank.BindActive(5);
    CHECK_EQ(bank.binding.boundIndex, 5);   // dword_64A1F8 == the bound idx (5)
    CHECK_EQ(bank.binding.mipWidth, 32);    // but geometry comes from master(2)
    CHECK_EQ((int)bank.binding.widthShift, 5);
    CHECK(bank.binding.palBase == mpal);
}

TEST(TexUpload, BindActiveSlotZeroIsWhiteDefault) {
    TextureBank bank(8);
    Texture& t = bank.records[1];
    t = Texture{};
    t.refCount = 1;
    t.slot = 0;               // +76 == 0 -> the 1x1 white default
    bank.BindActive(1);
    CHECK_EQ(bank.binding.slot, 0);
    CHECK_EQ(bank.binding.mipWidth, 1);
    CHECK_EQ((int)bank.binding.widthShift, 1);
    CHECK(bank.binding.texBase == nullptr);
    CHECK(bank.binding.palBase == nullptr);
}

// ===========================================================================
// ResetBinding — clears to white, marks nothing bound.
// ===========================================================================
TEST(TexUpload, ResetBindingClearsToWhite) {
    TextureBank bank(8);
    makeRecord(bank, 0, 128, "X");
    bank.BindActive(0);
    bank.ResetBinding();
    CHECK_EQ(bank.binding.mipWidth, 1);
    CHECK_EQ((int)bank.binding.widthShift, 1);
    CHECK_EQ(bank.binding.slot, 0);
    CHECK(bank.binding.texBase == nullptr);
    CHECK(bank.binding.palBase == nullptr);
    CHECK_EQ(bank.binding.boundIndex, 0);   // dword_64A1F8 = 0
}

// ===========================================================================
// PropagateToInstances — master surfaces/texels/widths copied to its clones.
// ===========================================================================
TEST(TexUpload, PropagateToInstancesCopiesToClones) {
    TextureBank bank(8);
    Texture* m = makeRecord(bank, 1, 64, "MASTER");
    m->texels[0] = 0x77;
    m->mipWidth = 64;
    m->baseWidth = 64;
    bank.gpu[1].srcSurface = 0xA1;
    bank.gpu[1].dstSurface = 0xB2;

    // Two clones referencing master 1, and one referencing a different master.
    for (int i : {4, 6}) {
        Texture& c = bank.records[i];
        c = Texture{};
        c.refCount = 1;
        c.flags = 2;          // clone
        c.slot = 1;           // -> master 1
    }
    Texture& other = bank.records[7];
    other = Texture{};
    other.refCount = 1;
    other.flags = 2;
    other.slot = 99;          // unrelated master

    bank.PropagateToInstances(1);
    for (int i : {4, 6}) {
        CHECK_EQ(bank.gpu[i].srcSurface, 0xA1u);
        CHECK_EQ(bank.gpu[i].dstSurface, 0xB2u);
        CHECK_EQ((int)bank.records[i].texels.size(), 64 * 64);
        CHECK_EQ(bank.records[i].texels[0], 0x77);
        CHECK_EQ(bank.records[i].mipWidth, 64);
        CHECK_EQ(bank.records[i].baseWidth, 64);
    }
    // The unrelated clone was NOT touched.
    CHECK_EQ(bank.gpu[7].srcSurface, 0u);
}

TEST(TexUpload, PropagateSkipsStarAndCloneMasters) {
    TextureBank bank(8);
    // master that is itself a clone (flags bit1) -> propagate is a no-op.
    Texture& m = bank.records[0];
    m = Texture{};
    m.refCount = 1; m.flags = 2; m.slot = 0; m.name = "C";
    bank.gpu[0].srcSurface = 0xDE;
    Texture& c = bank.records[1];
    c = Texture{}; c.refCount = 1; c.flags = 2; c.slot = 0;
    bank.PropagateToInstances(0);
    CHECK_EQ(bank.gpu[1].srcSurface, 0u);   // unchanged
}

// ===========================================================================
// UploadToSurface — GPU path (swEnabled): release stale, load fresh.
// ===========================================================================
TEST(TexUpload, UploadCreatesSurfacesWhenAbsent) {
    TextureBank bank(8);   // index 2: a real (non-reserved) master slot
    Texture* t = makeRecord(bank, 2, 128, "GROUND");
    t->loadShift = 0;
    bank.swEnabled = 1;
    bank.noTransparency = 0;

    MockSurface dev;
    bool ok = bank.UploadToSurface(2, /*force=*/false, "GROUND.BMP", dev);
    CHECK(ok);
    CHECK_EQ(dev.loads, 1);                 // LoadAndStretchTexture invoked once
    CHECK_EQ(dev.releases, 0);              // nothing stale to release
    CHECK_EQ(dev.lastWidth, 128);           // *(v4+120) baseWidth
    CHECK(!dev.lastStretch);                // flags bit2 clear
    CHECK(bank.gpu[2].srcSurface != 0);     // surfaces installed
    CHECK(bank.gpu[2].dstSurface != 0);
}

TEST(TexUpload, UploadReleasesStaleOnForce) {
    // Bank index 0 is the reserved "white" slot (+76 == 0); real master textures
    // live at non-zero indices, so this uses index 1.
    TextureBank bank(8);
    makeRecord(bank, 1, 64, "T");
    bank.swEnabled = 1;
    bank.gpu[1].srcSurface = 0x11;          // pre-existing surfaces
    bank.gpu[1].dstSurface = 0x22;

    MockSurface dev;
    bank.UploadToSurface(1, /*force=*/true, "T.BMP", dev);
    CHECK_EQ(dev.releases, 2);              // both stale surfaces released (vtable+8)
    CHECK_EQ(dev.loads, 1);                 // then reloaded
    CHECK(bank.gpu[1].srcSurface != 0x11);  // replaced
}

TEST(TexUpload, UploadStretchFlagSelectsStretchVariant) {
    TextureBank bank(8);
    Texture* t = makeRecord(bank, 1, 32, "DYN");
    t->flags = 4;                           // bit2 == stretched/dynamic
    bank.swEnabled = 1;
    MockSurface dev;
    bank.UploadToSurface(1, false, "DYN.BMP", dev);
    CHECK_EQ(dev.loads, 1);                 // dynamic record still loads
    CHECK(dev.lastStretch);                 // stretch variant chosen
}

TEST(TexUpload, UploadNoColorKeyFromFlagBit3) {
    TextureBank bank(8);
    Texture* t = makeRecord(bank, 1, 32, "OPAQUE");
    t->flags = 8;                           // bit3 == no-transparency
    bank.swEnabled = 1;
    bank.noTransparency = 7;                // would be used if bit3 clear
    MockSurface dev;
    bank.UploadToSurface(1, false, "O.BMP", dev);
    CHECK_EQ(dev.loads, 1);
    CHECK_EQ((int)dev.lastNoColorKey, 0);   // bit3 set -> v9 = 0
}

TEST(TexUpload, UploadStarRecordSkipsLoad) {
    TextureBank bank(8);
    makeRecord(bank, 0, 32, "*special");
    bank.swEnabled = 1;
    MockSurface dev;
    bank.UploadToSurface(0, false, "*special.BMP", dev);
    CHECK_EQ(dev.loads, 0);                 // name[0]=='*' -> no BMP load
}

// ===========================================================================
// UploadAllRecords — uploads every active non-clone record, fires the callback.
// ===========================================================================
namespace {
int g_cbCount = 0;
void countCb(void*) { ++g_cbCount; }
const char* pathForRec(const TextureBank&, int) { return "X.BMP"; }
} // namespace

TEST(TexUpload, UploadAllRecordsCountsActiveNonClones) {
    TextureBank bank(8);
    makeRecord(bank, 0, 32, "A");
    makeRecord(bank, 1, 32, "B");
    makeRecord(bank, 2, 32, "C");
    // A clone (flags bit1) must be SKIPPED by UploadAll.
    Texture& clone = bank.records[3];
    clone = Texture{}; clone.refCount = 1; clone.flags = 2; clone.slot = 0;
    bank.swEnabled = 1;

    g_cbCount = 0;
    MockSurface dev;
    int n = bank.UploadAllRecords(false, dev, pathForRec, countCb, nullptr);
    CHECK_EQ(n, 3);                         // 3 masters, clone skipped
    CHECK_EQ(g_cbCount, 3);                 // callback per uploaded record
    CHECK_EQ(bank.binding.boundIndex, 0);   // dword_64A1F8 cleared at the end
}

// ===========================================================================
// RestoreIfLost — only dynamic (flags bit2) records reload.
// ===========================================================================
TEST(TexUpload, RestoreIfLostOnlyDynamic) {
    TextureBank bank(8);   // non-reserved indices 1 (dynamic) and 2 (static)
    Texture* dyn = makeRecord(bank, 1, 32, "DYN");
    dyn->flags = 4;                         // dynamic
    Texture* stat = makeRecord(bank, 2, 32, "STAT");  // no bit2
    (void)stat;
    bank.swEnabled = 1;

    MockSurface dev;
    bank.RestoreIfLost(2, "STAT.BMP", dev);
    CHECK_EQ(dev.loads, 0);                 // static -> not restored
    bank.RestoreIfLost(1, "DYN.BMP", dev);
    CHECK_EQ(dev.loads, 1);                 // dynamic -> restored
}

// ===========================================================================
// TextureCache_Reset — clears every tile slot; fires the two subsystem hooks.
// ===========================================================================
namespace {
int g_relCount = 0;
u32 g_lastRel = 0;
int g_floorInval = 0;
void relGroup(u32 id, void*) { ++g_relCount; g_lastRel = id; }
void invalFloors(void*) { ++g_floorInval; }
} // namespace

TEST(TexUpload, TextureCacheResetClearsSlots) {
    TileCache cache(4);
    cache.frame = 50;
    cache.salt = 0x1234;
    // Occupy two slots.
    cache.slots[0].data = 0xAA; cache.slots[0].stamp = 10;
    cache.slots[0].size = 9; cache.slots[0].key = 0x55;
    cache.slots[0].sig[0] = 7;
    cache.slots[2].data = 0xBB; cache.slots[2].stamp = 20;

    g_relCount = 0; g_floorInval = 0; g_lastRel = 0;
    cache.Reset(relGroup, invalFloors, nullptr);

    for (auto& s : cache.slots) {
        CHECK_EQ(s.data, 0u);
        CHECK_EQ(s.stamp, 0u);
        CHECK_EQ(s.size, 0);
        CHECK_EQ(s.key, 0u);
        CHECK_EQ((int)s.sig[0], 0);
    }
    CHECK_EQ(g_relCount, 2);                 // released both occupied slots
    CHECK_EQ(g_lastRel, 0xBBu);
    CHECK_EQ(g_floorInval, 1);               // floor sweep once
    // frame/salt preserved (Reset does not touch the LRU clock).
    CHECK_EQ(cache.frame, 50u);
    CHECK_EQ(cache.salt, 0x1234u);
}

// ===========================================================================
// GOLDEN VECTOR: deterministic mip downsample (fixed texture -> exact pixels).
// Computed with python: dst[v][u] = src[2v][2u].
// ===========================================================================
TEST(TexUpload, MipDownsampleGolden4x4) {
    const u8 src[16] = {
        10, 11, 12, 13,
        14, 15, 16, 17,
        18, 19, 20, 21,
        22, 23, 24, 25,
    };
    u8 dst[4];
    DownsampleIndex2x(src, 4, dst);
    const u8 want[4] = {10, 12, 18, 20};     // python golden
    for (int i = 0; i < 4; ++i)
        CHECK_EQ(dst[i], want[i]);

    u8 dst1[1];
    DownsampleIndex2x(dst, 2, dst1);
    CHECK_EQ(dst1[0], 10);                    // 2x2 -> 1x1 golden
}

TEST(TexUpload, MipChainGolden8x8) {
    u8 base[64];
    for (int i = 0; i < 64; ++i) base[i] = (u8)i;   // 0..63 ascending

    std::vector<std::vector<u8>> chain = BuildIndexMipChain(base, 8);
    CHECK_EQ((int)chain.size(), 4);            // 8,4,2,1 == MipLevelCount(8)
    CHECK_EQ(MipLevelCount(8), 4);

    // Level 1 (4x4) golden from python.
    const u8 lvl1[16] = {0,2,4,6, 16,18,20,22, 32,34,36,38, 48,50,52,54};
    CHECK_EQ((int)chain[1].size(), 16);
    for (int i = 0; i < 16; ++i) CHECK_EQ(chain[1][i], lvl1[i]);

    // Level 2 (2x2) and level 3 (1x1) golden.
    const u8 lvl2[4] = {0, 4, 32, 36};
    CHECK_EQ((int)chain[2].size(), 4);
    for (int i = 0; i < 4; ++i) CHECK_EQ(chain[2][i], lvl2[i]);
    CHECK_EQ((int)chain[3].size(), 1);
    CHECK_EQ(chain[3][0], 0);
}

TEST(TexUpload, MipWidthSaturates) {
    CHECK_EQ(MipWidth(256, 0), 256);
    CHECK_EQ(MipWidth(256, 2), 64);
    CHECK_EQ(MipWidth(2, 8), 1);              // saturates to >= 1
}
