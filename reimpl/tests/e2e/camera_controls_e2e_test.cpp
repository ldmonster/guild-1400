// E2E: a scripted city-view input sequence (pan around + zoom) yields a deterministic
// camera path. Drives CameraControl through a fixed script of held-direction frames
// with a mid-script zoom change, asserts the resulting eye path against goldens, and
// proves the path is bit-identical across two independent runs (determinism).
#include "test.h"

#include <cmath>
#include <vector>

#include "play/camera_controls.h"

using namespace guild;

namespace {
bool Near(float a, float b, float eps = 1e-3f) { return std::fabs(a - b) <= eps; }

struct ScriptedFrame { int dirX; int dirZ; float zoomTo; bool setZoom; };

// Run the fixed script and return the per-frame eye path (x,z pairs).
std::vector<std::pair<float, float>> RunScript() {
    float eye[3] = {0, 0, 0};
    play::CameraControl cam = play::MakeCameraControl(eye, /*yaw=*/0, /*zoom=*/0, 320, 240);

    const ScriptedFrame script[] = {
        {1, 0, 0, false}, {1, 0, 0, false}, {1, 0, 0, false},   // 3 frames pan right
        {0, 1, 0, false}, {0, 1, 0, false},                      // 2 frames pan down
        {1, 0, 0.5f, true}, {1, 0, 0, false},                    // zoom to 0.5, 2 right
        {0, 0, 0, false}, {0, 0, 0, false},                      // 2 frames release (decay)
    };

    std::vector<std::pair<float, float>> path;
    for (const ScriptedFrame& f : script) {
        if (f.setZoom) cam.zoom = f.zoomTo;
        play::CameraUpdatePan(cam, {f.dirX, f.dirZ}, /*dt=*/0);
        path.emplace_back(cam.eye[0], cam.eye[2]);
    }
    return path;
}
} // namespace

TEST(CameraControlsE2E, ScriptedPanZoomPathMatchesGolden) {
    std::vector<std::pair<float, float>> path = RunScript();
    CHECK_EQ(static_cast<int>(path.size()), 9);

    // Golden eye (x,z) at the key checkpoints (independently computed from the model).
    CHECK(Near(path[2].first,  39.6f));   CHECK(Near(path[2].second,  0.0f));   // after 3 right
    CHECK(Near(path[4].first,  62.7f));   CHECK(Near(path[4].second, 23.1f));   // after 2 down
    CHECK(Near(path[6].first, 129.525f)); CHECK(Near(path[6].second, 60.225f)); // after zoom + 2 right
    CHECK(Near(path[8].first, 181.5f));   CHECK(Near(path[8].second, 67.65f));  // after 2 release
}

TEST(CameraControlsE2E, ScriptedPathIsDeterministicAcrossRuns) {
    std::vector<std::pair<float, float>> a = RunScript();
    std::vector<std::pair<float, float>> b = RunScript();
    CHECK_EQ(a.size(), b.size());
    bool identical = (a.size() == b.size());
    for (size_t i = 0; i < a.size() && i < b.size(); ++i) {
        if (a[i].first != b[i].first || a[i].second != b[i].second) identical = false;
    }
    CHECK(identical);   // exact bit-equality, not just near
}

TEST(CameraControlsE2E, PanThenReverseReturnsTowardOrigin) {
    // Pan right N frames, then pan left: the eye must turn back (x stops growing and
    // the velocity sign flips), exercising the full ramp/decay/reverse path.
    float eye[3] = {0, 0, 0};
    play::CameraControl cam = play::MakeCameraControl(eye, 0, 0, 320, 240);
    for (int f = 0; f < 4; ++f) play::CameraUpdatePan(cam, {1, 0}, 0);
    const float peakX = cam.eye[0];
    CHECK(peakX > 0.0f);
    CHECK(cam.velX > 0.0f);
    // Now hold left long enough for velocity to cross zero and go negative.
    for (int f = 0; f < 20; ++f) play::CameraUpdatePan(cam, {-1, 0}, 0);
    CHECK(cam.velX < 0.0f);          // velocity reversed
    CHECK(cam.eye[0] < peakX);       // eye moved back left of the peak
}

TEST(CameraControlsE2E, ZoomChangesPathSpeedDeterministically) {
    // The same held-input sequence at zoom=1 must move strictly farther than at zoom=0
    // (pan speed = zoom*1.5+0.6), and each run is reproducible.
    auto runAt = [](float zoom) {
        float eye[3] = {0, 0, 0};
        play::CameraControl cam = play::MakeCameraControl(eye, 0, zoom, 320, 240);
        for (int f = 0; f < 3; ++f) play::CameraUpdatePan(cam, {1, 0}, 0);
        return cam.eye[0];
    };
    const float outFar  = runAt(1.0f);
    const float outNear = runAt(0.0f);
    CHECK(outFar > outNear);
    CHECK(Near(runAt(1.0f), outFar));   // reproducible
    CHECK(Near(runAt(0.0f), outNear));
}
