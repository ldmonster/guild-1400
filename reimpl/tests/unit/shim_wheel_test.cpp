#include "test.h"

// =============================================================================
// shim::MouseState wheel axis (wave-3, ADDITIVE) — the WM_MOUSEWHEEL replacement
// feeding the camera wheel-zoom branch (VIBE_Camera_UpdateMovement @0x4b41a8's
// dword_672254 accumulator via play::SessionCamera).
//
//   * MouseState::wheel defaults to 0 (every pre-existing backend/test reads 0).
//   * ScriptedPlatform::setWheel queues notches; the NEXT getMouse() reports
//     them once, then clears (the same read-and-clear contract the SDL backends
//     implement over SDL_MOUSEWHEEL).
//   * scriptAt(..., wheelOnce) queues notches on a timeline pump.
//   * SessionCamera consumes the notches: one notch -> zoom +0.1 (the
//     UpdateMovement wheel branch, pinned in session_camera_test).
// =============================================================================
#include "play/session_camera.h"
#include "shim_impl/null_platform.h"
#include "shim_impl/scripted_platform.h"

#include <cmath>

using namespace guild;

TEST(ShimWheel, MouseStateDefaultsToZeroWheel) {
    shim::MouseState ms;
    CHECK_EQ(ms.wheel, 0);

    // Backends without a wheel keep reporting 0 (NullPlatform).
    shim::NullPlatform np;
    shim::MouseState out;
    np.getMouse(out);
    CHECK_EQ(out.wheel, 0);
}

TEST(ShimWheel, ScriptedSetWheelIsReadAndClear) {
    shim::ScriptedPlatform plat;
    plat.setMouse(100, 100, false);
    plat.setWheel(3);

    shim::MouseState ms;
    plat.getMouse(ms);
    CHECK_EQ(ms.wheel, 3);          // queued notches reported once
    CHECK_EQ(ms.x, 100);

    plat.getMouse(ms);
    CHECK_EQ(ms.wheel, 0);          // ... then cleared (read-and-clear)

    plat.setWheel(1);
    plat.setWheel(-2);              // accumulates until consumed
    plat.getMouse(ms);
    CHECK_EQ(ms.wheel, -1);
}

TEST(ShimWheel, TimelineWheelOnceFiresOnItsPump) {
    shim::ScriptedPlatform plat;
    plat.scriptAt(0, 10, 10, false);                       // no wheel
    plat.scriptAt(1, 10, 10, false, {}, std::string(), 2); // +2 notches on pump 1

    shim::MouseState ms;
    CHECK(plat.pumpMessages());     // pump 0
    plat.getMouse(ms);
    CHECK_EQ(ms.wheel, 0);

    CHECK(plat.pumpMessages());     // pump 1 -> wheelOnce queued
    plat.getMouse(ms);
    CHECK_EQ(ms.wheel, 2);
    plat.getMouse(ms);
    CHECK_EQ(ms.wheel, 0);          // consumed
}

TEST(ShimWheel, WheelNotchesDriveTheRealCameraZoomBranch) {
    // End-to-end inside the shim contract: scripted wheel notches -> MouseState
    // -> SessionCamera::Frame(wheelDelta) -> the REAL UpdateMovement @0x4b41a8
    // wheel branch (one notch == zoom +0.1, pinned by session_camera_test).
    shim::ScriptedPlatform plat;
    plat.setMouse(400, 240, false);     // mid-screen (outside the edge bands)
    plat.setWheel(2);

    play::SessionCamera cam;
    cam.Init(1000.0f, 2000.0f, 800, 480);

    shim::MouseState ms;
    plat.getMouse(ms);
    cam.Frame(ms, false, false, false, false, (float)ms.wheel, 16.0f);
    CHECK(std::fabs(cam.zoom() - 0.2f) < 1e-6f);   // 2 notches -> 0.2

    plat.getMouse(ms);                  // cleared: zoom holds
    cam.Frame(ms, false, false, false, false, (float)ms.wheel, 16.0f);
    CHECK(std::fabs(cam.zoom() - 0.2f) < 1e-6f);
}
