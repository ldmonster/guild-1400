// Unit: camera ray math + hit-test golden cases (computed independently here), plus
// deterministic camera movement. Reuses the REAL ScreenToWorldRay / ProjectRayDirection
// and exercises RaySphereHit / PickObject in isolation.
#include "test.h"

#include <cmath>

#include "play/camera_pick.h"
#include "sim/character_render3.h"

using namespace guild;

namespace {
bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }
}

// --- RaySphereHit golden cases (hand-computed) -----------------------------

TEST(CameraPickUnit, RaySphereHitsAheadAtKnownDistance) {
    // Ray from origin along +Z, unit sphere centered at (0,0,5). Nearest hit at z=4.
    float o[3] = {0, 0, 0}, d[3] = {0, 0, 1}, c[3] = {0, 0, 5};
    float t = 0;
    bool hit = play::RaySphereHit(o, d, c, 1.0f, &t);
    CHECK(hit);
    CHECK(Near(t, 4.0f));
}

TEST(CameraPickUnit, RaySphereMissesOffAxis) {
    // Sphere radius 1 at (0,0,5), ray offset by x=3 -> misses.
    float o[3] = {3, 0, 0}, d[3] = {0, 0, 1}, c[3] = {0, 0, 5};
    float t = -1;
    CHECK(!play::RaySphereHit(o, d, c, 1.0f, &t));
}

TEST(CameraPickUnit, RaySphereTangentGrazes) {
    // Tangent: ray at x=1 along +Z, sphere r=1 at (0,0,5). Touches at z=5, t=5.
    float o[3] = {1, 0, 0}, d[3] = {0, 0, 1}, c[3] = {0, 0, 5};
    float t = 0;
    CHECK(play::RaySphereHit(o, d, c, 1.0f, &t));
    CHECK(Near(t, 5.0f));
}

TEST(CameraPickUnit, RaySphereBehindRayMisses) {
    // Sphere behind the origin (z=-5), ray goes +Z -> both roots negative -> miss.
    float o[3] = {0, 0, 0}, d[3] = {0, 0, 1}, c[3] = {0, 0, -5};
    float t = -1;
    CHECK(!play::RaySphereHit(o, d, c, 1.0f, &t));
}

TEST(CameraPickUnit, RaySphereOriginInsideHitsForwardRoot) {
    // Origin inside the sphere (center at origin, r=2): t1 = +2 along +Z.
    float o[3] = {0, 0, 0}, d[3] = {0, 0, 1}, c[3] = {0, 0, 0};
    float t = 0;
    CHECK(play::RaySphereHit(o, d, c, 2.0f, &t));
    CHECK(Near(t, 2.0f));
}

TEST(CameraPickUnit, RaySphereDegenerateDirMisses) {
    float o[3] = {0, 0, 0}, d[3] = {0, 0, 0}, c[3] = {0, 0, 5};
    float t = -1;
    CHECK(!play::RaySphereHit(o, d, c, 1.0f, &t));
}

// --- ScreenToWorldRay reuse: center pixel with identity view = forward {0,0,1} ---

TEST(CameraPickUnit, CenterPixelRayIsForward) {
    // Identity view, click the screen center -> screen vector {0,0,focal}, normalized
    // {0,0,1}, rotated by identity -> {0,0,1}. This is the real reconstruction.
    float eye[3] = {0, 0, 0};
    play::CameraState cam = play::MakeCamera(eye, /*focal=*/1.0f, 100, 100);
    // MakeCamera puts eye in view[12..14]; the 3x3 stays identity, so direction is
    // unaffected. Click at the center (50,50 -> centerX/Y == 50).
    float dir[3] = {0, 0, 0}, scaled[3] = {0, 0, 0};
    sim::ScreenToWorldRay(cam.view, cam.centerX, cam.centerY, cam.centerX, cam.centerY,
                          cam.focal, cam.zNear, cam.zFar, dir, scaled);
    CHECK(Near(dir[0], 0.0f));
    CHECK(Near(dir[1], 0.0f));
    CHECK(Near(dir[2], 1.0f));
}

// --- PickObject: nearest-of-many + empty space ----------------------------

TEST(CameraPickUnit, PicksNearestAlongForwardRay) {
    float eye[3] = {0, 0, 0};
    play::CameraState cam = play::MakeCamera(eye, 1.0f, 100, 100);
    // Two spheres on the forward axis; nearest (z=5) should win over the far one (z=20).
    play::SceneObject objs[2];
    objs[0] = {101, {0, 0, 20}, 1.0f};
    objs[1] = {202, {0, 0, 5},  1.0f};
    play::PickResult r = play::PickObject(cam, cam.centerX, cam.centerY, objs, 2);
    CHECK_EQ(r.index, 1);
    CHECK_EQ(r.id, 202);
    CHECK(r.dist > 0.0f);
}

TEST(CameraPickUnit, EmptySpacePicksNothing) {
    float eye[3] = {0, 0, 0};
    play::CameraState cam = play::MakeCamera(eye, 1.0f, 100, 100);
    play::SceneObject objs[1];
    objs[0] = {303, {50, 0, 5}, 1.0f}; // way off the forward ray
    play::PickResult r = play::PickObject(cam, cam.centerX, cam.centerY, objs, 1);
    CHECK_EQ(r.index, -1);
    CHECK_EQ(r.id, 0);
}

TEST(CameraPickUnit, NullOrEmptyArrayPicksNothing) {
    float eye[3] = {0, 0, 0};
    play::CameraState cam = play::MakeCamera(eye, 1.0f, 100, 100);
    play::PickResult r0 = play::PickObject(cam, 50, 50, nullptr, 0);
    CHECK_EQ(r0.index, -1);
    play::SceneObject one{1, {0, 0, 5}, 1.0f};
    play::PickResult r1 = play::PickObject(cam, 50, 50, &one, 0);
    CHECK_EQ(r1.index, -1);
}

// --- Camera movement determinism ------------------------------------------

TEST(CameraPickUnit, PanShiftsEyeAndViewDeterministically) {
    float eye[3] = {10, 20, 30};
    play::CameraState a = play::MakeCamera(eye, 1.0f, 100, 100);
    play::CameraState b = play::MakeCamera(eye, 1.0f, 100, 100);
    play::CameraPan(a, 5, 0, -3);
    play::CameraPan(b, 5, 0, -3);
    CHECK(Near(a.eye[0], 15.0f));
    CHECK(Near(a.eye[2], 27.0f));
    CHECK(Near(a.view[12], 15.0f));
    CHECK(Near(a.view[14], 27.0f));
    // Two runs identical.
    CHECK(Near(a.eye[0], b.eye[0]));
    CHECK(Near(a.eye[2], b.eye[2]));
    CHECK(Near(a.view[12], b.view[12]));
}

TEST(CameraPickUnit, PanMovesThePickedRayHit) {
    // Panning the camera changes which world point the center ray reaches: an object
    // dead-center becomes off-center after a lateral pan -> no longer picked.
    float eye[3] = {0, 0, 0};
    play::CameraState cam = play::MakeCamera(eye, 1.0f, 100, 100);
    play::SceneObject o{42, {0, 0, 5}, 1.0f};
    CHECK_EQ(play::PickObject(cam, cam.centerX, cam.centerY, &o, 1).index, 0);
    play::CameraPan(cam, 10, 0, 0); // slide right; object now far to the left
    CHECK_EQ(play::PickObject(cam, cam.centerX, cam.centerY, &o, 1).index, -1);
}

TEST(CameraPickUnit, ZoomMovesEyeAlongForward) {
    float eye[3] = {0, 0, 0};
    play::CameraState cam = play::MakeCamera(eye, 1.0f, 100, 100);
    play::CameraZoom(cam, 4.0f); // identity view forward == +Z
    CHECK(Near(cam.eye[2], 4.0f));
    CHECK(Near(cam.view[14], 4.0f));
}

// --- Edge scroll resolution -----------------------------------------------

TEST(CameraPickUnit, EdgeScrollLeftEdgePansLeft) {
    float eye[3] = {0, 0, 0};
    play::CameraState cam = play::MakeCamera(eye, 1.0f, 200, 200);
    play::EdgeScrollResult r = play::CameraEdgeScroll(cam, /*mx=*/2, /*my=*/100,
                                                      200, 200, /*margin=*/10, 3.0f);
    CHECK(r.scrolled);
    CHECK_EQ(r.dirX, -1);
    CHECK(Near(r.panX, -3.0f));
    CHECK(Near(cam.eye[0], -3.0f));
}

TEST(CameraPickUnit, EdgeScrollBottomRightCorner) {
    float eye[3] = {0, 0, 0};
    play::CameraState cam = play::MakeCamera(eye, 1.0f, 200, 200);
    play::EdgeScrollResult r = play::CameraEdgeScroll(cam, 199, 199, 200, 200, 10, 2.0f);
    CHECK(r.scrolled);
    CHECK_EQ(r.dirX, 1);
    CHECK_EQ(r.dirY, 1);
    CHECK(Near(cam.eye[0], 2.0f));
    CHECK(Near(cam.eye[2], 2.0f));
}

TEST(CameraPickUnit, EdgeScrollCenterDoesNothing) {
    float eye[3] = {0, 0, 0};
    play::CameraState cam = play::MakeCamera(eye, 1.0f, 200, 200);
    play::EdgeScrollResult r = play::CameraEdgeScroll(cam, 100, 100, 200, 200, 10, 3.0f);
    CHECK(!r.scrolled);
    CHECK_EQ(r.dirX, 0);
    CHECK_EQ(r.dirY, 0);
    CHECK(Near(cam.eye[0], 0.0f));
}
