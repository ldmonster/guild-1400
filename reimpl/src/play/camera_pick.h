#pragma once
// Wave PLAY P2 — CAMERA + PICKING (PLAYABLE_PLAN P2).
//
// This module wires the interactive camera and object-picking layer on top of the
// reconstructed engine math. It reuses the REAL reconstructions (via extern), not
// re-implemented copies:
//   - guild::sim::ScreenToWorldRay   (gilde.exe 0x426850, character_render3.cpp)
//   - guild::sim::ProjectRayDirection(gilde.exe 0x426764, character_render3.cpp)
//   - guild::app::ResolveCombatScroll(gilde.exe 0x487b2c, combat_scroll.cpp) — the
//     reconstructed combat/camera edge-scroll decision core, used to turn an edge
//     scroll-direction input into per-edge scroll codes for the pan step.
//   - guild::sim::GameObjectResolveEntityById (gilde.exe 0x583b44, entity.cpp) —
//     resolves a picked object id into a concrete entity record.
//   - guild::util::MatrixIdentity / MatrixTransformVectors (matrix.cpp) for the view
//     transform.
//
// Nothing here is itself reconstructed from a single VIBE_ function — it is the thin
// interactive glue (the "what does the engine DO with a click / a scroll input"
// layer the binary spreads across the input dispatch). It is pure + deterministic so
// it can be exercised headless.

#include "guild/common/types.h"

namespace guild::play {

// ===========================================================================
// Camera state — the runtime camera the picking math reads from. Mirrors the
// engine globals the real ray routines consume (the +396 view matrix, the +76
// eye position, and the flt_13FCD0C/10/18 screen-plane constants), but as an
// owned struct so the camera is drivable headless and deterministically.
// ===========================================================================
struct CameraState {
    float eye[3]      = {0.0f, 0.0f, 0.0f};   // camera origin (engine +76..84)
    float view[16]    = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}; // view matrix (+396)
    float focal       = 1.0f;                 // flt_13FCD0C (focal length)
    float centerX     = 0.0f;                 // flt_13FCD18 (screen center x)
    float centerY     = 0.0f;                 // flt_13FCD10 (screen center y)
    float zNear       = 1.0f;                  // ray depth-scale near
    float zFar        = 1000.0f;               // ray depth-scale far
};

// Initializes a camera looking down +Z from `eye`, with the given screen size and
// focal. view = identity (looking along the engine's fixed forward basis). Screen
// center is (w/2, h/2). Deterministic; no globals touched.
CameraState MakeCamera(const float eye[3], float focal, int screenW, int screenH);

// ---------------------------------------------------------------------------
// Camera movement (deterministic view-transform mutation).
// ---------------------------------------------------------------------------

// Pan the camera by (dx, dy, dz) in world space: translates the eye and the view
// matrix translation column (view[12..14]) by the same delta. Same input -> same
// transform every time. Returns nothing observable; mutates `cam`.
void CameraPan(CameraState& cam, float dx, float dy, float dz);

// Zoom by moving the eye along its current forward axis (the +Z basis rotated by
// the view 3x3, the same forward the engine ray uses). `amount` > 0 dollies in.
// Also shifts the view translation column to keep the transform consistent.
void CameraZoom(CameraState& cam, float amount);

// ---------------------------------------------------------------------------
// Edge-scroll: turn a cursor position near a screen edge into a pan. Uses the
// REAL reconstructed combat-scroll decision core to resolve per-edge scroll codes,
// then converts the resulting direction into a world-space pan of `speed` units.
// `margin` is the edge band in pixels. Returns the (dx, dz) world pan applied
// (also applies it to `cam`); dy is left untouched (top-down ground pan).
// ---------------------------------------------------------------------------
struct EdgeScrollResult {
    int dirX = 0;    // -1 left edge, +1 right edge, 0 none
    int dirY = 0;    // -1 top edge, +1 bottom edge, 0 none
    float panX = 0.0f;
    float panZ = 0.0f;
    bool scrolled = false;
};
EdgeScrollResult CameraEdgeScroll(CameraState& cam, int mouseX, int mouseY,
                                  int screenW, int screenH, int margin, float speed);

// ===========================================================================
// Object picking.
// ===========================================================================

// A pickable scene object: an id (resolvable via GameObjectResolveEntityById) and a
// bounding sphere in world space. The engine hit-tests meshes; for the interactive
// layer the selection volume is the object's bounding sphere (center + radius),
// which is what the picking ray intersects.
struct SceneObject {
    i32   id     = 0;
    float center[3] = {0.0f, 0.0f, 0.0f};
    float radius = 1.0f;
};

// Ray-sphere intersection. ray origin `o`, normalized (or not) direction `d`,
// sphere center `c` radius `r`. If the ray hits, writes the nearest non-negative
// hit distance to `outT` and returns true; else returns false. Standard quadratic
// solve (independent of any engine code — golden-tested).
bool RaySphereHit(const float o[3], const float d[3],
                  const float c[3], float r, float* outT);

// Result of a pick: which object (index into the input array, -1 == none), its id,
// and the hit distance along the ray.
struct PickResult {
    int   index = -1;     // index into the objects array, -1 if nothing hit
    i32   id    = 0;      // picked object id (0 if none)
    float dist  = 0.0f;   // ray distance to the hit
};

// Cast a pick ray from screen pixel (sx, sy) through the camera and return the
// NEAREST object hit. Builds the ray with the REAL ScreenToWorldRay (direction) and
// uses cam.eye as the ray origin (the real ProjectRayDirection origin path). A click
// on empty space returns {index=-1, id=0}.
PickResult PickObject(const CameraState& cam, float sx, float sy,
                      const SceneObject* objects, int count);

// Higher-level: pick at (sx,sy) and resolve the picked id into a concrete entity via
// the REAL GameObjectResolveEntityById. `outKind` receives the resolve kind
// (1 object, 2 scene, 3 person, 0 none); returns the PickResult. If nothing is
// picked, outKind is 0 and the result index is -1.
PickResult PickAndResolveEntity(const CameraState& cam, float sx, float sy,
                                const SceneObject* objects, int count, int* outKind);

} // namespace guild::play
