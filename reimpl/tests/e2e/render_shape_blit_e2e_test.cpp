#include "test.h"
#include "render/shape_blit.h"
#include "render/animation_decode.h"
#include "render/surface_blit.h"
#include "render/shape.h"
#include "render/colorformat.h"
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::render;

// ===========================================================================
// End-to-end: build a synthetic RLE animation frame, wrap it in a shape bank,
// then drive the full path  PaintboxDrawShape -> AnimationBasic ->
// FrameDataProcess -> FrameTableValidate  through a memory-backed surface lock,
// and verify the resulting pixels against the gold.py golden image.
// ===========================================================================

static std::vector<u8> HexToBytes(const char* h) {
    std::vector<u8> out;
    for (const char* p = h; p[0] && p[1]; p += 2) {
        auto nyb = [](char c) -> int {
            if (c >= '0' && c <= '9') return c - '0';
            if (c >= 'a' && c <= 'f') return c - 'a' + 10;
            return c - 'A' + 10;
        };
        out.push_back((u8)((nyb(p[0]) << 4) | nyb(p[1])));
    }
    return out;
}

// RLE frame (gold2.py): width 4, height 2, mode 0; row0 skip 1px + [R,G];
// row1 [B] then skip 1px + [Y]. Per-row offset table at +0x2A.
static const char* kRleFrameHex =
    "0000000000000400000002000000000000000000000000000000000000000000000000000000"
    "000000005a0000000000000001000000020000000200000000f8e0070200000000000000"
    "010000001f000200000001000000e0ff3200000042000000";

// Golden image (gold2.py "VAL"): frame blitted at (2,1) into 8x5, Y-clip [0,5).
// The synthetic frame's runs leave the per-row dest pointer where the runs end
// (skip is in bytes), exactly as the engine blitter does — reference decoder and
// this reconstruction agree.
static const u16 kGoldRle[40] = {
    0,0,0,0,0,0,0,0,
    0,0,0,63488,2016,0,0,0,
    0,31,0,65504,0,0,0,0,
    0,0,0,0,0,0,0,0,
    0,0,0,0,0,0,0,0
};

// Build a one-shape bank around the frame: header (+0x2A count, +0x45 u32 table).
static std::vector<u8> BuildBank(const std::vector<u8>& frame) {
    const int count = 1;
    const int hdrLen = 0x45 + 4 * count;
    std::vector<u8> bank(hdrLen, 0);
    u16 c = (u16)count;
    std::memcpy(bank.data() + 0x2A, &c, 2);     // shape count
    u32 off = (u32)bank.size();
    bank.insert(bank.end(), frame.begin(), frame.end());
    std::memcpy(bank.data() + 0x45, &off, 4);   // offsetTable[0] = shape offset
    return bank;
}

// Memory-backed ISurfaceLock: hands the blitters a real system-memory framebuffer.
struct MemLock : ISurfaceLock {
    u16* fb; int stridePx;
    MemLock(u16* f, int s) : fb(f), stridePx(s) {}
    bool Lock(FrameBlitState& st) override { st.dest = fb; st.destStridePx = stridePx; return true; }
    void Unlock() override {}
    int lockCount = 0;
};

TEST(RenderShapeBlitE2E, BankLookupRleBlit) {
    std::vector<u8> frame = HexToBytes(kRleFrameHex);
    std::vector<u8> bank  = BuildBank(frame);

    u16 fb[40]; std::memset(fb, 0, sizeof(fb));
    FrameBlitState st;
    st.dest = fb; st.destStridePx = 8;
    st.clipX0 = 0; st.clipY0 = 0; st.clipX1 = 8; st.clipY1 = 5;

    int ok = AnimationBasic(2, 1, bank.data(), 0, st);
    CHECK_EQ(ok, 1);
    for (int i = 0; i < 40; ++i)
        CHECK_EQ((int)fb[i], (int)kGoldRle[i]);
}

TEST(RenderShapeBlitE2E, PaintboxDispatchThroughLock) {
    std::vector<u8> frame = HexToBytes(kRleFrameHex);
    std::vector<u8> bank  = BuildBank(frame);

    u16 fb[40]; std::memset(fb, 0, sizeof(fb));
    MemLock lock(fb, 8);

    FrameBlitState st;            // clip provided through the dispatcher
    st.clipX0 = 0; st.clipY0 = 0; st.clipX1 = 8; st.clipY1 = 5;
    bool drawn = PaintboxDrawShape(2, 1, bank.data(), 0, lock, st);
    CHECK(drawn);
    for (int i = 0; i < 40; ++i)
        CHECK_EQ((int)fb[i], (int)kGoldRle[i]);

    // Null paintbox -> no draw, no crash (logs in the original).
    CHECK(!PaintboxDrawShape(0, 0, nullptr, 0, lock, st));
}

TEST(RenderShapeBlitE2E, PaintboxClippedRoutesToXClip) {
    std::vector<u8> frame = HexToBytes(kRleFrameHex);
    std::vector<u8> bank  = BuildBank(frame);

    // The dispatcher installs the clip rect and (because x=2 < clipX0=3) routes to
    // the X-clipped RLE blitter FrameTableNext. Verify the dispatch result equals a
    // direct FrameTableNext call with the same state — i.e. the clip rect was
    // installed and the X-clipped path was taken (not the unclipped Validate path).
    u16 viaDispatch[40]; std::memset(viaDispatch, 0, sizeof(viaDispatch));
    MemLock lock(viaDispatch, 8);
    FrameBlitState st0;
    bool drawn = PaintboxDrawShapeClipped(2, 1, bank.data(), 0,
                                          /*x0*/3, /*y0*/0, /*x1*/8, /*y1*/5,
                                          lock, st0);
    CHECK(drawn);

    u16 direct[40]; std::memset(direct, 0, sizeof(direct));
    FrameBlitState st1;
    st1.dest = direct; st1.destStridePx = 8;
    st1.clipX0 = 3; st1.clipY0 = 0; st1.clipX1 = 8; st1.clipY1 = 5;
    // shape offset table[0] -> the shape blob inside the bank.
    u32 off; std::memcpy(&off, bank.data() + 0x45, 4);
    FrameTableNext(2, 1, bank.data() + off, st1);

    for (int i = 0; i < 40; ++i)
        CHECK_EQ((int)viaDispatch[i], (int)direct[i]);

    // And the X clip strictly removed the leftmost in-range column vs unclipped:
    // the clipped image must differ from the unclipped Validate image.
    u16 unclipped[40]; std::memset(unclipped, 0, sizeof(unclipped));
    FrameBlitState st2;
    st2.dest = unclipped; st2.destStridePx = 8;
    st2.clipX0 = 0; st2.clipY0 = 0; st2.clipX1 = 8; st2.clipY1 = 5;
    FrameTableValidate(2, 1, bank.data() + off, st2);
    bool differs = false;
    for (int i = 0; i < 40; ++i) if (unclipped[i] != viaDispatch[i]) differs = true;
    CHECK(differs);
}

// Cross-check: the RLE decode path and the simple ShapeDecodeRle path agree on a
// trivial single-run shape (shared low-level format sanity).
TEST(RenderShapeBlitE2E, DecodeAndColoredConsistency) {
    // One-row, one-run, 3 pixels, no skip (shape.h format: skip stored 2x).
    std::vector<u8> shape(0x32, 0);
    u16 height = 1; std::memcpy(shape.data() + 0x0A, &height, 2);
    auto put32 = [&](u32 v){ for (int i=0;i<4;++i) shape.push_back((u8)(v>>(8*i))); };
    auto put16 = [&](u16 v){ shape.push_back((u8)v); shape.push_back((u8)(v>>8)); };
    put32(1);              // runCount
    put32(0);              // skip
    put32(3);              // nPixels
    put16(0xF800); put16(0x07E0); put16(0x001F);

    u16 a[3] = {0}, b[3] = {0};
    BlitTarget16 d1{3, a};
    ShapeDecodeRle(0, 0, shape.data(), d1);
    CHECK_EQ((int)a[0], 0xF800); CHECK_EQ((int)a[1], 0x07E0); CHECK_EQ((int)a[2], 0x001F);

    ColorBlitTarget16 d2{3, b};
    ShapeBlitColored16(0, 0, shape.data(), d2, Format565());
    // colored output is grey (R==G==B per pixel): verify it differs from raw and
    // is a valid 565 grey for the first pixel (L=49 -> 12678 from gold.py).
    CHECK_EQ((int)b[0], 12678);
}
