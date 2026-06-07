// Integration: project a known set of object world positions through the REAL
// city-view projection, click at a known screen coordinate, and assert the right
// object is picked. A click on empty space returns -1. Also exercises the
// id-resolve path against the REAL sim::GameObjectResolveEntityById over a seeded
// g_objects array.
#include "test.h"

#include <cmath>
#include <cstring>

#include "play/scene_pick.h"
#include "sim/entity.h"

using namespace guild;

namespace {
bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

// Seed the real object array so picked ids resolve to live ObjectRecs (kind 1).
void SeedObjects(const i32* ids, int n) {
    sim::ResetEntityArrays();
    std::memset(sim::g_objects, 0, sizeof(sim::ObjectRec) * sim::kObjectCapacity);
    for (int i = 0; i < n && i < sim::kObjectCapacity; ++i) {
        sim::g_objects[i].alive = 1;
        sim::g_objects[i].id = ids[i];
    }
    sim::g_sceneArrayLoaded = true;
    sim::g_personArrayLoaded = true;
}
} // namespace

TEST(ScenePickItest, ProjectGridAndClickPicksCorrectObject) {
    // A 3x3 grid of objects on the ground (X/Z) plane, spaced 40 world-units apart,
    // centered on the camera eye. pixelsPerUnit = 4 -> 160 px between neighbours.
    float eye[3] = {200, 0, 200};
    play::CityViewCamera cam = play::MakeCityViewCamera(eye, /*ppu=*/4.0f, 1000, 1000);

    play::ScenePickObject grid[9];
    i32 ids[9];
    int k = 0;
    for (int gz = -1; gz <= 1; ++gz) {
        for (int gx = -1; gx <= 1; ++gx) {
            ids[k] = 5000 + k;
            grid[k].id = ids[k];
            grid[k].pos[0] = 200.0f + gx * 40.0f;
            grid[k].pos[1] = 0.0f;
            grid[k].pos[2] = 200.0f + gz * 40.0f;
            ++k;
        }
    }
    SeedObjects(ids, 9);

    // Project the center object (index 4, world == eye) -> screen (0.875, 0.875).
    float cx = 0, cy = 0;
    CHECK(play::ProjectWorldToScreen(cam, grid[4].pos, &cx, &cy));
    CHECK(Near(cx, 0.875f));
    CHECK(Near(cy, 0.875f));

    // The +x/+z corner object (index 8): sx = 40*4+0.875 = 160.875, sy = same.
    float ex = 0, ey = 0;
    CHECK(play::ProjectWorldToScreen(cam, grid[8].pos, &ex, &ey));
    CHECK(Near(ex, 160.875f));
    CHECK(Near(ey, 160.875f));

    // Click right on the corner object's projection -> picks index 8.
    play::ScenePickResult r = play::PickSceneObject(cam, 161.0f, 161.0f, grid, 9, 30.0f);
    CHECK_EQ(r.index, 8);
    CHECK_EQ(r.id, 5008);
    CHECK(r.screenDist < 2.0f);

    // Click halfway between two neighbours (well within 80px gap, > radius from each)
    // -> miss.
    play::ScenePickResult miss = play::PickSceneObject(cam, 80.0f, 0.875f, grid, 9, 10.0f);
    CHECK_EQ(miss.index, -1);
    CHECK_EQ(miss.id, 0);
}

TEST(ScenePickItest, ResolvesPickedIdToLiveEntity) {
    i32 ids[3] = {6001, 6002, 6003};
    SeedObjects(ids, 3);

    float eye[3] = {0, 0, 0};
    play::CityViewCamera cam = play::MakeCityViewCamera(eye, 2.0f, 800, 600);
    play::ScenePickObject objs[3];
    objs[0] = {6001, {10, 0, 10}};   // -> (20.875, 20.875)
    objs[1] = {6002, {60, 0, 10}};   // -> (120.875, 20.875)
    objs[2] = {6003, {10, 0, 60}};   // -> (20.875, 120.875)

    int kind = -99;
    play::ScenePickResult r =
        play::PickAndResolveSceneEntity(cam, 120.0f, 21.0f, objs, 3, 8.0f, &kind);
    CHECK_EQ(r.index, 1);
    CHECK_EQ(r.id, 6002);
    CHECK_EQ(kind, 1);   // resolved to a live ObjectRec via the REAL resolver

    // A click on empty space resolves to kind 0 and index -1.
    int kind2 = -99;
    play::ScenePickResult m =
        play::PickAndResolveSceneEntity(cam, 700.0f, 500.0f, objs, 3, 6.0f, &kind2);
    CHECK_EQ(m.index, -1);
    CHECK_EQ(m.id, 0);
    CHECK_EQ(kind2, 0);
}

TEST(ScenePickItest, OffScreenObjectsAreNotPicked) {
    i32 ids[2] = {7001, 7002};
    SeedObjects(ids, 2);
    float eye[3] = {0, 0, 0};
    play::CityViewCamera cam = play::MakeCityViewCamera(eye, 2.0f, 400, 400);
    play::ScenePickObject objs[2];
    objs[0] = {7001, {-100, 0, 10}};  // sx = -200+0.875 -> off-screen
    objs[1] = {7002, {30,  0, 10}};   // -> (60.875, 20.875)
    // Even clicking near where the off-screen object's math would land, it cannot be
    // picked (on-screen clip rejects it).
    play::ScenePickResult r = play::PickSceneObject(cam, 60.0f, 21.0f, objs, 2, 8.0f);
    CHECK_EQ(r.index, 1);
    CHECK_EQ(r.id, 7002);
}
