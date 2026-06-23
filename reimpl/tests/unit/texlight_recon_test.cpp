#include "tests/framework/test.h"
#include "render/texlight_recon.h"
#include "render/hicoltab.h"
#include "render/colorformat.h"
#include <cmath>
#include <vector>

using namespace guild;
using namespace guild::render;

// ============================================================================
// SurfaceCache (gilde.exe 0x5d93f4 / 0x5d9430 / 0x5d94c8 / 0x5d9580)
// ============================================================================

namespace {
int g_released = 0;
void CountingRelease(u32) { ++g_released; }
}

TEST(TexLightReconSurfaceCache, InitAllocatesAndZeros) {
    SurfaceCache c;
    c.Init(4);
    CHECK_EQ(c.capacity, 4u);
    CHECK_EQ(c.liveCount, 0u);
    CHECK_EQ(c.entries.size(), (size_t)4);
    for (auto& e : c.entries) CHECK_EQ(e.key, 0u);

    SurfaceCache z;
    z.Init(0);
    CHECK_EQ(z.capacity, 0u);
    CHECK(z.entries.empty());
}

TEST(TexLightReconSurfaceCache, StoreEntryFlagsAndStamp) {
    SurfaceCache c;
    c.Init(2);
    c.frame = 0x1234;
    TexRecord tex;
    tex.surf0 = 0xA0; tex.surf1 = 0xB0; tex.key = 0xCAFE;
    // flags bit2 set -> flag0=1 ; bit3 set -> flag1=1
    tex.flags = 0x04 | 0x08;          // (32*flags)>>7==1, (16*flags)>>7==1
    c.StoreEntry(0, tex);
    const SurfaceEntry& e = c.entries[0];
    CHECK_EQ(e.surf0, 0xA0u);
    CHECK_EQ(e.surf1, 0xB0u);
    CHECK_EQ(e.key,   0xCAFEu);
    CHECK_EQ(e.stamp, 0x1234u);
    CHECK_EQ((int)e.flag0, 1);
    CHECK_EQ((int)e.flag1, 1);

    // flags with bit2 clear, bit3 clear -> both 0
    TexRecord t2; t2.flags = 0x02; t2.surf0 = 1; t2.key = 7;
    c.StoreEntry(1, t2);
    CHECK_EQ((int)c.entries[1].flag0, 0);
    CHECK_EQ((int)c.entries[1].flag1, 0);
}

TEST(TexLightReconSurfaceCache, StoreEntryReleasesOldSurfaces) {
    g_released = 0;
    SurfaceCache c;
    c.Init(1);
    c.hooks.releaseSurface = CountingRelease;
    TexRecord a; a.surf0 = 1; a.surf1 = 2; a.key = 100;
    c.StoreEntry(0, a);
    CHECK_EQ(g_released, 0);          // slot was free (key==0), no release
    TexRecord b; b.surf0 = 3; b.surf1 = 4; b.key = 200;
    c.StoreEntry(0, b);              // slot occupied -> release both old surfs
    CHECK_EQ(g_released, 2);
    CHECK_EQ(c.entries[0].key, 200u);
}

TEST(TexLightReconSurfaceCache, FreeAllReleasesOccupiedOnly) {
    g_released = 0;
    SurfaceCache c;
    c.Init(3);
    c.hooks.releaseSurface = CountingRelease;
    TexRecord a; a.surf0 = 1; a.surf1 = 2; a.key = 10; c.StoreEntry(0, a);
    TexRecord b; b.surf0 = 3; b.surf1 = 0; b.key = 20; c.StoreEntry(2, b);
    // entry 1 stays free
    c.FreeAll();
    // entry0: surf0+surf1 -> 2 ; entry2: surf0 only -> 1 ; total 3
    CHECK_EQ(g_released, 3);
    CHECK_EQ(c.capacity, 0u);
    CHECK_EQ(c.liveCount, 0u);
}

TEST(TexLightReconSurfaceCache, EvictAndStoreFillsFreeSlot) {
    SurfaceCache c;
    c.Init(3);
    c.frame = 5;
    TexRecord t; t.surf0 = 9; t.surf1 = 8; t.key = 0x777;
    int idx = c.EvictAndStore(t);
    CHECK_EQ(idx, 0);
    CHECK_EQ(c.liveCount, 1u);
    CHECK_EQ(c.entries[0].key, 0x777u);
    CHECK_EQ(c.entries[0].stamp, 5u);
}

TEST(TexLightReconSurfaceCache, EvictAndStorePicksLowestStamp) {
    SurfaceCache c;
    c.Init(2);
    // Fill both slots at different frames so the cache is full.
    c.frame = 10; { TexRecord t; t.key = 1; t.surf0 = 1; c.EvictAndStore(t); }
    c.frame = 20; { TexRecord t; t.key = 2; t.surf0 = 2; c.EvictAndStore(t); }
    CHECK_EQ(c.liveCount, 2u);
    // Now full. EvictAndStore must replace slot 0 (stamp 10 < 20 and 10 < frame).
    c.frame = 30;
    TexRecord t; t.key = 3; t.surf0 = 3;
    int idx = c.EvictAndStore(t);
    CHECK_EQ(idx, 0);
    CHECK_EQ(c.entries[0].key, 3u);
    CHECK_EQ(c.entries[0].stamp, 30u);
    CHECK_EQ(c.liveCount, 2u);       // unchanged on eviction
}

TEST(TexLightReconSurfaceCache, EvictAndStoreNoEvictableReleasesTex) {
    g_released = 0;
    SurfaceCache c;
    c.Init(1);
    c.hooks.releaseSurface = CountingRelease;
    // Make the single slot occupied with stamp == current frame so no entry has
    // stamp strictly less than frame -> nothing evictable.
    c.frame = 7;
    { TexRecord t; t.key = 1; t.surf0 = 1; c.EvictAndStore(t); }
    CHECK_EQ(c.liveCount, 1u);
    // frame stays 7 -> best starts at 7, existing stamp 7, 7>7 is false.
    TexRecord t; t.key = 2; t.surf0 = 0x10; t.surf1 = 0x20;
    int idx = c.EvictAndStore(t);
    CHECK_EQ(idx, -1);
    CHECK_EQ(g_released, 2);          // tex's own surf0+surf1 released
    CHECK_EQ(t.surf0, 0u);
    CHECK_EQ(t.surf1, 0u);
}

// ============================================================================
// HiColTabBank::FindOrBuild (gilde.exe 0x5da04c)
// ============================================================================

TEST(TexLightReconHiColTab, FindOrBuildAllocatesAndMaps) {
    HiColTabBank bank;
    bank.capacity = 4;
    // 3 distinct colours
    u8 rgb[9] = { 255,0,0,  0,255,0,  0,0,255 };
    u8 out[3] = {0,0,0};
    int b = bank.FindOrBuild(rgb, 3, out);
    CHECK_EQ(b, 0);
    CHECK_EQ(bank.used, 1u);
    // count<256 -> a black entry (0,0,0) was seeded at index 0 first.
    // So red/green/blue get indices 1,2,3.
    CHECK_EQ((int)out[0], 1);
    CHECK_EQ((int)out[1], 2);
    CHECK_EQ((int)out[2], 3);
    // bank now has 4 used slots (black + 3) -> freeCount 252.
    CHECK_EQ(bank.banks[0].freeCount, 252);
    // The direct 565 value of pure red must match PackColor.
    CHECK_EQ(HiColTabDirect(bank.banks[0], 1),
             (u16)PackColor(Format565(), 255, 0, 0));
}

TEST(TexLightReconHiColTab, FindOrBuildDeduplicatesIntoSameBank) {
    HiColTabBank bank;
    bank.capacity = 4;
    u8 rgb1[3] = { 10,20,30 };
    u8 o1[1] = {0};
    bank.FindOrBuild(rgb1, 1, o1);
    CHECK_EQ(bank.used, 1u);
    int firstFree = bank.banks[0].freeCount;
    // Re-submit the same colour: should reuse bank 0, no new bank.
    u8 o2[1] = {0};
    int b = bank.FindOrBuild(rgb1, 1, o2);
    CHECK_EQ(b, 0);
    CHECK_EQ(bank.used, 1u);
    CHECK_EQ((int)o1[0], (int)o2[0]);          // same assigned index
    CHECK_EQ(bank.banks[0].freeCount, firstFree); // no new entry consumed
}

// ============================================================================
// BuildLog10ByteTable (gilde.exe 0x5d988c)
// ============================================================================

TEST(TexLightReconLog10Table, GoldenValues) {
    static i8 t[4097];
    BuildLog10ByteTable(t);
    // gilde.exe 0x5d988c: out[k] = trunc(log10(k-1)) — VIBE_Coord_ConvertX
    // @0x5c6b08 forces x87 round-toward-zero (cw HIBYTE=0x1F => RC=11) then
    // frndint, and the fistp inherits that control word, so the conversion
    // TRUNCATES toward zero (NOT round-to-nearest). Index 0 untouched.
    CHECK_EQ((int)t[1], 0);          // log10(0) -> 0 (indefinite path)
    CHECK_EQ((int)t[2], 0);          // log10(1) = 0
    // log10(9)=0.954 -> trunc 0 ; log10(10)=1 ; log10(99)=1.995 -> trunc 1 ;
    // log10(100)=2 ; log10(1000)=3 ; log10(4095)=3.612 -> trunc 3
    CHECK_EQ((int)t[10], 0);         // k=10 -> log10(9)=0.954 -> 0
    CHECK_EQ((int)t[11], 1);         // k=11 -> log10(10)=1
    CHECK_EQ((int)t[100], 1);        // k=100 -> log10(99)=1.995 -> 1
    CHECK_EQ((int)t[101], 2);        // k=101 -> log10(100)=2
    CHECK_EQ((int)t[1001], 3);       // k=1001 -> log10(1000)=3
    CHECK_EQ((int)t[4096], 3);       // k=4096 -> log10(4095)=3.612 -> 3
    // truncation boundaries: log10(3)=0.477 -> 0 ; log10(4)=0.602 -> 0.
    CHECK_EQ((int)t[4], 0);          // log10(3)
    CHECK_EQ((int)t[5], 0);          // log10(4)
}

TEST(TexLightReconSizing, RecordArrayByteFormulas) {
    // 776*count and 128*count.
    CHECK_EQ(HiColRecordArrayBytes(1), 776u);
    CHECK_EQ(HiColRecordArrayBytes(5), 776u * 5);
    CHECK_EQ(TextureRecordArrayBytes(1), 128u);
    CHECK_EQ(TextureRecordArrayBytes(10), 1280u);
}
