#pragma once
// guild::gui — in-game city/world map view: world<->screen coordinate transforms,
// marker placement, and the scroll/pan offset model.
//
// The map view is a 512x360 scrollable viewport onto a larger world bitmap
// (dword_1233440 x dword_1233444 pixels). Markers (offices, city points) are world
// objects whose 3D position is projected onto the map surface by
// VIBE_MapView_ComputeMarkerScreenPos @0x5440b4; the viewport is panned by a
// drag/auto-scroll model (VIBE_MapView_UpdateScrollState @0x543994,
// VIBE_MapView_StepScrollOffset @0x543bd0).
//
// This module recovers the DATA/LAYOUT half — the coordinate transforms, the pan
// clamping math, the corner-object placement offsets, and the office-marker
// collection — byte-for-byte. The 3D bone-chain projection of city points
// (VIBE_CityMap_CreateCityPointMarker) and the actual surface blit are render/world
// cluster work and are deferred.

#include "gui/types.h"

namespace guild::gui {

// ---------------------------------------------------------------------------
// Viewport geometry (recovered from the clamp constants in StepScrollOffset /
// UpdateScrollState).  The world bitmap size is a runtime value (g_mapWidth /
// g_mapHeight, originally dword_1233440 / dword_1233444); the visible window is a
// fixed 512x360 region whose top-left is the scroll offset.
// ---------------------------------------------------------------------------
inline constexpr int kMapViewW = 512; // visible width  (world - 512 = max scrollX)
inline constexpr int kMapViewH = 360; // visible height (world - 360 = max scrollY)

extern int g_mapWidth;   // dword_1233440  world bitmap width  (px)
extern int g_mapHeight;  // dword_1233444  world bitmap height (px)

// ---------------------------------------------------------------------------
// World->screen marker projection transform constants (recovered as exact bytes).
//   dbl_624058 = 0.5      camera-origin scale  (applied to *(camera+32))
//   flt_624060 = 5.33     world-unit -> map-pixel scale
//   flt_624064 = 0.5      perspective denominator scale
//   flt_624068 = 51.0     perspective bow amount
// ---------------------------------------------------------------------------
inline constexpr double kMarkerOriginScale = 0.5;   // dbl_624058
inline constexpr float  kMarkerWorldScale  = 5.33f; // flt_624060
inline constexpr float  kMarkerPerspScale  = 0.5f;  // flt_624064
inline constexpr float  kMarkerPerspBow    = 51.0f; // flt_624068

// A marker record carries its world position and the computed screen position.
// In the original this lives at offsets +8/+12 (world x/z) and +16/+20 (screen
// x/y, as floats) of a larger object record; here we model just those fields.
struct MapMarker {
    float worldX;   // +8   world x
    float worldZ;   // +12  world z
    float screenX;  // +16  computed screen x (float)
    float screenY;  // +20  computed screen y (float)
};

// gilde.exe 0x5440b4 — VIBE_MapView_ComputeMarkerScreenPos (a1@eax marker, a2@edx
// panX, a3@ebx panY).  Projects the marker's world (x,z) onto the map surface with
// the perspective "bow", offsets by half the viewport and the pan, and writes
// marker.screenX/screenY.  `cameraOriginX` is *(camera+32) in the original
// (dword_13ECF78[0] + 32).  Returns the half-height (the original's return value).
int MapView_ComputeMarkerScreenPos(MapMarker& m, int panX, int panY, int cameraOriginX);

// gilde.exe 0x543bd0 — VIBE_MapView_StepScrollOffset (a1@eax = scroll record).
// One auto-scroll step: nudges the scroll offset (+/-10 px) toward the cursor and by
// the held edge-scroll keys, then clamps to [0, world-512] x [0, world-360].
// Modeled on a small ScrollOffset record (the original stored offX at +600, offY at
// +584).  `mouseX/mouseY` are the cursor pixel coords (>>16 of the packed globals);
// edgeUp/edgeDown/edgeLeft/edgeRight are the held edge flags (byte_671E28/30/2B/2D).
// Returns 1 when the offset changed, else 0.  `boundLoX/boundHiX/boundLoY/boundHiY`
// are the drag dead-band bounds (dword_62D0CC/C4/D0/C8); pass them through unchanged
// for a pure key/cursor step.
struct ScrollOffset {
    int x; // +600  scrollX (left edge of viewport in world px)
    int y; // +584  scrollY (top  edge of viewport in world px)
};
int MapView_StepScrollOffset(ScrollOffset& off, int mouseX, int mouseY,
                             int boundLoX, int boundHiX, int boundLoY, int boundHiY,
                             bool edgeUp, bool edgeDown, bool edgeLeft, bool edgeRight);

// gilde.exe 0x5437d8 — VIBE_MapView_AddCornerObjects: the four corner decoration
// objects are placed at fixed (x,y) map coords (0,0),(0,418),(0,120),(579,120) via
// Object_AddToWindow(win, y, x, gfx+n).  We expose the placement table so the corner
// layout is testable without the renderer.
struct CornerPlacement { i16 x, y; };
extern const CornerPlacement kMapCornerPlacements[4];

} // namespace guild::gui
