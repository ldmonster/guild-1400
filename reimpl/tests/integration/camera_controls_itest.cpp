// Integration: city-view camera CONTROLS against the engine projection/transform.
//   1. Edge-scroll: a cursor near each screen edge/corner drives the right pan
//      direction and amount (via the REAL app::ResolveCombatScroll decision core).
//   2. The view transform CameraControl produces (eye + the +396 4x4) is consumed by
//      the REAL play::WorldToView (scene_pick.h, the BeginUniverseFrame convention)
//      and matches the expected world->view for a known eye and yaw.
#include "test.h"

#include <cmath>

#include "play/camera_controls.h"
#include "play/scene_pick.h"   // CityViewCamera + WorldToView (real BeginUniverseFrame conv)

using namespace guild;

namespace {
bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

// Bridge a CameraControl's (eye, view) into the CityViewCamera WorldToView reads.
play::CityViewCamera AsCityCamera(const play::CameraControl& c) {
    play::CityViewCamera cam;
    cam.eye[0] = c.eye[0]; cam.eye[1] = c.eye[1]; cam.eye[2] = c.eye[2];
    for (int i = 0; i < 16; ++i) cam.view[i] = c.view[i];
    cam.screenWidth = c.screenWidth;
    cam.screenHeight = c.screenHeight;
    return cam;
}
} // namespace

// --- Edge-scroll per edge: direction resolution ----------------------------

TEST(CameraControlsItest, EdgeScrollLeftEdgePansLeft) {
    play::PanInput in = play::ResolveEdgeScroll(/*mx=*/2, /*my=*/100, 200, 200, /*margin=*/10);
    CHECK_EQ(in.dirX, -1);   // cursor at left edge -> pan left
    CHECK_EQ(in.dirZ, 0);
}

TEST(CameraControlsItest, EdgeScrollRightEdgePansRight) {
    play::PanInput in = play::ResolveEdgeScroll(198, 100, 200, 200, 10);
    CHECK_EQ(in.dirX, 1);
    CHECK_EQ(in.dirZ, 0);
}

TEST(CameraControlsItest, EdgeScrollTopEdgePansUp) {
    play::PanInput in = play::ResolveEdgeScroll(100, 2, 200, 200, 10);
    CHECK_EQ(in.dirX, 0);
    CHECK_EQ(in.dirZ, -1);   // top edge -> pan north (-Z)
}

TEST(CameraControlsItest, EdgeScrollBottomEdgePansDown) {
    play::PanInput in = play::ResolveEdgeScroll(100, 198, 200, 200, 10);
    CHECK_EQ(in.dirX, 0);
    CHECK_EQ(in.dirZ, 1);
}

TEST(CameraControlsItest, EdgeScrollBottomRightCorner) {
    play::PanInput in = play::ResolveEdgeScroll(199, 199, 200, 200, 10);
    CHECK_EQ(in.dirX, 1);
    CHECK_EQ(in.dirZ, 1);
}

TEST(CameraControlsItest, EdgeScrollCenterNoMove) {
    play::PanInput in = play::ResolveEdgeScroll(100, 100, 200, 200, 10);
    CHECK_EQ(in.dirX, 0);
    CHECK_EQ(in.dirZ, 0);
}

// --- Edge-scroll moves the camera the right direction + amount --------------

TEST(CameraControlsItest, EdgeScrollLeftMovesEyeNegativeX) {
    float eye[3] = {100, 0, 100};
    play::CameraControl cam = play::MakeCameraControl(eye, 0, 0, 200, 200);
    // Left edge, one frame from rest, dt=0: worldDX = -(kick10+step5)*0.6*1.1 = -9.9.
    play::PanStep s = play::CameraEdgeScrollStep(cam, /*mx=*/2, /*my=*/100, /*margin=*/10, 0);
    CHECK(s.moved);
    CHECK(Near(s.worldDX, -9.9f));
    CHECK(Near(cam.eye[0], 90.1f));
    CHECK(Near(cam.eye[2], 100.0f));
}

TEST(CameraControlsItest, EdgeScrollBottomMovesEyePositiveZ) {
    float eye[3] = {100, 0, 100};
    play::CameraControl cam = play::MakeCameraControl(eye, 0, 0, 200, 200);
    play::PanStep s = play::CameraEdgeScrollStep(cam, 100, 198, 10, 0);
    CHECK(s.moved);
    CHECK(Near(s.worldDZ, 9.9f));
    CHECK(Near(cam.eye[2], 109.9f));
}

TEST(CameraControlsItest, EdgeScrollCenterDoesNotMove) {
    float eye[3] = {100, 0, 100};
    play::CameraControl cam = play::MakeCameraControl(eye, 0, 0, 200, 200);
    play::PanStep s = play::CameraEdgeScrollStep(cam, 100, 100, 10, 0);
    CHECK(!s.moved);
    CHECK(Near(cam.eye[0], 100.0f));
    CHECK(Near(cam.eye[2], 100.0f));
}

// --- Produced view transform matches WorldToView (yaw=0 identity basis) -----

TEST(CameraControlsItest, ViewMatchesWorldToViewIdentity) {
    float eye[3] = {50, 10, 70};
    play::CameraControl c = play::MakeCameraControl(eye, /*yaw=*/0, 0, 200, 200);
    play::CityViewCamera cam = AsCityCamera(c);
    // yaw=0 -> view 3x3 identity -> out = world - eye.
    float world[3] = {60, 14, 90};
    float out[3] = {0, 0, 0};
    play::WorldToView(cam, world, out);
    CHECK(Near(out[0], 10.0f));   // 60-50
    CHECK(Near(out[1], 4.0f));    // 14-10
    CHECK(Near(out[2], 20.0f));   // 90-70
    // A point at the eye projects to the origin.
    float at[3] = {50, 10, 70}, o2[3] = {0, 0, 0};
    play::WorldToView(cam, at, o2);
    CHECK(Near(o2[0], 0.0f));
    CHECK(Near(o2[1], 0.0f));
    CHECK(Near(o2[2], 0.0f));
}

// --- Produced view transform matches WorldToView under yaw -----------------

TEST(CameraControlsItest, ViewMatchesWorldToViewYawed) {
    // yaw=90deg: the view 3x3 rotates the ground plane. WorldToView applies
    // out = M3x3 . (world-eye) with rows [0,4,8],[1,5,9],[2,6,10]. Confirm the camera's
    // rebuilt matrix transforms a known offset to the rotated view coordinate.
    const float yaw = 3.14159265358979f / 2.0f;
    float eye[3] = {0, 0, 0};
    play::CameraControl c = play::MakeCameraControl(eye, yaw, 0, 200, 200);
    play::CityViewCamera cam = AsCityCamera(c);
    // RebuildView sets view[0]=cos, view[2]=sin, view[8]=-sin, view[10]=cos.
    // WorldToView row0 = [view[0],view[4],view[8]] = [cos,0,-sin]; for (1,0,0):
    //   out[0] = 1*cos = ~0;  out[2] = row2 [view[2],view[6],view[10]]=[sin,0,cos] -> sin = ~1.
    float world[3] = {1, 0, 0};
    float out[3] = {0, 0, 0};
    play::WorldToView(cam, world, out);
    CHECK(Near(out[0], std::cos(yaw), 1e-3f));   // ~0
    CHECK(Near(out[2], std::sin(yaw), 1e-3f));   // ~1
}
