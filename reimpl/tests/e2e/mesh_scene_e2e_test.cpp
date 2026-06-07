// End-to-end flow for guild::render mesh memory-accounting + AABB math.
// Builds a small scene-graph-shaped byte image (a scene with a sub-scene block of
// node entries, each carrying a sized node), installs the cross-module hooks, then
// drives the VIBE_Mesh memory-estimator + AABB pipeline:
//   ClearObjectPoolDirtyFlags -> SumObjectPoolMemory ->
//   ComputeSceneMemorySize (groups 0x1, 0x2) -> AccumulateVertexAabb -> ComputeAabbExtents
//
// NOTE on layout: the original 32-bit scene-entry has adjacent 4-byte pointer slots
// (e.g. +260 and +264). With native 64-bit pointers two such slots overlap, so the
// e2e exercises the groups that touch a single entry pointer (0x1 reads +260 only
// when +264 is clear; 0x2 reads +260). The 0x80 path (which needs +260 AND +264 live
// simultaneously) is covered structurally by the unit tests / translation; it is not
// golden-driven here to avoid a 64-bit-only pointer-overlap artifact.
#include "render/mesh_scene.h"
#include "render/mesh_transform.h" // reused AccumulateVertexAabb (0x5b29d8)
#include "test.h"

#include <cstdint>
#include <cstring>
#include <vector>

using namespace guild::render;

namespace {
struct Img {
    std::vector<std::uint8_t> b;
    explicit Img(std::size_t n) : b(n, 0) {}
    ByteRec rec() { return ByteRec(b.data()); }
    void u32(int o, std::uint32_t v) { std::memcpy(&b[o], &v, 4); }
    void u8 (int o, std::uint8_t  v) { b[o] = v; }
    void ptr(int o, void* p) { std::memcpy(&b[o], &p, sizeof(p)); }
};
} // namespace

TEST(MeshSceneE2E, BuildTraverseAndSize) {
    // ---- object pool (two 128-byte slots) ----
    std::vector<std::uint8_t> pool(128 * 2, 0);
    auto poolU32 = [&](int s, int o, std::uint32_t v) { std::memcpy(&pool[s * 128 + o], &v, 4); };
    poolU32(0, 64, 7);        // slot0 active
    pool[0 * 128 + 104] = 0;  // flag non-negative, no bits
    poolU32(0, 96, 0);        // header-only object -> 128 bytes
    poolU32(1, 64, 0);        // slot1 inactive (skipped)

    auto& h = MeshMemoryHooksMut();
    h.objectPoolBase = pool.data();
    h.objectPoolCount = 2;

    // Step 1: pool aggregation (clears dirty flags then sums header-only object).
    CHECK_EQ(SumObjectPoolMemory(), 128);

    // Step 2: scene group 0x1 — header + lighting block + sub-scene group-1 costs.
    // Entry: +244/+248 plane flags off, +264 array pointer left null so the
    // "4*node[480]" sub-branch is skipped (single-pointer-per-entry layout).
    {
        Img scene(1024);
        Img block(4096);
        scene.u32(488, 1);             // lighting block present -> 540 + 428
        scene.ptr(492, block.b.data());
        block.u8(2316, 0);             // zero entries (keeps the +2312 tail pointer
                                       // slot all-zero -> null, exercising the header
                                       // + lighting + 2320 base block cost only).
        int out = -1;
        CHECK(ComputeSceneMemorySize(scene.rec(), 0x1, &out));
        CHECK_EQ(out, 540 + 428 + 2320); // 3288
    }

    // Step 3: scene group 0x2 — sum ComputeNodeMemorySize over entries (uses +260).
    {
        Img scene(1024);
        Img block(4096);
        Img node(600);
        node.u32(68, 4);   // v2 = 4 (> 0) -> +24*(4+8) = 288
        node.u32(480, 1);
        node.u32(484, 2);  // 1*2 = 2
        node.u32(76, 1);   // 56
        block.u8(2316, 1);
        block.ptr(260, node.b.data());
        block.u32(1412, 0);
        scene.ptr(492, block.b.data());
        int out = -1;
        CHECK(ComputeSceneMemorySize(scene.rec(), 0x2, &out));
        // node = 56 + (2 + 524 + 288) = 56 + 814 = 870
        CHECK_EQ(out, 870);
    }

    // ---- AABB build/fold flow over a small vertex list ----
    Img meshObj(512);
    Img meshRec(64);
    std::vector<std::uint8_t> verts(80 * 4, 0);
    auto putf = [&](int v, int c, float f) { std::memcpy(&verts[v * 80 + c * 4], &f, 4); };
    putf(0, 0, 2.0f);  putf(0, 1, 0.0f);  putf(0, 2, 1.0f);
    putf(1, 0, -3.0f); putf(1, 1, 4.0f);  putf(1, 2, -1.0f);
    putf(2, 0, 0.5f);  putf(2, 1, -2.0f); putf(2, 2, 9.0f);
    putf(3, 0, 1.0f);  putf(3, 1, 1.0f);  putf(3, 2, 1.0f);
    meshRec.ptr(0, verts.data());
    meshRec.u32(8, 4);
    meshObj.ptr(460, meshRec.b.data());

    // box layout {min0,min1,min2, _gap_, max0,max1,max2} (reused 0x5b29d8).
    float box[7] = {1e30f, 1e30f, 1e30f, 0, -1e30f, -1e30f, -1e30f};
    CHECK(AccumulateVertexAabb(meshObj.b.data(), box));
    CHECK_EQ(box[0], -3.0f); CHECK_EQ(box[1], -2.0f); CHECK_EQ(box[2], -1.0f);
    CHECK_EQ(box[4], 2.0f);  CHECK_EQ(box[5], 4.0f);  CHECK_EQ(box[6], 9.0f);

    // ---- the per-triangle overlap predicate against the folded box ----
    float* tri[3] = {
        reinterpret_cast<float*>(&verts[0]),
        reinterpret_cast<float*>(&verts[80]),
        reinterpret_cast<float*>(&verts[160]),
    };
    float lo[3] = {box[0], box[1], box[2]};
    float hi[3] = {box[4], box[5], box[6]};
    CHECK(ComputeAabbExtents(tri, lo, hi)); // triangle lies within its own box

    // a far-away query box must NOT overlap.
    float farLo[3] = {100.0f, 100.0f, 100.0f};
    float farHi[3] = {200.0f, 200.0f, 200.0f};
    CHECK(!ComputeAabbExtents(tri, farLo, farHi));

    h.objectPoolBase = nullptr;
    h.objectPoolCount = 0;
}
