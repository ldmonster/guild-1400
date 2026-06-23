// =============================================================================
// weather_wave21_test.cpp — golden pins for the wave-21 weather/morph recon:
//   * VIBE_Rain_UpdateDrop   (0x4294d4) — projection numerator fix (flt_611674=0.5)
//   * VIBE_Snow_UpdateFlake  (0x42a644) — deterministic flake trajectory
//   * VIBE_Anim_CreateMorphAnim (0x5cf150) — morph-delta quantization + record layout
// Deterministic RNG seed -> byte-stable particle fields; hand-computed morph bytes.
// =============================================================================
#include "tests/framework/test.h"
#include "render/rain.h"
#include "render/snow_update.h"
#include "render/anim_morph.h"
#include "crt/rand.h"

#include <cmath>
#include <cstring>

using namespace guild;
using namespace guild::render;

namespace {

// A simple camera basis. The view rotation (m[0..8]) is the 3x3 identity laid out
// the way the kernel reads it (m[0]=+396, m[1]=+400, ... see snow.h). eye/anchor
// chosen so (anchor-eye) is the unit +Z, keeping the Euler matrix well-defined.
SnowCamera MakeCam() {
    SnowCamera c{};
    c.eye[0] = 0; c.eye[1] = 0; c.eye[2] = 0;
    c.anchor[0] = 0; c.anchor[1] = 0; c.anchor[2] = 1;
    // identity 3x3
    c.m[0] = 1; c.m[1] = 0; c.m[2] = 0;
    c.m[3] = 0; c.m[4] = 1; c.m[5] = 0;
    c.m[6] = 0; c.m[7] = 0; c.m[8] = 1;
    return c;
}

SnowViewport MakeVp() { return SnowViewport{0, 0, 640, 480}; }

bool feq(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

} // namespace

// ---------------------------------------------------------------------------
// Rain: with the projection-numerator fix the head point uses
//   num = (count*0.5 + maxHalf/3) * 3 ; proj = num/(pz*2+3).
// A zero-velocity, zero-dt step leaves p unchanged but recomputes the projection,
// so we can pin sx/sy exactly from the wrapped position.
// ---------------------------------------------------------------------------
TEST(weather21_rain, projection_numerator_uses_half) {
    crt::Srand(0x1234);
    RainDrop drops[8];
    std::memset(drops, 0, sizeof(drops));
    RainSystem sys; sys.count = 8; sys.capacity = 8; sys.drops = drops;
    RainSeedDrops(sys);

    SnowCamera cam = MakeCam();
    SnowViewport vp = MakeVp();
    RainUpdateDrop(sys, 0.0f, cam, vp);

    // half-extents: 320, 240 -> maxHalf = 320. count = 8.
    // num = (8*0.5 + 320*(1/3)) * 3 = (4 + 106.6666) * 3 = 331.99996
    const float maxHalf = 320.0f;
    const float depthBias = maxHalf * 0.3333333432674408f;
    const float num = (8.0f * 0.5f + depthBias) * 3.0f;
    const float cx = 0 + 320.0f, cy = 0 + 240.0f;

    for (int i = 0; i < 8; ++i) {
        RainDrop& s = drops[i];
        float proj = num / (s.pz * 2.0f + 3.0f);
        float expSx = s.px * proj + cx;
        float expSy = cy - s.py * proj;
        CHECK(feq(s.sx, expSx, 1e-2f));
        CHECK(feq(s.sy, expSy, 1e-2f));
    }
    // Guard the OLD buggy value (num with 0.0025 instead of 0.5) does NOT match:
    float buggyNum = (8.0f * 0.0024999999441206455f + depthBias) * 3.0f;
    CHECK(!feq(num, buggyNum, 1.0f));
}

TEST(weather21_rain, deterministic_seed_repeats) {
    RainDrop a[4], b[4];
    std::memset(a, 0, sizeof(a)); std::memset(b, 0, sizeof(b));
    RainSystem sa; sa.count = 4; sa.capacity = 4; sa.drops = a;
    RainSystem sb; sb.count = 4; sb.capacity = 4; sb.drops = b;
    crt::Srand(99); RainSeedDrops(sa);
    crt::Srand(99); RainSeedDrops(sb);
    CHECK(std::memcmp(a, b, sizeof(a)) == 0);
}

// ---------------------------------------------------------------------------
// Snow: deterministic seed + N steps must reproduce byte-for-byte, and the
// projection tail relation sx2 == sx + tail, sy2 == sy + tail must hold.
// ---------------------------------------------------------------------------
TEST(weather21_snow, deterministic_trajectory) {
    SnowFlake fa[16], fb[16];
    std::memset(fa, 0, sizeof(fa)); std::memset(fb, 0, sizeof(fb));
    SnowSystem sa; sa.count = 16; sa.capacity = 16; sa.flakes = fa;
    SnowSystem sb; sb.count = 16; sb.capacity = 16; sb.flakes = fb;
    SnowCamera cam = MakeCam();
    SnowViewport vp = MakeVp();

    crt::Srand(0xBEEF); SnowGoldenStep(sa, 5, 0.016f, cam, vp);
    crt::Srand(0xBEEF); SnowGoldenStep(sb, 5, 0.016f, cam, vp);
    CHECK(std::memcmp(fa, fb, sizeof(fa)) == 0);
}

TEST(weather21_snow, projection_tail_relation) {
    SnowFlake f[8];
    std::memset(f, 0, sizeof(f));
    SnowSystem s; s.count = 8; s.capacity = 8; s.flakes = f;
    crt::Srand(7);
    SnowGoldenStep(s, 3, 0.02f, MakeCam(), MakeVp());
    for (int i = 0; i < 8; ++i) {
        float tail = ((1.0f - f[i].pz) * 13.5f + 1.0f) * f[i].size;
        CHECK(feq(f[i].sx2, f[i].sx + tail, 1e-2f));
        CHECK(feq(f[i].sy2, f[i].sy + tail, 1e-2f));
        // positions wrapped into [-1, 1)
        CHECK(f[i].px >= -1.0f && f[i].px < 1.0f);
        CHECK(f[i].py >= -1.0f && f[i].py < 1.0f);
    }
}

// ---------------------------------------------------------------------------
// Morph quantize: b = trunc((delta - min) * 255 / range), low byte. Hand-pinned.
// ---------------------------------------------------------------------------
TEST(weather21_morph, quantize_byte_truncates) {
    // range [0,10], delta=0 -> 0 ; delta=10 -> 255 ; delta=5 -> trunc(127.5)=127
    CHECK_EQ((int)MorphQuantizeByte(0.0f, 0.0f, 10.0f), 0);
    CHECK_EQ((int)MorphQuantizeByte(10.0f, 0.0f, 10.0f), 255);
    CHECK_EQ((int)MorphQuantizeByte(5.0f, 0.0f, 10.0f), 127);
    // negative min: range [-2,2], delta=0 -> trunc(2*255/4)=127
    CHECK_EQ((int)MorphQuantizeByte(0.0f, -2.0f, 4.0f), 127);
    CHECK_EQ((int)MorphQuantizeByte(-2.0f, -2.0f, 4.0f), 0);
    CHECK_EQ((int)MorphQuantizeByte(2.0f, -2.0f, 4.0f), 255);
}

TEST(weather21_morph, create_reject_paths) {
    MorphSrcNode src{}; src.numPoints = 4;
    MorphDestNode dest{}; dest.numBones = 0;
    MorphControl ctrl{};
    MorphEndpoint prim{};
    // frames <= 1 rejected
    CHECK(CreateMorphAnim(src, dest, ctrl, &prim, nullptr, "x", 1) == nullptr);
    // !prim && !alt rejected
    CHECK(CreateMorphAnim(src, dest, ctrl, nullptr, nullptr, "x", 4) == nullptr);
    // !name rejected
    CHECK(CreateMorphAnim(src, dest, ctrl, &prim, nullptr, nullptr, 4) == nullptr);
}

TEST(weather21_morph, create_quantized_blob_and_layout) {
    // 3 points, identity-ish deltas so we can pin the bounds + bytes.
    // source points (stride 6 floats), target = source + known delta.
    const int NP = 3;
    float srcPts[6 * NP] = {0};
    float tgtPts[6 * NP] = {0};
    // deltas chosen: point0 (0,0,0), point1 (10, -2, 5), point2 (4, 2, 0)
    // -> minX=0 maxX=10 ; minY=-2 maxY=2 ; minZ=0 maxZ=5
    float dx[NP] = {0, 10, 4};
    float dy[NP] = {0, -2, 2};
    float dz[NP] = {0, 5, 0};
    for (int p = 0; p < NP; ++p) {
        tgtPts[6 * p + 0] = srcPts[6 * p + 0] + dx[p];
        tgtPts[6 * p + 1] = srcPts[6 * p + 1] + dy[p];
        tgtPts[6 * p + 2] = srcPts[6 * p + 2] + dz[p];
    }

    MorphSrcNode src{}; src.numPoints = NP; src.pointList = srcPts;
    MorphDestNode dest{}; dest.numBones = 0;
    MorphControl ctrl{}; ctrl.hasWPoints = true; ctrl.hasPoints = true;
    MorphEndpoint prim{}; prim.pointList = tgtPts; prim.numBones = 0;
    for (int i = 0; i < 6; ++i) { prim.xform[i] = (float)(i + 1); src.xform[i] = 0.0f; }

    MorphAnim* rec = CreateMorphAnim(src, dest, ctrl, &prim, nullptr, "morph", 4);
    CHECK(rec != nullptr);
    if (!rec) return;

    // record layout
    CHECK_EQ(rec->numPoints, NP);
    CHECK_EQ(rec->frameStride, 2);
    CHECK_EQ((int)rec->frame.size(), 192 * 2);
    CHECK(rec->name == "morph");
    // frame[+4] = 3*frames = 12 ; frame[+192]=4 ; frame[+196]=12
    CHECK_EQ(*(int*)(rec->frame.data() + 4), 12);
    CHECK_EQ(*(int*)(rec->frame.data() + 192), 4);
    CHECK_EQ(*(int*)(rec->frame.data() + 196), 12);

    // bounds: min stored at +200/+204/+208 ; scale = range/255 at +212/+216/+220
    CHECK(feq(rec->frameF(200)[0], 0.0f));   // minX
    CHECK(feq(rec->frameF(204)[0], -2.0f));  // minY
    CHECK(feq(rec->frameF(208)[0], 0.0f));   // minZ
    CHECK(feq(rec->frameF(212)[0], 10.0f / 255.0f, 1e-6f));
    CHECK(feq(rec->frameF(216)[0], 4.0f / 255.0f, 1e-6f));
    CHECK(feq(rec->frameF(220)[0], 5.0f / 255.0f, 1e-6f));

    // WPoints1 quantized bytes: point0 deltas at min -> 0,0,0
    CHECK_EQ((int)rec->wpoints1.size(), 3 * NP);
    CHECK_EQ((int)rec->wpoints1[0], 0);
    // point0 dy = 0, min=-2, range=4 -> (0-(-2))*255/4 = 127.5 -> trunc 127
    CHECK_EQ((int)rec->wpoints1[1], 127);
    CHECK_EQ((int)rec->wpoints1[2], 0);
    // point1: dx=10 -> 255 ; dy=-2 -> 0 ; dz=5 -> 255
    CHECK_EQ((int)rec->wpoints1[3], 255);
    CHECK_EQ((int)rec->wpoints1[4], 0);
    CHECK_EQ((int)rec->wpoints1[5], 255);
    // point2: dx=4 -> trunc(4*255/10)=trunc(102.0)=102 ; dy=2 -> 255 ; dz=0 -> 0
    CHECK_EQ((int)rec->wpoints1[6], 102);
    CHECK_EQ((int)rec->wpoints1[7], 255);
    CHECK_EQ((int)rec->wpoints1[8], 0);

    // Points1 float morph deltas (12*NP bytes = 3 floats per point)
    CHECK_EQ((int)rec->points1.size(), 12 * NP);
    float* pd = (float*)rec->points1.data();
    CHECK(feq(pd[3], 10.0f)); CHECK(feq(pd[4], -2.0f)); CHECK(feq(pd[5], 5.0f));

    // transform deltas frame[+224..+244] = prim.xform - src.xform, with +228=0.
    CHECK(feq(rec->frameF(224)[0], 1.0f));
    CHECK_EQ(*(unsigned*)(rec->frame.data() + 228), 0u); // deliberate overwrite
    CHECK(feq(rec->frameF(232)[0], 3.0f));

    delete rec;
}
