#pragma once
// Wave 25 PLAY P2 — CITY-VIEW CAMERA SETUP + SCREEN->WORLD OBJECT PICKING on the
// REAL scene (PLAYABLE_PLAN P2).
//
// This is the "what the engine does with a city-view click" layer. It is the
// SCENE-side companion to camera_pick.h (which owns the interactive camera + the
// ray/sphere hit-test): scene_pick.h reconstructs the actual city-view camera
// transform the renderer sets up, and the screen-projection-based object pick the
// original performs over the live scene objects.
//
// GROUNDING (decompiled this wave):
//   * VIBE_Render_BeginUniverseFrame @0x5b3900 is the per-frame city-view render
//     entry. It does NOT build the camera matrix itself; it READS the active
//     camera record `dword_13FCD1C` (the city camera object) at:
//        +76..84   -> the eye / world origin    (3 floats)
//        +396..    -> the 4x4 view matrix        (16 floats, column-major)
//     and feeds them to the projection helpers it drives:
//        VIBE_Heightmap_ProjectPointToView @0x5c4034 (fog depth)
//        VIBE_SceneGraph_WalkAndInvoke     @0x5ac738 (per-object project/cull walk
//                                                     -> VIBE_Mesh_ProjectVerticesToScreen)
//   * The world->view convention (recovered identically from ProjectPointToView,
//     VIBE_Character_ProjectRayDirection @0x426764 and ScreenToWorldRay @0x426850):
//        viewVec = M3x3 . (world - eye)
//     where M3x3 uses rows [0,4,8],[1,5,9],[2,6,10] of the +396 matrix.
//   * The actual screen projection of a world point is the per-vertex transform of
//     VIBE_Mesh_ProjectVerticesToScreen @0x5c5120 (render/mesh.cpp), which this
//     module REUSES verbatim (render::ProjectVerticesToScreen) rather than copying.
//
// The pick mirrors the original's mesh-projection pick: project each scene object's
// world position to screen via the REAL projection, then select the object whose
// projected screen point is nearest the cursor and within the pick radius. A miss
// (cursor over empty space) returns -1.
//
// Pure + deterministic; runs headless. Reuses render projection + the camera_pick
// ray helpers where they apply (camera_pick.cpp is NOT edited).

#include "guild/common/types.h"

namespace guild::play {

// ===========================================================================
// City-view camera — the active render camera `dword_13FCD1C` as BeginUniverseFrame
// reads it, plus the screen-projection scale terms VIBE_Mesh_ProjectVerticesToScreen
// consumes. Owned (not a global) so the city view is drivable headless and
// deterministically. Field comments cite the original engine source.
// ===========================================================================
struct CityViewCamera {
    // --- the camera record fields BeginUniverseFrame reads (dword_13FCD1C) -----
    float eye[3]   = {0.0f, 0.0f, 0.0f};                       // +76..84  world origin
    float view[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};     // +396..   view matrix

    // --- VIBE_Mesh_ProjectVerticesToScreen scale terms (ProjectParams) ---------
    // screenX = (x - eye[0]) * invDepthX + biasX
    // screenY = biasX + (z - eye[2]) * scaleZ
    // The original derived these per-frame from the view block; for the city view
    // they are the inverse pixels-per-world-unit on each ground axis.
    float invDepthX = 1.0f;   // 1/ *(v35+20) : screen-x scale (object X -> pixels)
    float scaleZ    = 1.0f;   // v32          : screen-y scale (object Z -> pixels)
    float scaleY    = 1.0f;   // v31          : depth/light scale (object Y)
    float biasX     = 0.875f; // flt_628B94   : the +0.875 screen bias
    float lightCap  = 254.0f; // flt_628B98   : light-index clamp ceiling
    float screenW   = 0.0f;   // *(v35+32)    : on-screen width clamp (== screen size)

    int   screenWidth  = 0;   // pixel width  (for the cursor/edge tests)
    int   screenHeight = 0;   // pixel height
};

// Build a top-down city-view camera centered on `eye`, with `pixelsPerUnit` mapping
// one ground world-unit to that many screen pixels (the city view is an orthographic
// top-down projection in the original). `screenW`/`screenH` set the on-screen clamp.
// view = identity (the city camera's fixed top-down basis); deterministic.
CityViewCamera MakeCityViewCamera(const float eye[3], float pixelsPerUnit,
                                  int screenW, int screenH);

// ===========================================================================
// Camera / projection transforms (reconstructed from BeginUniverseFrame's readers).
// ===========================================================================

// World -> view transform exactly as BeginUniverseFrame's downstream consumers
// (VIBE_Heightmap_ProjectPointToView / ScreenToWorldRay) compute it:
//   out = M3x3 . (world - eye), rows [0,4,8],[1,5,9],[2,6,10] of cam.view.
void WorldToView(const CityViewCamera& cam, const float world[3], float outView[3]);

// Project a world position to a screen pixel using the REAL engine projection
// (render::ProjectVerticesToScreen's per-vertex math, reused verbatim):
//   sx = (world.x - eye[0]) * invDepthX + biasX
//   sy = biasX + (world.z - eye[2]) * scaleZ
// Writes (sx, sy) and returns true when the point falls inside [0, screenW) on
// both axes (the original's on-screen clip), false otherwise.
bool ProjectWorldToScreen(const CityViewCamera& cam, const float world[3],
                          float* outSX, float* outSY);

// ===========================================================================
// Screen -> world object picking on the live scene.
// ===========================================================================

// A live scene object: an id (resolvable via GameObjectResolveEntityById) and a
// world position (e.g. an entry of sim::g_objects' transform). The pick projects
// each position to screen and compares against the cursor.
struct ScenePickObject {
    i32   id        = 0;
    float pos[3]    = {0.0f, 0.0f, 0.0f};
};

// Build a ScenePickObject from a REAL engine render node: read the node's true
// world translation (object_transform.h: SceneNodeWorldPlacement, +76 world pos /
// +396 frame matrix) so the pick projects the object at its actual city position
// rather than a synthetic one. `id` is the entity id to report on a hit. Returns
// {id, pos = decoded world XYZ}; for a null/invisible node the position is the
// origin (it will simply never out-rank a real on-screen object).
ScenePickObject ScenePickObjectFromNode(i32 id, const void* sceneNode);

struct ScenePickResult {
    int   index = -1;     // index into the objects array, -1 == nothing under cursor
    i32   id    = 0;      // picked object id (0 if none)
    float screenDist = 0.0f; // pixel distance from the cursor to the projected center
};

// Cast a screen pick at cursor (sx, sy): project every object via the REAL
// projection, and return the object whose projected screen point is NEAREST the
// cursor AND within `pickRadius` pixels (and on-screen). A click over empty space
// (no object within the radius) returns {index=-1, id=0}. Mirrors the original's
// mesh-projection pick reduced to the object's projected center.
ScenePickResult PickSceneObject(const CityViewCamera& cam, float sx, float sy,
                                const ScenePickObject* objects, int count,
                                float pickRadius);

// Higher-level: pick at (sx, sy) and resolve the picked id into a concrete entity
// via the REAL sim::GameObjectResolveEntityById. `outKind` receives the resolve
// kind (1 object, 2 scene, 3 person, 0 none). Returns the ScenePickResult.
ScenePickResult PickAndResolveSceneEntity(const CityViewCamera& cam, float sx, float sy,
                                          const ScenePickObject* objects, int count,
                                          float pickRadius, int* outKind);

} // namespace guild::play
