// session_camera_test.cpp — unit tests for play::SessionCamera, the session
// adapter over the REAL reconstructed camera state machine (Camera_Update
// @0x4b4c68 -> UpdatePan core @0x4b365c / UpdateMovement @0x4b41a8 /
// AnchorToTerrain @0x4b2900). Proves pan moves the eye, wheel zoom moves the
// scale (pixelsPerUnit) and the eye height, and drag drives the rotate branch.
#include "tests/framework/test.h"

#include "play/session_camera.h"

#include <cmath>

using guild::play::SessionCamera;
using guild::shim::MouseState;

namespace {

MouseState centerMouse() {
    MouseState ms;
    ms.x = 320; ms.y = 240;     // away from every edge of a 640x480 fb
    return ms;
}

SessionCamera makeCam() {
    SessionCamera cam;
    cam.Init(/*eyeX*/ 1000.0f, /*eyeZ*/ 2000.0f, /*fbW*/ 640, /*fbH*/ 480);
    return cam;
}

void idleFrame(SessionCamera& cam, float dt = 16.0f) {
    cam.Frame(centerMouse(), false, false, false, false, 0.0f, dt);
}

} // namespace

// ---------------------------------------------------------------------------
// Init: eye where asked, the CITY-ENTER anchor zoom 0.33 (gilde.exe boot call
// site 0x506fe9: `push 0.33f; call Camera_AnchorToTerrain@0x4b2900`, frida-
// verified live: flt_6316DC == 0.33) -> pixelsPerUnit 1.33, the REAL anchor
// height 450 + 1150*0.33 = 829.5, and the dispatcher's early path has
// initialized the screen-edge box (width-8 / height / 0 / 0).
// ---------------------------------------------------------------------------
TEST(SessionCamera, InitAnchorsEyeAndEdgeBox) {
    SessionCamera cam = makeCam();
    CHECK(cam.eyeX() == 1000.0f);
    CHECK(cam.eyeZ() == 2000.0f);
    CHECK(std::fabs(cam.zoom() - 0.33f) < 1e-6f);
    CHECK(std::fabs(cam.pixelsPerUnit() - 1.33f) < 1e-6f);
    CHECK(std::fabs(cam.obj.posY - 829.5f) < 1e-3f);   // AnchorToTerrain @0x4b2900
    CHECK_EQ(cam.st2.box0, 640 - 8);                   // dispatcher early path
    CHECK_EQ(cam.st2.box1, 480);
    CHECK_EQ(cam.st2.box2, 0);
    CHECK_EQ(cam.st2.box3, 0);
    CHECK_EQ(cam.st2.boxFlag, 1);
}

// ---------------------------------------------------------------------------
// Idle frames move nothing.
// ---------------------------------------------------------------------------
TEST(SessionCamera, IdleFrameIsStationary)  {
    SessionCamera cam = makeCam();
    for (int i = 0; i < 5; ++i) idleFrame(cam);
    CHECK(cam.eyeX() == 1000.0f);
    CHECK(cam.eyeZ() == 2000.0f);
    CHECK(std::fabs(cam.zoom() - 0.33f) < 1e-6f);
}

// ---------------------------------------------------------------------------
// Arrow-key pan drives the REAL UpdatePan core: held right ramps the velocity
// accumulator and moves the eye +X; releasing decays it back to rest.
// ---------------------------------------------------------------------------
TEST(SessionCamera, ArrowPanMovesEyeX) {
    SessionCamera cam = makeCam();
    const float x0 = cam.eyeX();
    float prev = x0;
    float firstStep = 0.0f, lastStep = 0.0f;
    for (int i = 0; i < 10; ++i) {
        cam.Frame(centerMouse(), /*L*/ false, /*R*/ true, false, false, 0.0f, 16.0f);
        float step = cam.eyeX() - prev;
        if (i == 0) firstStep = step;
        lastStep = step;
        prev = cam.eyeX();
    }
    CHECK(cam.eyeX() > x0);                  // panned right
    CHECK(cam.eyeZ() == 2000.0f);            // Z untouched
    CHECK(lastStep > firstStep);             // velocity RAMPED (accel model)
    // release: the accumulator decays; eventually the eye stops.
    float still = cam.eyeX();
    for (int i = 0; i < 200; ++i) idleFrame(cam);
    still = cam.eyeX();
    idleFrame(cam);
    CHECK(cam.eyeX() == still);              // decayed to rest
}

TEST(SessionCamera, ArrowPanMovesEyeZBothWays) {
    SessionCamera cam = makeCam();
    const float z0 = cam.eyeZ();
    for (int i = 0; i < 8; ++i)
        cam.Frame(centerMouse(), false, false, /*Up*/ true, false, 0.0f, 16.0f);
    const float zUp = cam.eyeZ();
    CHECK(zUp != z0);                        // moved on the Z axis
    CHECK(cam.eyeX() == 1000.0f);
    // opposite direction reverses the motion (fresh camera).
    SessionCamera cam2 = makeCam();
    for (int i = 0; i < 8; ++i)
        cam2.Frame(centerMouse(), false, false, false, /*Down*/ true, 0.0f, 16.0f);
    CHECK((cam2.eyeZ() - z0) * (zUp - z0) < 0.0f);   // opposite signs
}

// ---------------------------------------------------------------------------
// Edge scroll: cursor parked on the left screen edge pans without any key.
// ---------------------------------------------------------------------------
TEST(SessionCamera, EdgeScrollPansEye) {
    SessionCamera cam = makeCam();
    MouseState ms;
    ms.x = 0; ms.y = 240;                    // left edge band
    const float x0 = cam.eyeX();
    for (int i = 0; i < 8; ++i)
        cam.Frame(ms, false, false, false, false, 0.0f, 16.0f);
    CHECK(cam.eyeX() != x0);                 // edge pan moved the eye
}

// ---------------------------------------------------------------------------
// Wheel zoom: one notch -> UpdateMovement's wheel branch adds 0.1 to the zoom
// fraction and AnchorToTerrain re-derives the eye height; pixelsPerUnit maps
// 1:1 to RealCityRenderer::Options (1.0f + zoom, see city_frame.cpp).
// ---------------------------------------------------------------------------
TEST(SessionCamera, WheelZoomScalesAndMovesEyeHeight) {
    SessionCamera cam = makeCam();
    const float y0 = cam.obj.posY;           // 829.5 (boot zoom 0.33)
    cam.Frame(centerMouse(), false, false, false, false, /*wheel*/ 1.0f, 16.0f);
    CHECK(std::fabs(cam.zoom() - 0.43f) < 1e-6f);      // 0.33 + one 0.1 notch
    CHECK(std::fabs(cam.pixelsPerUnit() - 1.43f) < 1e-6f);
    CHECK(std::fabs(cam.obj.posY - 944.5f) < 1e-3f);   // 450 + 1150*0.43
    CHECK(cam.obj.posY > y0);                 // the eye MOVED on zoom
    CHECK(cam.eyeX() == 1000.0f);             // ground position unchanged
    CHECK(cam.eyeZ() == 2000.0f);
    // pitch follows the zoom fraction: worldX = baseAngle + span*0.43.
    const float expPitch = cam.cs.baseAngle
                         + (cam.cs.spanAngle - cam.cs.baseAngle) * 0.43f;
    CHECK(std::fabs(cam.obj.worldX - expPitch) < 1e-5f);
}

TEST(SessionCamera, WheelZoomClampsToUnitRange) {
    SessionCamera cam = makeCam();
    for (int i = 0; i < 15; ++i)             // 15 notches -> would be 1.5
        cam.Frame(centerMouse(), false, false, false, false, 1.0f, 16.0f);
    CHECK(cam.zoom() <= 1.0f + 1e-6f);       // clamped by the wheel branch
    CHECK(std::fabs(cam.pixelsPerUnit() - 2.0f) < 1e-5f);
    for (int i = 0; i < 30; ++i)             // zoom all the way back out
        cam.Frame(centerMouse(), false, false, false, false, -1.0f, 16.0f);
    CHECK(cam.zoom() >= 0.0f);               // clamped at 0
    CHECK(cam.zoom() <= 1e-6f);
}

// Zoom changes the pan speed (UpdatePan scales by zoom*1.5 + 0.6): the same
// held-key burst pans farther when zoomed in.
TEST(SessionCamera, ZoomScalesPanSpeed) {
    SessionCamera slow = makeCam();
    for (int i = 0; i < 6; ++i)
        slow.Frame(centerMouse(), false, true, false, false, 0.0f, 16.0f);
    const float dSlow = slow.eyeX() - 1000.0f;

    SessionCamera fast = makeCam();
    for (int i = 0; i < 10; ++i)             // zoom fully in first
        fast.Frame(centerMouse(), false, false, false, false, 1.0f, 16.0f);
    for (int i = 0; i < 6; ++i)
        fast.Frame(centerMouse(), false, true, false, false, 0.0f, 16.0f);
    const float dFast = fast.eyeX() - 1000.0f;
    CHECK(dFast > dSlow);
}

// ---------------------------------------------------------------------------
// Mouse drag (right button, no left) drives the REAL UpdateMovement rotate
// branch: with the identity camera basis the eye translates on the ground
// plane following the mouse delta, scaled by scroll_speed (dword_1233564).
// ---------------------------------------------------------------------------
TEST(SessionCamera, RightDragMovesEyeViaRotateBranch) {
    SessionCamera cam = makeCam();
    MouseState ms = centerMouse();
    ms.right = true;
    cam.Frame(ms, false, false, false, false, 0.0f, 16.0f);   // latch drag
    const float x0 = cam.eyeX();
    ms.x += 40;                                               // drag right
    cam.Frame(ms, false, false, false, false, 0.0f, 16.0f);
    CHECK(cam.eyeX() != x0);                  // the eye MOVED on drag
    // delta = mouseDelta * scrollSpeed * 6/99: 40 * 50 * 6 / 99 = 121.21...
    CHECK(std::fabs((cam.eyeX() - x0) - 40.0f * 50.0f * 6.0f / 99.0f) < 1e-2f);
    // Release DETACHES the camera (UpdateMovement drag-release @0x4b4ae3 sets
    // dword_62D4E4 = 1): with the cursor inside the box a wheel zoom is DEAD —
    // the REAL original behavior.
    ms.right = false;
    cam.Frame(ms, false, false, false, false, 0.0f, 16.0f);   // release frame
    cam.Frame(ms, false, false, false, false, 1.0f, 16.0f);   // wheel: blocked
    CHECK(std::fabs(cam.zoom() - 0.33f) < 1e-6f);
    CHECK_EQ(cam.in2.disableMove, 1);
    // The REAL re-attach: VIBE_Camera_EdgeScroll @0x4b2c34 — the cursor on a
    // boundary row (my == bottom = dword_62D0C8 - 1 = 479) steps the edge
    // state to center and the center commit clears dword_62D4E4/E8 @0x4b2d95.
    ms.y = 479;
    cam.Frame(ms, false, false, false, false, 0.0f, 16.0f);
    CHECK_EQ(cam.in2.disableMove, 0);
    CHECK_EQ(cam.st2.edgeSnapLatch, 1);       // dword_631DE0 latched
    // back inside the box the latch re-arms and the wheel works again.
    ms.y = 240;
    cam.Frame(ms, false, false, false, false, 1.0f, 16.0f);
    CHECK(std::fabs(cam.zoom() - 0.43f) < 1e-6f);
    CHECK_EQ(cam.st2.edgeSnapLatch, 0);
}

// While detached (after a drag release) the pan core is gated off too — the
// VIBE_Camera_UpdatePan entry gate @0x4b3685/0x4b3692 returns 0 when
// dword_62D4E8 || dword_62D4E4. Arrows move nothing until the EdgeScroll
// re-attach fires.
TEST(SessionCamera, DetachGatesPanUntilEdgeScrollReattach) {
    SessionCamera cam = makeCam();
    MouseState ms = centerMouse();
    ms.right = true;
    cam.Frame(ms, false, false, false, false, 0.0f, 16.0f);   // latch drag
    ms.right = false;
    cam.Frame(ms, false, false, false, false, 0.0f, 16.0f);   // release -> detach
    const float x0 = cam.eyeX();
    for (int i = 0; i < 5; ++i)
        cam.Frame(ms, false, /*Right*/ true, false, false, 0.0f, 16.0f);
    CHECK(cam.eyeX() == x0);                  // pan dead while detached
    ms.y = 479;                               // the re-attach row
    cam.Frame(ms, false, false, false, false, 0.0f, 16.0f);
    ms.y = 240;
    for (int i = 0; i < 5; ++i)
        cam.Frame(ms, false, /*Right*/ true, false, false, 0.0f, 16.0f);
    CHECK(cam.eyeX() > x0);                   // pan alive after re-attach
}

// ---------------------------------------------------------------------------
// pose(): the full eye + rotation state for the real-3D city view.
// eye = node +76/+80/+84; rot = the node rotation euler +132/+136/+140
// (== world +92../+144.. for the parent-less camera node). The view basis is
// MatrixFromEuler(-rot) per VIBE_Object_SetWorldTranslation @0x5af50c.
// ---------------------------------------------------------------------------
TEST(SessionCamera, PoseExposesEyeAndRotation) {
    SessionCamera cam = makeCam();
    const guild::play::CameraPose p = cam.pose();
    CHECK(p.eyeX == 1000.0f);
    CHECK(std::fabs(p.eyeY - 829.5f) < 1e-3f);     // AnchorToTerrain height
    CHECK(p.eyeZ == 2000.0f);
    // pitch = baseAngle + span*0 (flt_6316B8) after the zoom-0 anchor.
    const float bootPitch = cam.cs.baseAngle
                          + (cam.cs.spanAngle - cam.cs.baseAngle) * 0.33f;
    CHECK(std::fabs(p.rotX - bootPitch) < 1e-5f);
    CHECK(p.rotY == 0.0f);
    CHECK(p.rotZ == 0.0f);
    // pose mirrors the node fields and the world-pose mirror (+92/+144).
    CHECK(p.eyeX == cam.obj.posX);
    CHECK(p.rotX == cam.obj.worldX);
    CHECK(cam.obj.wposX == cam.obj.posX);
    CHECK(cam.obj.wrotX == cam.obj.worldX);
    // wheel zoom changes the exposed pitch exactly like obj.worldX.
    cam.Frame(centerMouse(), false, false, false, false, 1.0f, 16.0f);
    const float expPitch = cam.cs.baseAngle
                         + (cam.cs.spanAngle - cam.cs.baseAngle) * 0.43f;
    CHECK(std::fabs(cam.pose().rotX - expPitch) < 1e-5f);
}

// ---------------------------------------------------------------------------
// ROTATE INPUT mutates the exposed rotation: the UpdateMovement pan branch
// (@0x4b43f1, left+right drag) writes the yaw euler through
// VIBE_Object_SetWorldTranslation: vwld[1] = flt_631E18 - dx * 0.0035
// (dx measured from dword_11BC33C = trunc((1 - zoomBits) * 666.667) = 666
// at zoom 0 — the original's pan-init snapshot).
// ---------------------------------------------------------------------------
TEST(SessionCamera, RotateInputMutatesExposedRotation) {
    SessionCamera cam = makeCam();
    MouseState ms = centerMouse();        // x = 320
    ms.right = true; ms.left = true;      // 672238 + 672220: the pan branch
    const float yaw0 = cam.pose().rotY;   // 0
    cam.Frame(ms, false, false, false, false, 0.0f, 16.0f);
    const float yaw1 = cam.pose().rotY;
    // frame 1: the pan-init snapshot is trunc((1 - zoom) * 666.667) = 446 at
    // the 0.33 boot zoom; yaw = 0 - (320 - 446) * 0.0035 = 0.441.
    CHECK(std::fabs(yaw1 - (-(320.0f - 446.0f) * 0.0035f)) < 1e-4f);
    CHECK(yaw1 != yaw0);
    ms.x += 40;                           // drag right
    cam.Frame(ms, false, false, false, false, 0.0f, 16.0f);
    const float yaw2 = cam.pose().rotY;
    // golden: yaw delta = -dx * 0.0035 (kMv_flt_61DE28)
    CHECK(std::fabs((yaw2 - yaw1) - (-40.0f * 0.0035f)) < 1e-5f);
    // the same euler is the node field the engine's matrix build consumes.
    CHECK(yaw2 == cam.obj.worldY);
}

// Right-only drag takes the position branch — the eye moves, the exposed
// rotation must NOT change.
TEST(SessionCamera, RightOnlyDragKeepsRotation) {
    SessionCamera cam = makeCam();
    MouseState ms = centerMouse();
    ms.right = true;
    cam.Frame(ms, false, false, false, false, 0.0f, 16.0f);
    const guild::play::CameraPose before = cam.pose();
    ms.x += 40;
    cam.Frame(ms, false, false, false, false, 0.0f, 16.0f);
    const guild::play::CameraPose after = cam.pose();
    CHECK(after.eyeX != before.eyeX);     // position branch moved the eye
    CHECK(after.rotX == before.rotX);
    CHECK(after.rotY == before.rotY);
    CHECK(after.rotZ == before.rotZ);
}

// ---------------------------------------------------------------------------
// TERRAIN FOLLOW: BindTerrain wires the REAL VIBE_Terrain_AverageAreaHeight
// @0x427468 (render::AverageAreaHeight over a Heightmap) into the camera
// hooks. Heightmap: 64x64 grid, origin (0,10,0), scale (100,2,100), all
// height bytes 100 -> every interior cell = 100*2 + 10 = 210; the 8x8 box
// average at an interior point is exactly 210.
// ---------------------------------------------------------------------------
namespace {
guild::render::Heightmap makeFlatHeightmap(guild::u8* bytes) {
    for (int i = 0; i < 64 * 64; ++i) bytes[i] = 100;
    guild::render::Heightmap hm{};
    hm.originX = 0.0f; hm.originY = 10.0f; hm.originZ = 0.0f;
    hm.scaleX = 100.0f; hm.scaleY = 2.0f; hm.scaleZ = 100.0f;
    hm.size = 64;
    hm.heights = bytes;
    hm.entries = nullptr;
    return hm;
}
} // namespace

TEST(SessionCamera, TerrainBindAnchorsEyeToRealHeightmap) {
    static guild::u8 bytes[64 * 64];
    guild::render::Heightmap hm = makeFlatHeightmap(bytes);
    SessionCamera cam;
    cam.BindTerrain(&hm);                  // bind BEFORE Init (survives it)
    cam.Init(1000.0f, 2000.0f, 640, 480);
    // AnchorToTerrain @0x4b2900 at the boot zoom 0.33:
    // posY = terrain(210) + 450 + 1150*0.33 = 1039.5.
    CHECK(std::fabs(cam.obj.posY - 1039.5f) < 1e-3f);
    CHECK(std::fabs(cam.pose().eyeY - 1039.5f) < 1e-3f);
}

TEST(SessionCamera, TerrainClampEasesEyeTowardTerrain) {
    static guild::u8 bytes[64 * 64];
    guild::render::Heightmap hm = makeFlatHeightmap(bytes);
    SessionCamera cam;
    cam.Init(1000.0f, 2000.0f, 640, 480);
    cam.BindTerrain(&hm);                  // bind AFTER Init (also supported)
    // displaced eye: the per-frame Camera_ClampToTerrainHeight @0x4b2a0c
    // (gated by byte_6316D8, which BindTerrain enables) eases posY toward
    // terrain + baseH + span*zoom = 210 + 450 + 1150*0.33 = 1039.5 in dy/15
    // steps (clamped +-10, deadzone 5). From 600 that's ~44+ frames at max step.
    cam.obj.posY = 600.0f;
    for (int i = 0; i < 120; ++i) idleFrame(cam);
    CHECK(cam.obj.posY > 1033.0f);
    CHECK(cam.obj.posY <= 1039.5f + 1e-3f);
    // inside the 5.0 deadzone the clamp stops moving (dword_631DDC = 0).
    const float settled = cam.obj.posY;
    idleFrame(cam);
    CHECK(cam.obj.posY == settled);
    CHECK_EQ(cam.cs.clampResultBits, 0);
}

TEST(SessionCamera, WheelZoomFollowsBoundTerrain) {
    static guild::u8 bytes[64 * 64];
    guild::render::Heightmap hm = makeFlatHeightmap(bytes);
    SessionCamera cam;
    cam.BindTerrain(&hm);
    cam.Init(1000.0f, 2000.0f, 640, 480);  // posY = 1039.5 (boot zoom 0.33)
    // wheel branch -> AnchorToTerrain (h1 forwarded through UpdateMovement):
    // posY = terrain(210) + 450 + 1150*0.43 = 1154.5.
    cam.Frame(centerMouse(), false, false, false, false, 1.0f, 16.0f);
    CHECK(std::fabs(cam.zoom() - 0.43f) < 1e-6f);
    CHECK(std::fabs(cam.obj.posY - 1154.5f) < 1e-2f);
}

// scroll_speed 0 (slider minimum) kills the drag motion — the option is the
// REAL dword_1233564 multiplier in the rotate branch.
TEST(SessionCamera, ScrollSpeedZeroFreezesDrag) {
    SessionCamera cam = makeCam();
    cam.scrollSpeed = 0;
    MouseState ms = centerMouse();
    ms.right = true;
    cam.Frame(ms, false, false, false, false, 0.0f, 16.0f);
    ms.x += 40;
    const float x0 = cam.eyeX();
    cam.Frame(ms, false, false, false, false, 0.0f, 16.0f);
    CHECK(cam.eyeX() == x0);
}
