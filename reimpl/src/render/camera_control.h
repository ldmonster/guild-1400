#pragma once
#include "guild/common/types.h"
#include "util/matrix.h"

// =============================================================================
// guild::render — camera control: the view/frustum build from the camera's
// orientation, plus the orbit/zoom rate math that maps mouse deltas to camera
// rotation/zoom amounts.
//
// Faithful 1:1 reconstruction of:
//   0x5ACCD0  VIBE_Render_BuildViewMatrix          (build the 6 view-frustum
//                                                   planes from camera yaw/pitch,
//                                                   plus the 64-entry AABB-corner
//                                                   plane-selection table)
//   0x5E9024  VIBE_Camera_UpdateOrbitFromMouse     (the orbit/zoom RATE math —
//                                                   only the self-contained
//                                                   delta->rotation arithmetic is
//                                                   reconstructed here; the input/
//                                                   scene plumbing is out of scope,
//                                                   see report)
//
// THE CAMERA VIEW STATE (the globals BuildViewMatrix reads)
// -----------------------------------------------------------------------------
//   flt_13FC518 / flt_13FCD0C : the camera look-direction Z / X components, fed
//                               to atan2 to recover the yaw angle theta.
//   flt_13FC76C               : near-plane distance (-> near plane offset)
//   flt_13FCAFC               : far-plane distance  (-> far plane offset, negated)
// The six output planes (gilde.exe flt_13DCDA0..flt_13DCDFC):
//   side0 = ( cos T, 0,  sin T,  +eps )      eps = 1.9999999e-06 (~0)
//   side1 = (-cos T, 0,  sin T,  +eps )
//   side2 = ( 0,  cos P,  sin P,  +eps )      P = atan2(cos T, sin T)
//   side3 = ( 0, -cos P,  sin P,  +eps )
//   near  = ( 0, 0, 1,  nearZ )               (w = flt_13FC76C)
//   far   = ( 0, 0,-1, -farZ  )               (w = -flt_13FCAFC)
// (A point is outside a plane when a*x + b*y + c*z < w.)
// =============================================================================
namespace guild::render {

// The eps offset on the four side planes (gilde.exe ±1.9999999949504854e-06).
constexpr float kViewPlaneEps = 1.9999999949504854e-06f;

// One frustum plane: outside when a*x + b*y + c*z < w.
struct ViewPlane {
    float a, b, c, w;
};

// The camera orientation/depth inputs BuildViewMatrix reads from globals.
struct CameraViewInput {
    float lookZ;   // flt_13FC518  (look-direction z, atan2 numerator)
    float lookX;   // flt_13FCD0C  (look-direction x, atan2 denominator)
    float nearZ;   // flt_13FC76C  near-plane distance
    float farZ;    // flt_13FCAFC  far-plane distance
};

// The six view-frustum planes plus the recovered yaw/pitch angles.
struct CameraViewFrustum {
    ViewPlane side[4];  // 0..3 the four oblique side planes
    ViewPlane nearP;    // near plane (+z)
    ViewPlane farP;     // far plane (-z)
    float theta;        // yaw = atan2(lookZ, lookX)  (gilde.exe dword_13FC538)
    float phi;          // atan2(cos theta, sin theta) (gilde.exe dword_13FC530)
};

// gilde.exe 0x5ACCD0 — VIBE_Render_BuildViewMatrix (frustum-build portion).
//   theta = atan2(lookZ, lookX); phi = atan2(cos theta, sin theta);
//   builds the six planes above. Returns the frustum by value. (The original also
//   wrote a 64-entry AABB-corner plane table; see BuildBoundingBoxPlaneTable.)
CameraViewFrustum BuildViewFrustum(const CameraViewInput& in);

// One entry of the 64-entry AABB plane-selection table (gilde.exe stride 26
// dwords = 104 bytes; indexed by a 6-bit outcode). `count` = popcount(outcode),
// then up to 6 selected planes (a,b,c,w) packed contiguously at +8.
struct BBoxPlaneEntry {
    i32       count;       // +0x00  number of set outcode bits (planes selected)
    u8        pad;         // +0x04  (the original writes 0 here)
    ViewPlane planes[6];   // +0x08  the selected planes, `count` of them valid
};

// gilde.exe 0x5ACCD0 (table-build portion). For each outcode 0..63, selects the
// planes whose bit is set (bit0->side0, bit1->side1, bit2->side2, bit3->side3,
// bit4->near, bit5->far) into a contiguous list, and records the popcount. Fills
// `table[64]`. This is the per-frame bounding-box culling acceleration table.
void BuildBoundingBoxPlaneTable(const CameraViewFrustum& f, BBoxPlaneEntry table[64]);

// ---------------------------------------------------------------------------
// Orbit / zoom RATE math (from VIBE_Camera_UpdateOrbitFromMouse @0x5E9024).
// The original reads mouse pixel deltas and the screen size from globals; the
// arithmetic that turns them into camera rotation/zoom amounts is the portable
// part and is reconstructed here verbatim (constants flt_62BF10..flt_62BF28).
// ---------------------------------------------------------------------------

// Tunable orbit/zoom rate constants (gilde.exe flt_62BF1x..2x). Defaults are
// placeholders the caller fills from the recovered globals; the formulas are the
// faithful part.
struct OrbitRateConfig {
    float panSens;     // flt_62BF10  pan sensitivity   (delta/extent -> pan)
    float accelScale;  // flt_62BF14  acceleration scale ((|d|+1)*scale/extent)
    float accelClamp;  // flt_62BF18  acceleration clamp ceiling (else -> 3.0)
    float rateX;       // flt_62BF1C  yaw rate multiplier
    float rateY;       // flt_62BF20  pitch rate multiplier
};

// Result of the orbit-rate computation for one mouse-delta frame.
struct OrbitRates {
    float pan;     // straight pan amount   = (dx/width)  * panSens
    float panY;    // straight pan amount Y = (dy/height) * panSens
    float yaw;     // accelerated yaw       = (dx/width)  * rateX * clamp(dx)
    float pitch;   // accelerated pitch     = (dy/height) * rateY * clamp(dy)
};

// gilde.exe 0x5E9024 — the orbit/zoom delta arithmetic.
//   straight pan:  pan  = (dx / width)  * panSens
//                  panY = (dy / height) * panSens
//   accelerated:   cx   = (|dx| + 1) * accelScale / width
//                  ax   = (cx <= accelClamp) ? cx : 3.0
//                  yaw  = (dx / width) * rateX * ax
//                  (pitch symmetric with dy / height / rateY)
// `width`/`height` are the viewport pixel extents (dword_7626E0 / cy).
OrbitRates ComputeOrbitRates(int dx, int dy, int width, int height,
                             const OrbitRateConfig& cfg);

} // namespace guild::render
