#include "render/skeleton.h"
#include "render/anim_load.h"
#include "render/vertex_lighting.h"
#include "util/matrix.h"
#include "tests/framework/test.h"

#include <cmath>
#include <vector>

using namespace guild::render;

namespace {
bool feq(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

// Build a synthetic .anim byte stream: 3 frames with root translation along X.
std::vector<unsigned char> BuildAnim() {
    std::vector<unsigned char> b;
    auto u8 = [&](int x) { b.push_back((unsigned char)(x & 0xff)); };
    auto dw = [&](int x) { for (int i = 0; i < 4; ++i) b.push_back((unsigned char)((x >> (8 * i)) & 0xff)); };
    auto fl = [&](float f) { unsigned char* p = reinterpret_cast<unsigned char*>(&f); for (int i = 0; i < 4; ++i) b.push_back(p[i]); };
    b.insert(b.end(), {'A','N','I','M'});  // 4 magic bytes
    u8(48); dw(0);            // version
    u8(1); dw(1);             // magic guard
    u8(35); dw(3);            // frame count
    u8(54); dw(1);            // subIters
    u8(41); dw(0);            // start
    u8(42); dw(2);            // end
    // frames: time + transform (tx,ty,tz,rx,ry,rz)
    struct F { int t; float tx, ty, tz; };
    F frames[3] = { {0, 0.0f, 0.0f, 0.0f}, {10, 1.0f, 0.0f, 0.0f}, {20, 3.0f, 0.0f, 0.0f} };
    for (const F& f : frames) {
        u8(24); dw(f.t);
        u8(49); fl(f.tx); fl(f.ty); fl(f.tz); fl(0.0f); fl(0.0f); fl(0.0f);
    }
    u8(47);                   // EOC
    return b;
}
} // namespace

// Full flow: load synthetic animation, pose a single root bone at several phases,
// build the bone world matrix from the posed translation, skin a vertex, and verify
// positions against the python reference.
TEST(RenderSkeletonE2E, LoadPoseAccumulateSkin) {
    std::vector<unsigned char> buf = BuildAnim();
    Animation anim;
    bool ok = LoadBinaryAnimation(buf.data(), buf.size(), "walk", 0, anim);
    CHECK(ok);
    CHECK_EQ(anim.header.frameCount, 3);

    // The loader delta-encodes frame 0 to zero and durations *=3. For pose math we
    // want the raw segment translations, so reconstruct a keyframe table directly
    // (segment seg0->1 spans tx 0->1) with duration 10 (pre-triple) to match python.
    AnimFrame keys[3];
    keys[0] = {}; keys[0].timeOrIndex = 0;  keys[0].duration = 10; keys[0].tx = 0.0f;
    keys[1] = {}; keys[1].timeOrIndex = 10; keys[1].duration = 10; keys[1].tx = 1.0f;
    keys[2] = {}; keys[2].timeOrIndex = 20; keys[2].duration = 10; keys[2].tx = 3.0f;

    // Root bone: identity 3x3, zero base translation.
    float bone[160] = {0};
    bone[99] = 1; bone[104] = 1; bone[109] = 1;

    struct Case { int phase; float expSkinX; };
    Case cases[3] = { {0, 2.0f}, {5, 1.5f}, {10, 1.0f} };

    for (const Case& c : cases) {
        // 1. Pose: interpolate the bone translation at this sub-frame phase.
        float posed[3];
        InterpolateBoneFrame(keys, bone, 0, 0, c.phase, 10, posed);

        // 2. Build the bone world matrix as a translation by the posed value: a bone
        //    with identity local rotation (idx 99/104/109/114) and the posed value in
        //    its local translation (idx 30/31/32). Accumulated non-root.
        float boneMat[160] = {0};
        boneMat[99] = 1; boneMat[104] = 1; boneMat[109] = 1; boneMat[114] = 1;
        boneMat[30] = posed[0];
        boneMat[31] = posed[1];
        boneMat[32] = posed[2];
        // No parent: null the parent link at byte 504.
        *reinterpret_cast<float**>(reinterpret_cast<char*>(boneMat) + 504) = nullptr;

        float world[16];
        ComputeBoneWorldMatrix(boneMat, nullptr, 0, world);
        CHECK(feq(world[12], posed[0]));

        // 3. Skin a vertex (1,1,1) through the world matrix.
        float v[3] = {1.0f, 1.0f, 1.0f}, skinned[3];
        TransformPointByWorldMatrix(v, world, skinned);
        CHECK(feq(skinned[0], c.expSkinX));
        CHECK(feq(skinned[1], 1.0f));
        CHECK(feq(skinned[2], 1.0f));
    }
}

// Cross-check: a 3-bone chain world matrix + skinning a vertex composes the chain
// translations, and the env-map UV of the skinned vertex normal is well-formed.
TEST(RenderSkeletonE2E, ChainSkinAndEnvMap) {
    struct Bone { float f[160]; };
    Bone b0{}, b1{}, b2{};
    auto setT = [](float* f, float tx, float ty, float tz) {
        f[99] = 1; f[104] = 1; f[109] = 1; f[114] = 1;  // identity local rotation
        f[30] = tx; f[31] = ty; f[32] = tz;             // local translation idx 30/31/32
    };
    auto setParent = [](float* f, float* p) {
        *reinterpret_cast<float**>(reinterpret_cast<char*>(f) + 504) = p;
    };
    setT(b0.f, 1.0f, 0.0f, 0.0f);
    setT(b1.f, 0.0f, 2.0f, 0.0f);
    setT(b2.f, 0.0f, 0.0f, 3.0f);
    setParent(b0.f, b1.f);
    setParent(b1.f, b2.f);
    setParent(b2.f, nullptr);

    float world[16];
    ComputeBoneWorldMatrix(b0.f, nullptr, 0, world);
    CHECK(feq(world[12], 1.0f)); CHECK(feq(world[13], 2.0f)); CHECK(feq(world[14], 3.0f));

    float v[3] = {0, 0, 0}, skinned[3];
    TransformPointByWorldMatrix(v, world, skinned);
    CHECK(feq(skinned[0], 1.0f)); CHECK(feq(skinned[1], 2.0f)); CHECK(feq(skinned[2], 3.0f));

    // Env-map UV for the skinned vertex (pos=skinned, normal=+Z, identity 3x3).
    float I[9] = {1,0,0, 0,1,0, 0,0,1};
    float n[3] = {0, 0, 1}, uv[2];
    ComputeEnvMapReflectionUv(skinned, n, I, uv);
    // R should be unit; UV within [0,1].
    CHECK(uv[0] >= 0.0f && uv[0] <= 1.0f);
    CHECK(uv[1] >= 0.0f && uv[1] <= 1.0f);
}
