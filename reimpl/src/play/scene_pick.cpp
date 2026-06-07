// Wave 25 PLAY P2 — CITY-VIEW CAMERA SETUP + SCENE PICKING implementation.
// See scene_pick.h for the grounding (BeginUniverseFrame @0x5b3900 decomp).
//
// REUSE of real reconstructions (we call them; we do NOT redefine them):
//   render::ProjectVerticesToScreen  (render/mesh.h, VIBE_Mesh_ProjectVerticesToScreen
//                                     @0x5c5120) — the exact per-vertex screen
//                                     projection the city render walk uses.
//   util::MatrixIdentity             (util/matrix.h) — the view-matrix init.
//   sim::GameObjectResolveEntityById (sim/entity.h)  — id -> live entity resolve.
#include "play/scene_pick.h"

#include <cmath>

#include "play/object_transform.h" // SceneNodeWorldPlacement (REAL node transform)
#include "render/mesh.h"     // ProjectVerticesToScreen + geometry records (real)
#include "util/matrix.h"     // MatrixIdentity (real)
#include "sim/entity.h"      // GameObjectResolveEntityById (real)

namespace guild::play {

// ---------------------------------------------------------------------------
// City-view camera setup.
// ---------------------------------------------------------------------------

CityViewCamera MakeCityViewCamera(const float eye[3], float pixelsPerUnit,
                                  int screenW, int screenH) {
    CityViewCamera cam;
    cam.eye[0] = eye[0];
    cam.eye[1] = eye[1];
    cam.eye[2] = eye[2];
    // The city camera's view basis is the fixed top-down identity (the engine seats
    // its rotation in the +396 matrix; for the orthographic top-down city view this
    // is identity — the world X/Z ground plane maps straight to screen X/Y).
    guild::util::MatrixIdentity(cam.view);
    cam.invDepthX = pixelsPerUnit;   // object X -> pixels
    cam.scaleZ    = pixelsPerUnit;   // object Z -> pixels
    cam.scaleY    = pixelsPerUnit;   // object Y -> depth/light term
    cam.biasX     = 0.875f;          // flt_628B94
    cam.lightCap  = 254.0f;          // flt_628B98
    cam.screenW   = static_cast<float>(screenW);
    cam.screenWidth  = screenW;
    cam.screenHeight = screenH;
    return cam;
}

void WorldToView(const CityViewCamera& cam, const float world[3], float outView[3]) {
    // out = M3x3 . (world - eye), rows [0,4,8],[1,5,9],[2,6,10] — the convention
    // BeginUniverseFrame's readers (ProjectPointToView / ScreenToWorldRay) use.
    const float rx = world[0] - cam.eye[0];
    const float ry = world[1] - cam.eye[1];
    const float rz = world[2] - cam.eye[2];
    outView[0] = rx * cam.view[0] + ry * cam.view[4] + rz * cam.view[8];
    outView[1] = rx * cam.view[1] + ry * cam.view[5] + rz * cam.view[9];
    outView[2] = rx * cam.view[2] + ry * cam.view[6] + rz * cam.view[10];
}

// ---------------------------------------------------------------------------
// Screen projection — reuse the REAL VIBE_Mesh_ProjectVerticesToScreen per-vertex
// transform by projecting a single-vertex mesh through it and reading back the
// engine-computed screen x/y. This guarantees the pick projects points with the
// exact arithmetic the city render walk uses (no hand-copied formula drift).
// ---------------------------------------------------------------------------

bool ProjectWorldToScreen(const CityViewCamera& cam, const float world[3],
                          float* outSX, float* outSY) {
    render::Vertex vert{};
    vert.x = world[0];
    vert.y = world[1];
    vert.z = world[2];

    render::MeshGeometry geom{};
    geom.vertices    = &vert;
    geom.polygons    = nullptr;
    geom.polyCount   = 0;
    geom.polyCap     = 0;
    geom.vertexCount = 1;

    render::ProjectParams p{};
    p.eye[0] = cam.eye[0];
    p.eye[1] = cam.eye[1];
    p.eye[2] = cam.eye[2];
    // ProjectVerticesToScreen reads invDepth[1] for the screen-x scale and scaleX
    // for the screen-y scale (see render/mesh.cpp).
    p.invDepth[0] = 0.0f;
    p.invDepth[1] = cam.invDepthX;
    p.invDepth[2] = 0.0f;
    p.biasX    = cam.biasX;
    p.scaleY   = cam.scaleY;
    p.scaleX   = cam.scaleZ;
    p.lightCap = cam.lightCap;
    p.screenW  = cam.screenW;

    // A zero-capacity draw list: we only want the per-vertex projection side effect.
    render::DrawList dl{};
    dl.entries  = nullptr;
    dl.count    = 0;
    dl.capacity = 0;

    // objFlags530 = 0x10 (force) so the top back-cull gate passes regardless of the
    // (here unused) view backface byte; viewCull42 = 0.
    render::ProjectVerticesToScreen(&geom, p, /*objFlags530=*/0x10,
                                    /*viewCull42=*/0, &dl);

    const float sx = vert.screenX;
    const float sy = vert.screenY;
    if (outSX) *outSX = sx;
    if (outSY) *outSY = sy;

    // The original's on-screen clip: 0 <= screen < screenW on both axes.
    const float w = cam.screenW;
    return sx >= 0.0f && sx < w && sy >= 0.0f && sy < w;
}

// ---------------------------------------------------------------------------
// Real node -> pick object: decode the node's true world transform.
// ---------------------------------------------------------------------------

ScenePickObject ScenePickObjectFromNode(i32 id, const void* sceneNode) {
    ScenePickObject o;
    o.id = id;
    // Read the REAL engine layout: +76 world position, +396 frame matrix, +533
    // visibility. SceneNodeWorldPlacement returns a zeroed/invisible placement for
    // a null node, leaving pos at the origin.
    WorldPlacement wp = SceneNodeWorldPlacement(sceneNode);
    o.pos[0] = wp.x;
    o.pos[1] = wp.y;
    o.pos[2] = wp.z;
    return o;
}

// ---------------------------------------------------------------------------
// Picking.
// ---------------------------------------------------------------------------

ScenePickResult PickSceneObject(const CityViewCamera& cam, float sx, float sy,
                                const ScenePickObject* objects, int count,
                                float pickRadius) {
    ScenePickResult best;
    if (!objects || count <= 0)
        return best;

    const double r2 = static_cast<double>(pickRadius) * pickRadius;
    double bestDist2 = 0.0;
    for (int i = 0; i < count; ++i) {
        float px = 0.0f, py = 0.0f;
        const bool onScreen = ProjectWorldToScreen(cam, objects[i].pos, &px, &py);
        if (!onScreen)
            continue;
        const double dx = static_cast<double>(px) - sx;
        const double dy = static_cast<double>(py) - sy;
        const double d2 = dx * dx + dy * dy;
        if (d2 > r2)
            continue;  // outside the pick radius
        if (best.index < 0 || d2 < bestDist2) {
            bestDist2  = d2;
            best.index = i;
            best.id    = objects[i].id;
            best.screenDist = static_cast<float>(std::sqrt(d2));
        }
    }
    return best;
}

ScenePickResult PickAndResolveSceneEntity(const CityViewCamera& cam, float sx, float sy,
                                          const ScenePickObject* objects, int count,
                                          float pickRadius, int* outKind) {
    ScenePickResult r = PickSceneObject(cam, sx, sy, objects, count, pickRadius);
    int kind = 0;
    if (r.index >= 0) {
        // Resolve the picked id into a concrete entity via the REAL resolver. Probe
        // all three out-pointers so the unified resolver searches in its real order.
        guild::sim::ObjectRec* obj = nullptr;
        guild::sim::SceneNode* scene = nullptr;
        guild::sim::Person* person = nullptr;
        kind = guild::sim::GameObjectResolveEntityById(&obj, &scene, r.id, &person);
    }
    if (outKind)
        *outKind = kind;
    return r;
}

} // namespace guild::play
