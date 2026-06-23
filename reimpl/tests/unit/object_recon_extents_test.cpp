// Golden-vector tests for the object_recon_extents cluster.
//   0x5e89b4 VIBE_Object_ApplyTransformConstraints
//   0x5b6ebc VIBE_Object_ComputeBoneScreenExtents
#include "tests/framework/test.h"
#include "src/sim/object_recon_extents.h"

#include <cmath>
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

// Build a zeroed object record big enough for all offsets we touch (+533+1).
struct ObjRec {
    u8 b[1024];
    ObjRec() { std::memset(b, 0, sizeof(b)); }
    f32& f(int off) { return *reinterpret_cast<f32*>(b + off); }
    u32& u(int off) { return *reinterpret_cast<u32*>(b + off); }
    i8&  s(int off) { return *reinterpret_cast<i8*>(b + off); }
    u8&  ub(int off){ return *reinterpret_cast<u8*>(b + off); }
};

bool approx(f32 a, f32 b) { return std::fabs(a - b) < 1e-4f; }

// Write a pointer into a (possibly unaligned) original-layout dword offset.
template <class T>
void putPtr(u8* base, int off, T* p) {
    uintptr_t v = reinterpret_cast<uintptr_t>(p);
    std::memcpy(base + off, &v, sizeof(v));
}

} // namespace

// ---------------------------------------------------------------------------
// ApplyTransformConstraints: mode != 3, identity basis, position delta only.
// With basis+504 == 0 the matrix comes from basis[99..]; we set it to identity
// columns so resolved pos = obj.pos + dpos.
// ---------------------------------------------------------------------------
TEST(ObjectReconExtents, ApplyConstraints_PosOnly_Identity) {
    ObjRec o;
    o.ub(533) = 1;            // mode != 3, world enabled if dworld nonzero
    o.u(504)  = 0;            // use basis+99 as matrix
    o.s(528)  = 0;            // (unused in this path)
    o.f(76) = 10.0f; o.f(80) = 20.0f; o.f(84) = 30.0f;   // object position

    f32 basis[200];
    std::memset(basis, 0, sizeof(basis));
    // basis+99 holds the 3x3: col0=(1,0,0) col1=(0,1,0) col2=(0,0,1)
    basis[99 + 0] = 1.0f; basis[99 + 4] = 1.0f; basis[99 + 8] = 1.0f;

    f32 dpos[3]   = {1.0f, 2.0f, 3.0f};
    f32 dworld[3] = {0.0f, 0.0f, 0.0f};
    f32 outPos[3] = {0,0,0}, outWorld[3] = {0,0,0};

    TransformConstraintEnv env;
    auto h = ApplyTransformConstraints_DefaultHooks();
    u8 r = ApplyTransformConstraints(o.b, dpos, basis, dworld, outPos, outWorld, env, h);

    // dpos nonzero -> bit0 set; dworld zero -> bit1 clear.
    CHECK_EQ((int)r, 1);
    // resolved pos: x = px + (dx*c0x + dy*c1x + dz*c2x) etc. With identity:
    CHECK(approx(outPos[0], 11.0f));
    CHECK(approx(outPos[1], 22.0f));
    CHECK(approx(outPos[2], 33.0f));
}

// Mode != 3, world delta requested (obj+533 != 0). Matrix builders are inert
// (identity / matB-copy); world bit must be set, position bit clear.
TEST(ObjectReconExtents, ApplyConstraints_WorldOnly) {
    ObjRec o;
    o.ub(533) = 2;            // != 3, nonzero -> world path allowed
    o.u(504)  = 0;
    o.s(528)  = 0;            // >= 0 -> uses basis as local frame, then matMul path

    f32 basis[200];
    std::memset(basis, 0, sizeof(basis));
    basis[99 + 0] = 1.0f; basis[99 + 4] = 1.0f; basis[99 + 8] = 1.0f;

    f32 dpos[3]   = {0.0f, 0.0f, 0.0f};
    f32 dworld[3] = {0.1f, 0.2f, 0.3f};
    f32 outPos[3] = {0,0,0}, outWorld[3] = {0,0,0};

    TransformConstraintEnv env;
    auto h = ApplyTransformConstraints_DefaultHooks();
    u8 r = ApplyTransformConstraints(o.b, dpos, basis, dworld, outPos, outWorld, env, h);

    // dpos zero -> bit0 clear; dworld nonzero & obj+533!=0 -> bit1 set.
    CHECK_EQ((int)r, 2);
}

// Mode == 3 with world delta: outWorld = obj.world + dworld; world bit set.
TEST(ObjectReconExtents, ApplyConstraints_Mode3_World) {
    ObjRec o;
    o.ub(533) = 3;            // mode 3 path
    o.u(504)  = 0;
    o.f(132) = 5.0f; o.f(136) = 6.0f; o.f(140) = 7.0f;   // object world translation

    f32 basis[200];
    std::memset(basis, 0, sizeof(basis));
    basis[99 + 0] = 1.0f; basis[99 + 4] = 1.0f; basis[99 + 8] = 1.0f;

    f32 dpos[3]   = {0.0f, 0.0f, 0.0f};
    f32 dworld[3] = {1.0f, 1.0f, 1.0f};
    f32 outPos[3] = {0,0,0}, outWorld[3] = {0,0,0};

    TransformConstraintEnv env;
    auto h = ApplyTransformConstraints_DefaultHooks();
    u8 r = ApplyTransformConstraints(o.b, dpos, basis, dworld, outPos, outWorld, env, h);

    CHECK_EQ((int)r, 2);
    CHECK(approx(outWorld[0], 6.0f));
    CHECK(approx(outWorld[1], 7.0f));
    CHECK(approx(outWorld[2], 8.0f));
}

// Mode 3, position branch via the env.d649EFC==obj path (no alt flag). With an
// identity basis, the Z-basis projection used by the magnitude gate is col2z=1,
// so sqrt(...) == 1 >= kEps(0.1) and the ANGLE branch fires. The inert
// vectorAngleWrapped returns 0 -> sin=0, cos=1, so:
//   v35=dpos.x=0.01, v37=dpos.y=0.02, v36=0
//   v38 = v37*sin + v35*cos = 0.01 ; v40 = -v35*sin + v37*cos = 0.02
//   a5 = pos + (v38, v39=0, v40) = (0.01, 0, 0.02)
//   then v11=a5[2]=0.02; a5[1]+=dpos.z(=0.5)->0.5; a5[2]=v11+0=0.02
TEST(ObjectReconExtents, ApplyConstraints_Mode3_PosAngleBranch) {
    ObjRec o;
    o.ub(533) = 3;
    o.u(504)  = 0;
    o.f(76) = 0.0f; o.f(80) = 0.0f; o.f(84) = 0.0f;

    f32 basis[200];
    std::memset(basis, 0, sizeof(basis));
    basis[99 + 0] = 1.0f; basis[99 + 4] = 1.0f; basis[99 + 8] = 1.0f;

    f32 dpos[3]   = {0.01f, 0.02f, 0.5f};
    f32 dworld[3] = {0.0f, 0.0f, 0.0f};
    f32 outPos[3] = {0,0,0}, outWorld[3] = {0,0,0};

    TransformConstraintEnv env;
    env.d649EFC = (u32)(uintptr_t)o.b;   // active == this object -> else branch
    env.b64A024 = 0;
    auto h = ApplyTransformConstraints_DefaultHooks();
    u8 r = ApplyTransformConstraints(o.b, dpos, basis, dworld, outPos, outWorld, env, h);

    CHECK_EQ((int)r, 1);
    CHECK(approx(outPos[0], 0.01f));
    CHECK(approx(outPos[1], 0.5f));
    CHECK(approx(outPos[2], 0.02f));
}

// No deltas -> returns 0, outputs untouched.
TEST(ObjectReconExtents, ApplyConstraints_NoChange) {
    ObjRec o;
    o.ub(533) = 1;
    o.u(504)  = 0;
    f32 basis[200]; std::memset(basis, 0, sizeof(basis));
    f32 dpos[3]   = {0,0,0};
    f32 dworld[3] = {0,0,0};
    f32 outPos[3] = {99,99,99}, outWorld[3] = {88,88,88};
    TransformConstraintEnv env;
    auto h = ApplyTransformConstraints_DefaultHooks();
    u8 r = ApplyTransformConstraints(o.b, dpos, basis, dworld, outPos, outWorld, env, h);
    CHECK_EQ((int)r, 0);
    CHECK(approx(outPos[0], 99.0f));
    CHECK(approx(outWorld[0], 88.0f));
}

// VectorWithinTolerance default-hook behavior (the gate predicate itself).
TEST(ObjectReconExtents, VectorWithinTolerance) {
    auto h = ApplyTransformConstraints_DefaultHooks();
    f32 a[3] = {1.0f, 2.0f, 3.0f};
    f32 b[3] = {1.0005f, 2.0f, 3.0f};
    CHECK(h.vectorWithinTolerance(a, b, 0.001f) == true);
    f32 c[3] = {1.01f, 2.0f, 3.0f};
    CHECK(h.vectorWithinTolerance(a, c, 0.001f) == false);
}

// ---------------------------------------------------------------------------
// ComputeBoneScreenExtents: build a one-face, one-quad pose and verify the four
// corner selections. We lay out the pose node and a single face with 2 vertices
// so the inner loop runs twice (v10 != v19 after +1 step where +12 == 1 dword;
// face stride for the v19 end pointer is +12 bytes => 3 dword ptrs region... we
// construct the vertex-ptr array so the loop terminates after consuming the
// configured count).
// ---------------------------------------------------------------------------
TEST(ObjectReconExtents, ComputeBoneScreenExtents_Corners) {
    // Build a one-face, 3-vertex pose using the typed geometry model. Vertex
    // world coords map: f[4]=worldX -> screen px, f[5]=worldY -> screen py
    // (project() identity). f[0..2] reinterpreted as the int payload a2[1..3].
    const int poseVertId = 7;            // ext[0] must match this

    static BoneVertex verts[3];
    std::memset(verts, 0, sizeof(verts));
    auto setvert = [&](int i, int ix, int iy) {
        verts[i].f[4] = (f32)ix;   // -> px
        verts[i].f[5] = (f32)iy;   // -> py
        reinterpret_cast<i32*>(verts[i].f)[0] = 100 + i;  // a2[1..] payload
        reinterpret_cast<i32*>(verts[i].f)[1] = 200 + i;
        reinterpret_cast<i32*>(verts[i].f)[2] = 300 + i;
    };
    setvert(0, 5, 5);     // sum 10
    setvert(1, 2, 8);     // sum 10 (tie, not strictly less -> won't replace min)
    setvert(2, 1, 1);     // sum 2  -> new min

    // uv table: indexed by byte offset v11 (0,8,16) from uvBase => pairs at
    // dword indices 0/1, 2/3, 4/5.
    static const i32 uvBuf[6] = {10,11, 20,21, 30,31};

    static BoneFace face;
    std::memset(&face, 0, sizeof(face));
    face.verts[0] = &verts[0];
    face.verts[1] = &verts[1];
    face.verts[2] = &verts[2];
    face.uvBase   = uvBuf;
    face.boneId   = poseVertId;
    face.flagSign = (i8)0x80;   // < 0
    face.flag2    = 0;          // &2 == 0

    static i32 idCountBlock[200];
    std::memset(idCountBlock, 0, sizeof(idCountBlock));
    idCountBlock[120] = 4;                                 // [480/4]=index120 -> 4
    static const i32 idList[4] = {1, 2, poseVertId, 9};   // match at index 2 (<4)

    static BonePose pose;
    pose.facesBase  = &face;
    pose.facesCount = 1;
    pose.idBlock    = idCountBlock;
    pose.idList     = idList;

    ObjRec o;
    putPtr(o.b, 460, &pose);

    // extent record a2. Seed the 4 running extrema large so first hits replace.
    std::vector<i32> ext(40, 0);
    ext[0] = poseVertId;     // a2[0] bone id to match
    ext[25] = 1000000; ext[26] = 1000000;     // min-sum running (x in [25])
    ext[27] = -1000000; ext[28] = 1000000;    // (a2[28]+H-1-a2[27]) running
    ext[29] = -1000000; ext[30] = 1000000;
    ext[31] = 1000000; ext[32] = -1000000;

    BoneExtentEnv env; env.screenW = 640; env.screenH = 480;
    auto h = ComputeBoneScreenExtents_DefaultHooks();
    u8 r = ComputeBoneScreenExtents(o.b, ext.data(), env, h);
    CHECK_EQ((int)r, 1);

    // a2[33] byte flag set.
    CHECK_EQ((int)*reinterpret_cast<u8*>(ext.data() + 33), 1);
    // min-sum corner: vertex 2 (x=1,y=1, sum 2). px=x=1, py=y=1.
    //   a2[25] = px(=v22=1)? In code: a2[25]=v22 (=px), a2[26]=v21 (=py).
    CHECK_EQ(ext[25], 1);
    CHECK_EQ(ext[26], 1);
    // payload from vert2: a2[1..3] = verts[2][0..2] = 102,202,302
    CHECK_EQ(ext[1], 102);
    CHECK_EQ(ext[2], 202);
    CHECK_EQ(ext[3], 302);
    // uv for min-sum: v11 for vert2 = 16 bytes in -> uvBuf[4],uvBuf[5] = 30,31
    CHECK_EQ(ext[17], 30);
    CHECK_EQ(ext[18], 31);
}

// Bone id mismatch -> no faces processed, flag stays 0.
TEST(ObjectReconExtents, ComputeBoneScreenExtents_NoMatch) {
    static BoneFace face; std::memset(&face, 0, sizeof(face));
    face.boneId = 99;            // bone id 99 (won't match ext[0]==7)
    face.flagSign = (i8)0x80;
    static i32 idCountBlock[200]; std::memset(idCountBlock, 0, sizeof(idCountBlock));
    idCountBlock[120] = 2;
    static const i32 idList[2] = {7, 8};
    static BonePose pose;
    pose.facesBase = &face; pose.facesCount = 1;
    pose.idBlock = idCountBlock; pose.idList = idList;

    ObjRec o; putPtr(o.b, 460, &pose);
    std::vector<i32> ext(40, 0);
    ext[0] = 7;
    BoneExtentEnv env; env.screenW = 640; env.screenH = 480;
    auto h = ComputeBoneScreenExtents_DefaultHooks();
    u8 r = ComputeBoneScreenExtents(o.b, ext.data(), env, h);
    CHECK_EQ((int)r, 1);
    CHECK_EQ((int)*reinterpret_cast<u8*>(ext.data() + 33), 0);
}

// Null pose -> returns 1 immediately.
TEST(ObjectReconExtents, ComputeBoneScreenExtents_NullPose) {
    ObjRec o;   // +460 == 0
    std::vector<i32> ext(40, 0);
    BoneExtentEnv env;
    auto h = ComputeBoneScreenExtents_DefaultHooks();
    CHECK_EQ((int)ComputeBoneScreenExtents(o.b, ext.data(), env, h), 1);
}
