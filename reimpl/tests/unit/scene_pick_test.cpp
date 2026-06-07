// Unit: city-view camera transform + screen projection golden vectors
// (independently computed in Python), plus the screen-distance pick reducer in
// isolation. Reuses the REAL render::ProjectVerticesToScreen via play::scene_pick.
#include "test.h"

#include <cmath>

#include "play/scene_pick.h"

using namespace guild;

namespace {
bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }
}

// --- WorldToView golden cases ----------------------------------------------
// out = M3x3 . (world - eye); identity view -> out == (world - eye).

TEST(ScenePickUnit, WorldToViewIdentityEyeOrigin) {
    float eye[3] = {0, 0, 0};
    play::CityViewCamera cam = play::MakeCityViewCamera(eye, 1.0f, 640, 480);
    float world[3] = {3, 4, 5};
    float v[3] = {0, 0, 0};
    play::WorldToView(cam, world, v);
    CHECK(Near(v[0], 3.0f));   // python: (3, 4, 5)
    CHECK(Near(v[1], 4.0f));
    CHECK(Near(v[2], 5.0f));
}

TEST(ScenePickUnit, WorldToViewIdentityEyeOffset) {
    float eye[3] = {10, 20, 30};
    play::CityViewCamera cam = play::MakeCityViewCamera(eye, 1.0f, 640, 480);
    float world[3] = {13, 24, 35};
    float v[3] = {0, 0, 0};
    play::WorldToView(cam, world, v);
    CHECK(Near(v[0], 3.0f));   // python: (3, 4, 5)
    CHECK(Near(v[1], 4.0f));
    CHECK(Near(v[2], 5.0f));
}

// --- ProjectWorldToScreen golden cases (REAL projection arithmetic) ---------
// sx = (x - eye.x) * pixelsPerUnit + 0.875 ; sy = 0.875 + (z - eye.z) * pixelsPerUnit

TEST(ScenePickUnit, ProjectOriginEyeOrigin) {
    float eye[3] = {0, 0, 0};
    play::CityViewCamera cam = play::MakeCityViewCamera(eye, /*ppu=*/2.0f, 640, 480);
    float world[3] = {0, 0, 0};
    float sx = -1, sy = -1;
    bool on = play::ProjectWorldToScreen(cam, world, &sx, &sy);
    CHECK(on);
    CHECK(Near(sx, 0.875f));   // python: (0.875, 0.875)
    CHECK(Near(sy, 0.875f));
}

TEST(ScenePickUnit, ProjectKnownPointEyeOrigin) {
    float eye[3] = {0, 0, 0};
    play::CityViewCamera cam = play::MakeCityViewCamera(eye, 2.0f, 640, 480);
    float world[3] = {50, 0, 30};
    float sx = -1, sy = -1;
    bool on = play::ProjectWorldToScreen(cam, world, &sx, &sy);
    CHECK(on);
    CHECK(Near(sx, 100.875f));  // python: (100.875, 60.875)
    CHECK(Near(sy, 60.875f));
}

TEST(ScenePickUnit, ProjectKnownPointEyeOffset) {
    float eye[3] = {100, 0, 100};
    play::CityViewCamera cam = play::MakeCityViewCamera(eye, 2.0f, 640, 480);
    // Object exactly at the eye projects to the bias point.
    float atEye[3] = {100, 0, 100};
    float sx = -1, sy = -1;
    CHECK(play::ProjectWorldToScreen(cam, atEye, &sx, &sy));
    CHECK(Near(sx, 0.875f));    // python: (0.875, 0.875)
    CHECK(Near(sy, 0.875f));

    float off[3] = {150, 0, 130};
    CHECK(play::ProjectWorldToScreen(cam, off, &sx, &sy));
    CHECK(Near(sx, 100.875f));  // python: (100.875, 60.875)
    CHECK(Near(sy, 60.875f));
}

TEST(ScenePickUnit, ProjectOffScreenReturnsFalse) {
    float eye[3] = {0, 0, 0};
    play::CityViewCamera cam = play::MakeCityViewCamera(eye, 2.0f, 640, 480);
    // x maps to sx = -10*2+0.875 = -19.125 -> off-screen (sx < 0).
    float world[3] = {-10, 0, 30};
    float sx = 0, sy = 0;
    CHECK(!play::ProjectWorldToScreen(cam, world, &sx, &sy));
    CHECK(Near(sx, -19.125f));
}

// --- PickSceneObject reducer ------------------------------------------------

TEST(ScenePickUnit, PicksNearestProjectedToCursor) {
    float eye[3] = {0, 0, 0};
    play::CityViewCamera cam = play::MakeCityViewCamera(eye, 2.0f, 640, 480);
    // Two objects: one projects to ~(100.875,60.875), the other to ~(0.875,0.875).
    play::ScenePickObject objs[2];
    objs[0] = {11, {50, 0, 30}};   // -> (100.875, 60.875)
    objs[1] = {22, {0,  0, 0}};    // -> (0.875, 0.875)
    // Click right on the first object's projection.
    play::ScenePickResult r = play::PickSceneObject(cam, 100.0f, 61.0f, objs, 2, 10.0f);
    CHECK_EQ(r.index, 0);
    CHECK_EQ(r.id, 11);
    CHECK(r.screenDist < 2.0f);
}

TEST(ScenePickUnit, MissReturnsMinusOne) {
    float eye[3] = {0, 0, 0};
    play::CityViewCamera cam = play::MakeCityViewCamera(eye, 2.0f, 640, 480);
    play::ScenePickObject objs[1];
    objs[0] = {99, {50, 0, 30}};   // projects to (100.875, 60.875)
    // Click far away (>radius) -> miss.
    play::ScenePickResult r = play::PickSceneObject(cam, 400.0f, 400.0f, objs, 1, 8.0f);
    CHECK_EQ(r.index, -1);
    CHECK_EQ(r.id, 0);
}

TEST(ScenePickUnit, NullOrEmptyArrayPicksNothing) {
    float eye[3] = {0, 0, 0};
    play::CityViewCamera cam = play::MakeCityViewCamera(eye, 2.0f, 640, 480);
    play::ScenePickResult r0 = play::PickSceneObject(cam, 10, 10, nullptr, 0, 5.0f);
    CHECK_EQ(r0.index, -1);
    play::ScenePickObject one{1, {0, 0, 0}};
    play::ScenePickResult r1 = play::PickSceneObject(cam, 10, 10, &one, 0, 5.0f);
    CHECK_EQ(r1.index, -1);
}

TEST(ScenePickUnit, NearestWinsAmongOverlapping) {
    float eye[3] = {0, 0, 0};
    play::CityViewCamera cam = play::MakeCityViewCamera(eye, 2.0f, 640, 480);
    // Two close objects; the cursor sits closer to the second one.
    play::ScenePickObject objs[2];
    objs[0] = {1, {50, 0, 30}};    // -> (100.875, 60.875)
    objs[1] = {2, {52, 0, 30}};    // -> (104.875, 60.875)
    play::ScenePickResult r = play::PickSceneObject(cam, 104.5f, 61.0f, objs, 2, 20.0f);
    CHECK_EQ(r.index, 1);
    CHECK_EQ(r.id, 2);
}
