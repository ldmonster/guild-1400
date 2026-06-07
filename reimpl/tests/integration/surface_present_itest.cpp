#include "test.h"

// Cross-module integration: drive the reconstructed present lock/unlock/compose
// path (render/surface_present) against REAL sibling modules — the shim graphics
// backends (MemoryGraphicsDevice), the colorformat unpacker, and the surface
// stretch dispatch — exactly as the engine's frame loop wires them together.

#include "render/surface_present.h"
#include "render/surface_stretch.h"
#include "render/colorformat.h"
#include "shim_impl/memory_graphics.h"

#include <cstdint>
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::render;

namespace {

// Adapt a shim::IGraphicsDevice backbuffer Lock onto the DDraw IDDrawSurface the
// present code calls. This is precisely the role the original's DDraw primary
// surface played: Lock yields the framebuffer pixels/pitch/depth; Unlock presents.
struct DeviceSurfaceAdapter : IDDrawSurface {
    guild::shim::IGraphicsDevice& dev;
    explicit DeviceSurfaceAdapter(guild::shim::IGraphicsDevice& d) : dev(d) {}
    int lockCount = 0, unlockCount = 0;

    i32 GetSurfaceDesc(DDrawLock& out) override {
        auto* bb = dev.backbuffer();
        if (!bb) return kDDErrSurfaceLost;
        out.pixels = bb->pixels;
        out.pitch = static_cast<u32>(bb->pitch);
        out.bitDepth = static_cast<u32>(bb->bpp);
        return 0;
    }
    i32 Lock(u32 /*flags*/, DDrawLock& out) override {
        ++lockCount;
        return GetSurfaceDesc(out);
    }
    i32 Unlock() override { ++unlockCount; dev.present(); return 0; }
    i32 Restore() override { return 0; }
};

} // namespace

// Full DDraw-Blt-mode frame: AcquireBackBuffer locks the real device framebuffer,
// the renderer fills it, UnlockBackBuffer unlocks + presents through the backend,
// and the presented snapshot matches what we drew.
TEST(SurfacePresentITest, BltModeLockDrawUnlockPresents) {
    guild::shim::MemoryGraphicsDevice dev;
    CHECK(dev.init(64, 32, 16, false));
    DeviceSurfaceAdapter surf(dev);

    PresentGlobals g;
    g.mode = PresentBackend::DDrawBlt; // mode 1: draw straight into the lock
    g.primary = &surf;
    g.bytesPerPx = 2;

    CHECK(AcquireBackBuffer(g));
    CHECK_EQ(surf.lockCount, 1);
    CHECK(g.targetBase != 0);
    CHECK_EQ(g.strideWords, 64); // 128-byte pitch / 2

    // Draw a recognizable pattern straight into the locked framebuffer.
    auto* px = reinterpret_cast<u16*>(static_cast<std::intptr_t>(g.targetBase));
    for (int i = 0; i < 64 * 32; ++i) px[i] = static_cast<u16>(0xF800 + (i & 0x1F));

    i32 r = UnlockBackBuffer(g, 0);
    CHECK_EQ(r, 0);
    CHECK_EQ(surf.unlockCount, 1);
    CHECK_EQ(g.targetBase, std::uintptr_t(0));
    CHECK_EQ(dev.presentCount(), 1);

    // The presented snapshot equals the bytes we wrote.
    const auto& shot = dev.lastPresented();
    CHECK_EQ(shot.size(), static_cast<std::size_t>(64 * 32 * 2));
    const u16* sp = reinterpret_cast<const u16*>(shot.data());
    CHECK_EQ(sp[0], static_cast<u16>(0xF800));
    CHECK_EQ(sp[31], static_cast<u16>(0xF81F));
}

// Lock-copy mode (2): renderer draws into the software DIB (ppvBits), present
// would memcpy it to the device. Here we verify the acquire targets ppvBits and
// the readback path (CopyRegionRgb) decodes the DIB via the real colorformat.
TEST(SurfacePresentITest, LockCopyModeReadbackToRgb) {
    const int w = 16, h = 8;
    std::vector<u16> dib(static_cast<std::size_t>(w) * h, 0);

    PresentGlobals g;
    g.mode = PresentBackend::DDrawLockBlt; // mode 2
    g.ppvBits = reinterpret_cast<std::uintptr_t>(dib.data());
    g.dibPitch = w * 2;
    g.dibStride = w;

    CHECK(BeginFrameLock(g));
    CHECK_EQ(g.targetBase, g.ppvBits);
    CHECK_EQ(g.strideWords, w);

    // Fill: column c, row r -> distinct 565 colours.
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            dib[static_cast<std::size_t>(y) * w + x] =
                static_cast<u16>((x << 11) | (y << 5) | (x & 0x1F));

    // Readback the whole region as 24-bit RGB through the real unpacker.
    std::vector<u8> rgb(static_cast<std::size_t>(w) * h * 3, 0);
    u32 ret = CopyRegionRgb(static_cast<u32>(h), static_cast<u32>(w),
                            dib.data(), rgb.data(), Format565(), g);
    CHECK_EQ(ret, static_cast<u32>(3 * w));

    // Spot-check against the sibling UnpackColor directly. CopyRegionRgb stores the
    // original's R,B,G byte order (out[0]=R, out[1]=B, out[2]=G).
    for (int idx : {0, 5, w * h - 1}) {
        u8 er, eg, eb;
        UnpackColor(Format565(), dib[idx], er, eg, eb);
        CHECK_EQ(rgb[idx * 3 + 0], er);  // R
        CHECK_EQ(rgb[idx * 3 + 1], eb);  // B
        CHECK_EQ(rgb[idx * 3 + 2], eg);  // G
    }

    UnlockBackBuffer(g, 0); // mode 2: just clears the lock (no device touch)
    CHECK_EQ(g.targetBase, std::uintptr_t(0));
}

// CopySurfacePixels composes one locked surface into another (the capture path's
// stretch step) — verify it agrees with a direct StretchSurfaceDispatch on the
// real sibling, both producing the same down-sampled output.
TEST(SurfacePresentITest, CopySurfacePixelsMatchesStretchDispatch) {
    const int sw = 8, sh = 8, dw = 4, dh = 4;
    std::vector<u16> srcPix(static_cast<std::size_t>(sw) * sh);
    for (int i = 0; i < sw * sh; ++i) srcPix[i] = static_cast<u16>(0x0421 * (i + 1));

    auto makeSrc = [&]() {
        StretchSurfaceDesc d;
        d.width = sw; d.height = sh; d.pitch = sw * 2; d.bpp = 16;
        d.rMask = 0xF800; d.gMask = 0x07E0; d.bMask = 0x001F;
        d.pixels = reinterpret_cast<u8*>(srcPix.data());
        return d;
    };

    std::vector<u16> outA(static_cast<std::size_t>(dw) * dh, 0);
    std::vector<u16> outB(static_cast<std::size_t>(dw) * dh, 0);
    auto makeDst = [&](std::vector<u16>& buf) {
        StretchSurfaceDesc d;
        d.width = dw; d.height = dh; d.pitch = dw * 2; d.bpp = 16;
        d.rMask = 0xF800; d.gMask = 0x07E0; d.bMask = 0x001F;
        d.pixels = reinterpret_cast<u8*>(buf.data());
        return d;
    };

    StretchSurfaceDesc srcA = makeSrc(), dstA = makeDst(outA);
    CHECK(CopySurfacePixels(dstA, srcA));

    StretchSurfaceDesc srcB = makeSrc(), dstB = makeDst(outB);
    StretchSurfaceDispatch(dstB, srcB);

    CHECK(std::memcmp(outA.data(), outB.data(), outA.size() * 2) == 0);
}
