#include "test.h"
#include "render/camera_recon5_flight.h"

using namespace guild;
using namespace guild::render;

// ===========================================================================
// Camera_Flight: DrawTextLabels3D(a1/17, 2, a2); always returns 1.
// ===========================================================================
TEST(GameLogicRecon5, CameraFlight_DividesBy17AndReturns1) {
    CameraFlightCtx ctx{};
    ctx.drawOk = true;
    i32 r = Camera_Flight(170, 99, ctx);
    CHECK_EQ(r, 1);
    CHECK_EQ(ctx.drawArg0, 10);   // 170 / 17
    CHECK_EQ(ctx.drawArg2, 99);
    CHECK(ctx.drawCalled);
    CHECK(!ctx.reportedError);
}

TEST(GameLogicRecon5, CameraFlight_TruncatesDivision) {
    CameraFlightCtx ctx{};
    ctx.drawOk = true;
    Camera_Flight(35, 0, ctx);    // 35 / 17 = 2 (truncates)
    CHECK_EQ(ctx.drawArg0, 2);
    // negative numerator truncates toward zero (matches x86 idiv).
    CameraFlightCtx ctx2{}; ctx2.drawOk = true;
    Camera_Flight(-35, 0, ctx2);
    CHECK_EQ(ctx2.drawArg0, -2);
}

TEST(GameLogicRecon5, CameraFlight_ReportsErrorOnDrawFailureButStillReturns1) {
    CameraFlightCtx ctx{};
    ctx.drawOk = false;
    i32 r = Camera_Flight(34, 7, ctx);
    CHECK_EQ(r, 1);               // invariant return value
    CHECK(ctx.reportedError);
}
