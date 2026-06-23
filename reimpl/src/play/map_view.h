#pragma once
// =============================================================================
// guild::play — 2D OVERVIEW MAP VIEW (the full-screen city/world map panel).
//
// Reconstructs the RENDER + interaction half of VIBE_MapView_PanelDispatcher
// @0x5441d0 (the 0x1df5-byte map-panel handler): the map-background draw, the
// entity-marker placement/draw loop, the pan/scroll, and the click->world
// mapping — composited 1:1 into a software render::Surface and presented via the
// existing surface path (rules 1,3,4: engine math reconstructed; software draw,
// no new GPU code).
//
// WHAT THE ORIGINAL DOES (gilde.exe 0x5441d0, grounded line-by-line):
//   * Loads the map FORM around the background asset "Misc\Landkarte2"
//     (VIBE_GameTick_Finalize(0,0,aMiscLandkarte2) @0x41beb8) and positions a
//     512x360 scrollable child window (VIBE_Window_AddChildWindow(74,76,356,518)
//     @0x41a598) onto it.  +-> the BACKGROUND is the Landkarte2 artwork.
//   * Enumerates the world entities to mark:
//       - persons/buildings: VIBE_Person_QueryBegin(1,2,5,9,4,player) @0x586c20
//         filtered (skip object types 68/69; skip if entity[+90]&1; skip type 71
//         unless the player handle is set) -> up to 256 markers (kMapViewMaxMarkers).
//       - sp_AUFLAUERLEGEN ambush points (mode 0x04), mission markers
//         (dword_123343C, mode 0x08), and the focused/player object.
//   * For each entity it projects the world (x,z) through the bone chain
//     (VIBE_Transform_PointThroughBoneChain @0x5c8b38) + heightmap
//     (VIBE_Heightmap_WorldToTileWithHeight @0x5c6644) to a tile, then through
//     VIBE_MapView_ComputeMarkerScreenPos @0x5440b4 (reused as
//     gui::MapView_ComputeMarkerScreenPos) to the map-surface pixel.
//   * Sorts the markers ascending by screenY (painter's order; the selection sort
//     @0x54457e, reconstructed as play::MapViewSortMarkersByScreenY), CENTERS each
//     marker sprite ( screenX -= (w>>16)/2 ; screenY -= (h>>16)/2 ; @0x5457e1 ),
//     creates a window object for it (VIBE_Object_AddToWindow @0x41ae10) and draws
//     it; type-71 ("money") markers get a gold-label caption, others a name copy.
//   * Runs the frame loop (VIBE_GameLogic_RunFrameLoop(423879) @0x4c09a0): each
//     frame steps the auto-scroll (VIBE_MapView_StepScrollOffset @0x543bd0 /
//     _UpdateScrollState @0x543994, reconstructed in gui::MapView_*), and on a
//     click over a marker returns/opens the entity per the al-mode bits.
//
// WHAT THIS MODULE OWNS (the RENDER, reusing the already-reconstructed leaves):
//   The heavy Form/Window/Object widget TREE, the 3D bone-chain projection, and
//   the asset loader are deep engine subsystems already deferred elsewhere; this
//   module reconstructs the deterministic SOFTWARE COMPOSITION that the panel's
//   marker loop performs — given the markers (already projected by the real
//   gui::MapView_ComputeMarkerScreenPos), apply the real pan/clamp/sort, then
//   draw the background + each entity marker as a centered dot into a Surface via
//   the REAL render leaves (gui::MenuFillRect -> render::SurfaceDrawHLine,
//   render::SurfaceDrawRectOutline), and provide the click->world inverse of the
//   marker projection.  Two renders from identical inputs produce identical pixels.
//
// NAMED GAP (rule 8): the Landkarte2 background ARTWORK is the one asset edge.
//   It is the "Misc\Landkarte2" shape loaded by VIBE_GameTick_Finalize @0x41beb8
//   through the SHAPBANK loader; that bank is depth-2 (24bpp) and is converted to
//   depth-1 at runtime by VIBE_Shape_ConvertRgbTo16 @0x5d7c0c (NOT yet
//   reconstructed — see session_hud.h's gilde.gfx finding).  So the real bitmap
//   cannot be blitted here.  It is routed through MapViewHooks::drawBackground
//   with an INERT default (fills the viewport with the configured backdrop colour)
//   so the deterministic marker composition is testable in isolation, exactly the
//   hooks-with-inert-default convention used by play/hud_render.
//
// HANDOFF (how the map opens over the session): a HUD map key/button raises the
//   al-mode bits and calls VIBE_MapView_PanelDispatcher @0x5441d0.  In the
//   reimplementation the session HUD (play/session_hud) opens the overview by
//   calling play::MapView_RenderOverview into the session framebuffer surface;
//   the al-mode bits map to MapViewOpenMode (see below).  The orchestrator wires
//   the key/button -> MapView_RenderOverview; this module does NOT edit the
//   bind-site (per ownership rules).
// =============================================================================

#include "guild/common/types.h"
#include "render/types.h"               // render::Surface, ColorFormat
#include "gui/mapview.h"                // gui::MapView_ComputeMarkerScreenPos / MapMarker

#include <vector>

namespace guild::play {

// ---------------------------------------------------------------------------
// Viewport geometry — the panel's 512x360 scrollable child window onto the
// larger world bitmap (gui::g_mapWidth x g_mapHeight, dword_1233440/444).
// Recovered from the clamp constants in StepScrollOffset (world-512, world-360)
// and the AddChildWindow(74,76,356,518) call @0x5443c3.
// ---------------------------------------------------------------------------
inline constexpr int kOverviewViewW = 512;  // gui::kMapViewW (world-512 = max scrollX)
inline constexpr int kOverviewViewH = 360;  // gui::kMapViewH (world-360 = max scrollY)

// al-mode bits the dispatcher branches on (mirrors play::kMapViewMode* in
// ui_recon5_panels.h; restated here so a caller can pick the open mode without
// pulling the whole panel-logic header).
enum class MapViewOpenMode : u8 {
    Plain      = 0x00, // overview only
    ReturnObj  = 0x01, // v252&1 — click returns the marker's entity
    PersonSel  = 0x02, // v252&2 — click opens the person-selection sub-window
    Auflauer   = 0x04, // v252&4 — also place sp_AUFLAUERLEGEN ambush markers
    Missions   = 0x08, // v252&8 — also place dword_123343C mission markers
    Tooltip    = 0x10, // v252&16 — tooltip layer raised
};

// ---------------------------------------------------------------------------
// One entity marker to draw on the overview.  worldX/worldZ are the entity's
// already-projected world coords (what the bone-chain + heightmap stages feed
// into the real gui::MapView_ComputeMarkerScreenPos); `kind` selects the dot
// colour (so player/building/NPC/mission dots are distinguishable, matching the
// original's per-type marker artwork + the type-71 "money" branch).
// ---------------------------------------------------------------------------
enum class MarkerKind : u8 {
    Person   = 0, // generic person/NPC
    Building = 1, // owned/foreign building (office)
    Money    = 2, // object type 71 — the "gold label" branch @0x545848
    Mission  = 3, // mode-0x08 mission marker (dword_123343C)
    Ambush   = 4, // mode-0x04 sp_AUFLAUERLEGEN marker
    Player   = 5, // the focused/player object (the centred camera marker)
};

struct OverviewMarker {
    float      worldX = 0.0f;   // MapMarker +8
    float      worldZ = 0.0f;   // MapMarker +12
    MarkerKind kind   = MarkerKind::Person;
    i32        entity = -1;     // entity handle (returned on a click hit)
};

// Camera / pan state fed to the projection + the viewport scroll, exactly the
// dispatcher's inputs: cameraOrigX is *(camera+32) (dword_13ECF78[0]+32); panX/
// panY are the v225/v226 pan from byte_6477A1's 189-stride table; scrollX/scrollY
// are the child window's scroll offset (clamped to [0,world-512]x[0,world-360]).
struct OverviewCamera {
    int cameraOrigX = 0; // *(camera+32)
    int panX        = 0; // v225 (dword_13CD98C[189*byte_6477A1])
    int panY        = 0; // v226 (dword_13CD990[189*byte_6477A1])
    int scrollX     = 0; // child-window scroll left  (world px)
    int scrollY     = 0; // child-window scroll top   (world px)
};

// Visual style for the asset-less (reconstructed) draw.  Real Landkarte2 artwork
// is supplied via the background hook; the marker dot palette is per MarkerKind.
struct OverviewPalette {
    // Inert-background fill (used when no background hook is installed).
    u8 bgR = 30,  bgG = 50,  bgB = 30;
    // Per-kind marker dot colours (Person,Building,Money,Mission,Ambush,Player).
    u8 dotR[6] = {200, 200, 240, 240,  40, 255};
    u8 dotG[6] = {200, 160, 200,  40,  40, 255};
    u8 dotB[6] = {200,  40,  40,  40, 240, 255};
    // Marker outline.
    u8 edgeR = 0, edgeG = 0, edgeB = 0;
};

// ---------------------------------------------------------------------------
// Installable hook for the one asset-backed leaf: blitting the real Landkarte2
// background shape into the viewport.  The inert default paints the configured
// backdrop colour, so without assets the overview still gets its viewport +
// every marker dot (non-blank, readback-verifiable).
// ---------------------------------------------------------------------------
struct MapViewHooks {
    // Draw the map background into [viewX,viewX+viewW) x [viewY,viewY+viewH) given
    // the scroll offset (the visible window's top-left in world px).  Return true
    // if it drew (so the inert backdrop is skipped).  Default: nullptr (inert).
    bool (*drawBackground)(render::Surface* s, int viewX, int viewY,
                           int viewW, int viewH, int scrollX, int scrollY,
                           void* userData) = nullptr;
    void* userData = nullptr;
};
void SetMapViewHooks(const MapViewHooks& hooks);
const MapViewHooks& GetMapViewHooks();

// ===========================================================================
// PROJECTION (golden-vectorable; pure functions of their inputs).
// ===========================================================================

// Project one entity marker to its overview-viewport pixel via the REAL
// gui::MapView_ComputeMarkerScreenPos, then offset by the scroll so the result is
// the pixel WITHIN the visible viewport (screen pos minus scroll).  Returns the
// integer (x,y); `inView` is set when the pixel lies inside [0,viewW)x[0,viewH).
struct OverviewXY { int x; int y; bool inView; };
OverviewXY MapView_ProjectMarker(const OverviewMarker& mk, const OverviewCamera& cam);

// Inverse of the marker projection's affine half — map a viewport CLICK (vx,vy)
// back to a world (x,z).  The forward map (ignoring the small perspective "bow",
// which the original does not invert for click pick either) is:
//   screenX = mapW/2 + (worldX - camOrigX*0)·5.33 + panX     (origin scale = 0)
//   screenY = mapH/2 + (worldZ - camOrigX*0)·5.33 + panY
// so the inverse over the visible viewport (adding scroll back) is:
//   worldX = ((vx + scrollX - panX) - mapW/2) / 5.33
//   worldZ = ((vy + scrollY - panY) - mapH/2) / 5.33
// Returns the recovered world (x,z).  (Used for click->world; the dispatcher's
// own click handling is marker hit-testing — see MapView_PickMarker.)
struct WorldXZ { float x; float z; };
WorldXZ MapView_ClickToWorld(int vx, int vy, const OverviewCamera& cam);

// Marker hit-test: returns the index of the topmost marker whose centred dot of
// `dotSize` covers viewport pixel (vx,vy), or -1.  Mirrors the dispatcher's
// click->entity resolution (the marker whose window object the cursor is over);
// `markers` must already be in draw order (Y-sorted), so the LAST covering marker
// (drawn on top) wins.
int MapView_PickMarker(const std::vector<OverviewMarker>& markers,
                       const OverviewCamera& cam, int vx, int vy, int dotSize);

// ===========================================================================
// RENDER (rasterize the overview map into a Surface).
// ===========================================================================

// Result counts of one overview render pass (observable real output).
struct OverviewRenderResult {
    int markersDrawn = 0;  // markers whose dot landed inside the viewport
    int backgroundDrawn = 0; // 1 if a background (hook or inert) was painted
};

// Render the overview map (background + entity markers) into `surf` at viewport
// origin (viewX,viewY) of size viewW x viewH (default 512x360).  Steps, 1:1 with
// the panel's marker loop:
//   1. Background: the Landkarte2 hook (asset edge) or, inert, a backdrop fill.
//   2. Markers: sorted ascending by projected screenY (REAL
//      MapViewSortMarkersByScreenY), each projected (REAL
//      gui::MapView_ComputeMarkerScreenPos), offset by scroll, CENTRED on its dot
//      (the original's screenX-=w/2, screenY-=h/2), and drawn — filled dot
//      (gui::MenuFillRect) + outline (render::SurfaceDrawRectOutline) — clipped to
//      the viewport.  Markers outside the viewport are skipped.
// `dotSize` is the marker dot size in px (the original's per-type sprite size; the
// reconstructed default is 6).  All derefs guarded; a null/zero-pixel surface is a
// no-op.  Returns the draw counts.  `markers` is taken by value-copy internally
// for the sort, so the caller's vector is left untouched.
OverviewRenderResult MapView_RenderOverview(render::Surface& surf,
                                            std::vector<OverviewMarker> markers,
                                            const OverviewCamera& cam,
                                            int viewX = 0, int viewY = 0,
                                            int viewW = kOverviewViewW,
                                            int viewH = kOverviewViewH,
                                            int dotSize = 6,
                                            const OverviewPalette& pal = OverviewPalette());

} // namespace guild::play
