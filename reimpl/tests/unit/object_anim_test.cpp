// Golden vectors for render::object_anim — the Catmull-Rom tangent build
// (VIBE_Anim_ComputeFrameTangents @0x5ccf70) + Hermite sample that drives the
// CameraFlightEnhanced camera motion.
#include "render/object_anim.h"
#include "tests/framework/test.h"

#include <cmath>
#include <vector>

using namespace guild::render;

namespace {
bool Near(float a, float b, float e = 1e-3f) { return std::fabs(a - b) <= e; }
ObjAnimFrame F(float dur, float px, float py, float pz, float rx, float ry, float rz) {
    ObjAnimFrame f; f.dur = dur;
    f.pos[0] = px; f.pos[1] = py; f.pos[2] = pz;
    f.rot[0] = rx; f.rot[1] = ry; f.rot[2] = rz;
    return f;
}
} // namespace

// A Hermite spline passes through its control points: sampling at each segment
// boundary returns the waypoint exactly (the M-tangents only shape the interior).
TEST(ObjectAnim, PassesThroughWaypoints) {
    std::vector<ObjAnimFrame> f = {
        F(750, 0, 0, 0,    0, 0, 0),       // start
        F(750, 100, 50, 0, 0, 90, 0),      // A1
        F(0,   200, 0, 80, 0, 180, 0),     // A2 (last; dur unused)
    };
    BuildFrameTangents(f);
    float p[3], r[3];
    SampleObjectAnim(f, 0.0f, p, r);
    CHECK(Near(p[0], 0) && Near(p[1], 0) && Near(p[2], 0));
    SampleObjectAnim(f, 750.0f, p, r);           // exactly at A1
    CHECK(Near(p[0], 100) && Near(p[1], 50) && Near(p[2], 0));
    CHECK(Near(r[1], 90));
    SampleObjectAnim(f, 1500.0f, p, r);          // exactly at A2 (end)
    CHECK(Near(p[0], 200) && Near(p[1], 0) && Near(p[2], 80));
    CHECK(Near(r[1], 180));
}

// Clamps outside [0,total] to the endpoints.
TEST(ObjectAnim, ClampsEnds) {
    std::vector<ObjAnimFrame> f = {F(100, 0, 0, 0, 0, 0, 0), F(0, 10, 0, 0, 0, 0, 0)};
    BuildFrameTangents(f);
    float p[3], r[3];
    SampleObjectAnim(f, -50.0f, p, r); CHECK(Near(p[0], 0));
    SampleObjectAnim(f, 999.0f, p, r); CHECK(Near(p[0], 10));
    SampleObjectAnim(f, 50.0f, p, r);  CHECK(Near(p[0], 5));   // linear (2-pt, zero tangents)
}

// Catmull-Rom tangent: interior frame's tangent = 0.5*(in-slope + out-slope),
// stored scaled by the segment durations. Endpoints zeroed.
TEST(ObjectAnim, TangentValues) {
    std::vector<ObjAnimFrame> f = {
        F(10, 0, 0, 0, 0, 0, 0),
        F(10, 10, 0, 0, 0, 0, 0),       // interior; symmetric -> tangent.x = 1.0
        F(0,  20, 0, 0, 0, 0, 0),
    };
    BuildFrameTangents(f);
    // tangent_1.x = 0.5*((10-0)/10 + (20-10)/10) = 0.5*(1+1) = 1.0.
    // frame[1].posOut.x (M0 of seg[1,2]) = tangent * dur_1 = 1.0*10 = 10.
    CHECK(Near(f[1].posOut[0], 10.0f));
    // frame[0].posIn.x (M1 of seg[0,1]) = tangent_1 * dur_0 = 1.0*10 = 10.
    CHECK(Near(f[0].posIn[0], 10.0f));
    // boundaries zeroed: frame0 outTan, frame2 inTan.
    CHECK(Near(f[0].posOut[0], 0.0f));
    CHECK(Near(f[2].posIn[0], 0.0f));
}

// Monotone, smooth motion through the midpoint of a segment (no overshoot for a
// straight-line equal-spaced path).
TEST(ObjectAnim, SmoothMidpoint) {
    std::vector<ObjAnimFrame> f = {
        F(100, 0, 0, 0, 0, 0, 0),
        F(100, 100, 0, 0, 0, 0, 0),
        F(0,   200, 0, 0, 0, 0, 0),
    };
    BuildFrameTangents(f);
    float p[3], r[3];
    SampleObjectAnim(f, 50.0f, p, r);     // mid of segment 0
    CHECK(p[0] > 0.0f && p[0] < 100.0f);
    SampleObjectAnim(f, 150.0f, p, r);    // mid of segment 1
    CHECK(p[0] > 100.0f && p[0] < 200.0f);
}
