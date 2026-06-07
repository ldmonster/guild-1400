// Wave PLAY P2 — CAMERA + PICKING implementation. See camera_pick.h.
//
// REUSE of real reconstructions (declared in their own headers; we call them, we do
// NOT redefine them):
//   sim::ScreenToWorldRay / sim::ProjectRayDirection  (character_render3.h)
//   app::ResolveCombatScroll                          (app/combat_scroll.h)
//   sim::GameObjectResolveEntityById                  (sim/entity.h)
//   util::MatrixIdentity                              (util/matrix.h)
#include "play/camera_pick.h"

#include <cmath>

#include "sim/character_render3.h"  // ScreenToWorldRay, ProjectRayDirection (real)
#include "app/combat_scroll.h"      // ResolveCombatScroll (real combat/camera scroll)
#include "sim/entity.h"             // GameObjectResolveEntityById (real)
#include "util/matrix.h"            // MatrixIdentity (real)

namespace guild::play {

// ---------------------------------------------------------------------------
// Camera setup / movement.
// ---------------------------------------------------------------------------

CameraState MakeCamera(const float eye[3], float focal, int screenW, int screenH) {
    CameraState cam;
    cam.eye[0] = eye[0];
    cam.eye[1] = eye[1];
    cam.eye[2] = eye[2];
    guild::util::MatrixIdentity(cam.view);
    // Seat the eye in the view translation column so the transform is consistent.
    cam.view[12] = eye[0];
    cam.view[13] = eye[1];
    cam.view[14] = eye[2];
    cam.focal   = focal;
    cam.centerX = static_cast<float>(screenW) * 0.5f;
    cam.centerY = static_cast<float>(screenH) * 0.5f;
    return cam;
}

void CameraPan(CameraState& cam, float dx, float dy, float dz) {
    cam.eye[0] += dx;
    cam.eye[1] += dy;
    cam.eye[2] += dz;
    cam.view[12] += dx;
    cam.view[13] += dy;
    cam.view[14] += dz;
}

void CameraZoom(CameraState& cam, float amount) {
    // Forward axis == the engine's fixed forward {0,0,1} rotated by the view 3x3
    // (same basis ProjectRayDirection uses: rows 0/4/8, 1/5/9, 2/6/10 with z=1).
    const float fx = cam.view[8];
    const float fy = cam.view[9];
    const float fz = cam.view[10];
    CameraPan(cam, fx * amount, fy * amount, fz * amount);
}

EdgeScrollResult CameraEdgeScroll(CameraState& cam, int mouseX, int mouseY,
                                  int screenW, int screenH, int margin, float speed) {
    EdgeScrollResult r;
    // Resolve the raw edge-direction inputs (the dword_6316CC / dword_6316D0 the
    // engine derives from cursor-vs-edge), then run them through the REAL combat
    // scroll decision core. -1 = left/top, +1 = right/bottom.
    int scrollX = 0, scrollY = 0;
    if (mouseX <= margin)                 scrollX = 1;   // left edge
    else if (mouseX >= screenW - margin)  scrollX = -1;  // right edge
    if (mouseY <= margin)                 scrollY = -1;  // top edge
    else if (mouseY >= screenH - margin)  scrollY = 1;   // bottom edge

    // Edge motion vectors: the live edge-push vectors in combat. Headless, no unit is
    // pushing, so all four are the zero/settled vector — ResolveCombatScroll then
    // yields the "scroll, settled" code on the active edge.
    const float zero[3] = {0.0f, 0.0f, 0.0f};
    guild::app::CombatScrollDecision dec =
        guild::app::ResolveCombatScroll(scrollX, scrollY, zero, zero, zero, zero);

    // A non-zero per-edge code (kSettled/kActive) means "this edge scrolls".
    const bool left   = dec.left   != guild::app::CombatScrollCode::kNone;
    const bool right  = dec.right  != guild::app::CombatScrollCode::kNone;
    const bool top    = dec.top    != guild::app::CombatScrollCode::kNone;
    const bool bottom = dec.bottom != guild::app::CombatScrollCode::kNone;

    // Map the active edges back to a world pan direction. With scrollX==0 the decision
    // core lights BOTH horizontal edges (settled) — only treat it as a scroll when an
    // actual edge direction was requested.
    if (scrollX == 1 && left)        r.dirX = -1;
    else if (scrollX == -1 && right) r.dirX = 1;
    if (scrollY == -1 && top)        r.dirY = -1;
    else if (scrollY == 1 && bottom) r.dirY = 1;

    r.panX = static_cast<float>(r.dirX) * speed;
    r.panZ = static_cast<float>(r.dirY) * speed;
    r.scrolled = (r.dirX != 0 || r.dirY != 0);
    if (r.scrolled)
        CameraPan(cam, r.panX, 0.0f, r.panZ);
    return r;
}

// ---------------------------------------------------------------------------
// Picking.
// ---------------------------------------------------------------------------

bool RaySphereHit(const float o[3], const float d[3],
                  const float c[3], float r, float* outT) {
    // Solve |o + t d - c|^2 = r^2 for the smallest t >= 0.
    const double ox = o[0] - c[0];
    const double oy = o[1] - c[1];
    const double oz = o[2] - c[2];
    const double a = static_cast<double>(d[0]) * d[0]
                   + static_cast<double>(d[1]) * d[1]
                   + static_cast<double>(d[2]) * d[2];
    if (a <= 0.0)
        return false; // degenerate direction
    const double b = 2.0 * (ox * d[0] + oy * d[1] + oz * d[2]);
    const double cc = ox * ox + oy * oy + oz * oz - static_cast<double>(r) * r;
    const double disc = b * b - 4.0 * a * cc;
    if (disc < 0.0)
        return false; // no intersection
    const double sq = std::sqrt(disc);
    const double t0 = (-b - sq) / (2.0 * a);
    const double t1 = (-b + sq) / (2.0 * a);
    double t;
    if (t0 >= 0.0)        t = t0;   // nearest entry in front of the ray
    else if (t1 >= 0.0)   t = t1;   // origin inside the sphere
    else                  return false; // both behind the ray origin
    if (outT)
        *outT = static_cast<float>(t);
    return true;
}

PickResult PickObject(const CameraState& cam, float sx, float sy,
                      const SceneObject* objects, int count) {
    PickResult best;
    if (!objects || count <= 0)
        return best;

    // Build the world-space pick ray using the REAL ScreenToWorldRay (direction) and
    // cam.eye as the origin (ProjectRayDirection's origin == the eye position).
    float dir[3] = {0.0f, 0.0f, 0.0f};
    float scaled[3] = {0.0f, 0.0f, 0.0f};
    guild::sim::ScreenToWorldRay(cam.view, sx, sy, cam.centerX, cam.centerY,
                                 cam.focal, cam.zNear, cam.zFar, dir, scaled);

    float bestT = 0.0f;
    for (int i = 0; i < count; ++i) {
        float t = 0.0f;
        if (RaySphereHit(cam.eye, dir, objects[i].center, objects[i].radius, &t)) {
            if (best.index < 0 || t < bestT) {
                bestT = t;
                best.index = i;
                best.id = objects[i].id;
                best.dist = t;
            }
        }
    }
    return best;
}

PickResult PickAndResolveEntity(const CameraState& cam, float sx, float sy,
                                const SceneObject* objects, int count, int* outKind) {
    PickResult r = PickObject(cam, sx, sy, objects, count);
    int kind = 0;
    if (r.index >= 0) {
        // Resolve the picked id into a concrete entity via the REAL resolver. We probe
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
