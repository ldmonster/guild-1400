// Unit: city-view camera CONTROLS — pan/zoom/clamp math golden vectors. Drives the
// reconstructed VIBE_Camera_UpdatePan velocity/accel/clamp model and the zoom-fraction
// clamp in isolation (no globals, dt passed in). Goldens computed from the engine
// constants (kick 10, accel step 5*ft, clamp ±50, zoom speed = zoom*1.5+0.6, *1.1).
#include "test.h"

#include <cmath>

#include "play/camera_controls.h"

using namespace guild;

namespace {
bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }
}

// --- Single-frame pan from rest (kick + first accel step) ------------------

TEST(CameraControlsUnit, FirstFramePanKicksAndMoves) {
    // dt=0 => frame factor 1.0, step = 5.0. From rest, +X kicks vel to 10 then +5 = 15.
    // zoom=0 => applyFt = (0*1.5+0.6)*1 = 0.6; worldDX = 15 * 0.6 * 1.1 = 9.9.
    float eye[3] = {0, 0, 0};
    play::CameraControl cam = play::MakeCameraControl(eye, /*yaw=*/0, /*zoom=*/0, 200, 200);
    play::PanStep s = play::CameraUpdatePan(cam, {/*dirX=*/1, /*dirZ=*/0}, /*dt=*/0);
    CHECK(s.moved);
    CHECK(Near(s.velX, 15.0f));
    CHECK(Near(s.worldDX, 9.9f));
    CHECK(Near(cam.eye[0], 9.9f));
    CHECK(Near(cam.eye[2], 0.0f));
}

// --- Held N frames -> deterministic eye delta path -------------------------

TEST(CameraControlsUnit, HeldKeyRampsVelocityAndEye) {
    // 5 frames holding +X, zoom=0, dt=0: vel ramps 15,20,25,30,35; eye.x ends 82.5.
    float eye[3] = {0, 0, 0};
    play::CameraControl cam = play::MakeCameraControl(eye, 0, 0, 200, 200);
    const float velGold[5]  = {15, 20, 25, 30, 35};
    const float eyeGold[5]  = {9.9f, 23.1f, 39.6f, 59.4f, 82.5f};
    for (int f = 0; f < 5; ++f) {
        play::PanStep s = play::CameraUpdatePan(cam, {1, 0}, 0);
        CHECK(Near(s.velX, velGold[f]));
        CHECK(Near(cam.eye[0], eyeGold[f]));
    }
}

// --- Velocity clamp at ±50 -------------------------------------------------

TEST(CameraControlsUnit, VelocityClampsAtFifty) {
    float eye[3] = {0, 0, 0};
    play::CameraControl cam = play::MakeCameraControl(eye, 0, 0, 200, 200);
    play::PanStep s{};
    for (int f = 0; f < 40; ++f) s = play::CameraUpdatePan(cam, {1, 0}, 0);
    CHECK(Near(s.velX, 50.0f));   // dbl_61DDD0 clamp
    // Opposite direction clamps at -50.
    play::CameraControl cam2 = play::MakeCameraControl(eye, 0, 0, 200, 200);
    for (int f = 0; f < 40; ++f) s = play::CameraUpdatePan(cam2, {-1, 0}, 0);
    CHECK(Near(s.velX, -50.0f));  // dbl_61DDD8 clamp
}

// --- Idle decay toward zero ------------------------------------------------

TEST(CameraControlsUnit, ReleaseDecaysVelocityTowardZero) {
    float eye[3] = {0, 0, 0};
    play::CameraControl cam = play::MakeCameraControl(eye, 0, 0, 200, 200);
    for (int f = 0; f < 3; ++f) play::CameraUpdatePan(cam, {1, 0}, 0); // vel -> 25
    CHECK(Near(cam.velX, 25.0f));
    play::PanStep r1 = play::CameraUpdatePan(cam, {0, 0}, 0); // decay by 5 -> 20
    CHECK(Near(r1.velX, 20.0f));
    play::PanStep r2 = play::CameraUpdatePan(cam, {0, 0}, 0); // -> 15
    CHECK(Near(r2.velX, 15.0f));
    // Many idle frames eventually settle to exactly 0.
    for (int f = 0; f < 10; ++f) play::CameraUpdatePan(cam, {0, 0}, 0);
    CHECK(Near(cam.velX, 0.0f));
}

// --- Zoom fraction scales pan speed ----------------------------------------

TEST(CameraControlsUnit, ZoomScalesPanSpeed) {
    // zoom=1: applyFt = (1*1.5+0.6)*1 = 2.1; worldDX = 15*2.1*1.1 = 34.65.
    float eye[3] = {0, 0, 0};
    play::CameraControl cam = play::MakeCameraControl(eye, 0, /*zoom=*/1.0f, 200, 200);
    play::PanStep s = play::CameraUpdatePan(cam, {1, 0}, 0);
    CHECK(Near(s.worldDX, 34.65f));
    CHECK(Near(cam.eye[0], 34.65f));
}

// --- Zoom fraction clamps to [0,1] -----------------------------------------

TEST(CameraControlsUnit, ZoomFractionClamps) {
    float eye[3] = {0, 0, 0};
    play::CameraControl cam = play::MakeCameraControl(eye, 0, 0.5f, 200, 200);
    CHECK(Near(play::CameraZoomBy(cam, 0.3f), 0.8f));
    CHECK(Near(play::CameraZoomBy(cam, 0.5f), 1.0f));  // clamp high
    CHECK(Near(play::CameraZoomBy(cam, -5.0f), 0.0f)); // clamp low
}

// --- Yaw rotates the pan direction -----------------------------------------

TEST(CameraControlsUnit, YawRotatesPanDirection) {
    // At yaw=90deg, a +X pan input moves along world +Z (the ground basis rotated).
    float eye[3] = {0, 0, 0};
    play::CameraControl cam = play::MakeCameraControl(eye, /*yaw=*/3.14159265f / 2.0f,
                                                      0, 200, 200);
    play::PanStep s = play::CameraUpdatePan(cam, {1, 0}, 0);
    CHECK(Near(s.worldDX, 0.0f, 1e-2f));
    CHECK(Near(s.worldDZ, 9.9f, 1e-2f));
}

// --- Frame-time factor: dt scales accel, capped at 4.0 ---------------------

TEST(CameraControlsUnit, FrameTimeFactorCaps) {
    // dt=100ms -> ft = 100*0.4 = 40 -> capped to 4.0. step = 4*5 = 20. vel kick 10+20=30.
    float eye[3] = {0, 0, 0};
    play::CameraControl cam = play::MakeCameraControl(eye, 0, 0, 200, 200);
    play::PanStep s = play::CameraUpdatePan(cam, {1, 0}, /*dt=*/100.0f);
    CHECK(Near(s.velX, 30.0f));
}

// --- Sub-threshold move is suppressed --------------------------------------

TEST(CameraControlsUnit, NoInputNoMove) {
    float eye[3] = {0, 0, 0};
    play::CameraControl cam = play::MakeCameraControl(eye, 0, 0, 200, 200);
    play::PanStep s = play::CameraUpdatePan(cam, {0, 0}, 0);
    CHECK(!s.moved);
    CHECK(Near(cam.eye[0], 0.0f));
    CHECK(Near(cam.eye[2], 0.0f));
}

// --- Dolly moves the eye along the view forward ----------------------------

TEST(CameraControlsUnit, DollyMovesAlongForward) {
    // yaw=0 forward == view rows 8/9/10 == {0,0,1}; dolly +4 -> eye.z += 4.
    float eye[3] = {0, 0, 0};
    play::CameraControl cam = play::MakeCameraControl(eye, 0, 0, 200, 200);
    play::CameraDolly(cam, 4.0f);
    CHECK(Near(cam.eye[2], 4.0f));
    CHECK(Near(cam.view[14], 4.0f));
}
