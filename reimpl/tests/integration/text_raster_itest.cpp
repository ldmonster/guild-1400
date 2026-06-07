#include "test.h"

// Cross-module integration: drive the bitmap-font text rasterizer (render/
// text_raster) across its REAL siblings — the present lock/unlock dispatch
// (render/surface_present), the colour packer (render/colorformat), the runtime
// glyph map builder (render/font), and the shim graphics backend
// (MemoryGraphicsDevice). This wires them exactly as the engine's HUD text path
// does: acquire the back buffer, stamp a string of glyphs, unlock + present.

#include "render/text_raster.h"
#include "render/surface_present.h"
#include "render/colorformat.h"
#include "render/font.h"
#include "render/types.h"
#include "shim_impl/memory_graphics.h"

#include <cstdint>
#include <vector>

using namespace guild;
using namespace guild::render;

namespace {

// Same DDraw-surface adapter the present integration test uses: Lock yields the
// device framebuffer pixels/pitch/depth; Unlock presents.
struct DeviceSurfaceAdapter : IDDrawSurface {
    guild::shim::IGraphicsDevice& dev;
    int lockCount = 0, unlockCount = 0;
    explicit DeviceSurfaceAdapter(guild::shim::IGraphicsDevice& d) : dev(d) {}
    i32 GetSurfaceDesc(DDrawLock& out) override {
        auto* bb = dev.backbuffer();
        if (!bb) return kDDErrSurfaceLost;
        out.pixels   = bb->pixels;
        out.pitch    = static_cast<u32>(bb->pitch);
        out.bitDepth = static_cast<u32>(bb->bpp);
        return 0;
    }
    i32 Lock(u32, DDrawLock& out) override { ++lockCount; return GetSurfaceDesc(out); }
    i32 Unlock() override { ++unlockCount; dev.present(); return 0; }
    i32 Restore() override { return 0; }
};

const u8* glyphMap() {
    static u8 map[256];
    static bool init = [] { FontInitGlyphTable(map); return true; }();
    (void)init;
    return map;
}

} // namespace

// Full HUD-text frame in DDraw-Blt mode: DrawText acquires the real device
// backbuffer through LockSurfaceWait, packs the colour via the real PackColor,
// stamps each glyph, then unlocks+presents through the backend.
TEST(TextRasterITest, DrawTextLocksDrawsAndPresents) {
    guild::shim::MemoryGraphicsDevice dev;
    CHECK(dev.init(64, 16, 16, false));
    DeviceSurfaceAdapter surf(dev);

    PresentGlobals g;
    g.mode         = PresentBackend::DDrawBlt;   // mode 1: lock + draw + unlock
    g.primary      = &surf;
    g.bytesPerPx   = 2;
    g.dibStride    = 64;                          // framebuffer width  (clip)
    g.screenHeight = 16;                          // framebuffer height (clip)

    ColorFormat fmt = Format565();
    // Pure red 565 = (31<<11) = 0xF800; PackColor must produce that.
    const u16 expectColor = static_cast<u16>(PackColor(fmt, 255, 0, 0));
    CHECK_EQ(expectColor, (u16)0xF800);

    const u8 text[] = "HI";
    u8 r = DrawText(2, 4, text, 255, 0, 0, glyphMap(), g, fmt);
    (void)r;

    // Lock and Unlock each happened exactly once; the lock is released.
    CHECK_EQ(surf.lockCount, 1);
    CHECK_EQ(surf.unlockCount, 1);
    CHECK_EQ(g.targetBase, std::uintptr_t(0));
    CHECK_EQ(dev.presentCount(), 1);

    // Inspect the PRESENTED snapshot. 'H' is glyph 9 (full 5x7) drawn at x=2,y=4.
    // 'H' row0 = "#...#": cols 0 and 4 set, cols 1..3 clear.
    const auto& shot = dev.lastPresented();
    CHECK_EQ(shot.size(), static_cast<std::size_t>(64 * 16 * 2));
    const u16* sp = reinterpret_cast<const u16*>(shot.data());
    auto at = [&](int x, int y) { return sp[y * 64 + x]; };

    // 'H' at (2,4): row 0 -> "#...#"
    CHECK_EQ(at(2 + 0, 4 + 0), expectColor);
    CHECK_EQ(at(2 + 1, 4 + 0), (u16)0);
    CHECK_EQ(at(2 + 4, 4 + 0), expectColor);
    // 'H' row 3 -> "#####" (crossbar): all five set
    for (int c = 0; c < 5; ++c) CHECK_EQ(at(2 + c, 4 + 3), expectColor);

    // 'I' is glyph 24 (map['I']) at x = 2 + 6 = 8. row 0 of 'I' bitmap is
    // 0x0e -> ".###." : col0 clear, cols1..3 set, col4 clear.
    CHECK_EQ(at(8 + 0, 4 + 0), (u16)0);
    CHECK_EQ(at(8 + 1, 4 + 0), expectColor);
    CHECK_EQ(at(8 + 3, 4 + 0), expectColor);
    CHECK_EQ(at(8 + 4, 4 + 0), (u16)0);
}

// A space advances the cursor (6px) but stamps no pixels — verify the gap between
// two letters around a space stays background while the letters themselves draw.
TEST(TextRasterITest, SpaceAdvancesWithoutDrawing) {
    guild::shim::MemoryGraphicsDevice dev;
    CHECK(dev.init(64, 16, 16, false));
    DeviceSurfaceAdapter surf(dev);

    PresentGlobals g;
    g.mode = PresentBackend::DDrawBlt;
    g.primary = &surf;
    g.bytesPerPx = 2;
    g.dibStride = 64;
    g.screenHeight = 16;
    ColorFormat fmt = Format565();

    const u8 text[] = "A A";
    DrawText(1, 1, text, 255, 255, 255, glyphMap(), g, fmt);
    const u16 white = static_cast<u16>(PackColor(fmt, 255, 255, 255));

    const u16* sp = reinterpret_cast<const u16*>(dev.lastPresented().data());
    auto at = [&](int x, int y) { return sp[y * 64 + x]; };

    // First 'A' at x=1: row4 crossbar fully set.
    for (int c = 0; c < 5; ++c) CHECK_EQ(at(1 + c, 1 + 4), white);
    // The space cell sits at x = 1 + 6 = 7..11 — entirely background.
    for (int c = 0; c < 5; ++c)
        for (int rr = 0; rr < 7; ++rr)
            CHECK_EQ(at(7 + c, 1 + rr), (u16)0);
    // Second 'A' at x = 1 + 12 = 13: crossbar set again.
    for (int c = 0; c < 5; ++c) CHECK_EQ(at(13 + c, 1 + 4), white);
}

// DrawText must fail gracefully (return 0, no draw) when the back buffer cannot be
// acquired — here the DDraw mode has no primary surface set.
TEST(TextRasterITest, AcquireFailureReturnsZero) {
    PresentGlobals g;
    g.mode = PresentBackend::DDrawBlt;   // needs a primary; none provided
    g.primary = nullptr;
    g.dibStride = 64;
    g.screenHeight = 16;
    ColorFormat fmt = Format565();
    const u8 text[] = "X";
    u8 r = DrawText(0, 0, text, 255, 255, 255, glyphMap(), g, fmt);
    CHECK_EQ(r, (u8)0);
    CHECK_EQ(g.targetBase, std::uintptr_t(0));
}
