// Golden-vector unit tests for the VIBE_Camera_* cluster (camera_recon).
// Vectors are derived directly from the Hex-Rays decompile's math/constants.
#include "test.h"
#include "render/camera_recon.h"

#include <cmath>
#include <cstring>

using namespace guild;
using namespace guild::render;

// ---------------------------------------------------------------------------
// CmdCameraFlight / CmdCameraFlightTimed (0x43f528 / 0x43f5dc)
// ---------------------------------------------------------------------------
TEST(CameraReconCmd, SlotSelfWithMovingFlagsReturnsZeroAndWritesSlot) {
    CmdCameraFlightCtx ctx;
    ctx.slotPresent = true;
    ctx.slotIsSelf  = true;
    ctx.movingFlags = true;
    i32 r = Camera_CmdCameraFlight(170, ctx);
    CHECK_EQ(r, 0);
    CHECK(ctx.wroteSlot);
    CHECK(!ctx.drawCalled);
}

TEST(CameraReconCmd, SlotSelfNoMovingFlagsReturnsZeroNoWrite) {
    CmdCameraFlightCtx ctx;
    ctx.slotPresent = true;
    ctx.slotIsSelf  = true;
    ctx.movingFlags = false;
    i32 r = Camera_CmdCameraFlight(170, ctx);
    CHECK_EQ(r, 0);
    CHECK(!ctx.wroteSlot);
}

TEST(CameraReconCmd, FlightDividesBy17AndReturnsOne) {
    CmdCameraFlightCtx ctx;
    ctx.slotPresent = false; // not self -> flight path
    ctx.scriptFlag1 = true;
    ctx.drawOk = true;
    i32 r = Camera_CmdCameraFlight(170, ctx);
    CHECK_EQ(r, 1);
    CHECK(ctx.wroteSlot);     // scriptFlag1 -> wrote slot
    CHECK(ctx.drawCalled);
    CHECK_EQ(ctx.drawArg, 10); // 170 / 17
    CHECK(!ctx.reportedError);
}

TEST(CameraReconCmd, FlightReportsErrorWhenDrawFails) {
    CmdCameraFlightCtx ctx;
    ctx.slotPresent = false;
    ctx.drawOk = false;        // DrawTextLabels3D returned 0
    i32 r = Camera_CmdCameraFlight(34, ctx);
    CHECK_EQ(r, 1);
    CHECK_EQ(ctx.drawArg, 2);  // 34 / 17
    CHECK(ctx.reportedError);
}

TEST(CameraReconCmd, TimedDividesBy14AndNeverReports) {
    CmdCameraFlightCtx ctx;
    ctx.slotPresent = false;
    ctx.drawOk = false;        // even on "fail", Timed does not report
    i32 r = Camera_CmdCameraFlightTimed(140, ctx);
    CHECK_EQ(r, 1);
    CHECK_EQ(ctx.drawArg, 10); // 140 / 14
    CHECK(!ctx.reportedError);
}

// ---------------------------------------------------------------------------
// AnchorToTerrain (0x4b2900)
// ---------------------------------------------------------------------------
TEST(CameraReconAnchor, GlobalDisabledIsNoOp) {
    CameraObject obj; obj.present = true; obj.posY = 123.0f;
    CameraState st; st.globalDisabled = 1;
    CameraHooks h = Camera_DefaultHooks();
    Camera_AnchorToTerrain(obj, st, h, 0, 0, 0);
    CHECK_EQ(obj.posY, 123.0f); // unchanged
}

TEST(CameraReconAnchor, ZeroZoomTSetsBaseHeightAndBaseAngle) {
    CameraObject obj; obj.present = true;
    obj.posX = 1.0f; obj.posZ = 2.0f;
    obj.worldY = 7.0f; obj.worldZ = 9.0f;
    CameraState st;            // defaults: baseHeight=450, etc.
    CameraHooks h = Camera_DefaultHooks(); // terrainHeight -> 0
    // a3 = 0 -> zoomT bits = 0 -> zoomT = 0.0f
    Camera_AnchorToTerrain(obj, st, h, 0, 0, 0);
    // posY = terrain(0) + baseHeight + (spanHeight-baseHeight)*0 = 450
    CHECK_EQ(obj.posY, 450.0f);
    CHECK_EQ(obj.posX, 1.0f);
    CHECK_EQ(obj.posZ, 2.0f);
    // worldX = baseAngle + (spanAngle-baseAngle)*0 = baseAngle
    CHECK(std::fabs(obj.worldX - (-0.471238911151886f)) < 1e-6f);
    CHECK_EQ(obj.worldY, 7.0f);
    CHECK_EQ(obj.worldZ, 9.0f);
    // history mirrors updated fields
    f32 h2d8; std::memcpy(&h2d8, &st.hist_2D8, sizeof(f32));
    CHECK_EQ(h2d8, 450.0f);
}

TEST(CameraReconAnchor, ZoomTOneInterpolatesToSpan) {
    CameraObject obj; obj.present = true;
    CameraState st;
    CameraHooks h = Camera_DefaultHooks();
    // a3 must hold the BITS of 1.0f (LODWORD(flt_6316DC)=a3, then used as float)
    i32 oneBits; f32 one = 1.0f; std::memcpy(&oneBits, &one, sizeof(i32));
    Camera_AnchorToTerrain(obj, st, h, 0, 0, oneBits);
    // posY = 0 + 450 + (1600-450)*1 = 1600
    CHECK_EQ(obj.posY, 1600.0f);
    // worldX = baseAngle + (spanAngle-baseAngle)*1 = spanAngle
    CHECK(std::fabs(obj.worldX - (-1.0821040868759155f)) < 1e-6f);
}

TEST(CameraReconAnchor, TerrainHeightAddsToPosY) {
    CameraObject obj; obj.present = true;
    CameraState st;
    CameraHooks h = Camera_DefaultHooks();
    static f32 th = 100.0f;
    h.terrainHeight = [](i32, i32, const f32*, void*) -> f32 { return 100.0f; };
    (void)th;
    Camera_AnchorToTerrain(obj, st, h, 5, 6, 0);
    CHECK_EQ(obj.posY, 550.0f); // 100 + 450
}

// ---------------------------------------------------------------------------
// ClampToTerrainHeight (0x4b2a0c)
// ---------------------------------------------------------------------------
TEST(CameraReconClamp, NullObjectIsNoOp) {
    CameraObject obj; obj.present = false;
    CameraState st;
    CameraInput in;
    CameraHooks h = Camera_DefaultHooks();
    Camera_ClampToTerrainHeight(obj, st, in, h, 0);
    CHECK(!obj.present);
}

TEST(CameraReconClamp, DeadzoneClearsResultAndDoesNotMove) {
    // dy = baseH + (spanH-baseH)*zoomT + terrain - (posY + pos2Y)
    // Choose posY so dy is within +/-5.0 -> no move, clampResultBits = 0.
    CameraObject obj; obj.present = true;
    obj.posY = 450.0f;     // dy = 450 + 0 + 0 - (450 + 0) = 0
    CameraState st;        // zoomT default 0
    CameraInput in;
    CameraHooks h = Camera_DefaultHooks();
    st.clampResultBits = 0x7777;
    Camera_ClampToTerrainHeight(obj, st, in, h, 0);
    CHECK_EQ(st.clampResultBits, 0);
    CHECK_EQ(obj.posY, 450.0f); // unchanged
}

TEST(CameraReconClamp, PositiveStepSaturatesToTen) {
    // Large positive dy -> step = dy*0.06666667 saturates to +10.0f.
    CameraObject obj; obj.present = true;
    obj.posY = 0.0f;       // dy = 450 (big positive)
    CameraState st;
    CameraInput in;
    CameraHooks h = Camera_DefaultHooks();
    Camera_ClampToTerrainHeight(obj, st, in, h, 0);
    // posY moves by +10 (saturated), committed via setPosition
    CHECK_EQ(obj.posY, 10.0f);
    f32 stepApplied; std::memcpy(&stepApplied, &st.clampResultBits, sizeof(f32));
    CHECK_EQ(stepApplied, 10.0f);
}

TEST(CameraReconClamp, NegativeStepSaturatesToMinusTen) {
    CameraObject obj; obj.present = true;
    obj.posY = 1000.0f;    // dy = 450 - 1000 = -550 (big negative)
    CameraState st;
    CameraInput in;
    CameraHooks h = Camera_DefaultHooks();
    Camera_ClampToTerrainHeight(obj, st, in, h, 0);
    CHECK_EQ(obj.posY, 990.0f); // 1000 + (-10)
    f32 stepApplied; std::memcpy(&stepApplied, &st.clampResultBits, sizeof(f32));
    CHECK_EQ(stepApplied, -10.0f);
}

TEST(CameraReconClamp, SmallStepNotSaturatedAndApplied) {
    // dy = 8 -> step = 8 * 0.06666667 = 0.5333.. (within +/-10) applied as-is.
    CameraObject obj; obj.present = true;
    obj.posY = 442.0f;     // dy = 450 - 442 = 8
    CameraState st;
    CameraInput in;
    CameraHooks h = Camera_DefaultHooks();
    Camera_ClampToTerrainHeight(obj, st, in, h, 0);
    f32 expectStep = (f32)(8.0 * 0.06666667014360428f);
    CHECK(std::fabs(obj.posY - (442.0f + expectStep)) < 1e-4f);
}

TEST(CameraReconClamp, AltModeDoesNotCallSetPosition) {
    // altMoveMode set: the function reads the secondary triple and DOES NOT
    // commit via SetPosition, so the primary posY is unchanged.
    CameraObject obj; obj.present = true;
    obj.posY = 0.0f;       // big positive dy -> would move if committed
    obj.pos2Y = 0.0f;
    CameraState st; st.altMoveMode = 1;
    CameraInput in;
    CameraHooks h = Camera_DefaultHooks();
    Camera_ClampToTerrainHeight(obj, st, in, h, 0);
    CHECK_EQ(obj.posY, 0.0f);  // not committed
}

// ---------------------------------------------------------------------------
// ComputeZoomScale (0x4c20ec)
// ---------------------------------------------------------------------------
TEST(CameraReconZoomScale, HighBandReturnsTruncated16000WhenUnderTruncMax) {
    // ratio = wealth*500/area. Pick ratio >= 1600 AND ratio > 16000 so the
    // first OR is false on both sides -> high band -> 16000.
    ComputeZoomScaleCtx ctx;
    ctx.area = 1;
    ctx.wealth = 100;       // 100*500/1 = 50000 (>1600 and >16000)
    ctx.truncMax = 100000;  // > 16000 -> not capped to 0
    i32 r = Camera_ComputeZoomScale(ctx);
    CHECK_EQ(r, 16000);
}

TEST(CameraReconZoomScale, HighBandCappedToZeroWhenTruncMaxLE) {
    ComputeZoomScaleCtx ctx;
    ctx.area = 1;
    ctx.wealth = 100;       // -> 16000
    ctx.truncMax = 16000;   // truncMax <= 16000 -> return 0
    i32 r = Camera_ComputeZoomScale(ctx);
    CHECK_EQ(r, 0);
}

TEST(CameraReconZoomScale, LowBandReturns1600) {
    // ratio < 1600 -> first OR true -> inner: ratio3 < 1600 -> low band 1600.
    ComputeZoomScaleCtx ctx;
    ctx.area = 1000;
    ctx.wealth = 1;         // 1*500/1000 = 0.5 (<1600)
    ctx.truncMax = 100000;
    i32 r = Camera_ComputeZoomScale(ctx);
    CHECK_EQ(r, 1600);
}

TEST(CameraReconZoomScale, MidBandReturnsRatio) {
    // 1600 <= ratio <= 16000 -> first OR true via the <=16000 side,
    // inner ratio3 >= 1600 -> mid band = ratio.
    ComputeZoomScaleCtx ctx;
    ctx.area = 1;
    ctx.wealth = 10;        // 10*500/1 = 5000
    ctx.truncMax = 100000;
    i32 r = Camera_ComputeZoomScale(ctx);
    CHECK_EQ(r, 5000);
}
