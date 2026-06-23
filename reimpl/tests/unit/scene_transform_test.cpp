// Golden vectors for render::scene_transform — VIBE_Math_MatrixFromEuler @0x5cb1bc
// and the camera world->view it feeds (PointToBoneLocalSpace @0x5c8c40).
#include "render/scene_transform.h"
#include "tests/framework/test.h"

#include <cmath>

using namespace guild::render;

namespace {
bool Near(float a, float b, float e = 1e-4f) { return std::fabs(a - b) <= e; }
const float kPi = 3.14159265358979f;
}

// Identity euler -> identity rotation; camera forward = +Z.
TEST(SceneTransform, IdentityForwardPlusZ) {
    float e[3] = {0, 0, 0};
    Mat3 R = MatrixFromEuler(e);
    CHECK(Near(R.m[0], 1) && Near(R.m[4], 1) && Near(R.m[8], 1));
    CHECK(Near(R.m[1], 0) && Near(R.m[2], 0) && Near(R.m[3], 0));
    float f[3]; CameraForward(R, f);
    CHECK(Near(f[0], 0) && Near(f[1], 0) && Near(f[2], 1));
}

// 180-degree Y rotation flips the forward to -Z (the +144 "y ~ pi" the camera uses).
TEST(SceneTransform, YawPiFlipsToMinusZ) {
    float e[3] = {0, kPi, 0};
    Mat3 R = MatrixFromEuler(e);
    float f[3]; CameraForward(R, f);
    CHECK(Near(f[0], 0, 1e-3f) && Near(f[1], 0, 1e-3f) && Near(f[2], -1, 1e-3f));
}

// +90-degree pitch (ex) looks straight down (-Y).
TEST(SceneTransform, PitchHalfPiLooksDown) {
    float e[3] = {kPi / 2, 0, 0};
    Mat3 R = MatrixFromEuler(e);
    float f[3]; CameraForward(R, f);
    CHECK(Near(f[0], 0, 1e-3f) && Near(f[1], -1, 1e-3f) && Near(f[2], 0, 1e-3f));
}

// The real ChooseCity camera euler = -(dummy_A2 +144) = (0.9, -3.1, 0) rad looks
// down at the map table: forward ~ (0, -0.77, -0.64).
TEST(SceneTransform, ChooseCityA2LooksAtMap) {
    float e[3] = {0.9f, -3.1f, 0.0f};
    Mat3 R = MatrixFromEuler(e);
    float f[3]; CameraForward(R, f);
    CHECK(f[1] < -0.5f);     // strongly downward
    CHECK(f[2] < -0.4f);     // and toward -Z (the map is in front + below)
    CHECK(Near(f[0], 0.0f, 0.05f));
}

// World->view: a point in front of a +Z-looking camera has view.z > 0; behind < 0.
TEST(SceneTransform, WorldToViewDepthSign) {
    float e[3] = {0, 0, 0};
    Mat3 R = MatrixFromEuler(e);
    const float eye[3] = {0, 0, 0};
    float v[3];
    const float front[3] = {0, 0, 10};
    WorldToView(R, eye, front, v);
    CHECK(v[2] > 0.0f);
    const float behind[3] = {0, 0, -10};
    WorldToView(R, eye, behind, v);
    CHECK(v[2] < 0.0f);
    // lateral offset maps to view x.
    const float right[3] = {5, 0, 10};
    WorldToView(R, eye, right, v);
    CHECK(v[0] > 0.0f);
}
