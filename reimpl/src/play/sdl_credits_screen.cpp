// guild::play — native credits screen. See sdl_credits_screen.h.
//
// Drives the REAL reconstructed scroll model (gui::credits) over the REAL
// _CREDITS_BACKGROUND (#1792) art, painting the credit lines crawling upward and
// presenting through an IGraphicsDevice every frame.  gilde.exe 0x56e524.
#include "play/sdl_credits_screen.h"

#include "shim/IGraphicsDevice.h"
#include "shim/IPlatform.h"
#include "shim/IFileSystem.h"
#include "shim_impl/disk_filesystem.h"

#include "gui/credits.h"        // Credits_* scroll model (the recovered arithmetic)
#include "gui/menu_render.h"    // MenuRenderTarget
#include "render/gfx_archive.h" // GfxArchive / DecodedShape
#include "render/text_raster.h" // DrawText
#include "render/font.h"        // FontInitGlyphTable
#include "render/types.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace guild::play {
namespace {

constexpr int kVkEscape = 0x1B;

// The _CREDITS_BACKGROUND record — VIBE_Window_RenderEntityList(1792) @0x56e58b.
constexpr const char* kCreditsBgName = "_CREDITS_BACKGROUND";

// The crawl window geometry — VIBE_Window_Create(100, 0, screenW, 600, 16) @0x56e5ae.
// gui::credits.h: kCreditsScrollX=100, kCreditsScrollY=0, kCreditsScrollH=600.

// The lazily-initialised 5x7 GUI glyph map (mirrors sdl_menu.cpp's GlyphMap()).
const std::uint8_t* GlyphMap() {
    static std::uint8_t table[256];
    static bool init = false;
    if (!init) { render::FontInitGlyphTable(table); init = true; }
    return table;
}

// Draw text straight into a 32bpp render::Surface (Lock-copy mode); copied from
// sdl_menu.cpp's DrawLabel.
void DrawLabel(render::Surface& s, int x, int y, const char* text,
               std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    if (!text || !*text) return;
    render::PresentGlobals pg;
    pg.mode         = render::PresentBackend::DDrawLockBlt;
    pg.ppvBits      = reinterpret_cast<std::uintptr_t>(s.pixels);
    pg.dibPitch     = s.pitch;
    pg.dibStride    = s.widthPx;
    pg.screenHeight = s.height;
    pg.pitchExtra   = 4;
    pg.lockBitDepth = 32;
    pg.primary      = nullptr;
    render::DrawText(x, y, reinterpret_cast<const std::uint8_t*>(text), r, g, b,
                     GlyphMap(), pg, s.fmt);
}

// Draw a crawl line, truncated so the unclipped 5x7 glyph raster
// (render::DrawGlyph advances 6px/char, writes 5 cols/glyph) never writes outside
// [0,W). The original surface lock clipped per-pixel at the credits window edge;
// glyphs that fall off the surface were never correctly visible, so dropping them
// preserves observable output while keeping every write in-bounds. Width safety
// only — y is bounded by the caller's full-glyph-band guard.
void DrawCrawlLine(render::Surface& s, int x, int y, const std::string& text, int W,
                   std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    if (text.empty() || x < 0 || y < 0) return;
    // Max glyphs whose last column stays in [0,W): x + 6*(n-1) + 5 <= W.
    std::size_t maxGlyphs = 0;
    if (W - x >= 5) maxGlyphs = (std::size_t)((W - x - 5) / 6) + 1;
    if (maxGlyphs == 0) return;
    std::string shown = text.size() > maxGlyphs ? text.substr(0, maxGlyphs) : text;
    DrawLabel(s, x, y, shown.c_str(), r, g, b);
}

// Draw the decoded background into the 32bpp scratch (nearest-neighbour scale);
// copied from sdl_menu.cpp's DrawBackground.
void DrawBackground(std::uint32_t* dst, int W, int H,
                    const std::uint32_t* bg, int bw, int bh) {
    if (!bg || bw <= 0 || bh <= 0) return;
    for (int y = 0; y < H; ++y) {
        const int sy = (int)((long long)y * bh / H);
        const std::uint32_t* srow = bg + (std::size_t)sy * bw;
        std::uint32_t* drow = dst + (std::size_t)y * W;
        for (int x = 0; x < W; ++x) {
            const int sx = (int)((long long)x * bw / W);
            const std::uint32_t px = srow[sx];
            if (px & 0xFF000000u) drow[x] = px;
        }
    }
}

// Convert the 32bpp XRGB scratch into the device backbuffer; copied from
// sdl_menu.cpp's BlitToDevice (16/32/8bpp).
void BlitToDevice(const std::uint32_t* src, int w, int h, shim::IGraphicsDevice& dev) {
    shim::Surface* bb = dev.backbuffer();
    if (!bb || !bb->pixels) return;
    const int W = bb->width < w ? bb->width : w;
    const int H = bb->height < h ? bb->height : h;
    auto* base = static_cast<std::uint8_t*>(bb->pixels);
    if (bb->bpp == 32) {
        for (int y = 0; y < H; ++y)
            std::memcpy(base + (std::size_t)y * bb->pitch, src + (std::size_t)y * w,
                        (std::size_t)W * 4);
    } else if (bb->bpp == 16) {
        for (int y = 0; y < H; ++y) {
            auto* row = reinterpret_cast<std::uint16_t*>(base + (std::size_t)y * bb->pitch);
            const std::uint32_t* s = src + (std::size_t)y * w;
            for (int x = 0; x < W; ++x) {
                const std::uint32_t c = s[x];
                const std::uint32_t r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
                row[x] = (std::uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            }
        }
    } else if (bb->bpp == 8) {
        for (int y = 0; y < H; ++y) {
            auto* row = base + (std::size_t)y * bb->pitch;
            const std::uint32_t* s = src + (std::size_t)y * w;
            for (int x = 0; x < W; ++x) {
                const std::uint32_t c = s[x];
                row[x] = (std::uint8_t)(((c >> 16 & 0xFF) * 30 + (c >> 8 & 0xFF) * 59 +
                                         (c & 0xFF) * 11) / 100);
            }
        }
    }
}

} // namespace

// A faithful default credit list — the real rich-text resource 7135 is NOT
// reconstructed (the localized text DB BuildTextArray is deferred), so this stands
// in for it.  The strings are representative of the shipped credits crawl.
const std::vector<std::string>& DefaultCreditLines() {
    static const std::vector<std::string> kLines = {
        "EUROPA 1400",
        "THE GUILD",
        "",
        "A 4HEAD STUDIOS PRODUCTION",
        "",
        "GAME DESIGN",
        "",
        "PROGRAMMING",
        "",
        "GRAPHICS",
        "",
        "MUSIC AND SOUND",
        "",
        "QUALITY ASSURANCE",
        "",
        "SPECIAL THANKS",
        "",
        "THANK YOU FOR PLAYING",
    };
    return kLines;
}

CreditsScreenResult RunCreditsScreen(shim::IGraphicsDevice& device,
                                     shim::IPlatform& plat,
                                     const CreditsScreenConfig& cfg) {
    using namespace guild::gui;

    CreditsScreenResult res;
    const int W = cfg.fbW, H = cfg.fbH;
    std::vector<std::uint32_t> scratch((std::size_t)W * H, 0u);

    // ---- decode the REAL _CREDITS_BACKGROUND (#1792) when gfx/gilde.gfx exists ----
    render::DecodedShape bg;
    if (!cfg.gameDir.empty()) {
        shim::DiskFileSystem assetFs(cfg.gameDir);
        render::GfxArchive arc;
        if (arc.LoadFromFile(assetFs, "gfx/gilde.gfx")) {
            if (arc.DecodeShapeByName(kCreditsBgName, 0, bg) && bg.width > 0 &&
                bg.height > 0) {
                res.haveAssets = true;
            }
        }
    }

    const std::vector<std::string>& lines =
        cfg.lines.empty() ? DefaultCreditLines() : cfg.lines;

    // ---- the REAL scroll model (gui::credits) geometry ----
    // textHeight = the total stacked height of all credit lines (window+10 in the
    // original).  textBottom = the window content bottom (window+580); the crawl
    // window is kCreditsScrollH (600) tall, so the text must scroll past that.
    const int lineH     = cfg.lineHeight > 0 ? cfg.lineHeight : 9;
    const int textHeight = (int)lines.size() * lineH;
    const int textBottom = kCreditsScrollH;          // 600 — window content bottom
    const double lineScale = 1.5;                    // dbl_625324 @0x625324 == 1.5

    // Initial offset: text starts fully below the window (window+584 = -screenH).
    int offset = Credits_InitialOffset(H);           // 0x56e5d2
    int frame  = 0;
    int step   = Credits_ScrollStep(cfg.frameTimeMetric); // 0x56e549 (starts 4 in orig)

    bool prevLeft = false;
    {
        // Seed prevLeft so a button already-held at entry isn't a spurious edge.
        shim::MouseState ms0{};
        plat.getMouse(ms0);
        prevLeft = ms0.left;
    }

    for (;;) {
        if (cfg.maxFrames >= 0 && frame >= cfg.maxFrames) break;

        // ---- render: background + the crawl lines at the current offset ----
        std::fill(scratch.begin(), scratch.end(), 0u);
        if (res.haveAssets)
            DrawBackground(scratch.data(), W, H, bg.argb.data(), bg.width, bg.height);

        MenuRenderTarget tgt = MenuRenderTarget::Wrap(scratch.data(), W, H, W * 4);

        // The crawl window starts at x=kCreditsScrollX (100).  The scroll offset is a
        // content-scroll position seeded to -screenH (text starts fully BELOW the
        // window) and increasing toward 0; the content is drawn at (top - offset), so
        // the first line begins at +screenH (bottom) and rises as offset grows.
        const int baseX = kCreditsScrollX < W ? kCreditsScrollX : 0;
        // The 5x7 glyph raster (render::DrawGlyph) writes 7 rows starting at `ly`
        // WITHOUT clipping (faithful to VIBE_Render_DrawGlyph, which relied on the
        // window/surface to bound it). The crawl offset makes `ly` sweep through
        // negative values (text rising from below) and toward the top; only draw a
        // line when its full 7-row glyph band lies inside [0,H) so the unclipped
        // raster never writes outside the W*H scratch (the original surface lock
        // would have rejected those pixels — they were never correctly visible).
        constexpr int kGlyphH = 7;   // render::DrawGlyph row span
        for (int i = 0; i < (int)lines.size(); ++i) {
            const int ly = kCreditsScrollY - offset + i * lineH;
            if (ly < 0 || ly + kGlyphH > H) continue; // off-window / would clip: skip
            DrawCrawlLine(tgt.surf, baseX + 8, ly, lines[i], W, 255, 235, 200);
        }

        BlitToDevice(scratch.data(), W, H, device);
        device.present();
        ++res.framesPresented;

        // ---- input / advance ----
        if (!plat.pumpMessages()) {                   // window closed
            res.quitByWindow = true;
            res.back = true;
            break;
        }
        shim::MouseState ms{};
        plat.getMouse(ms);
        if (plat.keyDown(kVkEscape)) {                // ESC -> back (0x56e6c3)
            res.quitByEsc = true;
            res.back = true;
            break;
        }
        const bool leftEdge = ms.left && !prevLeft;
        prevLeft = ms.left;
        if (leftEdge) {                               // click -> back (byte_67225C)
            res.quitByClick = true;
            res.back = true;
            break;
        }

        // Advance the offset every `step` frames (0x56e635).
        offset = Credits_AdvanceOffset(offset, frame, step);
        ++frame;                                       // 0x56e65d

        // Crawl complete? (0x56e669)
        if (Credits_ScrollComplete(textBottom, textHeight, lineScale, offset)) {
            res.finished = true;
            break;
        }

        // Re-evaluate the speed ramp each frame (0x56e681).
        step = Credits_ScrollStep(cfg.frameTimeMetric);

        if (cfg.frameCapMs > 0) plat.sleepMs((std::uint32_t)cfg.frameCapMs);
    }

    res.scrollOffset = offset;
    return res;
}

} // namespace guild::play
