#include "gui/menu_render.h"

#include "render/surface.h"          // render::SurfaceSetPixelRgb/DrawHLine/DrawRectOutline
#include "render/text_raster.h"      // render::DrawText / DrawGlyph (the 5x7 GUI font)
#include "render/font.h"             // render::FontInitGlyphTable (byte_75FB50 glyph map)
#include "render/surface_present.h"  // render::PresentGlobals / PresentBackend
#include "render/colorformat.h"      // render::PackColor / UnpackColor
#include "gui/main_menu.h"           // kMainMenuButtons / layout constants

#include <cstring>

namespace guild::gui {

// 0xAARRGGBB layout: R@16, G@8, B@0, 8 bits each (no precision drop).
// ColorFormat field order is {gPos, gPrec, rPrec, bPos, rPos, bPrec}.
render::ColorFormat Argb8888() { return render::ColorFormat{8, 0, 0, 0, 16, 0}; }

// --------------------------------------------------------------------------
MenuRenderTarget MenuRenderTarget::Wrap(void* pixels, int width, int height,
                                        int pitch, const render::ColorFormat& fmt) {
    MenuRenderTarget t;
    render::Surface& s = t.surf;
    std::memset(&s, 0, sizeof(render::Surface));
    s.width   = width;
    s.height  = height;
    s.bpp     = 32;
    s.pitch   = pitch > 0 ? pitch : 4 * width;
    s.widthPx = s.pitch / 4;          // 32bpp => 4 bytes/px
    s.pixels  = reinterpret_cast<u8*>(pixels);
    s.clipX0  = 0;
    s.clipY0  = 0;
    s.clipX1  = width;
    s.clipY1  = height;
    s.fmt     = fmt;
    return t;
}

MenuRenderTarget MenuRenderTarget::FromSurface(const render::Surface& s) {
    MenuRenderTarget t;
    t.surf = s;   // share pixels
    return t;
}

// --------------------------------------------------------------------------
// MenuFillRect — gilde.exe 0x423c70 VIBE_Surface_ColorFillRect (software path).
// The original (Hex-Rays): clamp x to the +36 left bound, clamp x+w to width-1,
// y+h to height-1, reject when x>=width || y>=height, then linear-fill the
// clamped destination span (VIBE_Light_SetGrayColorThunk = a memset over the
// pitch*height-bounded run).  We clamp the same way and emit the fill through the
// REAL render::SurfaceDrawHLine sibling — one horizontal span per row — which is
// precisely the per-row linear span the original wrote.
int MenuFillRect(render::Surface* s, int x, int y, int w, int h,
                 u8 r, u8 g, u8 b) {
    if (!s || !s->pixels) return 0;
    if (w <= 0 || h <= 0) return 0;

    // Clamp the origin to the surface's left/top bounds (the original's +36 left
    // clamp; top has no separate clamp field but a negative y is rejected below by
    // SurfaceDrawHLine's own per-pixel clip).
    int x0 = x, y0 = y;
    if (x0 < s->clipX0) { w -= (s->clipX0 - x0); x0 = s->clipX0; }
    if (x0 >= s->width) return 0;            // original: x >= width -> nothing
    if (y0 >= s->height) return 0;           // original: y >= height -> nothing

    // Clamp the far edges to width-1 / height-1 (matches a1+a4 vs width-1 etc.).
    int x1 = x + w;  if (x1 > s->width)  x1 = s->width;
    int y1 = y + h;  if (y1 > s->height) y1 = s->height;
    int spanW = x1 - x0;
    if (spanW <= 0) return 0;

    int rows = 0;
    for (int row = (y0 < s->clipY0 ? s->clipY0 : y0); row < y1; ++row) {
        render::SurfaceDrawHLine(s, x0, row, spanW, r, g, b);
        ++rows;
    }
    return rows;
}

// --------------------------------------------------------------------------
// Hooks (asset-backed sprite-bank blit) — inert default defined in the library.
namespace {
MenuRenderHooks g_hooks;   // drawSprite == nullptr => inert (paints nothing)

// Lazily-initialised 256-entry glyph map (render::FontInitGlyphTable / byte_75FB50).
const u8* GlyphMap() {
    static u8 table[256];
    static bool init = false;
    if (!init) { render::FontInitGlyphTable(table); init = true; }
    return table;
}

// Build a PresentGlobals that drives DrawText/DrawGlyph straight into a 32bpp
// render::Surface, using the Lock-copy mode (no DDraw): AcquireBackBuffer then
// just sets targetBase = ppvBits with the DIB pitch/stride.  Clip extents are the
// surface width/height; bytes-per-pixel 4; depth 32.
render::PresentGlobals SurfacePresent(render::Surface& s) {
    render::PresentGlobals g;
    g.mode         = render::PresentBackend::DDrawLockBlt;   // ppvBits path
    g.ppvBits      = reinterpret_cast<std::uintptr_t>(s.pixels);
    g.dibPitch     = s.pitch;          // bytes per row
    g.dibStride    = s.widthPx;        // pixels per row (glyph x-clip uses this)
    g.screenHeight = s.height;         // glyph y-clip uses this
    g.pitchExtra   = 4;                // bytes per pixel (DrawGlyph x advance)
    g.lockBitDepth = 32;               // dword_762714 depth gate -> 32bpp path
    g.primary      = nullptr;
    return g;
}

// Draw a text label via the REAL render::DrawText leaf onto a 32bpp surface.
void DrawLabel(render::Surface& s, int x, int y, const char* text,
               u8 r, u8 g, u8 b) {
    if (!text || !*text) return;
    render::PresentGlobals pg = SurfacePresent(s);
    render::DrawText(x, y, reinterpret_cast<const u8*>(text), r, g, b,
                     GlyphMap(), pg, s.fmt);
}
} // namespace

void SetMenuRenderHooks(const MenuRenderHooks& hooks) { g_hooks = hooks; }
const MenuRenderHooks& GetMenuRenderHooks() { return g_hooks; }

// --------------------------------------------------------------------------
// Paint one button: try the real sprite-bank hook first; whether or not it draws,
// lay down the reconstructed face (fill) + border (outline) so the widget is
// visible in the asset-less path, then its caption.
static void PaintButton(render::Surface& s, int x, int y, int w, int h,
                        int gfxId, const char* label, const MenuPalette& pal) {
    bool drewSprite = false;
    if (g_hooks.drawSprite)
        drewSprite = g_hooks.drawSprite(&s, x, y, gfxId, g_hooks.userData);

    if (!drewSprite) {
        MenuFillRect(&s, x, y, w, h, pal.btnR, pal.btnG, pal.btnB);
        render::SurfaceDrawRectOutline(&s, x, y, w, h,
                                       pal.edgeR, pal.edgeG, pal.edgeB);
        if (label && *label)
            DrawLabel(s, x + 3, y + (h - 7) / 2, label,
                      pal.textR, pal.textG, pal.textB);
    }
}

// --------------------------------------------------------------------------
int RenderMenuForm(MenuRenderTarget& target, int bg,
                   const Window* win, const Widget* widgets, int count,
                   const MenuPalette& pal) {
    render::Surface& s = target.surf;
    if (!s.pixels) return 0;

    // 1. Background.  Try the real sprite hook for the backdrop gfx, then always
    //    lay a backdrop fill so the frame is non-blank (the original composites the
    //    3D scene here; the deterministic stand-in is a flat fill).
    if (bg >= 0) {
        if (g_hooks.drawSprite)
            g_hooks.drawSprite(&s, 0, 0, bg, g_hooks.userData);
        MenuFillRect(&s, 0, 0, s.width, s.height, pal.bgR, pal.bgG, pal.bgB);
    }

    if (!widgets || count <= 0) return 0;

    // Window origin: widget (x,y) are window-relative in the original; offset by
    // the window's (x,y) when a window record is supplied.
    int ox = 0, oy = 0;
    if (win) { ox = const_cast<Window*>(win)->x(); oy = const_cast<Window*>(win)->y(); }

    int drawn = 0;
    for (int i = 0; i < count; ++i) {
        Widget& wgt = const_cast<Widget&>(widgets[i]);
        if (!wgt.inUse()) continue;
        u8 ty = wgt.type();
        // Sprite buttons (gfx widgets) and text labels are the visible menu items.
        if (ty != kTypeLabel && ty != 0x00 /* generic sprite slot */ &&
            ty != kTypeEdit)
            continue;

        int wx = ox + wgt.x();
        int wy = oy + wgt.y();
        int ww = wgt.w() > 0 ? wgt.w() : 96;   // default button extent
        int wh = wgt.h() > 0 ? wgt.h() : 18;
        int gfx = wgt.id();

        PaintButton(s, wx, wy, ww, wh, gfx, /*label*/ nullptr, pal);
        ++drawn;
    }
    return drawn;
}

// --------------------------------------------------------------------------
// Build the canonical main-menu column (gui/main_menu.h) directly into `target`:
// backdrop, then the eight gfx-174 buttons at x=32 / the y-table with captions.
int RenderMainMenu(MenuRenderTarget& target, const MenuPalette& pal) {
    render::Surface& s = target.surf;
    if (!s.pixels) return 0;

    // Backdrop (stands in for the spielerauswahl 3D scene).
    MenuFillRect(&s, 0, 0, s.width, s.height, pal.bgR, pal.bgG, pal.bgB);

    const int bx = kMainMenuButtonX;     // 32
    const int bw = 96, bh = 18;          // reconstructed button extent
    int drawn = 0;
    for (int i = 0; i < kMainMenuButtonCount; ++i) {
        int by = kMainMenuButtons[i].y;
        PaintButton(s, bx, by, bw, bh, kMainMenuButtonSprite,
                    kMainMenuButtons[i].label, pal);
        ++drawn;
    }
    return drawn;
}

} // namespace guild::gui
