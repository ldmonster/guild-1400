// End-to-end exercise of the built-in bitmap-font text layer (render/text_raster)
// driving a realistic full-screen frame, plus a GUARDED check against the REAL
// shipped gfx catalog. The 5x7 debug font is hardcoded in the binary (data @0x62D59C)
// — it is NOT stored in gilde.gfx — so the asset arm validates that the catalog the
// engine renders text *over* loads with the recovered header; if the assets aren't
// present the test passes trivially so the suite stays green everywhere.
#include "test.h"
#include "render/text_raster.h"
#include "render/surface_present.h"
#include "render/colorformat.h"
#include "render/font.h"
#include "shim_impl/disk_filesystem.h"

#include <cstdint>
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::render;

namespace {

const char* kRoot =
    "/home/cnupt/work/reverse/reverse-guild/reimpl/europe_guild_1400_original";

const u8* glyphMap() {
    static u8 map[256];
    static bool init = [] { FontInitGlyphTable(map); return true; }();
    (void)init;
    return map;
}

// Count how many pixels in the framebuffer equal `color`.
std::size_t countColor(const std::vector<u16>& px, u16 color) {
    std::size_t n = 0;
    for (u16 v : px) if (v == color) ++n;
    return n;
}

} // namespace

// Full-frame software path (PresentBackend::GdiBitBlt with a software DIB): the way
// the GDI/Lock-copy modes draw HUD text. AcquireBackBuffer points the renderer at
// ppvBits; DrawText stamps a whole label; the DIB then holds exactly the glyph ink.
TEST(TextRasterE2E, FullFrameLabelIntoSoftwareDIB) {
    const int W = 640, H = 480;
    std::vector<u16> dib(static_cast<std::size_t>(W) * H, 0);

    PresentGlobals g;
    g.mode         = PresentBackend::DDrawLockBlt;   // mode 2: draw into ppvBits
    g.ppvBits      = reinterpret_cast<std::uintptr_t>(dib.data());
    g.dibPitch     = W * 2;
    g.dibStride    = W;                              // width  (clip)
    g.screenHeight = H;                              // height (clip)
    g.lockBitDepth = 16;
    g.pitchExtra   = 2;                             // bytes per pixel (mode 2 keeps
    g.pitchBytes   = W * 2;                         // the DIB pitch; set for raster)

    ColorFormat fmt = Format565();
    const u16 yellow = static_cast<u16>(PackColor(fmt, 255, 255, 0));

    const u8 label[] = "GOLD 1250";
    u8 r = DrawText(20, 16, label, 255, 255, 0, glyphMap(), g, fmt);
    (void)r;

    // mode 2 acquire never touches DDraw, so the lock is cleared by the inlined
    // UnlockBackBuffer with no Unlock call; targetBase back to 0.
    CHECK_EQ(g.targetBase, std::uintptr_t(0));

    // Some ink was laid down, and it is all the requested colour.
    std::size_t ink = countColor(dib, yellow);
    CHECK(ink > 0);
    // 9 chars, 2 of which are spaces -> 7 drawn glyphs; each glyph has at most 35
    // cells, so ink must be well under 7*35 and clearly above a single glyph.
    CHECK(ink < static_cast<std::size_t>(7 * 35 + 1));
    CHECK(ink > 20);

    // Nothing outside the text band [16,23] in y was touched.
    for (int y = 0; y < H; ++y) {
        if (y >= 16 && y <= 16 + 6) continue;
        for (int x = 0; x < W; ++x)
            CHECK_EQ(dib[static_cast<std::size_t>(y) * W + x], (u16)0);
    }
}

// Every printable ASCII char the glyph map covers must resolve to a valid glyph
// index (< 91) and rasterize at least one pixel for the dense ones — a sanity pass
// over the recovered map + bitmap so a corrupt transcription would be caught.
TEST(TextRasterE2E, GlyphMapCoversPrintableAscii) {
    const u8* map = glyphMap();
    const int W = 16, H = 16;
    std::vector<u16> fb(static_cast<std::size_t>(W) * H);

    PresentGlobals g;
    g.targetBase   = reinterpret_cast<std::uintptr_t>(fb.data());
    g.pitchExtra   = 2;
    g.pitchBytes   = W * 2;
    g.lockBitDepth = 16;
    g.dibStride    = W;
    g.screenHeight = H;

    // All glyph indices in range.
    for (int c = 0; c < 256; ++c) CHECK(map[c] < 91);

    // Letters A-Z and digits 0-9 all draw a non-empty cell.
    auto drawsInk = [&](char ch) {
        std::fill(fb.begin(), fb.end(), (u16)0);
        DrawGlyph((u8)ch, 1, 0x7FFF, 1, map, g);
        return countColor(fb, 0x7FFF) > 0;
    };
    for (char ch = 'A'; ch <= 'Z'; ++ch) CHECK(drawsInk(ch));
    for (char ch = '0'; ch <= '9'; ++ch) CHECK(drawsInk(ch));
}

// GUARDED real-asset arm: the gfx catalog the text layer renders over loads with
// the recovered header (1806 objects). Skip-pass when assets are absent.
TEST(TextRasterE2E, RealGfxCatalogHeaderPresent) {
    shim::DiskFileSystem fs(kRoot);
    shim::IFile* gf = fs.open("gfx/gilde.gfx", "rb");
    if (!gf) { CHECK(true); return; }                // skipped: no assets
    u8 hdr[8] = {0};
    gf->read(hdr, sizeof hdr);
    fs.close(gf);
    u32 count = 0; std::memcpy(&count, hdr, 4);
    CHECK_EQ(count, 1806u);                           // recovered objectCount

    // With assets present, still prove the font path is independent of the catalog:
    // rendering a glyph works without ever touching gilde.gfx.
    const int W = 16, H = 16;
    std::vector<u16> fb(static_cast<std::size_t>(W) * H, 0);
    PresentGlobals g;
    g.targetBase   = reinterpret_cast<std::uintptr_t>(fb.data());
    g.pitchExtra   = 2;
    g.pitchBytes   = W * 2;
    g.lockBitDepth = 16;
    g.dibStride    = W;
    g.screenHeight = H;
    DrawGlyph((u8)'G', 1, 0x1234, 1, glyphMap(), g);
    CHECK(countColor(fb, 0x1234) > 0);
}
