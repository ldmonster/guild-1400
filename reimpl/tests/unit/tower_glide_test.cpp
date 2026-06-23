// unit: the ChooseCity tower's slide (VIBE_Anim_CreateObjectAnim 2-keyframe object
// animation -> render::object_anim with zero endpoint tangents == smoothstep ease).
#include "test.h"
#include "play/scene_view.h"

#include <cmath>

using namespace guild;

TEST(TowerGlide, Endpoints) {
    const float from[3] = {10, 20, 30}, to[3] = {110, 20, 30};
    float p[3];
    play::SampleTowerGlide(from, to, 0.0f, p);
    CHECK(std::fabs(p[0] - 10) < 1e-3f);
    play::SampleTowerGlide(from, to, 1.0f, p);
    CHECK(std::fabs(p[0] - 110) < 1e-3f);
}

TEST(TowerGlide, SmoothstepMidAndEase) {
    const float from[3] = {0, 0, 0}, to[3] = {100, 0, 0};
    float p[3];
    play::SampleTowerGlide(from, to, 0.5f, p);
    CHECK(std::fabs(p[0] - 50.0f) < 0.5f);          // smoothstep(0.5) = 0.5
    // Ease-in / ease-out: at 1/4 it has covered < 1/4, at 3/4 it has covered > 3/4.
    float a[3], b[3];
    play::SampleTowerGlide(from, to, 0.25f, a);
    play::SampleTowerGlide(from, to, 0.75f, b);
    CHECK(a[0] < 25.0f);
    CHECK(b[0] > 75.0f);
    CHECK(std::fabs(a[0] - 15.625f) < 0.5f);        // 3t^2-2t^3 at 0.25
    CHECK(std::fabs(b[0] - 84.375f) < 0.5f);        // at 0.75
}

TEST(TowerGlide, Monotonic) {
    const float from[3] = {-50, 5, 200}, to[3] = {80, 5, 240};
    float prev[3]; play::SampleTowerGlide(from, to, 0.0f, prev);
    for (int i = 1; i <= 20; ++i) {
        float cur[3]; play::SampleTowerGlide(from, to, (float)i / 20.0f, cur);
        CHECK(cur[0] >= prev[0] - 1e-3f);            // x increases toward the target
        CHECK(cur[2] >= prev[2] - 1e-3f);            // z increases toward the target
        prev[0] = cur[0]; prev[1] = cur[1]; prev[2] = cur[2];
    }
}
