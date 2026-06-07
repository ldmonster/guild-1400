#include "test.h"

#include "render/mesh_transform.h"
#include "render/bone_sample.h"

#include <cstdint>
#include <cstring>
#include <cmath>
#include <vector>

using namespace guild::render;

// ---------------------------------------------------------------------------
// External leaves: ComputeBoneWorldMatrix is REAL (skeleton.cpp — linked). The
// two genuinely-unowned leaves (TransformBoundingVolume / BuildObjectCache) are
// defined ONCE for the whole test binary in the e2e TU; this TU just declares
// access to the call-count probes it shares with that definition.
// ---------------------------------------------------------------------------
namespace guild::render {
extern int  g_buildCacheCalls;   // defined in mesh_transform_e2e_test.cpp
} // namespace guild::render

// ---------------------------------------------------------------------------
// Raw byte-block builders for the opaque object/mesh/vertex records.
// ---------------------------------------------------------------------------
namespace {

inline void putPtr(std::uint8_t* base, std::size_t off, void* p)
{
    std::memcpy(base + off, &p, sizeof(void*));
}
inline void putI32(std::uint8_t* base, std::size_t off, std::int32_t v)
{
    std::memcpy(base + off, &v, sizeof(v));
}
inline void putF(std::uint8_t* base, std::size_t off, float v)
{
    std::memcpy(base + off, &v, sizeof(v));
}
inline float getF(const std::uint8_t* base, std::size_t off)
{
    float f; std::memcpy(&f, base + off, sizeof(f)); return f;
}

constexpr std::size_t kVtxStride = 80;

// Build a single-bone frame (no parent) whose local 4x4 (byte 396 / float idx 99)
// is `mat3x4` — 12 floats: rows of the 3x3 followed by the translation column,
// laid out as MatrixTransformVectors expects (idx 0..2,4..6,8..10,12..14). With
// pivot=null + isRoot + no parent, ComputeBoneWorldMatrix returns ident*local ==
// that matrix, so the mesh transforms see exactly `mat3x4` as `m`.
struct BoneFrame {
    std::vector<std::uint8_t> buf = std::vector<std::uint8_t>(640, 0);
    explicit BoneFrame(const float m[16])
    {
        // +504 parent link = null (already zeroed).
        for (int i = 0; i < 16; ++i)
            putF(buf.data(), 396 + 4 * i, m[i]);
    }
    void* obj() { return buf.data(); }
};

// A mesh block: vertices array at +0, sub-block at +4, vertex count at +8,
// sub-mesh count at +12, mesh+380 mode byte. We over-allocate so corner writes fit.
struct MeshBlock {
    std::vector<std::uint8_t> mesh = std::vector<std::uint8_t>(512, 0);
    std::vector<std::uint8_t> verts;
    int count = 0;

    void alloc(int vcount, int extraCorners = 0)
    {
        count = vcount;
        verts.assign(kVtxStride * (vcount + extraCorners) + 16, 0);
        putPtr(mesh.data(), 0, verts.data());
        putI32(mesh.data(), 8, vcount);
    }
    std::uint8_t* vtx(int i) { return verts.data() + kVtxStride * i; }
};

} // namespace

// ---------------------------------------------------------------------------
// TransformPackedVertices: dequant + 3x3 rotate into vertex +32.
// Golden vectors from python (m = swap/scale matrix).
// ---------------------------------------------------------------------------
TEST(MeshTransform, TransformPackedVertices) {
    MeshBlock mb;
    mb.alloc(2);

    // Bone local matrix = swap/scale matrix used for the golden vector. Translation
    // is irrelevant to the skin path (3x3 only), so leave it 0.
    float m[16] = {0,2,0,0,  0,0,2,0,  2,0,0,0,  0,0,0,1};
    BoneFrame bf(m);
    bf.buf[528] = 0x80; // sign set -> pivotless (null pivot) -> ident*local == m

    std::uint8_t packed[6] = {0,128,255,  64,64,64};
    void* r = TransformPackedVertices(bf.obj(), mb.mesh.data(), packed, nullptr);
    CHECK(r == mb.mesh.data());

    // Vertex 0: deq(0,128,255) -> rotate -> (1.992157, -2.007843, 0)
    CHECK(std::fabs(getF(mb.vtx(0), 32) - 1.992156982421875f) < 1e-5f);
    CHECK(std::fabs(getF(mb.vtx(0), 36) - (-2.007843255996704f)) < 1e-5f);
    CHECK(std::fabs(getF(mb.vtx(0), 40) - 0.0f) < 1e-5f);
    // Vertex 1: deq(64,64,64) all -0.50196 -> (-0.50196*2)= -1.003921627998352 each
    CHECK(std::fabs(getF(mb.vtx(1), 32) - (-1.003921627998352f)) < 1e-5f);
    CHECK(std::fabs(getF(mb.vtx(1), 36) - (-1.003921627998352f)) < 1e-5f);
    CHECK(std::fabs(getF(mb.vtx(1), 40) - (-1.003921627998352f)) < 1e-5f);
}

TEST(MeshTransform, TransformPackedVertices_NullSrcCallsThunk) {
    // packedSrc null -> invokes (*(mesh[4] + 492))(). Build a sub-block holding a
    // function-pointer thunk at +492 that returns a sentinel.
    static void* sentinel = reinterpret_cast<void*>(0x1234);
    struct Thunk { static void* run() { return sentinel; } };

    std::vector<std::uint8_t> sub(600, 0);
    void* (*fn)() = &Thunk::run;
    std::memcpy(sub.data() + 492, &fn, sizeof(fn));

    MeshBlock mb;
    mb.alloc(1);
    putPtr(mb.mesh.data(), 4, sub.data());

    std::vector<std::uint8_t> obj(600, 0);
    void* r = TransformPackedVertices(obj.data(), mb.mesh.data(), nullptr, nullptr);
    CHECK(r == sentinel);
}

// ---------------------------------------------------------------------------
// TransformVertexNormals: rotate each vertex's source normal (*(v+72)+12).
// ---------------------------------------------------------------------------
TEST(MeshTransform, TransformVertexNormals) {
    MeshBlock mb;
    mb.alloc(1);

    // Identity rotation -> normal copied through.
    float m[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    BoneFrame bf(m);
    bf.buf[528] = 0x80; // pivotless

    // Source block: model normal at +12.
    std::vector<std::uint8_t> srcblk(64, 0);
    float n[3] = {0.6f, 0.0f, 0.8f};
    std::memcpy(srcblk.data() + 12, n, sizeof(n));
    putPtr(mb.vtx(0), 72, srcblk.data());

    TransformVertexNormals(bf.obj(), mb.mesh.data(), nullptr);

    CHECK(std::fabs(getF(mb.vtx(0), 32) - 0.6f) < 1e-6f);
    CHECK(std::fabs(getF(mb.vtx(0), 36) - 0.0f) < 1e-6f);
    CHECK(std::fabs(getF(mb.vtx(0), 40) - 0.8f) < 1e-6f);
}

// ---------------------------------------------------------------------------
// ComputeBoundingBox branch A (mesh+380 == 0): transform 8 corner verts in place.
// ---------------------------------------------------------------------------
TEST(MeshTransform, ComputeBoundingBox_BranchA) {
    MeshBlock mb;
    mb.alloc(0, /*extraCorners=*/8); // vcount 0 means corners start at vtxBase

    // mesh+8 must be non-zero for the early-out NOT to fire, but the corner cursor
    // is vtxBase + 80*vcount; use vcount=2 with 8 corner slots after.
    mb.alloc(2, 8);
    putI32(mb.mesh.data(), 8, 2);
    mb.mesh[380] = 0;

    // 8 corner verts begin at vtxBase + 80*2. Each has a +72 source block.
    std::vector<std::vector<std::uint8_t>> srcs(8, std::vector<std::uint8_t>(16, 0));
    float pts[8][3] = {
        {-1,-2,-3},{4,-2,-3},{-1,5,-3},{4,5,-3},
        {-1,-2,6},{4,-2,6},{-1,5,6},{4,5,6}};
    for (int i = 0; i < 8; ++i) {
        std::memcpy(srcs[i].data(), pts[i], sizeof(pts[i]));
        std::uint8_t* corner = mb.verts.data() + kVtxStride * (2 + i);
        putPtr(corner, 72, srcs[i].data());
    }

    // Identity affine + translation (10,20,30).
    float A[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 10,20,30,1};
    void* res = ComputeBoundingBox(mb.mesh.data(), A);
    CHECK(res == mb.verts.data() + kVtxStride * 2);

    for (int i = 0; i < 8; ++i) {
        std::uint8_t* corner = mb.verts.data() + kVtxStride * (2 + i);
        CHECK(std::fabs(getF(corner, 0) - (pts[i][0] + 10.0f)) < 1e-5f);
        CHECK(std::fabs(getF(corner, 4) - (pts[i][1] + 20.0f)) < 1e-5f);
        CHECK(std::fabs(getF(corner, 8) - (pts[i][2] + 30.0f)) < 1e-5f);
    }
}

TEST(MeshTransform, ComputeBoundingBox_EmptyMesh) {
    MeshBlock mb;
    mb.alloc(2, 8);
    putI32(mb.mesh.data(), 8, 0); // no vertices -> returns null
    float A[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    CHECK(ComputeBoundingBox(mb.mesh.data(), A) == nullptr);
}

// ---------------------------------------------------------------------------
// ComputeBoundingBox branch B (mesh+380 != 0): reduce sub-mesh keyframe AABBs.
// ---------------------------------------------------------------------------
TEST(MeshTransform, ComputeBoundingBox_BranchB) {
    MeshBlock mb;
    mb.alloc(2, 8);
    putI32(mb.mesh.data(), 8, 2);
    mb.mesh[380] = 1;

    // Mesh sub-meshes are 116-byte records from mesh+0..mesh+348 (so 3 records).
    // Each: +132 -> anim ptr (deref +348 -> keyframe base), +28 -> keyframe index.
    // We give the first sub-mesh a keyframe holding the box, others +132 == 0.
    std::vector<std::uint8_t> anim(512, 0);
    std::vector<std::uint8_t> keyframes(512, 0);
    putPtr(anim.data(), 348, keyframes.data());
    // keyframe record 0: float indices 15..20 = min/max box.
    float b[6] = {-1,-2,-3, 4,5,6}; // minX,minY,minZ, maxX,maxY,maxZ
    std::memcpy(keyframes.data() + 15 * 4, b, sizeof(b));

    putPtr(mb.mesh.data() + 0 * 116, 132, anim.data());
    putI32(mb.mesh.data() + 0 * 116, 28, 0);

    float A[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 10,20,30,1};
    void* res = ComputeBoundingBox(mb.mesh.data(), A);
    CHECK(res == mb.verts.data() + kVtxStride * 2);

    // Expected 8 transformed corners (python golden).
    float exp[8][3] = {
        {9,18,27},{14,18,27},{9,25,27},{14,25,27},
        {9,18,36},{14,18,36},{9,25,36},{14,25,36}};
    for (int i = 0; i < 8; ++i) {
        std::uint8_t* corner = mb.verts.data() + kVtxStride * (2 + i);
        CHECK(std::fabs(getF(corner, 0) - exp[i][0]) < 1e-4f);
        CHECK(std::fabs(getF(corner, 4) - exp[i][1]) < 1e-4f);
        CHECK(std::fabs(getF(corner, 8) - exp[i][2]) < 1e-4f);
    }
}

// ---------------------------------------------------------------------------
// ComputeHeightRange: vegetation y-extent via ComputeBoundingBox branch A.
// ---------------------------------------------------------------------------
TEST(MeshTransform, ComputeHeightRange_NonVegetation) {
    std::vector<std::uint8_t> obj(600, 0);
    obj[533] = 1; // not 4 -> early-out returns 1
    float lo = 0, hi = 0;
    CHECK_EQ(ComputeHeightRange(obj.data(), &lo, &hi, nullptr), 1);
}

TEST(MeshTransform, ComputeHeightRange_Vegetation) {
    // Vegetation object: +533 == 4. Build a branch-A mesh (mesh+380 == 0) whose 8
    // corner verts (after vcount) carry +72 source points with varied y, and an
    // identity matrix at draw+8 so the bounding box passes y straight through.
    MeshBlock mb;
    mb.alloc(2, 8);
    putI32(mb.mesh.data(), 8, 2);
    mb.mesh[380] = 0;

    std::vector<std::vector<std::uint8_t>> srcs(8, std::vector<std::uint8_t>(16, 0));
    float ys[8] = {3, -2, 7, 0, -5, 9, 1, 4};
    for (int i = 0; i < 8; ++i) {
        float p[3] = {0, ys[i], 0};
        std::memcpy(srcs[i].data(), p, sizeof(p));
        std::uint8_t* corner = mb.verts.data() + kVtxStride * (2 + i);
        putPtr(corner, 72, srcs[i].data());
    }

    std::vector<std::uint8_t> obj(600, 0);
    obj[533] = 4;
    putPtr(obj.data(), 460, mb.mesh.data());

    std::vector<std::uint8_t> draw(512, 0);
    putPtr(obj.data(), 492, draw.data());
    // Identity affine at draw+8 (the ComputeBoundingBox matrix arg).
    float A[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    std::memcpy(draw.data() + 8, A, sizeof(A));

    float lo = 0, hi = 0;
    CHECK_EQ(ComputeHeightRange(obj.data(), &lo, &hi, nullptr), 0);
    CHECK(std::fabs(lo - (-5.0f)) < 1e-5f); // min y across the 8 corners
    CHECK(std::fabs(hi - 9.0f) < 1e-5f);    // max y
}

// ---------------------------------------------------------------------------
// AccumulateVertexAabb / Bounds: grow a box by mesh verts.
// ---------------------------------------------------------------------------
TEST(MeshTransform, AccumulateVertexAabb) {
    MeshBlock mb;
    mb.alloc(3);
    float verts[3][3] = {{1,2,3},{-4,5,-6},{0,-1,9}};
    for (int i = 0; i < 3; ++i)
        std::memcpy(mb.vtx(i), verts[i], sizeof(verts[i]));

    std::vector<std::uint8_t> obj(600, 0);
    putPtr(obj.data(), 460, mb.mesh.data());

    float box[8] = {1e10f, 1e10f, 1e10f, 0, -1e10f, -1e10f, -1e10f, 0};
    CHECK(AccumulateVertexAabb(obj.data(), box) == 1);
    CHECK(std::fabs(box[0] - (-4.0f)) < 1e-6f);   // min x
    CHECK(std::fabs(box[1] - (-1.0f)) < 1e-6f);   // min y
    CHECK(std::fabs(box[2] - (-6.0f)) < 1e-6f);   // min z
    CHECK(std::fabs(box[4] - 1.0f) < 1e-6f);      // max x
    CHECK(std::fabs(box[5] - 5.0f) < 1e-6f);      // max y
    CHECK(std::fabs(box[6] - 9.0f) < 1e-6f);      // max z
}

TEST(MeshTransform, AccumulateVertexBounds_CullGate) {
    MeshBlock mb;
    mb.alloc(1);
    float v[3] = {7,8,9};
    std::memcpy(mb.vtx(0), v, sizeof(v));

    std::vector<std::uint8_t> obj(600, 0);
    putPtr(obj.data(), 460, mb.mesh.data());

    // +530 with bit2 set ((f & 0xC) != 0) and bit4 clear -> gate BLOCKS accumulation.
    obj[530] = 0x04;
    float box[8] = {1e10f, 1e10f, 1e10f, 0, -1e10f, -1e10f, -1e10f, 0};
    AccumulateVertexBounds(obj.data(), box);
    CHECK(box[0] > 1e9f); // unchanged

    // Now allow via bit4 (0x10) set.
    obj[530] = 0x14;
    AccumulateVertexBounds(obj.data(), box);
    CHECK(std::fabs(box[0] - 7.0f) < 1e-6f);
    CHECK(std::fabs(box[4] - 7.0f) < 1e-6f);
}

// ---------------------------------------------------------------------------
// ComputeBoundingRadius: centroid + farthest corner.
// ---------------------------------------------------------------------------
static float g_corners[8 * 20];
static const float* CornerBufferStub(void* /*obj*/) { return g_corners; }

TEST(MeshTransform, ComputeBoundingRadius) {
    float pts[8][3] = {
        {-1,-2,-3},{4,-2,-3},{-1,5,-3},{4,5,-3},
        {-1,-2,6},{4,-2,6},{-1,5,6},{4,5,6}};
    std::memset(g_corners, 0, sizeof(g_corners));
    for (int i = 0; i < 8; ++i) {
        g_corners[i * 20 + 0] = pts[i][0];
        g_corners[i * 20 + 1] = pts[i][1];
        g_corners[i * 20 + 2] = pts[i][2];
    }

    std::vector<std::uint8_t> obj(600, 0);
    std::vector<std::uint8_t> draw(512, 0);
    putPtr(obj.data(), 492, draw.data());
    putI32(draw.data(), 260, 1); // non-zero -> fetch corners

    float radius = -1.0f, center[3] = {0,0,0};
    guild::u8 ok = ComputeBoundingRadius(obj.data(), nullptr, &radius, center, &CornerBufferStub);
    CHECK(ok == 1);
    CHECK(std::fabs(center[0] - 1.5f) < 1e-5f);
    CHECK(std::fabs(center[1] - 1.5f) < 1e-5f);
    CHECK(std::fabs(center[2] - 1.5f) < 1e-5f);
    CHECK(std::fabs(radius - 6.224949836730957f) < 1e-4f);
}

TEST(MeshTransform, ComputeBoundingRadius_NullArgs) {
    CHECK(ComputeBoundingRadius(nullptr, nullptr, nullptr, nullptr, nullptr) == 0);
}

// ---------------------------------------------------------------------------
// Vertex colour stamping.
// ---------------------------------------------------------------------------
TEST(MeshTransform, SetVertexColors) {
    // mesh+4 -> array of vertex pointers, 10-ptr stride per sub-mesh, mesh+12 count.
    MeshBlock mb;
    std::vector<std::uint8_t> v0(80, 0), v1(80, 0), v2(80, 0);
    void* ptrs[3] = {v0.data(), v1.data(), v2.data()};
    putPtr(mb.mesh.data(), 4, ptrs);
    putI32(mb.mesh.data(), 12, 1); // 1 sub-mesh; loop runs v4..v4+3 (3 vertices)

    std::vector<std::uint8_t> obj(600, 0);
    putPtr(obj.data(), 460, mb.mesh.data());

    SetVertexColors(obj.data(), /*b=*/0x11, /*g=*/0x22, /*r=*/0x33);
    // Write mapping: +70=b, +69=r, +68=g.
    for (auto* v : {v0.data(), v1.data(), v2.data()}) {
        CHECK_EQ(v[70], 0x11);
        CHECK_EQ(v[69], 0x33);
        CHECK_EQ(v[68], 0x22);
    }
    CHECK((obj[530] & 0x02) != 0);
    CHECK((obj[528] & 0x04) != 0);
}

TEST(MeshTransform, SetVertexColors_AllZeroRebuildsCache) {
    MeshBlock mb;
    std::vector<std::uint8_t> obj(600, 0);
    putPtr(obj.data(), 460, mb.mesh.data());
    obj[530] = 0x02;

    int before = g_buildCacheCalls;
    SetVertexColors(obj.data(), 0, 0, 0);
    CHECK(g_buildCacheCalls == before + 1);
    CHECK((obj[530] & 0x02) == 0);
    CHECK((obj[528] & 0x04) != 0);
}

TEST(MeshTransform, ResetVertexColors_Neutral) {
    MeshBlock mb;
    std::vector<std::uint8_t> v0(80, 0), v1(80, 0), v2(80, 0);
    void* ptrs[3] = {v0.data(), v1.data(), v2.data()};
    putPtr(mb.mesh.data(), 4, ptrs);
    putI32(mb.mesh.data(), 12, 1);

    std::vector<std::uint8_t> obj(600, 0);
    putPtr(obj.data(), 460, mb.mesh.data());

    ResetVertexColors(obj.data(), 1);
    for (auto* v : {v0.data(), v1.data(), v2.data()}) {
        CHECK_EQ(v[68], 0x80);
        CHECK_EQ(v[69], 0x80);
        CHECK_EQ(v[70], 0x80);
    }
}

// ---------------------------------------------------------------------------
// Bone samplers.
// ---------------------------------------------------------------------------
TEST(MeshTransform, GetBonePosition) {
    // bone+104 -> anim; anim+348 -> keyframe base; 192-byte records.
    std::vector<std::uint8_t> bone(256, 0);
    std::vector<std::uint8_t> anim(512, 0);
    std::vector<std::uint8_t> keyframes(512, 0);
    putPtr(bone.data(), 104, anim.data());
    putPtr(anim.data(), 348, keyframes.data());

    // keyframe 1 translation (float idx 11..13 = bytes +44/+48/+52).
    float trans[3] = {1.0f, 2.0f, 3.0f};
    std::memcpy(keyframes.data() + 192 * 1 + 44, trans, sizeof(trans));

    float boneFrame[36] = {0};
    boneFrame[33] = 10.0f; boneFrame[34] = 20.0f; boneFrame[35] = 30.0f;

    float out[3] = {0,0,0};
    float* r = GetBonePosition(boneFrame, bone.data(), 1, out);
    CHECK(r == out);
    CHECK(std::fabs(out[0] - 11.0f) < 1e-6f);
    CHECK(std::fabs(out[1] - 22.0f) < 1e-6f);
    CHECK(std::fabs(out[2] - 33.0f) < 1e-6f);
}

TEST(MeshTransform, GetBoneFramePose) {
    std::vector<std::uint8_t> bone(256, 0);
    std::vector<std::uint8_t> anim(512, 0);
    std::vector<std::uint8_t> keyframes(512, 0);
    putPtr(bone.data(), 104, anim.data());
    putPtr(anim.data(), 348, keyframes.data());

    float rot[3]   = {0.1f, 0.2f, 0.3f};
    float trans[3] = {4.0f, 5.0f, 6.0f};
    std::memcpy(keyframes.data() + 192 * 2 + 32, rot, sizeof(rot));
    std::memcpy(keyframes.data() + 192 * 2 + 44, trans, sizeof(trans));

    float oR[3] = {0,0,0}, oT[3] = {0,0,0};
    int e = GetBoneFramePose(bone.data(), oR, 2, oT);
    CHECK_EQ(e, 192 * 2);
    CHECK(std::fabs(oR[0] - 0.1f) < 1e-6f);
    CHECK(std::fabs(oR[2] - 0.3f) < 1e-6f);
    CHECK(std::fabs(oT[0] - 4.0f) < 1e-6f);
    CHECK(std::fabs(oT[2] - 6.0f) < 1e-6f);
}
