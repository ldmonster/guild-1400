#include "play/hud_render.h"

#include "render/surface.h"          // render::SurfaceDrawHLine / DrawRectOutline
#include "render/text_raster.h"      // render::DrawText / DrawGlyph (the 5x7 GUI font)
#include "render/font.h"             // render::FontInitGlyphTable
#include "render/surface_present.h"  // render::PresentGlobals / PresentBackend
#include "gui/menu_render.h"         // gui::MenuFillRect (real fill), gui::Argb8888
#include "gui/hud.h"                 // gui::Clock_ComputeTimeOfDay / ClockTime
#include "world/money_format.h"      // world::MoneyFormatWithSeparators

#include <cstdint>
#include <cstdio>

namespace guild::play {

// ===========================================================================
// Hooks (asset-backed sprite-bank blit) — inert default defined in the library,
// per the build-model convention.
// ===========================================================================
namespace {
HudRenderHooks g_hooks;   // drawSprite == nullptr => inert (paints nothing)

// Lazily-initialised 256-entry glyph map (render::FontInitGlyphTable / byte_75FB50).
const u8* GlyphMap() {
    static u8 table[256];
    static bool init = false;
    if (!init) { render::FontInitGlyphTable(table); init = true; }
    return table;
}

// PresentGlobals that drives DrawText/DrawGlyph straight into a 32bpp surface
// (the Lock-copy path; no DDraw).  Same wiring the menu renderer uses.
render::PresentGlobals SurfacePresent(render::Surface& s) {
    render::PresentGlobals g;
    g.mode         = render::PresentBackend::DDrawLockBlt;
    g.ppvBits      = reinterpret_cast<std::uintptr_t>(s.pixels);
    g.dibPitch     = s.pitch;
    g.dibStride    = s.widthPx;
    g.screenHeight = s.height;
    g.pitchExtra   = 4;     // bytes per pixel
    g.lockBitDepth = 32;
    g.primary      = nullptr;
    return g;
}

// Draw a caption via the REAL render::DrawText leaf.  Returns the number of
// non-space glyphs the leaf would have stamped (chars submitted that advance + draw).
int DrawCaption(render::Surface& s, int x, int y, const std::string& text,
                u8 r, u8 g, u8 b) {
    if (text.empty()) return 0;
    render::PresentGlobals pg = SurfacePresent(s);
    render::DrawText(x, y, reinterpret_cast<const u8*>(text.c_str()), r, g, b,
                     GlyphMap(), pg, s.fmt);
    return static_cast<int>(text.size());
}
} // namespace

void SetHudRenderHooks(const HudRenderHooks& hooks) { g_hooks = hooks; }
const HudRenderHooks& GetHudRenderHooks() { return g_hooks; }

// ===========================================================================
// Layout math.
// ===========================================================================

// Bar fill pixels: VIBE_Hud_BuildScaledTiledBar scales the production ratio into the
// sub-window width.  The reconstructed ratio->percent leaf (PlayerBar_OutputRatio-
// Percent, pct = (int)(ratio*100)) gives the % the slot label shows; the pixel fill
// is that fraction of the sub-window (clamped to the track).
int HudBarFillPixels(double ratio, int subWinW) {
    if (subWinW <= 0) return 0;
    int pct = gui::PlayerBar_OutputRatioPercent(ratio);  // (int)(ratio*100)
    if (pct < 0)   pct = 0;
    if (pct > 100) pct = 100;
    // px = pct/100 * subWinW (integer, truncated toward zero — matches the original's
    // (int) of the scaled double width).
    int px = (pct * subWinW) / 100;
    if (px < 0)       px = 0;
    if (px > subWinW) px = subWinW;
    return px;
}

std::string HudMoneyString(i32 money, i32 rate) {
    std::string s = world::MoneyFormatWithSeparators(money, rate);
    // Replace the trailing currency glyph (0x11) with a printable stand-in so the
    // 5x7 GUI font draws a visible caption in the asset-less path.
    for (char& c : s)
        if (c == world::kCurrencyGlyph) c = '$';
    return s;
}

std::string HudDateString(int gameDay, int clockTick) {
    gui::ClockTime t = gui::Clock_ComputeTimeOfDay(clockTick);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "DAY %d  %02d:%02d", gameDay, t.h, t.m);
    return std::string(buf);
}

MarkerXY HudMarkerScreenXY(const HudMarker& mk, int panX, int panY, int cameraOrigX) {
    gui::MapMarker m{};
    m.worldX = mk.worldX;
    m.worldZ = mk.worldZ;
    gui::MapView_ComputeMarkerScreenPos(m, panX, panY, cameraOrigX);
    MarkerXY xy;
    xy.x = static_cast<int>(m.screenX);
    xy.y = static_cast<int>(m.screenY);
    return xy;
}

// ===========================================================================
// Render the HUD overlay.
// ===========================================================================
HudRenderResult RenderHud(render::Surface& surf, const HudRenderState& state,
                          int barOriginX, int barOriginY,
                          int captionX, int captionY,
                          int mapOriginX, int mapOriginY,
                          const HudPalette& pal) {
    HudRenderResult res;
    if (!surf.pixels) return res;

    // -----------------------------------------------------------------------
    // 1. Bottom player bar.  Assign each owned object a slot (real PlayerBar_-
    //    AssignSlot de-dup/free-scan), resolve its layout (real PlayerBar_Slot-
    //    Layout 78px pitch + sub-window offsets), then draw: optional icon sprite
    //    (hook), the sub-window track, the production fill, and a frame outline.
    // -----------------------------------------------------------------------
    gui::ResetPlayerBar();
    for (const HudBarObject& obj : state.barObjects) {
        int slot = gui::PlayerBar_AssignSlot(obj.objId);
        if (slot < 0) continue;                       // bar full (>32)
        gui::PlayerBarLayout L = gui::PlayerBar_SlotLayout(slot);

        // Sub-window (the output-ratio bar): top-left in frame space.
        int subX = barOriginX + L.subWinX;
        int subY = barOriginY + L.subWinY;

        // Optional icon artwork (asset edge -> hook; inert default draws nothing).
        if (g_hooks.drawSprite)
            g_hooks.drawSprite(&surf, barOriginX + L.spriteX,
                               barOriginY + L.spriteY, /*gfx*/ 1403, g_hooks.userData);

        // Track (empty part) — the whole sub-window, via the REAL MenuFillRect leaf
        // (-> render::SurfaceDrawHLine per row).
        gui::MenuFillRect(&surf, subX, subY, L.subWinW, L.subWinH,
                          pal.barTrackR, pal.barTrackG, pal.barTrackB);

        // Fill (production part) — the scaled width, the same real fill leaf.
        int fillPx = HudBarFillPixels(obj.ratio, L.subWinW);
        if (fillPx > 0) {
            int rows = gui::MenuFillRect(&surf, subX, subY, fillPx, L.subWinH,
                                         pal.barFillR, pal.barFillG, pal.barFillB);
            res.barFillRows += rows;
        }

        // Frame outline — the REAL render::SurfaceDrawRectOutline leaf.
        render::SurfaceDrawRectOutline(&surf, subX, subY, L.subWinW, L.subWinH,
                                       pal.barFrameR, pal.barFrameG, pal.barFrameB);
        ++res.barSlotsDrawn;
    }

    // -----------------------------------------------------------------------
    // 2. Money/date caption.  Format via the REAL money + clock leaves, then blit
    //    through the REAL render::DrawText leaf.  The money line sits above the
    //    date line (one 5x7 row + a 2px gap = 9px).
    // -----------------------------------------------------------------------
    std::string money = HudMoneyString(state.money, state.moneyRate);
    std::string date  = HudDateString(state.gameDay, state.clockTick);
    res.captionGlyphs += DrawCaption(surf, captionX, captionY, money,
                                     pal.textR, pal.textG, pal.textB);
    res.captionGlyphs += DrawCaption(surf, captionX, captionY + 9, date,
                                     pal.textR, pal.textG, pal.textB);

    // -----------------------------------------------------------------------
    // 3. Map markers.  Project each world marker to its HUD pixel (REAL
    //    MapView_ComputeMarkerScreenPos), then draw a small filled dot + outline
    //    at (mapOrigin + projected).  The optional marker artwork is the hook.
    // -----------------------------------------------------------------------
    const int kMarkerSize = 4;
    for (const HudMarker& mk : state.markers) {
        MarkerXY xy = HudMarkerScreenXY(mk, state.markerPanX, state.markerPanY,
                                        state.markerCameraOrigX);
        int mx = mapOriginX + xy.x;
        int my = mapOriginY + xy.y;

        if (g_hooks.drawSprite)
            g_hooks.drawSprite(&surf, mx, my, /*gfx*/ 1404, g_hooks.userData);

        gui::MenuFillRect(&surf, mx, my, kMarkerSize, kMarkerSize,
                          pal.markerR, pal.markerG, pal.markerB);
        render::SurfaceDrawRectOutline(&surf, mx, my, kMarkerSize, kMarkerSize,
                                       pal.markerEdgeR, pal.markerEdgeG, pal.markerEdgeB);
        ++res.markersDrawn;
    }

    return res;
}

} // namespace guild::play
