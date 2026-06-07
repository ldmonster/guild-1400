#include "test.h"

#include "render/mesh_transform.h"
#include "render/bone_sample.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <vector>

using namespace guild::render;

// ---------------------------------------------------------------------------
// External leaves: ComputeBoneWorldMatrix is REAL (skeleton.cpp). The two
// genuinely-unowned leaves (TransformBoundingVolume / BuildObjectCache) and the
// g_buildCacheCalls counter now live ONCE in the library
// (src/render/mesh_transform_unowned_stubs.cpp), so every test executable links.
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

// Single-bone frame holding `m` as its local 4x4 (byte 396), no parent — see the
// unit test's BoneFrame. Doubles as the object record (we only read +528 / +492).
struct BoneFrame {
    std::vector<std::uint8_t> buf = std::vector<std::uint8_t>(640, 0);
    explicit BoneFrame(const float m[16])
    {
        for (int i = 0; i < 16; ++i)
            putF(buf.data(), 396 + 4 * i, m[i]);
    }
    void* obj() { return buf.data(); }
};

} // namespace

// ---------------------------------------------------------------------------
// Full skin -> bound -> radius pipeline across the module's functions.
// Skins a packed mesh, accumulates its model-space AABB, and (separately, from a
// world-corner buffer) derives a bounding radius. Verifies the pieces agree.
// ---------------------------------------------------------------------------
TEST(MeshTransformE2E, SkinThenBound) {
    // Identity rotation: skinned positions == dequantised packed bytes.
    float m[16] = {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    BoneFrame bf(m);
    bf.buf[528] = 0x80; // pivotless

    const int vcount = 4;
    std::vector<std::uint8_t> mesh(512, 0);
    std::vector<std::uint8_t> verts(kVtxStride * vcount, 0);
    putPtr(mesh.data(), 0, verts.data());
    putI32(mesh.data(), 8, vcount);

    std::uint8_t* obj = reinterpret_cast<std::uint8_t*>(bf.obj());
    putPtr(obj, 460, mesh.data());

    // Packed positions (unsigned bytes). dequant = (b - 128) / 127.5.
    std::uint8_t packed[12] = {
        0,   128, 255,   // corner-ish extremes
        128, 0,   128,
        255, 255, 0,
        64,  192, 100,
    };
    TransformPackedVertices(obj, mesh.data(), packed, nullptr);

    // The skinned xyz landed in vertex +32; copy them to +0 so the AABB walk (which
    // reads +0) sees the skinned model positions (mirrors how the engine's skin
    // slot feeds the later passes).
    for (int i = 0; i < vcount; ++i) {
        float x = getF(verts.data() + kVtxStride * i, 32);
        float y = getF(verts.data() + kVtxStride * i, 36);
        float z = getF(verts.data() + kVtxStride * i, 40);
        std::memcpy(verts.data() + kVtxStride * i + 0, &x, 4);
        std::memcpy(verts.data() + kVtxStride * i + 4, &y, 4);
        std::memcpy(verts.data() + kVtxStride * i + 8, &z, 4);
    }

    float box[8] = {1e10f, 1e10f, 1e10f, 0, -1e10f, -1e10f, -1e10f, 0};
    CHECK(AccumulateVertexAabb(obj, box) == 1);

    // Independently compute the expected extremes via the same dequant.
    auto deq = [](std::uint8_t b) {
        return (static_cast<float>(static_cast<std::int16_t>(b)) + kPackedBias) * kPackedScale;
    };
    float mnx = 1e10f, mny = 1e10f, mnz = 1e10f, mxx = -1e10f, mxy = -1e10f, mxz = -1e10f;
    for (int i = 0; i < vcount; ++i) {
        float x = deq(packed[i * 3 + 0]);
        float y = deq(packed[i * 3 + 1]);
        float z = deq(packed[i * 3 + 2]);
        if (x < mnx) mnx = x;
        if (x > mxx) mxx = x;
        if (y < mny) mny = y;
        if (y > mxy) mxy = y;
        if (z < mnz) mnz = z;
        if (z > mxz) mxz = z;
    }
    CHECK(std::fabs(box[0] - mnx) < 1e-5f);
    CHECK(std::fabs(box[1] - mny) < 1e-5f);
    CHECK(std::fabs(box[2] - mnz) < 1e-5f);
    CHECK(std::fabs(box[4] - mxx) < 1e-5f);
    CHECK(std::fabs(box[5] - mxy) < 1e-5f);
    CHECK(std::fabs(box[6] - mxz) < 1e-5f);

    // Build the 8 world corners of that box and derive a radius. Centroid is the box
    // centre; radius is the half-diagonal length.
    static float corners[8 * 20];
    std::memset(corners, 0, sizeof(corners));
    float xs[2] = {box[0], box[4]}, ys[2] = {box[1], box[5]}, zs[2] = {box[2], box[6]};
    int c = 0;
    for (int zi = 0; zi < 2; ++zi)
        for (int yi = 0; yi < 2; ++yi)
            for (int xi = 0; xi < 2; ++xi) {
                corners[c * 20 + 0] = xs[xi];
                corners[c * 20 + 1] = ys[yi];
                corners[c * 20 + 2] = zs[zi];
                ++c;
            }
    struct Buf { static const float* get(void*) { return corners; } };

    std::vector<std::uint8_t> draw(512, 0);
    putPtr(obj, 492, draw.data());
    putI32(draw.data(), 260, 1);

    float radius = 0.0f, center[3] = {0,0,0};
    guild::u8 ok = ComputeBoundingRadius(obj, nullptr, &radius, center, &Buf::get);
    CHECK(ok == 1);
    CHECK(std::fabs(center[0] - (mnx + mxx) * 0.5f) < 1e-4f);
    CHECK(std::fabs(center[1] - (mny + mxy) * 0.5f) < 1e-4f);
    CHECK(std::fabs(center[2] - (mnz + mxz) * 0.5f) < 1e-4f);

    float hx = (mxx - mnx) * 0.5f, hy = (mxy - mny) * 0.5f, hz = (mxz - mnz) * 0.5f;
    float expR = std::sqrt(hx * hx + hy * hy + hz * hz);
    CHECK(std::fabs(radius - expR) < 1e-3f);
}

// ---------------------------------------------------------------------------
// Bone position/pose sampling chained: pose's translation must match position's
// keyframe contribution (position = boneFrame world + keyframe translation).
// ---------------------------------------------------------------------------
TEST(MeshTransformE2E, BoneSampleChain) {
    std::vector<std::uint8_t> bone(256, 0);
    std::vector<std::uint8_t> anim(512, 0);
    std::vector<std::uint8_t> kf(1024, 0);
    putPtr(bone.data(), 104, anim.data());
    putPtr(anim.data(), 348, kf.data());

    float rot[3]   = {0.25f, -0.5f, 0.75f};
    float trans[3] = {2.0f, -3.0f, 4.0f};
    std::memcpy(kf.data() + 192 * 3 + 32, rot, sizeof(rot));
    std::memcpy(kf.data() + 192 * 3 + 44, trans, sizeof(trans));

    float oR[3], oT[3];
    GetBoneFramePose(bone.data(), oR, 3, oT);

    float frame[36] = {0};
    frame[33] = 100.0f; frame[34] = 200.0f; frame[35] = 300.0f;
    float pos[3];
    GetBonePosition(frame, bone.data(), 3, pos);

    CHECK(std::fabs(pos[0] - (frame[33] + oT[0])) < 1e-6f);
    CHECK(std::fabs(pos[1] - (frame[34] + oT[1])) < 1e-6f);
    CHECK(std::fabs(pos[2] - (frame[35] + oT[2])) < 1e-6f);
    CHECK(std::fabs(oR[1] - (-0.5f)) < 1e-6f);
}

// ---------------------------------------------------------------------------
// GUARDED real-asset check: Objects.BIN / Groups.BIN hold the real meshes. We do
// not have a full BGF parser wired into this leaf module, so this test only
// confirms the asset containers are present and non-trivial (skip-pass if absent).
// ---------------------------------------------------------------------------
TEST(MeshTransformE2E, RealAssetsPresent) {
    const char* paths[] = {
        "europe_guild_1400_original/Resources/Objects.BIN",
        "europe_guild_1400_original/Resources/Groups.BIN",
    };
    bool any = false;
    for (const char* p : paths) {
        std::FILE* f = std::fopen(p, "rb");
        if (!f) continue;
        any = true;
        std::fseek(f, 0, SEEK_END);
        long sz = std::ftell(f);
        std::fclose(f);
        CHECK(sz > 0);
    }
    if (!any) {
        std::printf("    [skip] real asset BINs absent — skip-pass\n");
    }
    CHECK(true);
}
