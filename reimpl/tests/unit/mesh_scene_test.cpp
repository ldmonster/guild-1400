// Unit tests for guild::render mesh memory-accounting + AABB-math leaves
// (gilde.exe VIBE_Mesh_*). Golden values computed independently with python3 by
// replaying the exact integer/float arithmetic of the originals.
#include "render/mesh_scene.h"
#include "render/mesh_transform.h" // reused AccumulateVertexAabb (0x5b29d8)
#include "test.h"

#include <cstdint>
#include <cstring>
#include <vector>

using namespace guild::render;

namespace {

// A zero-initialised byte buffer big enough for the largest record we exercise.
struct Buf {
    std::vector<std::uint8_t> bytes;
    explicit Buf(std::size_t n) : bytes(n, 0) {}
    ByteRec rec() { return ByteRec(bytes.data()); }
    void putU32(int off, std::uint32_t v) { std::memcpy(&bytes[off], &v, 4); }
    void putU8 (int off, std::uint8_t  v) { bytes[off] = v; }
    void putPtr(int off, void* p) { std::memcpy(&bytes[off], &p, sizeof(p)); }
};

} // namespace

// ---------------------------------------------------------------------------
// VIBE_Mesh_ComputeBitmapMemorySize (0x5f4f7c)
//   base, dim@+32, flagA@+40, flagB@+36 ; v=48; if(flagA) v=dim*dim+48; if(flagB) v+=24*dim*dim
// ---------------------------------------------------------------------------
TEST(MeshScene, BitmapMemoryNull) {
    CHECK_EQ(ComputeBitmapMemorySize(ByteRec()), 0);
}
TEST(MeshScene, BitmapMemorySizes) {
    Buf b(64);
    b.putU32(32, 16); // dim
    // no flags -> just header 48
    CHECK_EQ(ComputeBitmapMemorySize(b.rec()), 48);
    b.putU32(40, 1);  // flagA
    CHECK_EQ(ComputeBitmapMemorySize(b.rec()), 16 * 16 + 48); // 304
    b.putU32(36, 1);  // flagB
    CHECK_EQ(ComputeBitmapMemorySize(b.rec()), 16 * 16 + 48 + 24 * 16 * 16); // 6448
}

// ---------------------------------------------------------------------------
// VIBE_Mesh_ComputeNodeMemorySize (0x5f4e08)
//   guard: a1 && !(node[521]&1); v2=node[68]; v3=node[480]*node[484]+524;
//   if v2>0 v3 += 24*(v2+8); v1 = 56*node[76]+v3; sets bit0 of +521.
// ---------------------------------------------------------------------------
TEST(MeshScene, NodeMemoryDirtyGuard) {
    Buf b(600);
    b.putU8(521, 1); // already dirty
    CHECK_EQ(ComputeNodeMemorySize(b.rec()), 0);
    CHECK_EQ(ComputeNodeMemorySize(ByteRec()), 0);
}
TEST(MeshScene, NodeMemoryValue) {
    Buf b(600);
    b.putU32(68, 10);   // v2
    b.putU32(480, 4);
    b.putU32(484, 5);   // 4*5=20
    b.putU32(76, 3);    // 56*3
    // v3 = 20 + 524 + 24*(10+8) = 544 + 432 = 976 ; v1 = 168 + 976 = 1144
    CHECK_EQ(ComputeNodeMemorySize(b.rec()), 1144);
    // bit0 of +521 now set -> a second call returns 0.
    CHECK_EQ(ComputeNodeMemorySize(b.rec()), 0);
}
TEST(MeshScene, NodeMemoryNoExtra) {
    Buf b(600);
    b.putU32(68, 0);    // v2 <= 0
    b.putU32(480, 2);
    b.putU32(484, 3);   // 6
    b.putU32(76, 0);
    // v3 = 6 + 524 = 530 ; v1 = 0 + 530
    CHECK_EQ(ComputeNodeMemorySize(b.rec()), 530);
}

// ---------------------------------------------------------------------------
// VIBE_Mesh_ComputeObjectMemorySize (0x5f4e8c)
//   flag@+104; if flag<0 -> 0. v3=128. if flag&8: raw = w*w*v6, else mip = trunc(w*w*v6 * 4 * 0.3333..)
//   w@+116; v6 = (+124>>3) (+1 if +124&7); pointer guard +96; LOD copies via +112/+113 when a2.
// ---------------------------------------------------------------------------
TEST(MeshScene, ObjectMemoryNegativeFlag) {
    Buf b(160);
    b.putU8(104, 0x80); // negative -> v4>=0 false -> v3 stays 0
    CHECK_EQ(ComputeObjectMemorySize(b.rec(), false), 0);
}
TEST(MeshScene, ObjectMemoryHeaderOnly) {
    Buf b(160);
    b.putU8(104, 0);    // non-negative, no flags
    b.putU32(96, 0);    // no geometry pointer -> just header
    CHECK_EQ(ComputeObjectMemorySize(b.rec(), false), 128);
}
TEST(MeshScene, ObjectMemoryRawArea) {
    Buf b(160);
    b.putU8(104, 8);    // bit3 set -> raw area path
    b.putU32(96, 1);    // geometry present
    b.putU32(116, 8);   // w = 8
    b.putU8(124, 16);   // v6 = 16>>3 = 2 (no remainder)
    // v10 = 2*8*8 = 128 ; v3 = 128 + 128 = 256
    CHECK_EQ(ComputeObjectMemorySize(b.rec(), false), 256);
}
TEST(MeshScene, ObjectMemoryMipArea) {
    Buf b(160);
    b.putU8(104, 0);    // no bit3 -> mip pyramid path
    b.putU32(96, 1);
    b.putU32(116, 8);   // w = 8
    b.putU8(124, 17);   // 17>>3 = 2, 17&7=1 -> v6 = 3
    // texels = 8*8*3 = 192 ; mip = trunc((double)192 * 4.0f * 0.33333334f)
    //        = trunc(256.0000076...) = 256  (verified against the exact C++ product)
    CHECK_EQ(ComputeObjectMemorySize(b.rec(), false), 128 + 256);
}
TEST(MeshScene, ObjectMemoryLodCopies) {
    Buf b(160);
    b.putU8(104, 8);    // raw path, area = 2*8*8 = 128
    b.putU32(96, 1);
    b.putU32(116, 8);
    b.putU8(124, 16);
    b.putU8(112, 4);    // 4 LOD copies
    b.putU8(113, 0);    // not suppressed
    // a2=true -> v10 *= 4 -> 512 ; v3 = 128 + 512 = 640
    CHECK_EQ(ComputeObjectMemorySize(b.rec(), true), 640);
    // a2=false -> no multiply -> 256
    Buf c(160);
    c.putU8(104, 8); c.putU32(96, 1); c.putU32(116, 8); c.putU8(124, 16);
    c.putU8(112, 4); c.putU8(113, 0);
    CHECK_EQ(ComputeObjectMemorySize(c.rec(), false), 256);
}

// ---------------------------------------------------------------------------
// VIBE_Mesh_ComputeSurfaceMemorySize (0x5f4fb8) — header + planes + (no mip rows set)
// ---------------------------------------------------------------------------
TEST(MeshScene, SurfaceMemoryNull) {
    CHECK_EQ(ComputeSurfaceMemorySize(ByteRec(), false), 0);
}
TEST(MeshScene, SurfaceMemoryHeaderAndPlanes) {
    Buf b(8192);
    b.putU32(0, 8);  // dim -> v5 = 64
    b.putU32(4, 8);
    // plane flags +16/+20/+24/+28
    b.putU32(16, 1); // v4 = 64 + 7288
    b.putU32(20, 1); // += 64
    // +24,+28 zero. mip rows / sub-surfaces all zero, +6624 zero.
    // sub-surface descriptor loop runs but all present-flags are 0 -> no add.
    int expect = 7288 + 64 + 64;
    CHECK_EQ(ComputeSurfaceMemorySize(b.rec(), false), expect);
}

// ---------------------------------------------------------------------------
// VIBE_Mesh_ComputeSceneMemorySize (0x5f51e4) — exercise the deterministic groups.
// ---------------------------------------------------------------------------
TEST(MeshScene, SceneMemoryNullArgs) {
    int out = 99;
    CHECK(!ComputeSceneMemorySize(ByteRec(), 0x1, &out));
    Buf b(4096);
    CHECK(!ComputeSceneMemorySize(b.rec(), 0x1, nullptr));
}
TEST(MeshScene, SceneMemoryGroup1NoBlock) {
    Buf scene(1024);
    scene.putU32(488, 0); // no lighting block
    // +492 block pointer null -> block branch skipped.
    int out = -1;
    bool ok = ComputeSceneMemorySize(scene.rec(), 0x1, &out);
    CHECK(ok);
    CHECK_EQ(out, 540); // just the 540 header
    scene.putU32(488, 1);
    ComputeSceneMemorySize(scene.rec(), 0x1, &out);
    CHECK_EQ(out, 540 + 428);
}
TEST(MeshScene, SceneMemoryGroup2Nodes) {
    // Build a scene with a +492 block containing 1 node-entry whose +260 node ptr
    // points at a node record we can size. Group bit 0x2 (NodeMemorySize sum).
    Buf scene(1024);
    Buf block(4096);
    Buf node(600);
    node.putU32(68, 0); node.putU32(480, 2); node.putU32(484, 3); node.putU32(76, 0);
    // node value = 6 + 524 = 530
    block.putU8(2316, 1);            // node count = 1
    block.putPtr(260, node.bytes.data()); // entry[0].+260 -> node
    block.putU32(1412, 0);           // no global node
    scene.putPtr(492, block.bytes.data());
    int out = -1;
    bool ok = ComputeSceneMemorySize(scene.rec(), 0x2, &out);
    CHECK(ok);
    CHECK_EQ(out, 530);
}

// ---------------------------------------------------------------------------
// Object pool aggregation: SumObjectPoolMemory + ClearObjectPoolDirtyFlags via hooks.
// ---------------------------------------------------------------------------
TEST(MeshScene, SumObjectPoolMemory) {
    // Two 128-byte pool slots. Slot 0: +64 > 0, header-only object. Slot 1: +64 == 0
    // (skipped). ComputeObjectMemorySize(slot,0): flag@+104 non-neg, +96 == 0 -> 128.
    std::vector<std::uint8_t> pool(128 * 2, 0);
    auto put32 = [&](int slot, int off, std::uint32_t v) {
        std::memcpy(&pool[slot * 128 + off], &v, 4);
    };
    put32(0, 64, 5);   // slot0 active
    pool[0 * 128 + 104] = 0; // flag non-negative
    put32(0, 96, 0);   // header only
    put32(1, 64, 0);   // slot1 inactive
    auto& h = MeshMemoryHooksMut();
    h.objectPoolBase = pool.data();
    h.objectPoolCount = 2;
    CHECK_EQ(SumObjectPoolMemory(), 128);
    h.objectPoolBase = nullptr;
    h.objectPoolCount = 0;
}
TEST(MeshScene, ClearObjectPoolDirtyFlags) {
    std::vector<std::uint8_t> pool(128 * 2, 0);
    pool[0 * 128 + 104] = 0xFF;
    pool[1 * 128 + 104] = 0x81;
    auto& h = MeshMemoryHooksMut();
    h.objectPoolBase = pool.data();
    h.objectPoolCount = 2;
    ClearObjectPoolDirtyFlags(0);          // bit 0x100 clear -> clears bit7
    CHECK_EQ((int)pool[0 * 128 + 104], 0x7F);
    CHECK_EQ((int)pool[1 * 128 + 104], 0x01);
    // bit 0x100 set -> no-op
    pool[0 * 128 + 104] = 0xFF;
    ClearObjectPoolDirtyFlags(0x100);
    CHECK_EQ((int)pool[0 * 128 + 104], 0xFF);
    h.objectPoolBase = nullptr;
    h.objectPoolCount = 0;
}

// ---------------------------------------------------------------------------
// VIBE_Mesh_ComputeAabbExtents (0x42756c) — per-triangle min/max overlap test.
// ---------------------------------------------------------------------------
TEST(MeshScene, AabbExtentsOverlap) {
    float v0[3] = {0.0f, 0.0f, 0.0f};
    float v1[3] = {1.0f, 2.0f, 1.0f};
    float v2[3] = {2.0f, 1.0f, 3.0f};
    float* tri[3] = {v0, v1, v2};
    // triangle x-extent [0,2], y [0,2], z [0,3]
    float lo[3] = {-1.0f, -1.0f, -1.0f};
    float hi[3] = { 5.0f,  5.0f,  5.0f};
    CHECK(ComputeAabbExtents(tri, lo, hi)); // fully inside -> overlap

    float loFar[3] = {10.0f, 10.0f, 10.0f};
    float hiFar[3] = {20.0f, 20.0f, 20.0f};
    CHECK(!ComputeAabbExtents(tri, loFar, hiFar)); // disjoint -> no overlap
}
TEST(MeshScene, AabbExtentsBoundary) {
    float v0[3] = {0.0f, 0.0f, 0.0f};
    float v1[3] = {0.0f, 0.0f, 0.0f};
    float v2[3] = {0.0f, 0.0f, 0.0f};
    float* tri[3] = {v0, v1, v2};
    // point at origin. hi must be >= maxX (0) and <= ... ; lo <= 0.
    float lo[3] = {0.0f, 0.0f, 0.0f};
    float hi[3] = {0.0f, 0.0f, 0.0f};
    CHECK(ComputeAabbExtents(tri, lo, hi)); // touching counts (>= / <=)
}

// ---------------------------------------------------------------------------
// VIBE_Mesh_AccumulateVertexAabb (0x5b29d8) — reused from render/mesh_transform.cpp.
// box layout (per the original do/while): {min0,min1,min2, _gap_, max0,max1,max2}
// i.e. min at [0..2], max at [4..6].
// ---------------------------------------------------------------------------
TEST(MeshScene, AccumulateVertexAabbNull) {
    Buf obj(512);
    obj.putPtr(460, nullptr); // no mesh
    float box[7] = {100, 100, 100, 0, -100, -100, -100};
    CHECK(AccumulateVertexAabb(obj.bytes.data(), box));
    CHECK_EQ(box[0], 100.0f);
    CHECK_EQ(box[4], -100.0f);
}
TEST(MeshScene, AccumulateVertexAabbFold) {
    // mesh record: +0 vertex base ptr, +8 count. Vertices 80-byte stride, xyz at +0.
    Buf obj(512);
    Buf mesh(64);
    std::vector<std::uint8_t> verts(80 * 3, 0);
    auto putf = [&](int v, int comp, float f) {
        std::memcpy(&verts[v * 80 + comp * 4], &f, 4);
    };
    putf(0, 0, 1.0f);  putf(0, 1, 2.0f);  putf(0, 2, 3.0f);
    putf(1, 0, -1.0f); putf(1, 1, 5.0f);  putf(1, 2, 0.0f);
    putf(2, 0, 4.0f);  putf(2, 1, -2.0f); putf(2, 2, 7.0f);
    mesh.putPtr(0, verts.data());
    mesh.putU32(8, 3);
    obj.putPtr(460, mesh.bytes.data());

    // seed box wide so every vertex tightens it.
    float box[7] = {1e9f, 1e9f, 1e9f, 0, -1e9f, -1e9f, -1e9f};
    CHECK(AccumulateVertexAabb(obj.bytes.data(), box));
    CHECK_EQ(box[0], -1.0f); // min x
    CHECK_EQ(box[1], -2.0f); // min y
    CHECK_EQ(box[2], 0.0f);  // min z
    CHECK_EQ(box[4], 4.0f);  // max x
    CHECK_EQ(box[5], 5.0f);  // max y
    CHECK_EQ(box[6], 7.0f);  // max z
}
