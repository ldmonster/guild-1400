// Golden-vector unit tests for guild::render model/AGF I/O + resource helpers.
// gilde.exe: VIBE_Model_ComputeNormals @0x5f8eb0, VIBE_Model_ComputeBounds
// @0x5f8f98, VIBE_Model_ComputeChunkSize @0x5f9430, the material-field Bio
// thunks @0x5e3fac.., VIBE_Resource_FindFreeSlot @0x40df94,
// VIBE_Resource_EvictOldestEntry @0x40decc.
#include "tests/framework/test.h"
#include "render/modelio_recon.h"

#include <cstring>
#include <cstdint>
#include <cmath>
#include <vector>

using namespace guild;
using namespace guild::render;

TEST(ModelIoReconNormals, FaceAndVertexNormalsForSingleTriangle) {
    std::vector<ModelVertex> verts(3, ModelVertex{});
    std::vector<ModelFace>   faces(1, ModelFace{});
    // Triangle in the z=0 plane; expected face normal along +/- Z.
    verts[0] = ModelVertex{0, 0, 0, 0, 0, 0};
    verts[1] = ModelVertex{1, 0, 0, 0, 0, 0};
    verts[2] = ModelVertex{0, 1, 0, 0, 0, 0};
    faces[0].v0 = 0; faces[0].v1 = 1; faces[0].v2 = 2;

    ModelRecord rec;
    rec.verts = verts.data(); rec.vertCount = 3;
    rec.faces = faces.data(); rec.faceCount = 1;

    ModelComputeNormals(rec);

    float nx = faces[0].nx, ny = faces[0].ny, nz = faces[0].nz;
    CHECK(std::fabs(nx) < 1e-5f);
    CHECK(std::fabs(ny) < 1e-5f);
    // gilde.exe 0x5f8eb0 calls TriangleNormal(v8=v0, v7=v1, /*out*/, v6=v2). The
    // original (0x5cb824) computes out = (a2-a1) x (a4-a1) = (v1-v0) x (v2-v0) =
    // (1,0,0)x(0,1,0) = (0,0,+1). The sign is load-bearing (an earlier reimpl
    // swapped the b/c args and flipped it to -1); pin nz=+1.
    CHECK(std::fabs(nz - 1.0f) < 1e-5f);

    for (int i = 0; i < 3; ++i) {
        CHECK(std::fabs(verts[i].nx - nx) < 1e-5f);
        CHECK(std::fabs(verts[i].ny - ny) < 1e-5f);
        CHECK(std::fabs(verts[i].nz - nz) < 1e-5f);
    }
}

TEST(ModelIoReconBounds, RadiusAndCornerVerts) {
    // 8 real verts + 8 corner slots.
    std::vector<ModelVertex> verts(16, ModelVertex{});
    int n = 0;
    for (int xi = -1; xi <= 1; xi += 2)
      for (int yi = -1; yi <= 1; yi += 2)
        for (int zi = -1; zi <= 1; zi += 2)
            verts[n++] = ModelVertex{(float)xi, (float)yi, (float)zi, 0, 0, 0};

    ModelRecord rec;
    rec.verts = verts.data(); rec.vertCount = 8;

    ModelComputeBounds(rec);

    CHECK(std::fabs(rec.radius - (float)std::sqrt(3.0)) < 1e-4f);

    // Corner 0 = (minX,minY,minZ) = (-1,-1,-1); corner 7 = (1,1,1).
    CHECK_EQ(verts[8].x, -1.0f);  CHECK_EQ(verts[8].y, -1.0f);  CHECK_EQ(verts[8].z, -1.0f);
    CHECK_EQ(verts[15].x, 1.0f);  CHECK_EQ(verts[15].y, 1.0f);  CHECK_EQ(verts[15].z, 1.0f);
    // Corner 1 = (maxX,minY,minZ) = (1,-1,-1).
    CHECK_EQ(verts[9].x, 1.0f);   CHECK_EQ(verts[9].y, -1.0f);  CHECK_EQ(verts[9].z, -1.0f);
    // Corner 4 = (minX,minY,maxZ) = (-1,-1,1).
    CHECK_EQ(verts[12].x, -1.0f); CHECK_EQ(verts[12].y, -1.0f); CHECK_EQ(verts[12].z, 1.0f);
}

TEST(ModelIoReconChunkSize, GoldenFormula) {
    std::vector<u8> rec(512, 0);
    auto putRec = [&](int wordIdx, i32 v) { std::memcpy(rec.data() + wordIdx * 4, &v, 4); };
    putRec(17, 2);     // normVtxCnt
    putRec(19, 3);     // faceCnt
    putRec(120, 1);    // materialCount (<= 254 path)

    std::vector<u8> aux(256, 0);
    std::vector<u8> matTable(224, 0);  // 1 material
    std::strcpy((char*)(matTable.data() + 0),   "abc");   // len 3
    std::strcpy((char*)(matTable.data() + 64),  "de");    // len 2
    std::strcpy((char*)(matTable.data() + 128), "f");     // len 1
    u8* mp = matTable.data();
    std::memcpy(aux.data() + 16, &mp, sizeof(mp));
    i32 subCount = 1; std::memcpy(aux.data() + 52, &subCount, 4);
    std::strcpy((char*)(aux.data() + 56), "xy");          // len 2 sub string

    //   v3=2 -> v4 = 24*(2+8)+24 = 264
    //   matCount=1<=254 -> v5 = 49*3 = 147
    //   v6 = 6*1 + 147 + 264 = 417
    //   material strings: (3+1)+(2+1)+(1+1) = 9 -> v6 = 426
    //   sub: v9=1 -> v11 = 24 + 426 = 450 ; + (2+1)=3 -> 453
    CHECK_EQ(ModelComputeChunkSize(rec.data(), aux.data()), 453);
}

TEST(ModelIoReconChunkSize, LargeMaterialCountPath) {
    std::vector<u8> rec(512, 0);
    auto putRec = [&](int wordIdx, i32 v) { std::memcpy(rec.data() + wordIdx * 4, &v, 4); };
    putRec(17, 0);     // normVtxCnt 0 -> v4 stays 24
    putRec(19, 2);     // faceCnt
    putRec(120, 300);  // materialCount > 254 -> 52*faceCnt path

    std::vector<u8> aux(64, 0);
    std::vector<u8> matTable((size_t)300 * 224, 0);  // all-empty strings (len 0)
    u8* mp = matTable.data();
    std::memcpy(aux.data() + 16, &mp, sizeof(mp));
    i32 subCount = 0; std::memcpy(aux.data() + 52, &subCount, 4);

    //   v4=24 ; v5 = 52*2 = 104 ; v6 = 6*300 + 104 + 24 = 1928
    //   material strings: 300 * 3*(0+1) = 900 -> v6 = 2828 ; sub 0 -> 2828
    CHECK_EQ(ModelComputeChunkSize(rec.data(), aux.data()), 2828);
}

// ---- material Bio thunks -------------------------------------------------
namespace {
struct FakeStream { std::vector<u8> bytes; size_t pos = 0; };
int fakeReadByte(void* s, u8* dst) {
    auto* fs = (FakeStream*)s;
    if (fs->pos >= fs->bytes.size()) return 0;
    *dst = fs->bytes[fs->pos++];
    return 1;
}
int fakeReadDword(void* s, u32* dst) {
    auto* fs = (FakeStream*)s;
    if (fs->pos + 4 > fs->bytes.size()) return 0;
    u32 v; std::memcpy(&v, fs->bytes.data() + fs->pos, 4); fs->pos += 4;
    *dst = v; return 4;
}
} // namespace

TEST(ModelIoReconThunks, WriteIntoCorrectMaterialFields) {
    ModelIoBioHooks h;
    h.readByte = fakeReadByte;
    h.readDword = fakeReadDword;
    SetModelIoBioHooks(h);

    std::vector<u8> matTable((size_t)3 * 224, 0);
    std::vector<u8> ctx(64, 0);
    i32 idx = 2; std::memcpy(ctx.data() + 8, &idx, 4);
    u8* mp = matTable.data();
    std::memcpy(ctx.data() + 16, &mp, sizeof(mp));

    u8* mat2 = matTable.data() + 224 * 2;

    FakeStream s; s.bytes = {0xAA, 0xBB,
                             0x11, 0x22, 0x33, 0x44,   // dword for +204
                             0x55, 0x66, 0x77, 0x88};  // dword for +208

    CHECK_EQ(ModelIoReadFloatThunk(&s, ctx.data()), 1);   // byte -> +200
    CHECK_EQ(ModelIoReadFloatThunk2(&s, ctx.data()), 1);  // byte -> +201
    CHECK_EQ(ModelIoReadDwordThunk(&s, ctx.data()), 4);   // dword -> +204
    CHECK_EQ(ModelIoReadDwordThunk2(&s, ctx.data()), 4);  // dword -> +208

    CHECK_EQ((int)mat2[200], 0xAA);
    CHECK_EQ((int)mat2[201], 0xBB);
    u32 d204; std::memcpy(&d204, mat2 + 204, 4);
    CHECK_EQ(d204, 0x44332211u);
    u32 d208; std::memcpy(&d208, mat2 + 208, 4);
    CHECK_EQ(d208, 0x88776655u);

    SetModelIoBioHooks(ModelIoBioHooks{});  // reset
}

// ---- resource handle table ----------------------------------------------
TEST(ModelIoReconFindFreeSlot, FirstFreeAndFull) {
    std::vector<u8> table((size_t)511 * 740, 0);
    auto setUsed = [&](int slot, u32 v) { std::memcpy(table.data() + (size_t)slot * 740 + 4, &v, 4); };

    CHECK_EQ(ResourceFindFreeSlot(table.data()), 0);

    setUsed(0, 1); setUsed(1, 7); setUsed(2, 99);
    CHECK_EQ(ResourceFindFreeSlot(table.data()), 3);

    for (int i = 0; i < 511; ++i) setUsed(i, 1);
    CHECK_EQ(ResourceFindFreeSlot(table.data()), -1);
}

// ---- LRU eviction --------------------------------------------------------
namespace {
int   g_freeCalls = 0;
void* g_lastFreed = nullptr;
void recordFree(void* p) { ++g_freeCalls; g_lastFreed = p; }
} // namespace

TEST(ModelIoReconEvict, EvictsOldestEligibleEntry) {
    ResourceFreeHook fh; fh.freeData = recordFree;
    SetResourceFreeHook(fh);
    g_freeCalls = 0; g_lastFreed = nullptr;

    ResourceEntry e[4] = {};
    // Entry 0 is the scan-skipped "slot 0".
    e[0] = ResourceEntry{(void*)(uintptr_t)0x1000, 10, 0, 0, 50};
    e[1] = ResourceEntry{(void*)(uintptr_t)0x2000, 20, 0, 0, 99};  // newest
    e[2] = ResourceEntry{(void*)(uintptr_t)0x3000, 30, 0, 0, 10};  // OLDEST eligible
    e[3] = ResourceEntry{(void*)(uintptr_t)0x4000, 40, 0, 0, 40};

    ResourceCache cache;
    cache.entries = e; cache.count = 4; cache.usedMem = 100;
    cache.floor = 0xFFFFFFFFu;

    CHECK_EQ(ResourceEvictOldestEntry(cache), 1);
    CHECK_EQ(g_freeCalls, 1);
    CHECK(g_lastFreed == (void*)(uintptr_t)0x3000);
    CHECK_EQ(cache.usedMem, 70);
    CHECK(e[2].data == nullptr);

    SetResourceFreeHook(ResourceFreeHook{});
}

TEST(ModelIoReconEvict, SkipsLockedPinnedAndEmpty) {
    ResourceFreeHook fh; fh.freeData = recordFree;
    SetResourceFreeHook(fh);
    g_freeCalls = 0; g_lastFreed = nullptr;

    ResourceEntry e[4] = {};
    e[0] = ResourceEntry{(void*)(uintptr_t)0x1000, 10, 0, 0, 50};
    e[1] = ResourceEntry{(void*)(uintptr_t)0x2000, 20, 5, 0, 1};   // locked
    e[2] = ResourceEntry{(void*)(uintptr_t)0x3000, 30, 0, 1, 2};   // pinned
    e[3] = ResourceEntry{(void*)(uintptr_t)0x4000, 40, 0, 0, 80};  // only eligible

    ResourceCache cache;
    cache.entries = e; cache.count = 4; cache.usedMem = 100;
    cache.floor = 0xFFFFFFFFu;

    CHECK_EQ(ResourceEvictOldestEntry(cache), 1);
    CHECK(g_lastFreed == (void*)(uintptr_t)0x4000);
    CHECK_EQ(cache.usedMem, 60);

    SetResourceFreeHook(ResourceFreeHook{});
}

TEST(ModelIoReconEvict, NothingEligibleReturnsZero) {
    g_freeCalls = 0;
    ResourceEntry e[2] = {};  // entry 1 has no data ptr -> ineligible
    ResourceCache cache;
    cache.entries = e; cache.count = 2; cache.usedMem = 0;
    cache.floor = 0xFFFFFFFFu;
    CHECK_EQ(ResourceEvictOldestEntry(cache), 0);
    CHECK_EQ(g_freeCalls, 0);
}

// ---- FreeEntryData -------------------------------------------------------
namespace {
struct FreeLog {
    int flushCalls = 0, releaseCalls = 0, freeCalls = 0, seekCalls = 0;
    int closeCalls = 0, returnCalls = 0, tempCalls = 0, postCalls = 0, extraCalls = 0;
    int lastReleaseFh = -1, lastSeekFh = -1, lastSeekPos = -1, lastCloseFh = -1;
    void* lastReturned = nullptr; u8 lastTempId = 0;
} g_log;
int  flb()           { ++g_log.flushCalls;  return 2; }   // flush returns status 2
void mrel(int fh)    { ++g_log.releaseCalls; g_log.lastReleaseFh = fh; }
int  fbk0(int)       { ++g_log.freeCalls;   return -1; }  // no block freed
int  fbk1(int)       { ++g_log.freeCalls;   return 7; }   // block freed (!= -1)
void fas(int fh,int p){ ++g_log.seekCalls;  g_log.lastSeekFh = fh; g_log.lastSeekPos = p; }
int  clh(int fh)     { ++g_log.closeCalls;  g_log.lastCloseFh = fh; return 4; } // status 4
void rtf(void* p)    { ++g_log.returnCalls; g_log.lastReturned = p; }
void dtf(u8 id)      { ++g_log.tempCalls;   g_log.lastTempId = id; }
void post()          { ++g_log.postCalls; }
void extra()         { ++g_log.extraCalls; }
ResourceFreeEntryHooks fullHooks() {
    ResourceFreeEntryHooks h;
    h.flushBuffer = flb; h.onMemoryRelease = mrel; h.freeBlock = fbk1;
    h.flushAndSeek = fas; h.closeHandle = clh; h.returnToFreeList = rtf;
    h.deleteTempFile = dtf; h.onPostRelease = post; h.onCloseExtra = extra;
    return h;
}
} // namespace

TEST(ModelIoReconFreeEntry, NoDataReturnsMinusOne) {
    g_log = FreeLog{};
    SetResourceFreeEntryHooks(fullHooks());
    ResourceEntryView ent;  // infoWord == 0
    CHECK_EQ(ResourceFreeEntryData(ent, true), -1);
    CHECK_EQ(g_log.flushCalls, 0);
    CHECK_EQ(g_log.releaseCalls, 0);
    SetResourceFreeEntryHooks(ResourceFreeEntryHooks{});
}

TEST(ModelIoReconFreeEntry, FullTeardownWithClose) {
    g_log = FreeLog{};
    SetResourceFreeEntryHooks(fullHooks());
    ResourceEntryView ent;
    // flags1 (low byte) bit3 set -> return spill; flags2 (byte1) bits 0x10|0x08 set
    // -> flush write buffer + delete temp file. infoWord = 0x0000_18_08.
    ent.infoWord = (0x18u << 8) | 0x08u;
    ent.fileHandle = 42;
    ent.spill = (void*)(uintptr_t)0xABCD;
    ent.tempId = 9;

    i32 r = ResourceFreeEntryData(ent, /*alsoClose=*/true);

    // status = flush(2) | close(4) = 6
    CHECK_EQ(r, 6);
    CHECK_EQ(g_log.flushCalls, 1);
    CHECK_EQ(g_log.releaseCalls, 1);   CHECK_EQ(g_log.lastReleaseFh, 42);
    CHECK_EQ(g_log.freeCalls, 1);
    CHECK_EQ(g_log.seekCalls, 1);      // freeBlock returned 7 (!= -1) -> seek
    CHECK_EQ(g_log.lastSeekFh, 42);    CHECK_EQ(g_log.lastSeekPos, 0);
    CHECK_EQ(g_log.closeCalls, 1);     CHECK_EQ(g_log.lastCloseFh, 42);
    CHECK_EQ(g_log.returnCalls, 1);    CHECK(g_log.lastReturned == (void*)(uintptr_t)0xABCD);
    CHECK(ent.spill == nullptr);       // spill pointer zeroed
    CHECK_EQ(g_log.tempCalls, 1);      CHECK_EQ((int)g_log.lastTempId, 9);
    CHECK_EQ(g_log.postCalls, 1);
    CHECK_EQ(g_log.extraCalls, 1);     // alsoClose -> onCloseExtra
    SetResourceFreeEntryHooks(ResourceFreeEntryHooks{});
}

TEST(ModelIoReconFreeEntry, NoFlagsNoCloseSkipsBranches) {
    g_log = FreeLog{};
    ResourceFreeEntryHooks h = fullHooks();
    h.freeBlock = fbk0;  // returns -1 -> no seek
    SetResourceFreeEntryHooks(h);

    ResourceEntryView ent;
    ent.infoWord = 0x00000001;  // non-zero (has data) but no teardown flag bits
    ent.fileHandle = 5;

    i32 r = ResourceFreeEntryData(ent, /*alsoClose=*/false);

    CHECK_EQ(r, 0);                    // no flush (flags2 bit4 unset), no close
    CHECK_EQ(g_log.flushCalls, 0);
    CHECK_EQ(g_log.releaseCalls, 1);   // unconditional
    CHECK_EQ(g_log.freeCalls, 1);
    CHECK_EQ(g_log.seekCalls, 0);      // freeBlock returned -1 -> no seek
    CHECK_EQ(g_log.closeCalls, 0);     // alsoClose false
    CHECK_EQ(g_log.returnCalls, 0);    // flags1 bit3 unset
    CHECK_EQ(g_log.tempCalls, 0);      // flags2 bit3 unset
    CHECK_EQ(g_log.postCalls, 1);      // unconditional
    CHECK_EQ(g_log.extraCalls, 0);     // alsoClose false
    SetResourceFreeEntryHooks(ResourceFreeEntryHooks{});
}
