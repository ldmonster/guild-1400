#include "render/camera_control.h"

#include "util/math.h"  // guild::util::Atan2

#include <cmath>

namespace guild::render {

// gilde.exe 0x5ACCD0 — VIBE_Render_BuildViewMatrix (frustum-build portion).
//   v33 = Atan2(flt_13FC518, flt_13FCD0C);            ; theta (yaw)
//   v0  = sin(theta);  v1 = cos(theta);
//   v32 = Atan2(v1, v0);                               ; phi (= atan2(cos,sin))
//   v2  = cos(phi);
// Then the six planes are stamped (see header for the layout). The eps offsets
// alternate sign exactly as the binary does (dword_13DCDAC = -eps, ..DBC = +eps,
// ..CCC = +eps, ..DDC = -eps).
CameraViewFrustum BuildViewFrustum(const CameraViewInput& in) {
    CameraViewFrustum f;

    const double theta = guild::util::Atan2(in.lookZ, in.lookX);
    const double s0 = std::sin(theta);  // v0
    const double c0 = std::cos(theta);  // v1
    const double phi = guild::util::Atan2(c0, s0);
    const double c2 = std::cos(phi);    // v2 = cos(phi)
    const double s2 = std::sin(phi);    // flt_13DCDC8 = sin(phi)

    f.theta = static_cast<float>(theta);
    f.phi   = static_cast<float>(phi);

    // side0: ( cos T, 0,  sin T, -eps )
    f.side[0] = ViewPlane{ static_cast<float>(c0), 0.0f, static_cast<float>(s0), -kViewPlaneEps };
    // side1: (-cos T, 0,  sin T, +eps )
    f.side[1] = ViewPlane{ -static_cast<float>(c0), 0.0f, static_cast<float>(s0), +kViewPlaneEps };
    // side2: ( 0,  cos P,  sin P, +eps )
    f.side[2] = ViewPlane{ 0.0f, static_cast<float>(c2), static_cast<float>(s2), +kViewPlaneEps };
    // side3: ( 0, -cos P,  sin P, -eps )
    f.side[3] = ViewPlane{ 0.0f, -static_cast<float>(c2), static_cast<float>(s2), -kViewPlaneEps };

    // near: ( 0, 0,  1,  nearZ )   far: ( 0, 0, -1, -farZ )
    f.nearP = ViewPlane{ 0.0f, 0.0f,  1.0f,  in.nearZ };
    f.farP  = ViewPlane{ 0.0f, 0.0f, -1.0f, -in.farZ };
    return f;
}

// gilde.exe 0x5ACCD0 — the 64-entry AABB-corner plane-selection table build.
//   for (outcode = 0; outcode < 0x40; ++outcode):
//     count = popcount(outcode); entry.count = count; entry.pad = 0;
//     if (bit0) entry.planes[k++] = side0;
//     if (bit1) entry.planes[k++] = side1;
//     ... bit2 -> side2, bit3 -> side3, bit4 -> near, bit5 -> far.
void BuildBoundingBoxPlaneTable(const CameraViewFrustum& f, BBoxPlaneEntry table[64]) {
    const ViewPlane* sel[6] = {
        &f.side[0], &f.side[1], &f.side[2], &f.side[3], &f.nearP, &f.farP
    };
    for (u32 outcode = 0; outcode < 0x40u; ++outcode) {
        // popcount of the low 6 bits.
        int count = 0;
        for (u32 t = outcode; t; t >>= 1)
            if (t & 1) ++count;

        BBoxPlaneEntry& e = table[outcode];
        e.count = count;
        e.pad = 0;
        int k = 0;
        for (int bit = 0; bit < 6; ++bit) {
            if (outcode & (1u << bit))
                e.planes[k++] = *sel[bit];
        }
    }
}

// gilde.exe 0x5E9024 — orbit/zoom rate arithmetic.
OrbitRates ComputeOrbitRates(int dx, int dy, int width, int height,
                             const OrbitRateConfig& cfg) {
    OrbitRates r{0.0f, 0.0f, 0.0f, 0.0f};

    const double invW = 1.0 / static_cast<double>(width);
    const double invH = 1.0 / static_cast<double>(height);

    // Straight pan: pan = (dx/width)*panSens ; panY = (dy/height)*panSens.
    //   (v30 = (double)dx / width * flt_62BF10 ; v24 = flt_62BF10 * (dy/height).)
    r.pan  = static_cast<float>(static_cast<double>(dx) * invW * cfg.panSens);
    r.panY = static_cast<float>(cfg.panSens * (static_cast<double>(dy) * invH));

    // Accelerated yaw: cx = (|dx|+1)*accelScale/width ; ax = cx<=clamp ? cx : 3.0 ;
    //   yaw = (dx*invW) * rateX * ax.
    const double vx = static_cast<double>(dx) * invW;            // v22
    double cx = (std::fabs(static_cast<double>(dx)) + 1.0) * cfg.accelScale * invW; // v29
    double ax = (static_cast<double>(cfg.accelClamp) >= cx) ? cx : 3.0;             // v28
    r.yaw = static_cast<float>(vx * cfg.rateX * ax);             // v26

    // Accelerated pitch: symmetric in dy / height / rateY.
    const double vy = static_cast<double>(dy) * invH;            // v21
    double cy = (std::fabs(static_cast<double>(dy)) + 1.0) * cfg.accelScale * invH; // v25
    double ay = (static_cast<double>(cfg.accelClamp) >= cy) ? cy : 3.0;             // v27
    r.pitch = static_cast<float>(vy * cfg.rateY * ay);          // v31

    return r;
}

} // namespace guild::render
