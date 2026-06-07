#pragma once
// guild::gui — the VISIBLE half of VIBE_Menu_RunMainMenu (gilde.exe 0x529d08):
// rasterize a loaded menu form/window tree into a CPU framebuffer so a menu frame
// is non-blank and readback-verifiable.
//
// What the original draws each frame (0x529d08 -> 0x4134f0 VIBE_Window_RenderEntityList):
//   1. The 3D backdrop scenes ("scenes/*spielerauswahl.ed3") are composed first
//      (VIBE_Window_RenderEntityScene / SetupViewTransform) — that is the menu's
//      *background*.  The retained-mode GUI is then composited on top.
//   2. The main_menu form ("MENU\MAIN_MENU") owns a window whose children are a
//      vertical column of EIGHT sprite buttons, all created with
//        VIBE_Widget_AddSpriteToWindow(32, Y, gfx=174, win)  (see gui/main_menu.h),
//      each flagged as a label (widget+88 = 1) with text colour 300, plus a
//      VIBE_Object_CreateTextLabel title (VIBE_Object_SetColor 67).
//   3. The window/widget tree is walked and each widget paints its sprite into the
//      locked back buffer, after which VIBE_Render_PresentFrame flips it.
//
// The per-frame pixel work bottoms out in deterministic 2D leaves we have already
// reconstructed 1:1 from gilde.exe gfx.c / text.c:
//   render::SurfaceSetPixelRgb      0x423e5c  (clipped 8/15/16/24/32bpp pixel write)
//   render::SurfaceDrawHLine        0x423ffc  (horizontal span — the fill primitive)
//   render::SurfaceDrawRectOutline  0x4242d4  (rect border)
//   render::DrawText / DrawGlyph    0x434E18 / 0x434D0C  (the 5x7 GUI label font)
// This module wires those REAL siblings over the Window/Widget data model
// (gui/types.h) to paint background + buttons + labels.  The one small leaf the
// original has that we did not yet have on render::Surface — the *filled* rect
// (VIBE_Surface_ColorFillRect 0x423c70, software/linear-span path) — is
// reconstructed here as MenuFillRect (clamp to bounds, then real SurfaceDrawHLine
// per row, exactly the original's per-row linear span fill).
//
// Asset-backed sprite banks (the real gfx-174 button artwork) are a loader/device
// boundary; that single edge is routed through an installable MenuRenderHooks with
// an inert default (draws nothing) so the deterministic path is testable in
// isolation, per the build model's hooks-with-inert-defaults convention.

#include "guild/common/types.h"
#include "render/types.h"      // render::Surface, ColorFormat, Format helpers
#include "gui/types.h"         // guild::gui Widget / Window / Form

namespace guild::gui {

// 32bpp 0xAARRGGBB channel layout (R@16,G@8,B@0) — the natural format for a
// readback golden test.  (render::Format565 etc. exist for the 16bpp paths.)
render::ColorFormat Argb8888();

// ---------------------------------------------------------------------------
// Target: a 32bpp CPU framebuffer the menu rasterizes into.  Either an existing
// render::Surface, or a raw (pixels,w,h,pitch) buffer the caller owns (the brief's
// "shim::Surface or a raw 32bpp buffer + w/h/pitch").  Internally everything is
// expressed as a render::Surface so the real gfx leaves apply unchanged.
// ---------------------------------------------------------------------------
struct MenuRenderTarget {
    render::Surface surf;   // 32bpp view (pixels/width/height/pitch + clip + fmt)

    // Wrap a raw 32bpp buffer.  pitch is bytes-per-row (0 => 4*width).  The clip
    // rect defaults to the whole buffer.  fmt defaults to packed 0xAARRGGBB
    // (R@16,G@8,B@0) so a readback test can decode pixels deterministically.
    static MenuRenderTarget Wrap(void* pixels, int width, int height,
                                 int pitch = 0,
                                 const render::ColorFormat& fmt = Argb8888());

    // Wrap an existing render::Surface (shares its pixels; no copy).
    static MenuRenderTarget FromSurface(const render::Surface& s);
};

// ---------------------------------------------------------------------------
// MenuFillRect — gilde.exe 0x423c70 VIBE_Surface_ColorFillRect (software path).
// The original clamps (x,y,w,h) to the surface [width,height] (and to the +36
// left clamp), then linear-fills the clamped destination span.  We reproduce the
// clamp 1:1 and emit the fill through the REAL render::SurfaceDrawHLine sibling,
// one horizontal span per row (the per-row linear span the original memsets).
// Returns the number of rows actually filled (0 if fully clipped away).
int MenuFillRect(render::Surface* s, int x, int y, int w, int h,
                 u8 r, u8 g, u8 b);

// ---------------------------------------------------------------------------
// Installable hooks for the one non-deterministic / asset-backed leaf: painting a
// real sprite-bank shape (the gfx-174 button artwork) into the framebuffer.  The
// inert default paints nothing (so without assets the button still gets its
// reconstructed fill+outline+label, and the frame is non-blank).  A real backend
// installs a hook that resolves the bank and calls render::ShapeShowFromBank.
// ---------------------------------------------------------------------------
struct MenuRenderHooks {
    // Paint sprite `gfxId` at (x,y) into `s`.  Return true if it drew anything.
    bool (*drawSprite)(render::Surface* s, int x, int y, int gfxId,
                       void* userData) = nullptr;
    void* userData = nullptr;
};
void SetMenuRenderHooks(const MenuRenderHooks& hooks);
const MenuRenderHooks& GetMenuRenderHooks();

// ---------------------------------------------------------------------------
// Visual style for the reconstructed (asset-less) button/label drawing.  These
// colours stand in for the gfx-174 artwork so the deterministic path produces a
// recognisable, readback-stable menu frame.  A real backend overrides via the
// sprite hook; the layout/coords come straight from the Widget records.
// ---------------------------------------------------------------------------
struct MenuPalette {
    u8 bgR = 24,  bgG = 28,  bgB = 64;   // backdrop fill (stands in for the 3D scene)
    u8 btnR = 96, btnG = 96, btnB = 160; // button face
    u8 edgeR = 200, edgeG = 200, edgeB = 220; // button border
    u8 textR = 255, textG = 255, textB = 255;  // label text
};

// ---------------------------------------------------------------------------
// RenderMenuForm — paint a loaded menu window/widget tree into `target`.
//
//   bg       : optional background sprite gfx id (>=0 => drawSprite hook tried,
//              then a MenuFillRect backdrop is laid down regardless so the frame
//              is non-blank).  Pass <0 to skip the explicit backdrop fill.
//   win      : the window whose child widgets are the menu buttons/labels.
//   widgets  : the widget array indexed by the window's child id list; `count`
//              widgets are considered.  Each widget with type kTypeLabel/sprite
//              (and inUse) is drawn as a button (sprite hook + fill + outline) at
//              its (x,y,w,h); its label text (if any) is drawn via real DrawText.
//   pal      : visual style for the asset-less path.
//
// All widget derefs are guarded.  Returns the number of widgets drawn.
//
// This is the reconstruction of the 0x529d08 build+render: the layout it walks is
// exactly the column main_menu.h recovered (x=32, the 8-entry y-table, gfx 174),
// and the per-widget paint reuses the real gfx.c/text.c leaves named above.
// ---------------------------------------------------------------------------
int RenderMenuForm(MenuRenderTarget& target, int bg,
                   const Window* win, const Widget* widgets, int count,
                   const MenuPalette& pal = MenuPalette());

// Convenience overload: build the canonical main-menu column (gui/main_menu.h) into
// `target` directly (no caller-supplied tree).  Lays the backdrop, then the eight
// gfx-174 buttons at x=32 / the y-table with their captions.  Returns buttons drawn.
int RenderMainMenu(MenuRenderTarget& target,
                   const MenuPalette& pal = MenuPalette());

} // namespace guild::gui
