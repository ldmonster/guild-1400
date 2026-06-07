// E2E: prove the camera + picking layer RUNS end to end against a populated scene.
//   - Place known objects in the scene (bounding spheres) and seed matching entity
//     records in the real g_objects array.
//   - Issue a scripted click at a screen coord through the camera; assert the correct
//     object is picked AND resolves to a real entity via GameObjectResolveEntityById.
//   - A click on empty space picks nothing (and resolves to kind 0).
//   - A camera pan changes the view transform deterministically (two runs identical)
//     and demonstrably alters what the same click picks.
#include "test.h"

#include <cmath>
#include <cstring>

#include "play/camera_pick.h"
#include "sim/entity.h"

using namespace guild;

namespace {
bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

// Seed the real entity arrays with two objects whose ids match scene objects, so a
// picked id resolves to a live ObjectRec (resolve kind == 1).
void SeedScene(i32 idA, i32 idB) {
    sim::ResetEntityArrays();
    std::memset(sim::g_objects, 0, sizeof(sim::ObjectRec) * sim::kObjectCapacity);
    sim::g_objects[0].alive = 1; sim::g_objects[0].id = idA;
    sim::g_objects[1].alive = 1; sim::g_objects[1].id = idB;
    // The resolver bails unless the scene/array base is "loaded".
    sim::g_sceneArrayLoaded = true;
    sim::g_personArrayLoaded = true;
}
} // namespace

TEST(CameraPickE2E, ScriptedClickPicksAndResolvesCorrectObject) {
    const i32 kNearId = 7001, kFarId = 7002;
    SeedScene(kNearId, kFarId);

    float eye[3] = {0, 0, 0};
    play::CameraState cam = play::MakeCamera(eye, /*focal=*/1.0f, 640, 480);

    // Two objects on the forward axis; nearest at z=8, far at z=30. A center click
    // should pick the near one and resolve it to a live entity.
    play::SceneObject scene[2];
    scene[0] = {kFarId,  {0, 0, 30}, 2.0f};
    scene[1] = {kNearId, {0, 0, 8},  2.0f};

    int kind = -99;
    play::PickResult r = play::PickAndResolveEntity(cam, cam.centerX, cam.centerY,
                                                    scene, 2, &kind);
    CHECK_EQ(r.index, 1);
    CHECK_EQ(r.id, kNearId);
    CHECK(r.dist > 0.0f);
    CHECK_EQ(kind, 1); // resolved to a live ObjectRec via the REAL resolver
}

TEST(CameraPickE2E, ClickOnEmptySpacePicksNothing) {
    SeedScene(8001, 8002);
    float eye[3] = {0, 0, 0};
    play::CameraState cam = play::MakeCamera(eye, 1.0f, 640, 480);

    play::SceneObject scene[2];
    scene[0] = {8001, {0, 0, 8},  2.0f};
    scene[1] = {8002, {0, 0, 30}, 2.0f};

    // Click far from center -> the ray misses both spheres.
    int kind = -99;
    play::PickResult r = play::PickAndResolveEntity(cam, /*sx=*/600, /*sy=*/40,
                                                    scene, 2, &kind);
    CHECK_EQ(r.index, -1);
    CHECK_EQ(r.id, 0);
    CHECK_EQ(kind, 0);
}

TEST(CameraPickE2E, OffCenterClickPicksTheOffsetObject) {
    SeedScene(9001, 9002);
    float eye[3] = {0, 0, 0};
    // focal large enough that small pixel offsets map to small angles.
    play::CameraState cam = play::MakeCamera(eye, /*focal=*/300.0f, 640, 480);

    // One object on-axis, one shifted +x. A click to the right of center should pick
    // the +x object, not the on-axis one.
    play::SceneObject scene[2];
    scene[0] = {9001, {0,  0, 50}, 3.0f};   // on-axis
    scene[1] = {9002, {20, 0, 50}, 3.0f};   // to the right

    // Center click -> the on-axis object.
    play::PickResult center = play::PickObject(cam, cam.centerX, cam.centerY, scene, 2);
    CHECK_EQ(center.id, 9001);

    // A click well to the right -> ray angles toward +x -> the shifted object.
    play::PickResult right = play::PickObject(cam, cam.centerX + 120.0f, cam.centerY,
                                              scene, 2);
    CHECK_EQ(right.id, 9002);
}

TEST(CameraPickE2E, PanChangesViewTransformDeterministically) {
    float eye[3] = {5, 5, 5};
    play::CameraState run1 = play::MakeCamera(eye, 1.0f, 640, 480);
    play::CameraState run2 = play::MakeCamera(eye, 1.0f, 640, 480);

    const float before12 = run1.view[12];
    play::CameraPan(run1, 7, 0, -4);
    play::CameraEdgeScroll(run1, 2, 240, 640, 480, 16, 3.0f); // left edge -> pan -x
    play::CameraPan(run2, 7, 0, -4);
    play::CameraEdgeScroll(run2, 2, 240, 640, 480, 16, 3.0f);

    // The view transform actually changed (not inert).
    CHECK(!Near(run1.view[12], before12));
    // Two identical runs produce an identical transform (determinism).
    CHECK(Near(run1.view[12], run2.view[12]));
    CHECK(Near(run1.view[13], run2.view[13]));
    CHECK(Near(run1.view[14], run2.view[14]));
    CHECK(Near(run1.eye[0], run2.eye[0]));
    CHECK(Near(run1.eye[2], run2.eye[2]));
}

TEST(CameraPickE2E, PanChangesWhatTheClickPicks) {
    SeedScene(10001, 10002);
    float eye[3] = {0, 0, 0};
    play::CameraState cam = play::MakeCamera(eye, 1.0f, 640, 480);

    play::SceneObject scene[1];
    scene[0] = {10001, {0, 0, 8}, 2.0f};

    // Before panning, the center click hits the object.
    CHECK_EQ(play::PickObject(cam, cam.centerX, cam.centerY, scene, 1).id, 10001);

    // Pan the camera far to the side; the same center click now misses.
    play::CameraPan(cam, 100, 0, 0);
    play::PickResult after = play::PickObject(cam, cam.centerX, cam.centerY, scene, 1);
    CHECK_EQ(after.index, -1);
}
