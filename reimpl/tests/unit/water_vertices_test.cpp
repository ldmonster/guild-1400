// Unit tests for render/water_vertices (VIBE_Floor_AnimateWaterVertices driver),
// render/floorwater FindRegionOffset, and render/mirror_silhouette. Golden vectors
// computed with python3 (math.fmod / math.sin/cos as the x87 oracle).
#include "test.h"

#include "render/water_vertices.h"
#include "render/floorwater.h"
#include "render/mirror_silhouette.h"

#include <cmath>
#include <cstring>

using namespace guild;

namespace {

constexpr double kTwoPi = 6.283185307179586;

bool nearF(float a, float b, float eps = 1e-4f) {
    return std::fabs(a - b) <= eps * (1.0f + std::fabs(b));
}

// Test FindGroupMember: returns a deterministic encoding so the driver's group/frame
// selection is observable. ctx unused.
u32 TestFindMember(i32 groupId, u8 frameByte, void* /*ctx*/) {
    return (u32)((groupId << 8) | frameByte);
}

} // namespace

// ---------------------------------------------------------------------------
// PropagatePhases: phaseOut[k] = Fmod(speed[k]*dt + phase[k], 2π).
// python: speed=[0.5,1,1.5,2], phase=[0.1,0.2,0.3,0.4], dt=3.0 ->
//   1.6, 3.2, 4.8, 0.1168147
// ---------------------------------------------------------------------------
TEST(WaterVerticesUnit, PropagatePhasesGolden) {
    float speed[4] = {0.5f, 1.0f, 1.5f, 2.0f};
    float phase[4] = {0.1f, 0.2f, 0.3f, 0.4f};
    float out[4] = {0, 0, 0, 0};
    render::PropagatePhases(speed, phase, 3.0, out);
    CHECK(nearF(out[0], 1.6f));
    CHECK(nearF(out[1], 3.2f));
    CHECK(nearF(out[2], 4.8f));
    CHECK(nearF(out[3], 0.1168147f));   // 6.4 mod 2π
}

// ---------------------------------------------------------------------------
// Wave grid reuse: AnimateWaterWaveGrid with amp=[1,2,3,4],phase=[.1,.2,.3,.4],t=2π.
// python golden for c=0,1,5,15 (x,y,z,w).
// ---------------------------------------------------------------------------
TEST(WaterVerticesUnit, WaveGridGolden) {
    float amp[4] = {1.0f, 2.0f, 3.0f, 4.0f};
    float phase[4] = {0.1f, 0.2f, 0.3f, 0.4f};
    float out[64];
    render::AnimateWaterWaveGrid(out, amp, phase, kTwoPi);
    CHECK(nearF(out[0], 0.09983341f));   // c=0 x
    CHECK(nearF(out[1], 1.960133f));     // c=0 y
    CHECK(nearF(out[2], 1.795416f));     // c=0 z
    CHECK(nearF(out[3], -1.229331f));    // c=0 w
    CHECK(nearF(out[4 * 1 + 0], 0.991309f));   // c=1 x
    CHECK(nearF(out[4 * 5 + 2], 2.828116f));   // c=5 z
    CHECK(nearF(out[4 * 15 + 3], 3.793196f));  // c=15 w
}

// ---------------------------------------------------------------------------
// Full driver: one mesh. dt>0 path runs texture, accumulators, phase prop, grid.
//   texRateA=0.3, texAccumA=0.8, dt=3 -> accumA=0.7, accumB=0.3.
//   phase prop overlap: phase[0..2]<-prop[1..3], phase[3] untouched.
//     speed=[0.5,1,1.5,2], phase0=[0.1,0.2,0.3,0.4]:
//     prop=[1.6,3.2,4.8,0.1168147]; phase -> [3.2,4.8,0.1168147,0.4(unchanged)].
// ---------------------------------------------------------------------------
TEST(WaterVerticesUnit, DriverDtPositive) {
    render::WaterMesh m;
    std::memset(&m, 0, sizeof(m));
    m.hasTexture = true;
    m.groupId = 7;
    m.texMemberCount = 4;
    m.texSpeedNibble = 1;        // selector 1 -> speed table divisor 15
    m.activeMember = 0;
    m.texRateA = 0.3f;
    m.texAccumA = 0.8f;
    m.texRateB = 0.0f;
    for (int k = 0; k < 4; ++k) {
        m.waveSpeed[k] = 0.5f + 0.5f * k;
        m.amp[k] = 1.0f + k;
        m.phase[k] = 0.1f + 0.1f * k;
    }
    m.lastTime = 0;

    render::AnimateWaterVertices(&m, 1, 3, TestFindMember, nullptr);

    // texture: frame = (3 / 15) % 4 = 0; member = (7<<8)|0 = 0x700.
    CHECK_EQ(m.activeMember, (u32)0x700);
    // accumulators:
    CHECK(nearF(m.texAccumA, 0.7f));
    CHECK(nearF(m.texAccumB, 0.3f));
    // phase propagation overlap:
    CHECK(nearF(m.phase[0], 3.2f));
    CHECK(nearF(m.phase[1], 4.8f));
    CHECK(nearF(m.phase[2], 0.1168147f));
    CHECK(nearF(m.phase[3], 0.4f));   // untouched (+0x144 never written)
    // wave grid was filled (c=0 x uses the propagated phase[0]=3.2):
    float expX = std::sin(std::cos(0.0) * kTwoPi + 0.0 + 3.2);
    CHECK(nearF(m.waveOut[0], (float)expX));
    // latch:
    CHECK_EQ(m.lastTime, 3);
}

// dt<=0: only texture advance runs; grid/accum/latch untouched.
TEST(WaterVerticesUnit, DriverDtZeroSkipsWave) {
    render::WaterMesh m;
    std::memset(&m, 0, sizeof(m));
    m.hasTexture = true;
    m.groupId = 2;
    m.texMemberCount = 5;
    m.texSpeedNibble = 2;          // divisor 13
    m.texAccumA = 0.55f;
    m.phase[0] = 1.23f;
    m.lastTime = 100;              // time==100 -> dt==0

    render::AnimateWaterVertices(&m, 1, 100, TestFindMember, nullptr);

    // texture still advances: frame = (100 / 13) % 5 = 7 % 5 = 2; member=(2<<8)|2.
    CHECK_EQ(m.activeMember, (u32)0x202);
    // wave half skipped:
    CHECK(nearF(m.texAccumA, 0.55f));
    CHECK(nearF(m.phase[0], 1.23f));
    CHECK_EQ(m.lastTime, 100);     // latch unchanged
    CHECK(nearF(m.waveOut[0], 0.0f)); // never written
}

// No texture / zero member count / zero speed selector: texture advance skipped.
TEST(WaterVerticesUnit, DriverNoTextureAdvance) {
    render::WaterMesh m;
    std::memset(&m, 0, sizeof(m));
    m.hasTexture = false;          // gate 1
    m.activeMember = 99;
    m.lastTime = 0;
    render::AnimateWaterVertices(&m, 1, 5, TestFindMember, nullptr);
    CHECK_EQ(m.activeMember, (u32)99);   // untouched

    m.hasTexture = true; m.texMemberCount = 0; m.activeMember = 99;
    render::AnimateWaterVertices(&m, 1, 5, TestFindMember, nullptr);
    CHECK_EQ(m.activeMember, (u32)99);   // memberCount==0 skips

    m.texMemberCount = 3; m.texSpeedNibble = 0; m.activeMember = 99;
    render::AnimateWaterVertices(&m, 1, 5, TestFindMember, nullptr);
    CHECK_EQ(m.activeMember, (u32)99);   // selector 0 skips
}

// ---------------------------------------------------------------------------
// FindRegionOffset golden (python):
//   spans: [base0 type2 lo10 hi20 m255][base5 type2 lo21 hi30 m255][term type-1]
//   q=15,type=2 -> 80*(0+15-10)+1000 = 1400 (span0, marker stamped)
//   q=25,type=2 -> 80*(5+25-21)+1000 = 1720 (span1)
//   q=5 ,type=2 -> no span contains -> walk to terminator -> 0
//   type3 (no match) -> 0
// ---------------------------------------------------------------------------
TEST(FloorWaterUnit, FindRegionOffsetGolden) {
    auto mk = [](i32 base, i32 type, i32 lo, i32 hi, u8 marker) {
        render::WaterRegionSpan s{};
        s.base = base; s.type = type; s.lo = lo; s.hi = hi; s.marker = marker;
        return s;
    };
    render::WaterRegionSpan spans[3] = {
        mk(0, 2, 10, 20, 0xFF),
        mk(5, 2, 21, 30, 0xFF),
        mk(0, -1, 0, 0, 0xFF),     // terminator (type < 0)
    };
    CHECK_EQ(render::FindRegionOffset(spans, 3, 1000, 15, 0x42, 2), 1400);
    CHECK_EQ(spans[0].marker, (u8)0x42);     // stamped
    // second hit on an already-marked span keeps the prior marker:
    CHECK_EQ(render::FindRegionOffset(spans, 3, 1000, 18, 0x99, 2), 1640);
    CHECK_EQ(spans[0].marker, (u8)0x42);
    // span1:
    CHECK_EQ(render::FindRegionOffset(spans, 3, 1000, 25, 0x55, 2), 1720);
    // out of all spans -> 0:
    CHECK_EQ(render::FindRegionOffset(spans, 3, 1000, 5, 0x55, 2), 0);
    // wrong type -> 0:
    CHECK_EQ(render::FindRegionOffset(spans, 3, 1000, 15, 0x55, 3), 0);
    // empty list -> 0:
    CHECK_EQ(render::FindRegionOffset(spans, 0, 1000, 15, 0x55, 2), 0);
    // null -> 0:
    CHECK_EQ(render::FindRegionOffset(nullptr, 3, 1000, 15, 0x55, 2), 0);
    // first span type<0 (no regions) -> 0:
    render::WaterRegionSpan none[1] = { mk(0, -1, 0, 0, 0xFF) };
    CHECK_EQ(render::FindRegionOffset(none, 1, 1000, 15, 0x55, 2), 0);
}

// ---------------------------------------------------------------------------
// BuildSilhouettePoints golden (python): interior point + apex -> only the 3
// coplanar outer edges are silhouettes. pts = {(2,0,0),(-1,2,0),(-1,-2,0),
// (0,0,3),(0.1,0.1,0.1)} -> edges (0,1),(1,2),(2,0); outCount=6.
// ---------------------------------------------------------------------------
TEST(MirrorSilhouetteUnit, BuildSilhouettePointsGolden) {
    float p0[3] = {2, 0, 0};
    float p1[3] = {-1, 2, 0};
    float p2[3] = {-1, -2, 0};
    float p3[3] = {0, 0, 3};        // apex (off-plane)
    float p4[3] = {0.1f, 0.1f, 0.1f}; // interior
    float* pts[5] = {p0, p1, p2, p3, p4};
    u8 conn[25] = {0};
    i32 outCount = -1;
    const float* outPairs[64] = {nullptr};

    bool ok = render::BuildSilhouettePoints(5, conn, &outCount, pts, outPairs);
    CHECK(ok);
    CHECK_EQ(outCount, 6);
    // edges (0,1),(1,2),(2,0) -> pairs: p0,p1, p1,p2, p2,p0.
    CHECK(outPairs[0] == p0 && outPairs[1] == p1);
    CHECK(outPairs[2] == p1 && outPairs[3] == p2);
    CHECK(outPairs[4] == p2 && outPairs[5] == p0);
    // connectivity stamped symmetric for taken edges:
    CHECK_EQ(conn[0 * 5 + 1], (u8)1);
    CHECK_EQ(conn[1 * 5 + 0], (u8)1);
    CHECK_EQ(conn[1 * 5 + 2], (u8)1);
    CHECK_EQ(conn[2 * 5 + 1], (u8)1);
    // edge to the apex/interior was NOT taken:
    CHECK_EQ(conn[0 * 5 + 3], (u8)0);
    CHECK_EQ(conn[0 * 5 + 4], (u8)0);
}

// All-coplanar square: every edge is a silhouette (all dots == 0 >= tol).
TEST(MirrorSilhouetteUnit, CoplanarAllEdges) {
    float p0[3] = {1, 0, 1};
    float p1[3] = {-1, 0, 1};
    float p2[3] = {-1, 0, -1};
    float p3[3] = {1, 0, -1};
    float* pts[4] = {p0, p1, p2, p3};
    u8 conn[16] = {0};
    i32 outCount = 0;
    const float* outPairs[64] = {nullptr};
    render::BuildSilhouettePoints(4, conn, &outCount, pts, outPairs);
    CHECK_EQ(outCount, 12);   // 6 edges * 2
}

// count==0 -> outCount 0, returns true.
TEST(MirrorSilhouetteUnit, EmptySet) {
    i32 outCount = 99;
    const float* outPairs[1] = {nullptr};
    bool ok = render::BuildSilhouettePoints(0, nullptr, &outCount, nullptr, outPairs);
    CHECK(ok);
    CHECK_EQ(outCount, 0);
}
