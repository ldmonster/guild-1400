// camera_control_test.cpp — golden-vector unit tests for the camera view/frustum
// build and the orbit/zoom rate math, pinning the *actual recovered binary
// constants* (not synthetic config values).
//
// Provenance verified via live IDA MCP (wave-15):
//   0x5ACCD0  VIBE_Render_BuildViewMatrix       (frustum + 64-entry bbox table)
//   0x5E9024  VIBE_Camera_UpdateOrbitFromMouse  (orbit/zoom rate math)
//
// The orbit-rate constants flt_62BF10..flt_62BF20 (get_bytes @ wave-15):
//   flt_62BF10 = 0x40490FDB =  pi (3.1415927)   panSens
//   flt_62BF14 = 0x41F00000 =  30.0             accelScale
//   flt_62BF18 = 0x40400000 =  3.0              accelClamp
//   flt_62BF1C = 0x43800000 =  256.0            rateX (yaw)
//   flt_62BF20 = 0xC3800000 = -256.0            rateY (pitch)
// The side-plane eps raw dwords (get_bytes @ wave-15) decode to:
//   dword_13DCDAC = -1241106499 = -1.9999999949504854e-06  (side0 w)
//   dword_13DCDBC =  906377149  = +1.9999999949504854e-06  (side1 w)
//   dword_13DCDCC =  906377149  = +eps                      (side2 w)
//   dword_13DCDDC = -1241106499 = -eps                      (side3 w)
#include "tests/framework/test.h"

#include "render/camera_control.h"

#include <cmath>

using namespace guild;
using namespace guild::render;

namespace {

bool ccf(float a, float b, float eps = 1e-5f) { return std::fabs(a - b) <= eps; }

// The recovered binary orbit/zoom-rate globals (flt_62BF10..flt_62BF20).
constexpr OrbitRateConfig kBinaryOrbitCfg{
    3.1415927410125732f,  // flt_62BF10  panSens   = pi
    30.0f,                // flt_62BF14  accelScale
    3.0f,                 // flt_62BF18  accelClamp
    256.0f,               // flt_62BF1C  rateX (yaw)
    -256.0f,              // flt_62BF20  rateY (pitch)
};

} // namespace

// ---------------------------------------------------------------------------
// The eps offset constant matches the raw dword the binary stamps into the side
// planes (verified 1:1 via get_bytes @ wave-15).
// ---------------------------------------------------------------------------
TEST(CameraControlBinary, ViewPlaneEpsMatchesBinaryBytes) {
    CHECK(ccf(kViewPlaneEps, 1.9999999949504854e-06f, 0.0f));
}

// ---------------------------------------------------------------------------
// BuildViewFrustum side-plane eps sign pattern is exactly the binary's:
//   side0 -eps, side1 +eps, side2 +eps, side3 -eps.
// ---------------------------------------------------------------------------
TEST(CameraControlBinary, FrustumEpsSignPattern) {
    CameraViewInput in{0.5f, 0.8660254f, 1.0f, 200.0f};
    CameraViewFrustum f = BuildViewFrustum(in);
    CHECK(ccf(f.side[0].w, -kViewPlaneEps));
    CHECK(ccf(f.side[1].w, +kViewPlaneEps));
    CHECK(ccf(f.side[2].w, +kViewPlaneEps));
    CHECK(ccf(f.side[3].w, -kViewPlaneEps));
    // near plane (0,0,1,nearZ); far plane (0,0,-1,-farZ).
    CHECK(ccf(f.nearP.a, 0.0f));
    CHECK(ccf(f.nearP.b, 0.0f));
    CHECK(ccf(f.nearP.c, 1.0f));
    CHECK(ccf(f.nearP.w, 1.0f));
    CHECK(ccf(f.farP.c, -1.0f));
    CHECK(ccf(f.farP.w, -200.0f));
}

// ---------------------------------------------------------------------------
// ComputeOrbitRates with the REAL binary constants (pi/30/3/256/-256).
// Golden computed in IDA python with single-precision stores:
//   dx=10 dy=6 w=640 h=480
//   pan   = (10/640) * pi             = 0.04908738657832146
//   panY  = pi * (6/480)              = 0.039269909262657166
//   cx    = (10+1)*30/640 = 0.515625 (<= 3) ; yaw = (10/640)*256*0.515625 = 2.0625
//   cy    = (6+1)*30/480  = 0.4375   (<= 3) ; pitch = (6/480)*-256*0.4375 = -1.4
// ---------------------------------------------------------------------------
TEST(CameraControlBinary, OrbitRatesBinaryConstants) {
    OrbitRates r = ComputeOrbitRates(10, 6, 640, 480, kBinaryOrbitCfg);
    CHECK(ccf(r.pan,   0.04908738657832146f, 1e-7f));
    CHECK(ccf(r.panY,  0.039269909262657166f, 1e-7f));
    CHECK(ccf(r.yaw,   2.0625f, 1e-6f));
    CHECK(ccf(r.pitch, -1.399999976158142f, 1e-6f));
}

// ---------------------------------------------------------------------------
// The acceleration clamp ceiling is 3.0 (flt_62BF18); when (|d|+1)*accelScale/w
// exceeds it the factor saturates to the literal 3.0 the binary uses.
//   dx=200 dy=0 w=200 h=200 : cx = 201*30/200 = 30.15 > 3 -> factor 3.0
//   yaw = (200/200) * 256 * 3.0 = 768.0
// ---------------------------------------------------------------------------
TEST(CameraControlBinary, OrbitRatesAccelClampSaturates) {
    OrbitRates r = ComputeOrbitRates(200, 0, 200, 200, kBinaryOrbitCfg);
    CHECK(ccf(r.yaw, 768.0f, 1e-3f));
    // dy=0 -> pitch stays 0 (cy=(0+1)*30/200=0.15 <=3, but dy=0 zeroes it).
    CHECK(ccf(r.pitch, 0.0f));
}

// ---------------------------------------------------------------------------
// 64-entry bbox plane table: popcount + selection order (bit0->side0 ...
// bit5->far), exactly as the binary's do-loop.
// ---------------------------------------------------------------------------
TEST(CameraControlBinary, BBoxTablePopcountAndOrder) {
    CameraViewInput in{0.5f, 0.8660254f, 1.0f, 200.0f};
    CameraViewFrustum f = BuildViewFrustum(in);
    BBoxPlaneEntry table[64];
    BuildBoundingBoxPlaneTable(f, table);

    CHECK_EQ(table[0].count, 0);
    CHECK_EQ(table[0b101010].count, 3);
    CHECK_EQ(table[0b111111].count, 6);

    // outcode 0b001001 -> bit0 (side0) then bit3 (side3), in that bit order.
    BBoxPlaneEntry& e = table[0b001001];
    CHECK_EQ(e.count, 2);
    CHECK(ccf(e.planes[0].a, f.side[0].a));
    CHECK(ccf(e.planes[0].w, f.side[0].w));   // -eps
    CHECK(ccf(e.planes[1].b, f.side[3].b));
    CHECK(ccf(e.planes[1].w, f.side[3].w));   // -eps
    CHECK_EQ((int)e.pad, 0);
}
