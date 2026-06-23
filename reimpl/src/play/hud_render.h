#pragma once
// IN-GAME HUD OVERLAY RENDERER (PLAYABLE_PLAN P2) — guild::play.
//
// Rasterize the live in-game HUD (the bottom player bar, the money/date caption,
// and the map markers) ON TOP of an already-rendered world frame, into a target
// render::Surface, using the REAL reconstructed gui/render draw leaves.
//
// What the original draws each frame (grounding from gilde.exe):
//   * VIBE_Render_DrawUniverseAndStats @0x5b3bbc composes the 3D world frame and
//     then the HUD stats overlay on top of it (the brief's "DrawUniverseAndStats").
//   * The bottom owned-object strip is VIBE_PlayerBar_BuildContent @0x4b11e4: a row
//     of up-to-32 slots at a fixed 78px row pitch, each with an icon, a name/qty
//     label, and an output-ratio sub-bar (the per-slot fill).  The fill amount is
//     VIBE_Hud_BuildScaledTiledBar @0x4bd758, whose width is the production-ratio
//     (VIBE_Building_ComputeProductionPixels) scaled into the kPlayerBarSubWinW px
//     sub-window.  We reuse gui::PlayerBar_SlotLayout / _AssignSlot / _OutputRatio
//     directly for the slot placement + ratio math.
//   * The money/date caption is the stats line: the money string is
//     VIBE_Money_FormatWithSeparators @0x58f798 (reused as world::MoneyFormat-
//     WithSeparators) and the clock/date is VIBE_Clock_ComputeGameTimeOfDay
//     @0x527778 (reused as gui::Clock_ComputeTimeOfDay).
//   * Map markers are VIBE_MapView_ComputeMarkerScreenPos @0x5440b4 (reused as
//     gui::MapView_ComputeMarkerScreenPos) projecting a world (x,z) onto the map
//     surface; we draw a small filled marker + outline at the projected pixel.
//
// The per-pixel work bottoms out in the SAME deterministic 2D leaves the menu
// renderer uses (gui::MenuFillRect -> render::SurfaceDrawHLine, render::Surface-
// DrawRectOutline, render::DrawText/DrawGlyph — the 5x7 GUI font).  The asset-
// backed icon/sprite-bank blit (the gfx-1403 icon artwork) is the one loader/device
// edge; it is routed through an installable HudRenderHooks with an INERT default
// (draws nothing) so the deterministic fill+text+marker path is testable in
// isolation, per the build model's hooks-with-inert-defaults convention.
//
// Determinism: RenderHud is a pure function of (HudRenderState, target surface,
// palette) — two renders from the same inputs produce identical pixels.

#include "guild/common/types.h"
#include "render/types.h"          // render::Surface, ColorFormat
#include "gui/menu_render.h"       // gui::Argb8888 / MenuFillRect (real fill leaf)
#include "gui/playerbar.h"         // gui::PlayerBar_* slot layout + ratio math
#include "gui/mapview.h"           // gui::MapMarker / MapView_ComputeMarkerScreenPos

#include <string>
#include <vector>

namespace guild::play {

// ---------------------------------------------------------------------------
// Live HUD state the overlay rasterizes.  Each field maps to a real in-game HUD
// input; the bar fill ratios, money amount and marker world positions are exactly
// what the reconstructed leaves consume.
// ---------------------------------------------------------------------------

// One owned-object slot for the bottom player bar: the object id (drives the slot
// assignment / de-dup) plus its output-production ratio in [0,1] (the fill).
struct HudBarObject {
    u16    objId  = 0;
    double ratio  = 0.0;   // VIBE_Building_ComputeProductionRatio result in [0,1]
};

// One world-space marker to project + draw onto the HUD map region.
struct HudMarker {
    float worldX = 0.0f;   // MapMarker +8
    float worldZ = 0.0f;   // MapMarker +12
};

struct HudRenderState {
    // Money/date caption.
    i32  money        = 0;     // raw player money (pre-rate); formatted via MoneyFormat
    i32  moneyRate    = 1;     // per-currency display rate (1 = identity)
    int  clockTick    = 0;     // tick accumulator -> Clock_ComputeTimeOfDay
    int  gameDay      = 0;     // calendar day shown next to the clock

    // Bottom player bar: the owned objects + their fill ratios, in draw order.
    std::vector<HudBarObject> barObjects;

    // Map markers (projected onto the map region).
    std::vector<HudMarker> markers;
    int markerPanX        = 0; // MapView pan x
    int markerPanY        = 0; // MapView pan y
    int markerCameraOrigX = 0; // *(camera+32)
};

// ---------------------------------------------------------------------------
// Visual style for the asset-less (reconstructed) HUD draw.  Real artwork is
// supplied via the sprite hook; layout/coords come from the reconstructed leaves.
// ---------------------------------------------------------------------------
struct HudPalette {
    // Player-bar slot sub-window: track (empty part) + fill (production part).
    u8 barTrackR = 40,  barTrackG = 40,  barTrackB = 40;
    u8 barFillR  = 60,  barFillG  = 200, barFillB  = 60;
    u8 barFrameR = 200, barFrameG = 200, barFrameB = 200;
    // Money/date caption text.
    u8 textR = 255, textG = 255, textB = 200;
    // Map marker.
    u8 markerR = 220, markerG = 40, markerB = 40;
    u8 markerEdgeR = 255, markerEdgeG = 255, markerEdgeB = 255;
};

// ---------------------------------------------------------------------------
// Installable hook for the one asset-backed leaf: blitting a real sprite-bank
// shape (the gfx-1403 slot icon / map-marker artwork) into the framebuffer.  The
// inert default paints nothing, so without assets the slot still gets its
// reconstructed track+fill+frame and the marker its reconstructed dot — the HUD
// overlay is still non-blank and readback-verifiable.
// ---------------------------------------------------------------------------
struct HudRenderHooks {
    bool (*drawSprite)(render::Surface* s, int x, int y, int gfxId,
                       void* userData) = nullptr;
    void* userData = nullptr;
};
void SetHudRenderHooks(const HudRenderHooks& hooks);
const HudRenderHooks& GetHudRenderHooks();

// ===========================================================================
// LAYOUT MATH (golden-vectorable; pure functions of their inputs).
// ===========================================================================

// Bar fill width in pixels for a production `ratio` in [0,1] within a sub-window of
// `subWinW` pixels (kPlayerBarSubWinW = 15).  Mirrors VIBE_Hud_BuildScaledTiledBar's
// production->pixels scaling (ratio * subWinW), clamped to [0, subWinW].  Uses the
// reconstructed gui::PlayerBar_OutputRatioPercent (pct = (int)(ratio*100)) so the
// ratio math is the same the slot's "%i%%" label shows.
int HudBarFillPixels(double ratio, int subWinW = gui::kPlayerBarSubWinW);

// The money string the caption renders, via the REAL world::MoneyFormatWithSeparators
// (thousands-grouped + currency glyph).  The trailing glyph byte (0x11) is replaced
// with a printable '$' so the 5x7 GUI font (which has no glyph 0x11) draws a visible
// caption (the real game uses the artwork glyph; this is the asset-less stand-in).
std::string HudMoneyString(i32 money, i32 rate = 1);

// The clock/date caption text: "DAY n  hh:mm" from the reconstructed
// gui::Clock_ComputeTimeOfDay over the tick accumulator.
std::string HudDateString(int gameDay, int clockTick);

// Project a world marker to its HUD pixel coordinate via the REAL
// gui::MapView_ComputeMarkerScreenPos.  Returns the integer (x,y) the marker draws at.
struct MarkerXY { int x; int y; };
MarkerXY HudMarkerScreenXY(const HudMarker& m, int panX, int panY, int cameraOrigX);

// ===========================================================================
// RENDER (rasterize the HUD overlay onto an existing world frame).
// ===========================================================================

// Result counts of one HUD render pass (observable real output).
struct HudRenderResult {
    int barSlotsDrawn  = 0;   // owned-object slots laid out + filled
    int barFillRows    = 0;   // total filled rows across all slot sub-bars
    int captionGlyphs  = 0;   // chars submitted to the real text leaf (money+date)
    int markersDrawn   = 0;   // markers projected + drawn
};

// Rasterize the HUD widgets in `state` onto `surf` (a 32bpp world frame), using the
// real reconstructed leaves.  `barOriginX/Y` is the top-left of the player-bar strip
// (the slot rows step down by kPlayerBarRowPitch); `captionX/Y` is the money/date
// caption anchor; the markers are drawn at their projected pixels offset by
// `mapOriginX/Y` (the map region's top-left within the frame).  The world frame is
// NOT cleared — the HUD composites on top, leaving the world visible everywhere the
// HUD does not cover (exactly the original's DrawUniverseAndStats overlay model).
// Returns the draw counts.  All derefs guarded; a null/zero-pixel surface is a no-op.
HudRenderResult RenderHud(render::Surface& surf, const HudRenderState& state,
                          int barOriginX, int barOriginY,
                          int captionX, int captionY,
                          int mapOriginX, int mapOriginY,
                          const HudPalette& pal = HudPalette());

} // namespace guild::play
