// Golden tests for assorted engine leaves.
//   0x41e57c StateFinalize, 0x429290 RainDestroy, 0x54f884 DragSlotResetGridTable,
//   0x423648 ResultFinalize / 0x42395c ResultHandlerInteraction.
#include "test.h"
#include "sim/misc_recon4_leaves.h"

#include <vector>

using namespace guild::sim;
using guild::i32; using guild::u8; using guild::u16;

// ----- StateFinalize --------------------------------------------------------
namespace {
struct FakeStateBank {
    int  tag = 7;
    i32  recordPtr = 0x1000;
    i32  edx = 0;
    u16  lineH = 19;
};
int   bankTag(void* c, int) { return static_cast<FakeStateBank*>(c)->tag; }
i32   bankUpdate(void* c, int, i32* e) { auto* b = static_cast<FakeStateBank*>(c); *e = b->edx; return b->recordPtr; }
u16   bankLine(void* c, i32) { return static_cast<FakeStateBank*>(c)->lineH; }
} // namespace

static StateEnv makeStateEnv(FakeStateBank* b, i32 base) {
    StateEnv e;
    e.recordTag60 = &bankTag;
    e.stateUpdate = &bankUpdate;
    e.recordLineHeight = &bankLine;
    e.ctx = b;
    e.baseStateIndex = base;
    return e;
}

TEST(MiscRecon4, StateFinalizeTagMismatchReturnsZero) {
    FakeStateBank b; b.tag = 5;
    StateOutputs out;
    i32 r = StateFinalize(makeStateEnv(&b, 0), 3, &out);
    CHECK_EQ(r, 0);
}

TEST(MiscRecon4, StateFinalizeSpacing8_2WhenEdxEqualsBase) {
    FakeStateBank b; b.edx = 10; b.recordPtr = 0xABCD; b.lineH = 21;
    StateOutputs out;
    i32 r = StateFinalize(makeStateEnv(&b, 10), 2, &out);
    CHECK_EQ(r, 0xABCD);
    CHECK_EQ(out.recordPtr, 0xABCD);
    CHECK_EQ(out.recordEdx, 10);
    CHECK_EQ(out.lineHeight, 21);
    CHECK_EQ(out.spacingA, 8);
    CHECK_EQ(out.spacingB, 2);
}

TEST(MiscRecon4, StateFinalizeSpacing8_2WhenEdxEqualsBasePlus2) {
    FakeStateBank b; b.edx = 12;
    StateOutputs out;
    StateFinalize(makeStateEnv(&b, 10), 0, &out);
    CHECK_EQ(out.spacingA, 8);
    CHECK_EQ(out.spacingB, 2);
}

TEST(MiscRecon4, StateFinalizeSpacing4_2Otherwise) {
    FakeStateBank b; b.edx = 99;
    StateOutputs out;
    StateFinalize(makeStateEnv(&b, 10), 0, &out);
    CHECK_EQ(out.spacingA, 4);
    CHECK_EQ(out.spacingB, 2);
}

// ----- RainDestroy ----------------------------------------------------------
namespace {
struct FreeRecorder { std::vector<u8*> freed; };
void recFree(void* c, u8* p) { static_cast<FreeRecorder*>(c)->freed.push_back(p); }
} // namespace

TEST(MiscRecon4, RainDestroyFreesSubThenSelf) {
    FreeRecorder rec;
    RainFreeHooks h; h.freeDebug = &recFree; h.ctx = &rec;
    u8 baseMem[4]; u8 subMem[4];
    RainObject obj; obj.base = baseMem; obj.sub = subMem;
    u8* ret = RainDestroy(&obj, h);
    CHECK_EQ(ret, baseMem);
    CHECK_EQ((int)rec.freed.size(), 2);
    CHECK_EQ(rec.freed[0], subMem);   // sub-buffer first
    CHECK_EQ(rec.freed[1], baseMem);  // then self
    CHECK(obj.sub == nullptr);        // slot cleared
}

TEST(MiscRecon4, RainDestroyNoSubOnlyFreesSelf) {
    FreeRecorder rec;
    RainFreeHooks h; h.freeDebug = &recFree; h.ctx = &rec;
    u8 baseMem[4];
    RainObject obj; obj.base = baseMem; obj.sub = nullptr;
    u8* ret = RainDestroy(&obj, h);
    CHECK_EQ(ret, baseMem);
    CHECK_EQ((int)rec.freed.size(), 1);
    CHECK_EQ(rec.freed[0], baseMem);
}

TEST(MiscRecon4, RainDestroyNullIsNoop) {
    FreeRecorder rec;
    RainFreeHooks h; h.freeDebug = &recFree; h.ctx = &rec;
    RainObject obj; obj.base = nullptr;
    u8* ret = RainDestroy(&obj, h);
    CHECK(ret == nullptr);
    CHECK_EQ((int)rec.freed.size(), 0);
}

// ----- DragSlotResetGridTable ----------------------------------------------
TEST(MiscRecon4, DragGridResetClearsAllRows) {
    std::vector<DragGridRow> rows(kGridRows);
    // Pre-fill with junk to verify it gets cleared.
    for (auto& r : rows) {
        for (int k = 0; k < 4; ++k) r.header[k] = 7;
        for (int k = 0; k < 8; ++k) { r.cellId[k] = 5; r.cellFlag[k] = 9; }
    }
    DragSlotResetGridTable(rows.data());

    for (int i = 0; i < kGridRows; ++i) {
        const DragGridRow& r = rows[i];
        CHECK_EQ(r.header[0], 0);
        CHECK_EQ(r.header[1], -1);
        CHECK_EQ(r.header[2], -1);
        CHECK_EQ(r.header[3], -1);
        // cells 1..6 reset; cells 0 and 7 untouched (still junk).
        for (int c = 1; c <= 6; ++c) {
            CHECK_EQ(r.cellId[c], -1);
            CHECK_EQ((int)r.cellFlag[c], 0);
        }
        CHECK_EQ(r.cellId[0], 5);    // index 0 not touched by the inner loop
        CHECK_EQ(r.cellId[7], 5);    // index 7 not touched
    }
}

// ----- ResultFinalize (clip + software copy) --------------------------------
namespace {
Surface makeSurface(int w, int h, int bpp, std::vector<u8>& buf) {
    Surface s;
    s.bppByte = bpp;
    s.strideBytes = (bpp >> 3);   // one pixel per "column step" for the test
    s.clipMinX = 0; s.clipMinY = 0;
    s.clipMaxX = w; s.clipMaxY = h;
    buf.assign((size_t)w * h * (bpp >> 3) + 64, 0);
    s.pixels = buf.data();
    s.ddObj = nullptr;   // forces the software path
    return s;
}
} // namespace

TEST(MiscRecon4, ResultFinalizeEmptyRectReturnsZero) {
    std::vector<u8> sb, db;
    Surface src = makeSurface(8, 8, 8, sb);
    Surface dst = makeSurface(8, 8, 8, db);
    BlitHooks h;  // no hooks
    // zero width
    int r = ResultFinalize(&src, &dst, /*dstY*/0, /*dstX*/0, /*w*/0, /*h*/4,
                           /*srcY*/0, /*srcX*/0, false, h);
    CHECK_EQ(r, 0);
}

TEST(MiscRecon4, ResultFinalizeSoftwareCopyRuns) {
    // 8bpp, stride 1, src/dst 8x8. Copy a 4x4 block; verify success + decompress
    // hooks are invoked on the software path.
    std::vector<u8> sb, db;
    Surface src = makeSurface(8, 8, 8, sb);
    Surface dst = makeSurface(8, 8, 8, db);
    int decompressCalls = 0;
    BlitHooks h;
    h.ctx = &decompressCalls;
    h.decompressBlob = [](void* c, Surface*, int){ (*(int*)c)++; };
    h.decompressFinalize = [](void* c, Surface*){ (*(int*)c)++; };
    int r = ResultFinalize(&src, &dst, 0, 0, 4, 4, 0, 0, false, h);
    CHECK_EQ(r, 1);
    // software path: 2 decompressBlob (pre) + post bookkeeping (2 finalize, lockState=0)
    CHECK(decompressCalls >= 2);
}

TEST(MiscRecon4, ResultFinalizeClampsToClipWindow) {
    // dst clip window starts at x=2,y=2: a negative-origin copy should clamp w/h.
    std::vector<u8> sb, db;
    Surface src = makeSurface(8, 8, 8, sb);
    Surface dst = makeSurface(8, 8, 8, db);
    dst.clipMinX = 2; dst.clipMinY = 2;
    BlitHooks h;
    // src origin below dst clip -> rect shrinks but stays positive.
    int r = ResultFinalize(&src, &dst, 0, 0, 6, 6, 0, 0, false, h);
    CHECK_EQ(r, 1);   // still a non-empty rect after clamping
}

TEST(MiscRecon4, ResultHandlerInteractionIsAlphaOffWrapper) {
    std::vector<u8> sb, db;
    Surface src = makeSurface(8, 8, 8, sb);
    Surface dst = makeSurface(8, 8, 8, db);
    bool sawAlpha = true;
    BlitHooks h;
    h.ctx = &sawAlpha;
    h.decompressBlob = [](void*, Surface*, int){};
    h.decompressFinalize = [](void*, Surface*){};
    // No GPU; software path. Just confirm it returns success like Finalize(...,0).
    int r = ResultHandlerInteraction(&src, &dst, 0, 0, 4, 4, 0, 0, h);
    CHECK_EQ(r, 1);
    (void)sawAlpha;
}
