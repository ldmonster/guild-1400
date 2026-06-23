// guild::render — in-game performance overlay. See perf_overlay.h.
#include "render/perf_overlay.h"

#include "render/font.h"
#include "render/surface.h"
#include "render/surface_present.h"
#include "render/text_raster.h"
#include "render/types.h"

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace guild::render {
namespace {

const u8* GlyphMap() {
    static u8 table[256];
    static bool init = false;
    if (!init) { FontInitGlyphTable(table); init = true; }
    return table;
}

// The 32 bpp software surfaces store raw 0xAARRGGBB (the scene rasteriser writes that
// directly), but carry a 565 `fmt`; pack text/primitives with the matching XRGB8888
// format for those, and the surface's own fmt for 16 bpp.
ColorFormat PackFmt(const Surface* s) { return s->bpp == 32 ? Format8888() : s->fmt; }

// DrawText onto a 16/32 bpp software surface (mirrors the menu's DrawLabel setup).
void DrawString(Surface* s, int x, int y, const char* text, u8 r, u8 g, u8 b) {
    if (!text || !*text) return;
    PresentGlobals pg;
    pg.mode         = PresentBackend::DDrawLockBlt;
    pg.ppvBits      = reinterpret_cast<std::uintptr_t>(s->pixels);
    pg.dibPitch     = s->pitch;
    pg.dibStride    = s->widthPx;
    pg.screenHeight = s->height;
    pg.pitchExtra   = 4;
    pg.lockBitDepth = s->bpp;
    pg.primary      = nullptr;
    const ColorFormat f = PackFmt(s);
    DrawText(x, y, reinterpret_cast<const u8*>(text), r, g, b, GlyphMap(), pg, f);
}

// One pixel, honouring the surface's real storage (raw 0xAARRGGBB for 32 bpp).
void PutPx(Surface* s, int x, int y, u8 r, u8 g, u8 b) {
    const int W = s->widthPx ? s->widthPx : s->width;
    if (x < 0 || y < 0 || x >= W || y >= s->height) return;
    if (s->bpp == 32) {
        auto* px = reinterpret_cast<std::uint32_t*>(
            static_cast<std::uint8_t*>(s->pixels) + (std::size_t)y * s->pitch) + x;
        *px = 0xFF000000u | ((std::uint32_t)r << 16) | ((std::uint32_t)g << 8) | b;
    } else {
        SurfaceSetPixelRgb(s, x, y, r, g, b);
    }
}

// Opaque filled rectangle.
void FillRect(Surface* s, int x, int y, int w, int h, u8 r, u8 g, u8 b) {
    for (int row = 0; row < h; ++row)
        for (int col = 0; col < w; ++col) PutPx(s, x + col, y + row, r, g, b);
}

constexpr int kGlyphAdv = 6;   // DrawText advances 6 px per char
constexpr int kLineH    = 9;   // glyph is 7 px tall; 9 px line pitch
constexpr int kPad      = 3;
constexpr int kGraphW   = 100; // frame-time graph width (px / samples shown)
constexpr int kGraphH   = 32;  // graph height (px)

} // namespace

void PerfOverlay::CycleDetail() {
    if (!cfg_.enabled) { cfg_.enabled = true; cfg_.detail = 1; return; }
    if (cfg_.detail >= 3) { cfg_.enabled = false; cfg_.detail = 1; return; }
    ++cfg_.detail;
}

void PerfOverlay::Frame(std::uint32_t nowMs) {
    if (have_) {
        // Interval since the previous frame (clamp 0 -> 1 so FPS stays finite).
        std::uint32_t dt = nowMs - lastMs_;
        float ms = (dt == 0) ? 1.0f : (float)dt;
        hist_[histHead_] = ms;
        histHead_ = (histHead_ + 1) % kHist;
        if (histN_ < kHist) ++histN_;
        // Smoothed frame time = mean of the ring.
        float sum = 0.0f;
        for (int i = 0; i < histN_; ++i) sum += hist_[i];
        frameMs_ = (histN_ > 0) ? (sum / (float)histN_) : ms;
        fps_ = (frameMs_ > 0.0f) ? (1000.0f / frameMs_) : 0.0f;
        // Refresh the DISPLAYED number a few times/second so it is readable.
        if (!dispValid_ || (std::uint32_t)(nowMs - lastRefreshMs_) >= 400u) {
            dispFps_ = fps_; dispMs_ = frameMs_; lastRefreshMs_ = nowMs; dispValid_ = true;
        }
    }
    lastMs_ = nowMs;
    have_ = true;
}

void PerfOverlay::Draw(Surface* fb) {
    if (!cfg_.enabled || !fb || !fb->pixels) return;
    const int W = fb->widthPx ? fb->widthPx : fb->width;
    const int H = fb->height;
    if (W <= 0 || H <= 0) return;

    // Compose the text lines.
    char l0[32], l1[32];
    std::snprintf(l0, sizeof(l0), "%d FPS", (int)(dispValid_ ? dispFps_ + 0.5f : 0.0f));
    int lines = 1;
    l1[0] = '\0';
    if (cfg_.detail >= 2) {
        std::snprintf(l1, sizeof(l1), "%.1f ms", dispValid_ ? dispMs_ : 0.0f);
        lines = 2;
    }
    const bool graph = (cfg_.detail >= 3);

    // Block extent.
    int textW = (int)std::strlen(l0);
    if (lines >= 2) { int w1 = (int)std::strlen(l1); if (w1 > textW) textW = w1; }
    int blockW = textW * kGlyphAdv;
    if (graph && kGraphW > blockW) blockW = kGraphW;
    int blockH = lines * kLineH + (graph ? (kPad + kGraphH) : 0);

    const int boxW = blockW + 2 * kPad, boxH = blockH + 2 * kPad;
    int bx, by;
    switch (cfg_.corner) {
        case PerfCorner::TopLeft:     bx = kPad;            by = kPad;            break;
        case PerfCorner::TopRight:    bx = W - boxW - kPad; by = kPad;            break;
        case PerfCorner::BottomLeft:  bx = kPad;            by = H - boxH - kPad; break;
        case PerfCorner::BottomRight: bx = W - boxW - kPad; by = H - boxH - kPad; break;
        default:                      bx = kPad;            by = kPad;            break;
    }
    if (bx < 0) bx = 0;
    if (by < 0) by = 0;

    if (cfg_.background) FillRect(fb, bx, by, boxW, boxH, 12, 12, 12);   // high-contrast dark box

    const int tx = bx + kPad, ty = by + kPad;
    // Colour the FPS by health (green good / yellow ok / red poor), like Steam.
    u8 r = 0, g = 255, b = 64;
    const float f = dispValid_ ? dispFps_ : 0.0f;
    if (f < 20.0f)      { r = 255; g = 48;  b = 48; }
    else if (f < 40.0f) { r = 255; g = 220; b = 32; }
    DrawString(fb, tx, ty, l0, r, g, b);
    if (lines >= 2) DrawString(fb, tx, ty + kLineH, l1, 220, 220, 220);

    if (graph) {
        const int gy = ty + lines * kLineH + kPad;
        const int gx = tx;
        // Baseline grid line at the bottom.
        for (int i = 0; i < kGraphW; ++i) PutPx(fb, gx + i, gy + kGraphH, 64, 64, 64);
        // Bars: most recent on the right. 50 ms maps to full height.
        const int n = histN_ < kGraphW ? histN_ : kGraphW;
        for (int i = 0; i < n; ++i) {
            // walk back from the newest sample
            int idx = (histHead_ - 1 - i + kHist) % kHist;
            float ms = hist_[idx];
            int barH = (int)(ms / 50.0f * (float)kGraphH);
            if (barH < 1) barH = 1;
            if (barH > kGraphH) barH = kGraphH;
            u8 br = 0, bg = 255, bb = 64;
            if (ms > 50.0f)      { br = 255; bg = 48;  bb = 48; }  // <20 fps
            else if (ms > 25.0f) { br = 255; bg = 220; bb = 32; }  // <40 fps
            const int cx = gx + (kGraphW - 1 - i);
            for (int yy = 0; yy < barH; ++yy)
                PutPx(fb, cx, gy + kGraphH - 1 - yy, br, bg, bb);
        }
    }
}

void PerfOverlay::Draw(std::uint32_t* pixels, int width, int height) {
    if (!cfg_.enabled || !pixels || width <= 0 || height <= 0) return;
    Surface s{};
    s.width = width; s.height = height; s.widthPx = width;
    s.pitch = width * 4; s.bpp = 32;
    s.pixels = reinterpret_cast<u8*>(pixels);
    s.clipX0 = 0; s.clipY0 = 0; s.clipX1 = width; s.clipY1 = height;
    s.fmt = Format8888();
    Draw(&s);
}

PerfOverlay& GlobalPerfOverlay() {
    static PerfOverlay inst;
    static bool envInit = false;
    if (!envInit) {
        envInit = true;
        PerfOverlayConfig c;
        if (const char* e = std::getenv("GUILD_PERF_OVERLAY")) {
            int lvl = std::atoi(e);
            if (lvl >= 1) { c.enabled = true; c.detail = (lvl > 3) ? 3 : lvl; }
        }
        if (const char* p = std::getenv("GUILD_PERF_OVERLAY_POS")) {
            if (!std::strcmp(p, "tr")) c.corner = PerfCorner::TopRight;
            else if (!std::strcmp(p, "bl")) c.corner = PerfCorner::BottomLeft;
            else if (!std::strcmp(p, "br")) c.corner = PerfCorner::BottomRight;
            else c.corner = PerfCorner::TopLeft;
        }
        inst.Configure(c);
    }
    return inst;
}

} // namespace guild::render
