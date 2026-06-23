// camera_recon2_test.cpp — golden-vector unit tests for the VIBE_Camera_* big
// movers (camera_recon2). Self-contained; uses the inert default hooks so the
// pure arithmetic / clamps / state machine are exercised in isolation.
#include "tests/framework/test.h"

#include "render/camera_recon2.h"

#include <cmath>
#include <cstring>

using namespace guild::render;
using guild::i32;
using guild::u8;

namespace {

// Build a CameraObject with an identity world matrix and a known pose.
CameraObject makeObj() {
    CameraObject o;
    o.present = true;
    o.posX = 0.0f; o.posY = 100.0f; o.posZ = 0.0f;
    o.worldX = 0.1f; o.worldY = 0.2f; o.worldZ = 0.3f;
    // identity 4x4 in the first 12 floats (R = I, cols at [0..2],[3..5],[6..8]).
    std::memset(o.matrix, 0, sizeof(o.matrix));
    o.matrix[0] = 1.0f; o.matrix[4] = 1.0f; o.matrix[8] = 1.0f; o.matrix[10] = 1.0f;
    return o;
}

bool feq(float a, float b, float tol = 1e-4f) { return std::fabs(a - b) <= tol; }

// 3D-sound listener capture (shared by ZoomOut / OrientToTarget pins).
f32 g_capPos[3];
f32 g_capAng[3];
i32 g_capKind = -1;
i32 g_capDist = -1;
i32 capture_listener(CameraObject*, const f32* pos, i32 dist, const f32* ang, i32 kind) {
    g_capPos[0]=pos[0]; g_capPos[1]=pos[1]; g_capPos[2]=pos[2];
    g_capAng[0]=ang[0]; g_capAng[1]=ang[1]; g_capAng[2]=ang[2];
    g_capKind = kind;
    g_capDist = dist;
    return 0;
}

// applyConstraints capture: record which dpos/dworld components received the
// routed deltas (the axis-routing tree of UpdateTrackTargetFromMouse). Returns
// the inert "both changed" bits so the commit path runs.
f32 g_capDpos[3];
f32 g_capDworld[3];
u8 capture_constraints(CameraObject* obj, const f32* dpos, const f32* /*basis*/,
                       const f32* dworld, f32* outPos, f32* outWorld) {
    g_capDpos[0]=dpos[0]; g_capDpos[1]=dpos[1]; g_capDpos[2]=dpos[2];
    g_capDworld[0]=dworld[0]; g_capDworld[1]=dworld[1]; g_capDworld[2]=dworld[2];
    outPos[0]=obj->posX+dpos[0]; outPos[1]=obj->posY+dpos[1]; outPos[2]=obj->posZ+dpos[2];
    outWorld[0]=obj->worldX+dworld[0]; outWorld[1]=obj->worldY+dworld[1]; outWorld[2]=obj->worldZ+dworld[2];
    return 3;
}

} // namespace

// ---------------------------------------------------------------------------
// RotateView: drag inactive -> no movement, returns 0.
// ---------------------------------------------------------------------------
TEST(Camera2ReconRotateView, IdleReturnsZero) {
    CameraObject obj = makeObj();
    Camera2State st;
    Camera2Input in;
    auto h = Camera2_DefaultHooks();
    in.d672238 = 0;  // no drag
    CHECK_EQ(Camera_RotateView(obj, st, in, h), 0);
    CHECK_EQ(st.rotActive, 0);
}

// ---------------------------------------------------------------------------
// RotateView: first drag frame latches start state and (with rotate button)
// applies a world-translation yaw. Non-inverted axis touches worldY.
// ---------------------------------------------------------------------------
TEST(Camera2ReconRotateView, ButtonYawLatchAndApply) {
    CameraObject obj = makeObj();
    Camera2State st;
    Camera2Input in;
    auto h = Camera2_DefaultHooks();
    in.d672238 = 1;            // drag active
    in.d672220 = 1;            // rotate button held
    in.g_13FCD1C_present = 1;
    in.byte671D6F = 0;         // non-inverted -> yaw on worldY
    in.viewShift = 40;         // SHIWORD delta
    in.d672174  = 0;

    // First call latches: rotPrevView <- viewShift (40), rotActive <- 1.
    i32 r = Camera_RotateView(obj, st, in, h);
    CHECK_EQ(r, 1);
    CHECK_EQ(st.rotActive, 1);
    CHECK_EQ(st.rotPrevView, 40);
    // On the latch frame, v36 = viewShift - rotPrevView. Since the latch set
    // rotPrevView to viewShift in the SAME call (before the yaw block reads it),
    // v36 = 40 - 40 = 0 -> worldY unchanged (Fmod(0.2, 2pi) = 0.2).
    CHECK(feq(obj.worldX, 0.1f));
    CHECK(feq(obj.worldY, 0.2f));
}

// ---------------------------------------------------------------------------
// RotateView: second frame with a real delta rotates worldY by delta*scale,
// wrapped by fmod(2*pi). scale = 0.0024999999441206455.
// ---------------------------------------------------------------------------
TEST(Camera2ReconRotateView, YawDeltaMatchesGolden) {
    CameraObject obj = makeObj();
    Camera2State st;
    Camera2Input in;
    auto h = Camera2_DefaultHooks();
    in.d672238 = 1; in.d672220 = 1; in.g_13FCD1C_present = 1; in.byte671D6F = 0;

    // Frame 1: latch at viewShift=0.
    in.viewShift = 0; in.d672174 = 0;
    Camera_RotateView(obj, st, in, h);
    // Frame 2: viewShift=100 -> v36 = 100 - 0 = 100.
    in.viewShift = 100;
    Camera_RotateView(obj, st, in, h);

    const double scale = 0.0024999999441206455;
    double expectedY = std::fmod(0.2 - 100.0 * scale, 6.28318530718);
    CHECK(feq(obj.worldY, (float)expectedY));
    CHECK(feq(obj.worldX, 0.1f));        // worldX untouched in non-inverted yaw
    CHECK_EQ(st.rotPrevView, 100);
}

// ---------------------------------------------------------------------------
// RotateView: INVERTED-AXIS pitch branch (byte_671D6F set, rotate button held).
// Pins the recovered constants on the previously-untested inverted path:
//   wt0 = Fmod(v36 * 0.0024999999441206455 + worldX, 6.28318530718)   (0x4b30b1)
// where v36 = (dword_672174>>16) - dword_631DE8 (the Y delta, not the view
// delta). worldY/worldZ are copied unchanged. (camera_recon2.cpp:221-226)
// ---------------------------------------------------------------------------
TEST(Camera2ReconRotateView, InvertedAxisPitchMatchesGolden) {
    CameraObject obj = makeObj();   // worldX=0.1, worldY=0.2, worldZ=0.3
    Camera2State st;
    Camera2Input in;
    auto h = Camera2_DefaultHooks();
    in.d672238 = 1; in.d672220 = 1; in.g_13FCD1C_present = 1;
    in.byte671D6F = 1;              // INVERTED -> pitch on worldX

    // Frame 1: latch at d672174 (Y) = 0 -> rotPrevY = 0.
    in.viewShift = 0; in.d672174 = 0;
    Camera_RotateView(obj, st, in, h);
    // Frame 2: d672174 = 100 -> v36 = 100 - 0 = 100.
    in.d672174 = 100;
    Camera_RotateView(obj, st, in, h);

    const double kRotScale = 0.0024999999441206455;  // flt_61DDA0
    const double kTwoPi    = 6.28318530718;          // dbl_61DDA8
    double expectedX = std::fmod(100.0 * kRotScale + 0.1, kTwoPi);
    CHECK(feq(obj.worldX, (float)expectedX));   // pitch (worldX) rotated + wrapped
    CHECK(feq(obj.worldY, 0.2f));               // yaw untouched on inverted path
    CHECK(feq(obj.worldZ, 0.3f));
    CHECK_EQ(st.rotPrevY, 100);
}

// ---------------------------------------------------------------------------
// RotateView: drag released after being active clears rotActive, returns 0.
// ---------------------------------------------------------------------------
TEST(Camera2ReconRotateView, ReleaseClearsActive) {
    CameraObject obj = makeObj();
    Camera2State st;
    Camera2Input in;
    auto h = Camera2_DefaultHooks();
    st.rotActive = 1;
    in.d672238 = 0;            // released
    CHECK_EQ(Camera_RotateView(obj, st, in, h), 0);
    CHECK_EQ(st.rotActive, 0);
}

// ---------------------------------------------------------------------------
// UpdateMovement: disableMove gate returns 0 immediately.
// ---------------------------------------------------------------------------
TEST(Camera2ReconUpdateMovement, DisableGate) {
    CameraObject obj = makeObj();
    CameraState cs;
    Camera2State st;
    Camera2Input in;
    auto h = Camera2_DefaultHooks();
    in.disableMove = 1;
    CHECK_EQ(Camera_UpdateMovement(obj, cs, st, in, h), 0);
}

// ---------------------------------------------------------------------------
// UpdateMovement: idle (no drag, not active) returns the wheel accumulator
// delta and does not move. Return = d672254 + mvDone + mvPanInit - d672250.
// ---------------------------------------------------------------------------
TEST(Camera2ReconUpdateMovement, IdleReturnsWheelDelta) {
    CameraObject obj = makeObj();
    CameraState cs;
    Camera2State st;
    Camera2Input in;
    auto h = Camera2_DefaultHooks();
    in.d672238 = 0;            // no drag
    in.d672220 = 0;
    in.g_13FCD1C_present = 1;
    in.d672250 = 10; in.d672254 = 10;   // equal -> no wheel zoom
    // not active, no drag -> falls into the (d672238 || !mvActive) block, then
    // the rotate-branch bail (no drag) returns the accumulator expression.
    i32 r = Camera_UpdateMovement(obj, cs, st, in, h);
    CHECK_EQ(r, 10 + 0 + 0 - 10);
}

// ---------------------------------------------------------------------------
// UpdateMovement: wheel zoom changes zoomT by (delta)*0.10000000149011612 and
// anchors to terrain (inert terrain height = 0). zoomT clamps to [0,1] feed.
// ---------------------------------------------------------------------------
TEST(Camera2ReconUpdateMovement, WheelZoomAdvancesZoomT) {
    CameraObject obj = makeObj();
    CameraState cs;
    Camera2State st;
    Camera2Input in;
    auto h = Camera2_DefaultHooks();
    in.d672238 = 0; in.d672220 = 0; in.g_13FCD1C_present = 1;
    cs.zoomT = 0.0f;
    in.d672250 = 0; in.d672254 = 3;     // wheel delta = 3
    Camera_UpdateMovement(obj, cs, st, in, h);
    const float k = 0.10000000149011612f;
    // zoomT becomes 3*k = 0.30000001..., still <= 1.0 so AnchorToTerrain uses it.
    CHECK(feq(cs.zoomT, 3.0f * k));
}

// ---------------------------------------------------------------------------
// ZoomReset: returns the supplied mesh handle and records last-reset state.
// With inert hooks the math runs without touching real subsystems.
// ---------------------------------------------------------------------------
TEST(Camera2ReconZoomReset, ReturnsMeshHandleAndClearsGates) {
    CameraObject obj = makeObj();
    CameraState cs;
    Camera2State st;
    auto h = Camera2_DefaultHooks();
    cs.disableMove = 1; cs.altMoveMode = 1;
    i32 r = Camera_ZoomReset(obj, cs, st, h, /*meshHandle*/ 0x1234, /*a2*/ 0, /*a3*/ 0);
    CHECK_EQ(r, 0x1234);
    CHECK_EQ(st.zr_lastReset, 0x1234);
    CHECK_EQ(st.zr_lastA1, 0x1234);
    CHECK_EQ(cs.disableMove, 0);   // cleared
    CHECK_EQ(cs.altMoveMode, 0);   // cleared
}

// ---------------------------------------------------------------------------
// ZoomOut: with inert sound3d hook returning 0, the whole math path runs and
// returns 0. Verifies it clears the move gates like ZoomReset.
// ---------------------------------------------------------------------------
TEST(Camera2ReconZoomOut, RunsAndClearsGates) {
    CameraObject obj = makeObj();
    CameraState cs;
    Camera2State st;
    auto h = Camera2_DefaultHooks();
    cs.disableMove = 1; cs.altMoveMode = 1;
    i32 r = Camera_ZoomOut(obj, cs, st, h, /*meshHandle*/ 7, 0, 0, 0);
    CHECK_EQ(r, 0);                // inert sound3d
    CHECK_EQ(cs.disableMove, 0);
    CHECK_EQ(cs.altMoveMode, 0);
}

// ---------------------------------------------------------------------------
// ZoomOut: the terminal 3D-sound listener call uses KIND 104 (the pushed
// constant at 0x4b5bxx; distinct from ZoomReset's commit and OrientToTarget's
// kind 40). The angVec/posVec is the node's world-translation euler triple
// (+132/+136/+140). Pins the listener kind via a capturing hook.
// (camera_recon2.cpp:723)
// ---------------------------------------------------------------------------
TEST(Camera2ReconZoomOut, ListenerKindIs104) {
    CameraObject obj = makeObj();
    CameraState cs;
    Camera2State st;
    auto h = Camera2_DefaultHooks();
    h.sound3dSetListener = &capture_listener;
    g_capKind = -1;
    Camera_ZoomOut(obj, cs, st, h, /*meshHandle*/ 7, 0, 0, 0);
    CHECK_EQ(g_capKind, 104);
}

// ---------------------------------------------------------------------------
// ZoomOut 3-band distance clamp (the wave-13 NEEDS-LIVE-MCP item, resolved via
// live decompile @0x4b5974). The terminal listener distance is:
//   v10 = max(minZoom, sqrt(|eye-pos|)*minZoom*0.0005)
//   v11 = 3*minZoom
//   if (v11 < v10)            dist = v11                 (band 1: cap at 3*min)
//   else if (minZoom <= v38b) dist = v38b (raw, pre-max) (band 2)
//   else                      dist = minZoom             (band 3: floor)
// With the makeObj/default CameraState pose the eased raw v38b = 42.90 (< 50),
// so band 3 fires and the listener distance is exactly minZoom = 50.
// Golden traced in IDA python (single-precision) @ wave-15.
// ---------------------------------------------------------------------------
TEST(Camera2ReconZoomOut, ThreeBandDistClampBand3) {
    CameraObject obj = makeObj();
    CameraState cs;
    Camera2State st;
    auto h = Camera2_DefaultHooks();
    h.sound3dSetListener = &capture_listener;
    g_capDist = -1;
    Camera_ZoomOut(obj, cs, st, h, /*meshHandle*/ 7, 0, 0, 0);
    CHECK_EQ(g_capDist, 50);    // band 3: floored to minZoomDist
    // posVec/angVec are the node world-translation triple (+132/+136/+140),
    // i.e. obj.world (0.1,0.2,0.3) here. (Both posVec and angVec == v31.)
    CHECK(feq(g_capPos[0], 0.1f));
    CHECK(feq(g_capPos[1], 0.2f));
    CHECK(feq(g_capPos[2], 0.3f));
    CHECK(feq(g_capAng[0], 0.1f));
    CHECK(feq(g_capAng[1], 0.2f));
    CHECK(feq(g_capAng[2], 0.3f));
}

// ---------------------------------------------------------------------------
// OrientToTarget: with an identity frame the listener call gets kind 40 and
// the inert hook returns 0. Capture the angVec via a custom hook to verify the
// yaw/pitch are finite and the eye is offset by 600 along -Z + height nudges.
// ---------------------------------------------------------------------------
TEST(Camera2ReconOrientToTarget, ListenerKindAndFiniteAngles) {
    CameraObject obj = makeObj();
    CameraState cs;
    Camera2State st;
    auto h = Camera2_DefaultHooks();
    h.sound3dSetListener = &capture_listener;

    // Frame: 110+ floats. Identity-ish; local -Z axis pulls eye along it.
    f32 frame[128];
    std::memset(frame, 0, sizeof(frame));
    // local translation at [19..21] (the point fed to PointThroughBoneChain).
    frame[19] = 0.0f; frame[20] = 0.0f; frame[21] = 0.0f;
    // local translation [30..32] used by PointThroughBoneChain.
    frame[30] = 0.0f; frame[31] = 0.0f; frame[32] = 0.0f;
    // 3x3 at [99..] identity (so -Z stays -Z).
    frame[99] = 1.0f; frame[103] = 1.0f; frame[107] = 1.0f;  // diag of the basis used by Orient
    // parent link byte 504 (= float index 126) zero -> no parent.
    g_capKind = -1;
    Camera_OrientToTarget(obj, cs, st, h, frame);
    CHECK_EQ(g_capKind, 40);
    CHECK(std::isfinite(g_capAng[0]));
    CHECK(std::isfinite(g_capAng[1]));
    CHECK_EQ((int)g_capAng[2], 0);   // v24 third angle component is 0
    // PINS the eye-offset constants flt_61DE74=600 / flt_61DE78=900 /
    // flt_61DE88=-1.875 (camera_recon2.cpp:747-754,811). With this identity
    // frame the bone-chain base is 0 and the local -Z axis maps to (-1,0,0)
    // through the 3x3 (frame[107]=1): eye = (0 + -1*600, 0 + 0 + 900 - 1.875,
    // 0) = (-600, 898.125, 0).
    CHECK(feq(g_capPos[0], -600.0f));
    CHECK(feq(g_capPos[1], 898.125f));
    CHECK(feq(g_capPos[2], 0.0f));
}

// ---------------------------------------------------------------------------
// UpdateTrackTargetFromMouse: null active object -> returns false immediately.
// ---------------------------------------------------------------------------
TEST(Camera2ReconTrackTarget, NullActiveReturnsFalse) {
    Camera2Input in;
    auto h = Camera2_DefaultHooks();
    TrackTargetCtx ctx;
    ctx.active = nullptr;
    CHECK_EQ(Camera_UpdateTrackTargetFromMouse(in, h, ctx), false);
}

// ---------------------------------------------------------------------------
// UpdateTrackTargetFromMouse: pan-drag latch path. First frame with 672220 not
// previously latched sets latchPan and snapshots prevX/prevY, returns false
// (no move yet) since no constraints applied and no light edge.
// ---------------------------------------------------------------------------
TEST(Camera2ReconTrackTarget, PanLatchSnapshotsPrev) {
    CameraObject active = makeObj();
    Camera2Input in;
    auto h = Camera2_DefaultHooks();
    TrackTargetCtx ctx;
    ctx.active = &active;
    ctx.basisSel = 0;
    ctx.screenW = 800; ctx.screenH = 600;
    in.d672220 = 1;            // pan drag this frame
    ctx.latchPan = 0;          // not yet latched
    in.d67220E = 123;
    in.d672210 = 456;
    bool moved = Camera_UpdateTrackTargetFromMouse(in, h, ctx);
    CHECK_EQ(ctx.latchPan, 1);
    CHECK_EQ(ctx.prevX, 123);
    CHECK_EQ(ctx.prevY, 456);
    CHECK_EQ(moved, false);
}

// ---------------------------------------------------------------------------
// UpdateTrackTargetFromMouse: axis-routing tree (the wave-13 NEEDS-LIVE-MCP
// item, resolved via live decompile @0x5e967c). Drives a tilt drag (672234)
// with both mouse-axis edges and NO axis locks, in the big-tree, !byte_64A024
// branch. The decompile routes:
//   !671D8A && !671D7D  ->  v9 = v30 (dpos.x), v10 = v31 (dpos.y); dworld = 0.
// Tilt deltas (W=H=200, dx=20, dy=10) traced in python: v30=76.8, v31=-21.12.
// (latchTilt pre-set so the latch path is skipped and the compute path runs.)
// ---------------------------------------------------------------------------
TEST(Camera2ReconTrackTarget, AxisRouteNoLockGoesToPos) {
    CameraObject active = makeObj();
    Camera2Input in;
    auto h = Camera2_DefaultHooks();
    h.applyConstraints = &capture_constraints;
    TrackTargetCtx ctx;
    ctx.active = &active;
    ctx.basisSel = 0;
    ctx.screenW = 200; ctx.screenH = 200;
    ctx.activeTrackable = 1;
    ctx.btnX = 1; ctx.btnY = 1;
    ctx.prevX = 0; ctx.prevY = 0;
    ctx.latchTilt = 1;              // skip the tilt latch, run compute
    in.d672234 = 1;                // tilt drag this frame
    in.d67220E = 20; in.d672210 = 10;
    g_capDpos[0]=g_capDpos[1]=g_capDpos[2]=999.0f;
    g_capDworld[0]=g_capDworld[1]=g_capDworld[2]=999.0f;
    Camera_UpdateTrackTargetFromMouse(in, h, ctx);
    // dpos = (v30, v31, 0); dworld = 0.
    CHECK(feq(g_capDpos[0], 76.8f, 1e-2f));
    CHECK(feq(g_capDpos[1], -21.12f, 1e-2f));
    CHECK(feq(g_capDpos[2], 0.0f));
    CHECK(feq(g_capDworld[0], 0.0f));
    CHECK(feq(g_capDworld[1], 0.0f));
    CHECK(feq(g_capDworld[2], 0.0f));
}

// UpdateTrackTargetFromMouse: axis-routing with byte_671D8A LOCKED (big-tree,
// !byte_64A024). The decompile: when !671D8A is false it falls straight to
//   v13 = v32 (dworld.y) ; v11 = v31 (dpos.z)   [LABEL_15]
// With a tilt-only drag v32 = 0, so dworld stays 0 and dpos = (0,0,v31).
TEST(Camera2ReconTrackTarget, AxisRouteLock8AGoesToDposZ) {
    CameraObject active = makeObj();
    Camera2Input in;
    auto h = Camera2_DefaultHooks();
    h.applyConstraints = &capture_constraints;
    TrackTargetCtx ctx;
    ctx.active = &active;
    ctx.basisSel = 0;
    ctx.screenW = 200; ctx.screenH = 200;
    ctx.activeTrackable = 1;
    ctx.btnX = 1; ctx.btnY = 1;
    ctx.prevX = 0; ctx.prevY = 0;
    ctx.latchTilt = 1;
    ctx.lock671D8A = 1;            // pitch axis locked
    in.d672234 = 1;
    in.d67220E = 20; in.d672210 = 10;
    g_capDpos[0]=g_capDpos[1]=g_capDpos[2]=999.0f;
    g_capDworld[0]=g_capDworld[1]=g_capDworld[2]=999.0f;
    Camera_UpdateTrackTargetFromMouse(in, h, ctx);
    CHECK(feq(g_capDpos[0], 0.0f));
    CHECK(feq(g_capDpos[1], 0.0f));
    CHECK(feq(g_capDpos[2], -21.12f, 1e-2f));   // v11 = v31
    CHECK(feq(g_capDworld[0], 0.0f));
    CHECK(feq(g_capDworld[1], 0.0f));            // v13 = v32 = 0 (tilt-only)
    CHECK(feq(g_capDworld[2], 0.0f));
}

// UpdateTrackTargetFromMouse: pan-release with 67221C set fires the scene-graph
// walk; inert walk returns 0 -> light-refresh-all branch, sets moved=true.
// ---------------------------------------------------------------------------
TEST(Camera2ReconTrackTarget, PanReleaseRefreshesLight) {
    CameraObject active = makeObj();
    Camera2Input in;
    auto h = Camera2_DefaultHooks();
    TrackTargetCtx ctx;
    ctx.active = &active;
    ctx.basisSel = 0;
    ctx.screenW = 800; ctx.screenH = 600;
    ctx.latchPan = 1;          // previously latched
    in.d672220 = 0;            // released this frame
    in.d67221C = 1;            // edge -> trigger light update
    bool moved = Camera_UpdateTrackTargetFromMouse(in, h, ctx);
    CHECK_EQ(ctx.latchPan, 0);
    CHECK_EQ(moved, true);
}
