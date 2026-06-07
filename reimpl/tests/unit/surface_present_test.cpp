#include "test.h"

#include "render/surface_present.h"
#include "render/colorformat.h"

#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::render;

// ---------------------------------------------------------------------------
// A memory-backed IDDrawSurface mock: a single locked system-memory block with a
// scriptable error sequence so the retry-on-SURFACELOST path can be exercised.
// ---------------------------------------------------------------------------
namespace {
struct MockDDrawSurface : IDDrawSurface {
    std::vector<std::uint8_t> mem;
    DDrawLock desc{};
    int lockCalls = 0, unlockCalls = 0, restoreCalls = 0, descCalls = 0;
    int restoresLeft = 0;     // # of SURFACELOST replies before Lock succeeds
    i32 restoreResult = 0;    // what Restore returns (0 = keep retrying)
    u32 lastLockFlags = 0;

    void setup(int w, int h, int bpp) {
        int bytesPerPx = bpp / 8;
        desc.pitch = static_cast<u32>(w * bytesPerPx);
        desc.bitDepth = static_cast<u32>(bpp);
        mem.assign(static_cast<std::size_t>(desc.pitch) * h, 0);
        desc.pixels = mem.data();
    }
    i32 GetSurfaceDesc(DDrawLock& out) override { ++descCalls; out = desc; return 0; }
    i32 Lock(u32 flags, DDrawLock& out) override {
        ++lockCalls; lastLockFlags = flags;
        if (restoresLeft > 0) { --restoresLeft; return kDDErrSurfaceLost; }
        out = desc;
        return 0;
    }
    i32 Unlock() override { ++unlockCalls; return 0; }
    i32 Restore() override { ++restoreCalls; return restoreResult; }
};
} // namespace

// LockSurface fills the present globals from the desc and returns the base.
TEST(SurfacePresent, LockSurfaceFillsGlobals) {
    MockDDrawSurface s; s.setup(320, 240, 16);
    PresentGlobals g; g.bytesPerPx = 2; // 16bpp divisor
    auto base = LockSurface(s, g);
    CHECK(base != 0);
    CHECK_EQ(s.lockCalls, 1);
    CHECK_EQ(g.lockPitch, 640);          // 320 * 2
    CHECK_EQ(g.pitchBytes, 640);
    CHECK_EQ(g.lockBitDepth, 16);
    CHECK_EQ(g.pitchExtra, 16 >> 3);     // 2
    CHECK_EQ(g.strideWords, 640 / 2);    // 320 pixels/row
    CHECK_EQ(s.lastLockFlags, 1u);       // DDLOCK_SURFACEMEMORYPTR
}

// LockSurface retries while Restore() succeeds after DDERR_SURFACELOST.
TEST(SurfacePresent, LockSurfaceRetriesOnLost) {
    MockDDrawSurface s; s.setup(64, 64, 16);
    s.restoresLeft = 2; s.restoreResult = 0;     // two lost replies then OK
    PresentGlobals g; g.bytesPerPx = 2;
    auto base = LockSurface(s, g);
    CHECK(base != 0);
    CHECK_EQ(s.lockCalls, 3);    // lost, lost, ok
    CHECK_EQ(s.restoreCalls, 2);
}

// If Restore() fails (non-zero) the lock aborts and returns 0.
TEST(SurfacePresent, LockSurfaceRestoreFailAborts) {
    MockDDrawSurface s; s.setup(64, 64, 16);
    s.restoresLeft = 5; s.restoreResult = -1;    // Restore never recovers
    PresentGlobals g; g.bytesPerPx = 2;
    auto base = LockSurface(s, g);
    CHECK_EQ(base, std::uintptr_t(0));
    CHECK_EQ(s.lockCalls, 1);
    CHECK_EQ(s.restoreCalls, 1);
}

// LockSurfaceWait ORs in the 0x801 WAIT|MEMPTR flags.
TEST(SurfacePresent, LockSurfaceWaitFlags) {
    MockDDrawSurface s; s.setup(32, 32, 32);
    PresentGlobals g; g.bytesPerPx = 4;
    auto base = LockSurfaceWait(s, 0, g);
    CHECK(base != 0);
    CHECK_EQ(s.lastLockFlags, 0x801u);
    CHECK_EQ(g.strideWords, (32 * 4) / 4); // 32 pixels/row
}

// BeginFrameLock — GDI mode with a DIB base: negated pitch/stride (bottom-up).
TEST(SurfacePresent, BeginFrameLockGdiDib) {
    PresentGlobals g;
    g.mode = PresentBackend::GdiBitBlt;
    g.dibBase = 0x1000; g.dibPitch = 640; g.dibStride = 320;
    CHECK(BeginFrameLock(g));
    CHECK_EQ(g.targetBase, std::uintptr_t(0x1000));
    CHECK_EQ(g.pitchBytes, -640);
    CHECK_EQ(g.strideWords, -320);
}

// BeginFrameLock — GDI mode with no DIB base falls through to the ppvBits path.
TEST(SurfacePresent, BeginFrameLockGdiFallthrough) {
    PresentGlobals g;
    g.mode = PresentBackend::GdiBitBlt;
    g.dibBase = 0; g.ppvBits = 0x2000; g.dibPitch = 640; g.dibStride = 320;
    CHECK(BeginFrameLock(g));
    CHECK_EQ(g.targetBase, std::uintptr_t(0x2000));
    CHECK_EQ(g.pitchBytes, 640);     // positive (not the DIB-flip path)
    CHECK_EQ(g.strideWords, 320);
}

// BeginFrameLock — Lock-copy modes (2/4) draw straight into ppvBits.
TEST(SurfacePresent, BeginFrameLockLockCopy) {
    PresentGlobals g;
    g.mode = PresentBackend::DDrawLockBlt;
    g.ppvBits = 0x3000; g.dibPitch = 1280; g.dibStride = 640;
    CHECK(BeginFrameLock(g));
    CHECK_EQ(g.targetBase, std::uintptr_t(0x3000));
    CHECK_EQ(g.pitchBytes, 1280);
    CHECK_EQ(g.strideWords, 640);
}

// BeginFrameLock — Blt/Flip modes lock the primary; base == lock result.
TEST(SurfacePresent, BeginFrameLockDDrawPrimary) {
    MockDDrawSurface s; s.setup(100, 50, 16);
    PresentGlobals g;
    g.mode = PresentBackend::DDrawBlt;
    g.primary = &s; g.bytesPerPx = 2;
    CHECK(BeginFrameLock(g));
    CHECK(g.targetBase != 0);
    CHECK_EQ(s.lockCalls, 1);
}

// AcquireBackBuffer uses the WAIT lock for the DDraw modes.
TEST(SurfacePresent, AcquireBackBufferWaitLock) {
    MockDDrawSurface s; s.setup(80, 60, 16);
    PresentGlobals g;
    g.mode = PresentBackend::DDrawFlip;
    g.primary = &s; g.bytesPerPx = 2;
    CHECK(AcquireBackBuffer(g));
    CHECK_EQ(s.lastLockFlags, 0x801u);
}

// UnlockBackBuffer — Lock-copy / GDI modes just clear the held lock, no Unlock.
TEST(SurfacePresent, UnlockBackBufferLockModesClearOnly) {
    MockDDrawSurface s; s.setup(16, 16, 16);
    PresentGlobals g;
    g.mode = PresentBackend::DDrawLockBlt;
    g.primary = &s; g.targetBase = std::uintptr_t(0x1234);
    i32 r = UnlockBackBuffer(g, 7);
    CHECK_EQ(r, 7);                  // passthrough
    CHECK_EQ(g.targetBase, std::uintptr_t(0));
    CHECK_EQ(s.unlockCalls, 0);
}

// UnlockBackBuffer — Blt/Flip modes Unlock the primary.
TEST(SurfacePresent, UnlockBackBufferDDrawUnlocks) {
    MockDDrawSurface s; s.setup(16, 16, 16);
    PresentGlobals g;
    g.mode = PresentBackend::DDrawFlip;
    g.primary = &s; g.targetBase = std::uintptr_t(0x1234);
    UnlockBackBuffer(g, 0);
    CHECK_EQ(s.unlockCalls, 1);
    CHECK_EQ(g.targetBase, std::uintptr_t(0));
}

// UnlockBackBuffer — no lock held: pure passthrough, nothing touched.
TEST(SurfacePresent, UnlockBackBufferNoLock) {
    PresentGlobals g;
    g.mode = PresentBackend::DDrawFlip;
    g.targetBase = std::uintptr_t(0);
    CHECK_EQ(UnlockBackBuffer(g, 99), 99);
}

// CopyRegionRgb — golden vector. RGB565 unpack of a 2x2 block with a known stride.
// Compute oracle below with the same shifts the format uses.
TEST(SurfacePresent, CopyRegionRgbGolden) {
    ColorFormat fmt = Format565(); // R=5@11, G=6@5, B=5@0; rPrec=3 gPrec=2 bPrec=3
    // A 2x2 region inside a surface whose row stride is 4 pixels (so cols 0..1).
    const int strideWords = 4;
    std::vector<u16> src(strideWords * 2, 0);
    // Pixel values: pure red, pure green, pure blue, white.
    src[0] = 0xF800; // (0,0) red
    src[1] = 0x07E0; // (1,0) green
    src[strideWords + 0] = 0x001F; // (0,1) blue
    src[strideWords + 1] = 0xFFFF; // (1,1) white

    PresentGlobals g; g.strideWords = strideWords;
    std::vector<u8> dst(2 * 2 * 3, 0xAB);
    u32 ret = CopyRegionRgb(2, 2, src.data(), dst.data(), fmt, g);
    CHECK_EQ(ret, 6u); // 3 * cols

    // Oracle: r = px>>11<<3, g = px>>5<<2, b = px>>0<<3 (truncated to u8).
    // Destination byte order is the original's R,B,G (out[0]=R, out[1]=B, out[2]=G).
    auto unpack = [](u16 px, u8& r, u8& g2, u8& b) {
        r = static_cast<u8>(px >> 11 << 3);
        g2 = static_cast<u8>(px >> 5 << 2);
        b = static_cast<u8>(px >> 0 << 3);
    };
    u16 px[4] = {0xF800, 0x07E0, 0x001F, 0xFFFF};
    for (int i = 0; i < 4; ++i) {
        u8 r, gg, b; unpack(px[i], r, gg, b);
        CHECK_EQ(dst[i * 3 + 0], r);   // R
        CHECK_EQ(dst[i * 3 + 1], b);   // B
        CHECK_EQ(dst[i * 3 + 2], gg);  // G
    }
    // red -> (R,B,G) = (0xF8,0,0)
    CHECK_EQ(dst[0], 0xF8); CHECK_EQ(dst[1], 0); CHECK_EQ(dst[2], 0);
    // green -> (0,0,0xFC)
    CHECK_EQ(dst[3], 0); CHECK_EQ(dst[4], 0); CHECK_EQ(dst[5], 0xFC);
    // blue -> (0,0xF8,0)
    CHECK_EQ(dst[6], 0); CHECK_EQ(dst[7], 0xF8); CHECK_EQ(dst[8], 0);
}

// CopyRegionRgb honours the source stride (skips the padding pixels per row).
TEST(SurfacePresent, CopyRegionRgbHonoursStride) {
    ColorFormat fmt = Format565();
    const int strideWords = 5;       // each row is 5 wide, copy only 2 cols
    std::vector<u16> src(strideWords * 3, 0);
    src[strideWords * 2 + 0] = 0xF800; // row 2, col 0 = red
    PresentGlobals g; g.strideWords = strideWords;
    std::vector<u8> dst(2 * 3 * 3, 0);
    CopyRegionRgb(3, 2, src.data(), dst.data(), fmt, g);
    // Dest is contiguous 2-wide: row2 col0 -> dst index (col + cols*row)=(0+2*2)=4
    int idx = (0 + 2 * 2) * 3;
    CHECK_EQ(dst[idx + 0], 0xF8);
}

// CopySurfacePixels — same-size 16bpp copy via the stretch dispatch (memcpy path).
// The original's same-size branch loops the row count as a1[3]==WIDTH (it is used
// for square capture blocks), so we use a square w==h region to match that
// contract faithfully.
TEST(SurfacePresent, CopySurfacePixelsSameSize16) {
    const int w = 8, h = 8;
    std::vector<u16> srcPix(w * h), dstPix(w * h, 0);
    for (int i = 0; i < w * h; ++i) srcPix[i] = static_cast<u16>(0x1000 + i);

    StretchSurfaceDesc src;
    src.width = w; src.height = h; src.pitch = w * 2; src.bpp = 16;
    src.pixels = reinterpret_cast<u8*>(srcPix.data());
    StretchSurfaceDesc dst = src;
    dst.pixels = reinterpret_cast<u8*>(dstPix.data());

    CHECK(CopySurfacePixels(dst, src));
    CHECK(std::memcmp(srcPix.data(), dstPix.data(), srcPix.size() * 2) == 0);
}

// CopySurfacePixels rejects unlocked (null pixel) surfaces.
TEST(SurfacePresent, CopySurfacePixelsRejectsNull) {
    StretchSurfaceDesc dst, src;
    dst.bpp = src.bpp = 16; dst.width = src.width = 4; dst.height = src.height = 4;
    src.pixels = nullptr; dst.pixels = reinterpret_cast<u8*>(&dst);
    CHECK(!CopySurfacePixels(dst, src));
}

// ReportDDrawError maps known codes and falls back to hex.
TEST(SurfacePresent, ReportDDrawError) {
    CHECK(std::strcmp(ReportDDrawError(0), "") == 0);
    CHECK(std::strcmp(ReportDDrawError(kDDErrSurfaceLost), "DDERR_SURFACELOST") == 0);
    CHECK(std::strcmp(ReportDDrawError(static_cast<i32>(0x8876017Cu)),
                      "DDERR_OUTOFVIDEOMEMORY") == 0);
    // Unknown code -> hex fallback, non-empty.
    const char* f = ReportDDrawError(static_cast<i32>(0x88761234u));
    CHECK(std::strncmp(f, "DDERR_0x", 8) == 0);
}
