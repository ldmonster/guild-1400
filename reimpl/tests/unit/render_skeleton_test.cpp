#include "render/skeleton.h"
#include "render/vertex_lighting.h"
#include "render/anim_load.h"
#include "util/matrix.h"
#include "tests/framework/test.h"

#include <cmath>
#include <string>

using namespace guild::render;
using guild::u8;
using guild::i16;

namespace {
bool feq(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

// Build a bone float-record big enough for all touched offsets. Indices used:
//   19/20/21 base translation; 27/28/29 pivot; 30/31/32 local translation;
//   99..114 the local 4x4; byte 504 (idx 126) parent link; byte 528 (idx 132) sign.
struct Bone {
    float f[160];
    Bone() { for (float& x : f) x = 0.0f; }
    void SetParent(Bone* p) {
        float** slot = reinterpret_cast<float**>(reinterpret_cast<char*>(f) + 504);
        *slot = p ? p->f : nullptr;
    }
    // Identity rotation in the local 4x4 (idx 99/104/109 diagonal, idx 114 homog).
    // The translation column (idx 111/112/113 == bytes 444/448/452) is the +444
    // scratch the accumulator recomputes per call from the pivot/base/local fields.
    void SetIdentityRotation() { f[99]=1; f[104]=1; f[109]=1; f[114]=1; }
    // Local translation lives in float idx 30/31/32 (bytes 120/124/128); with pivot
    // and base both zero the accumulator yields exactly this translation.
    void SetLocalTranslation(float tx, float ty, float tz) { f[30]=tx; f[31]=ty; f[32]=tz; }
};

AnimFrame MakeFrame(int time, int dur, float tx, float ty, float tz) {
    AnimFrame f{};
    f.timeOrIndex = time; f.duration = dur;
    f.tx = tx; f.ty = ty; f.tz = tz;
    return f;
}
} // namespace

// ---- VIBE_Anim_InterpolateBoneFrame translation core (golden, python) ----
TEST(RenderSkeleton, InterpolateBoneFrame_Phases) {
    AnimFrame frames[3] = {
        MakeFrame(0, 10, 0.0f, 0.0f, 0.0f),
        MakeFrame(10, 10, 1.0f, 2.0f, 3.0f),
        MakeFrame(20, 10, 5.0f, 4.0f, 9.0f),
    };
    float bone[160] = {0};
    bone[99] = 1; bone[104] = 1; bone[109] = 1;  // identity 3x3 (idx 99/104/109 diag)
    bone[19] = 10.0f; bone[20] = 20.0f; bone[21] = 30.0f;  // base translation

    float out[3];
    // from==to==0, phase 0/10 -> full seg0->1 + base => (11,22,33)
    InterpolateBoneFrame(frames, bone, 0, 0, 0, 10, out);
    CHECK(feq(out[0], 11.0f)); CHECK(feq(out[1], 22.0f)); CHECK(feq(out[2], 33.0f));
    // phase 5/10 -> half => (10.5, 21, 31.5)
    InterpolateBoneFrame(frames, bone, 0, 0, 5, 10, out);
    CHECK(feq(out[0], 10.5f)); CHECK(feq(out[1], 21.0f)); CHECK(feq(out[2], 31.5f));
    // phase 10/10 -> zero delta => base (10,20,30)
    InterpolateBoneFrame(frames, bone, 0, 0, 10, 10, out);
    CHECK(feq(out[0], 10.0f)); CHECK(feq(out[1], 20.0f)); CHECK(feq(out[2], 30.0f));
    // from=0 to=1 phaseNum=5 phaseEnd=5 -> (12.5, 22, 34.5)
    InterpolateBoneFrame(frames, bone, 0, 1, 5, 5, out);
    CHECK(feq(out[0], 12.5f)); CHECK(feq(out[1], 22.0f)); CHECK(feq(out[2], 34.5f));
}

// ---- VIBE_Anim_AdvanceFrameIndex (golden, python) ----
TEST(RenderSkeleton, AdvanceFrameIndex_Modes) {
    CHECK_EQ(AdvanceFrameIndex(0x00, 2, 5, 0, 8), 3);     // forward mid
    CHECK_EQ(AdvanceFrameIndex(0x00, 5, 5, 0, 8), 0);     // hold-end no-loop -> first
    CHECK_EQ(AdvanceFrameIndex(0x01, 5, 5, 0, 8), 4);     // loop-end -> cur-1
    CHECK_EQ(AdvanceFrameIndex(0x10, 7, 5, 0, 8), 7);     // clamp-to-count (8>7 -> count-1)
    CHECK_EQ(AdvanceFrameIndex(0x10, 5, 5, 0, 8), 6);     // clamp ok (v8=6<=7)
    CHECK_EQ(AdvanceFrameIndex(0x02, 3, 5, 0, 8), 2);     // reverse mid -> cur-1
    CHECK_EQ(AdvanceFrameIndex(0x02, 0, 5, 0, 8), 1);     // reverse at first -> first+1
}

// ---- 3-bone chain world-matrix accumulation (golden, python) ----
TEST(RenderSkeleton, AccumulateBoneMatrices_Chain) {
    Bone b0, b1, b2;
    b0.SetIdentityRotation(); b0.SetLocalTranslation(1.0f, 0.0f, 0.0f);
    b1.SetIdentityRotation(); b1.SetLocalTranslation(0.0f, 2.0f, 0.0f);
    b2.SetIdentityRotation(); b2.SetLocalTranslation(0.0f, 0.0f, 3.0f);
    b0.SetParent(&b1);
    b1.SetParent(&b2);
    b2.SetParent(nullptr);

    float ident[16];
    guild::util::MatrixIdentity(ident);
    float out[16];
    // Non-root: each bone offsets the accumulator by its local translation; with
    // identity rotations the chain composes to (1,2,3).
    AccumulateBoneMatrices(b0.f, 0, out, ident);
    CHECK(feq(out[12], 1.0f)); CHECK(feq(out[13], 2.0f)); CHECK(feq(out[14], 3.0f));
    CHECK(feq(out[0], 1.0f)); CHECK(feq(out[5], 1.0f)); CHECK(feq(out[10], 1.0f));

    // Root flag zeroes the per-bone translation scratch -> identity translation.
    AccumulateBoneMatrices(b0.f, 1, out, ident);
    CHECK(feq(out[12], 0.0f)); CHECK(feq(out[13], 0.0f)); CHECK(feq(out[14], 0.0f));
}

// ---- ComputeBoneWorldMatrix with null pivot == raw accumulation ----
TEST(RenderSkeleton, ComputeBoneWorldMatrix_NoPivot) {
    Bone b0, b1;
    b0.SetIdentityRotation(); b0.SetLocalTranslation(2.0f, 0.0f, 0.0f);
    b1.SetIdentityRotation(); b1.SetLocalTranslation(0.0f, 5.0f, 0.0f);
    b0.SetParent(&b1);
    b1.SetParent(nullptr);

    float out[16];
    ComputeBoneWorldMatrix(b0.f, nullptr, 0, out);
    CHECK(feq(out[12], 2.0f)); CHECK(feq(out[13], 5.0f)); CHECK(feq(out[14], 0.0f));
}

// ---- env-map reflection UV (golden, python) ----
TEST(RenderSkeleton, EnvMapReflectionUv) {
    float I[9] = {1,0,0, 0,1,0, 0,0,1};
    float uv[2];

    float pos1[3] = {0,0,1}, n1[3] = {0,0,1};
    ComputeEnvMapReflectionUv(pos1, n1, I, uv);
    CHECK(feq(uv[0], 0.5f)); CHECK(feq(uv[1], 0.5f));

    float pos2[3] = {1,0,0};
    ComputeEnvMapReflectionUv(pos2, n1, I, uv);
    CHECK(feq(uv[0], 1.0f)); CHECK(feq(uv[1], 0.5f));

    float pos3[3] = {1,1,1}, n3[3] = {0,1,0};
    ComputeEnvMapReflectionUv(pos3, n3, I, uv);
    CHECK(feq(uv[0], 0.78867513f)); CHECK(feq(uv[1], 0.21132487f));
}

// ---- skin normal unpack (golden, python) ----
TEST(RenderSkeleton, UnpackSkinNormal) {
    u8 b1[3] = {128, 128, 255};
    float n[3];
    UnpackSkinNormal(b1, n);
    CHECK(feq(n[0], 0.0f)); CHECK(feq(n[1], 0.0f)); CHECK(feq(n[2], 0.99607855f));
    u8 b2[3] = {0, 255, 128};
    UnpackSkinNormal(b2, n);
    CHECK(feq(n[0], -1.0039216f)); CHECK(feq(n[1], 0.99607855f)); CHECK(feq(n[2], 0.0f));
}

// ---- morph blend kernel (golden, python) ----
TEST(RenderSkeleton, InterpolateMorphVertex) {
    float s[3] = {1,1,1}, b[3] = {0,0,0};
    float world[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    i16 p0[3] = {10, 20, 30}, p1[3] = {40, 50, 60};
    float out[3];
    InterpolateMorphVertex(p0, s, b, p1, s, b, 0.5f, 0.5f, world, out);
    CHECK(feq(out[0], 25.0f)); CHECK(feq(out[1], 35.0f)); CHECK(feq(out[2], 45.0f));

    float world2[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 5,6,7,1};
    i16 q0[3] = {2,2,2}, q1[3] = {4,4,4};
    InterpolateMorphVertex(q0, s, b, q1, s, b, 0.25f, 0.75f, world2, out);
    CHECK(feq(out[0], 8.5f)); CHECK(feq(out[1], 9.5f)); CHECK(feq(out[2], 10.5f));
}

// ---- binary anim loader parses synthetic buffer (golden, python) ----
TEST(RenderSkeleton, LoadBinaryAnimation_Parse) {
    static const unsigned char kBuf[] = {
        65,78,73,77,48,0,0,0,0,1,1,0,0,0,35,3,0,0,0,54,1,0,0,0,41,0,0,0,0,42,2,0,
        0,0,24,0,0,0,0,49,0,0,0,0,0,0,0,0,0,0,0,0,205,204,204,61,205,204,76,62,
        154,153,153,62,24,10,0,0,0,49,0,0,128,63,0,0,0,64,0,0,64,64,205,204,204,
        62,0,0,0,63,154,153,25,63,24,25,0,0,0,49,0,0,128,64,0,0,128,64,0,0,16,65,
        51,51,51,63,205,204,76,63,102,102,102,63,47
    };
    Animation anim;
    bool ok = LoadBinaryAnimation(kBuf, sizeof(kBuf), "synthtest", 0, anim);
    CHECK(ok);
    CHECK(anim.valid);
    CHECK_EQ(anim.header.frameCount, 3);
    CHECK_EQ(anim.header.startFrame, 0);
    CHECK_EQ(anim.header.endFrame, 2);
    CHECK(anim.frames.size() == 3);

    // durations: (10,15,15) then *3 => (30,45,45)
    CHECK_EQ(anim.frames[0].duration, 30);
    CHECK_EQ(anim.frames[1].duration, 45);
    CHECK_EQ(anim.frames[2].duration, 45);

    // root translation delta-encoded against frame 0.
    CHECK(feq(anim.frames[0].tx, 0.0f)); CHECK(feq(anim.frames[0].ty, 0.0f));
    CHECK(feq(anim.frames[1].tx, 1.0f)); CHECK(feq(anim.frames[1].ty, 2.0f)); CHECK(feq(anim.frames[1].tz, 3.0f));
    CHECK(feq(anim.frames[2].tx, 4.0f)); CHECK(feq(anim.frames[2].ty, 4.0f)); CHECK(feq(anim.frames[2].tz, 9.0f));
    // rotation/aux deltas (0.4-0.1, 0.5-0.2, 0.6-0.3) = (0.3,0.3,0.3)
    CHECK(feq(anim.frames[1].rx, 0.3f)); CHECK(feq(anim.frames[1].ry, 0.3f)); CHECK(feq(anim.frames[1].rz, 0.3f));

    // name copied + padded.
    CHECK(std::string(anim.header.name) == "synthtest");
}

// ---- loader rejects malformed input ----
TEST(RenderSkeleton, LoadBinaryAnimation_Reject) {
    Animation anim;
    CHECK(!LoadBinaryAnimation(nullptr, 0, "x", 0, anim));
    unsigned char tooShort[3] = {1, 2, 3};
    CHECK(!LoadBinaryAnimation(tooShort, 3, "x", 0, anim));  // no token 48
}
