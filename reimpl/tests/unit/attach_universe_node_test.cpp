#include "test.h"

// =============================================================================
// AttachUniverseNode — golden coverage of VIBE_Object_AttachToUniverseNode @0x5b3e30
// (guild::sim::ObjectAttachToUniverseNode, sim/object_lifecycle10.cpp), the universe
// scene-node factory every character/object creation bottoms out on. No assets:
// recording mock hooks + a synthetic node/draw-block with a known LOD/texture layout.
//
// The reconstruction is 1:1 with the Hex-Rays decompile:
//   node = Object_Spawn(4, name); if (!node) return node;
//   if (parent) SetParent(parent, node);
//   if (!Mesh_FindStockObject()) Mesh_LoadOrFindByName(name);
//   Mesh_AttachStockObjectLods(node, 0, name, lodArg);
//   v7 = node[123] /*node+492 draw block*/;
//   if (v7 && *(v7+260) /*first LOD mesh present*/) {
//       for (i = 0; i < *(u8*)(v7+2316) /*LOD count*/; ++i) {        // stride 384
//           lodMesh = *(v7 + i*384 + 260);
//           if (lodMesh) {
//               texCount = *(lodMesh + 480);
//               texArr   = *(v7 + i*384 + 264);                       // handle array
//               for (j = 0; j < texCount; ++texArr, ++j)
//                   if (*texArr) Texture_UploadToSurface(*texArr, 0, texArr, i);
//           }
//       }
//       SetPosition(node, pos); SetWorldTranslation(node, worldRot);
//       if (!parent) LinkIntoScene(node);
//       return node;
//   } else { Dispose(node); return 0; }
//
// The draw-block (node+492) byte layout this test models:
//   +260 + i*384   LOD i mesh ptr        (first-LOD presence gate is +260)
//   +264 + i*384   LOD i texture-handle array ptr (array of pointers)
//   +2316          LOD count (u8)
//   lodMesh+480    texture/material count for that LOD
// =============================================================================
#include "sim/object_lifecycle10.h"

#include <cstdint>
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

// One recorded VIBE_Texture_UploadToSurface(handle, 0, arrayPtr, lod) call.
struct Upload { void* handle; void* arrayPtr; int lod; };

struct AunRec {
    int spawnCalls = 0;
    int setParentCalls = 0;
    int setPositionCalls = 0;
    int setWorldXlateCalls = 0;
    int linkSceneCalls = 0;
    int disposeCalls = 0;
    int attachLodsCalls = 0;
    int findStockCalls = 0;
    int loadOrFindCalls = 0;
    float lastPos[3] = {0, 0, 0};
    float lastXlate[3] = {0, 0, 0};
    std::vector<Upload> uploads;
};
AunRec g_aun;

SceneNode10* g_aunSpawned = nullptr;

void* AunSpawn(int /*kind*/, const char*) { g_aun.spawnCalls++; return g_aunSpawned; }
void  AunSetParent(void*, SceneNode10*)   { g_aun.setParentCalls++; }
void  AunSetPosition(SceneNode10*, const float* p) {
    g_aun.setPositionCalls++;
    if (p) std::memcpy(g_aun.lastPos, p, sizeof(g_aun.lastPos));
}
void  AunSetWorldXlate(SceneNode10*, const float* p) {
    g_aun.setWorldXlateCalls++;
    if (p) std::memcpy(g_aun.lastXlate, p, sizeof(g_aun.lastXlate));
}
void  AunLinkScene(SceneNode10*) { g_aun.linkSceneCalls++; }
void  AunDispose(SceneNode10*)   { g_aun.disposeCalls++; }
void  AunAttachLods(SceneNode10*, const char*) { g_aun.attachLodsCalls++; }
int   AunFindStock() { g_aun.findStockCalls++; return 0; }   // not loaded => LoadOrFind
void  AunLoadOrFind(const char*) { g_aun.loadOrFindCalls++; }
void  AunUpload(void* handle, void* arrayPtr, int lod) {
    g_aun.uploads.push_back({handle, arrayPtr, lod});
}

ObjLife10Hooks MakeFullHooks() {
    ObjLife10Hooks h{};
    h.objSpawn = &AunSpawn;
    h.objSetParent = &AunSetParent;
    h.objSetPosition = &AunSetPosition;
    h.objSetWorldTranslation = &AunSetWorldXlate;
    h.objLinkIntoScene = &AunLinkScene;
    h.objDispose = &AunDispose;
    h.meshAttachLods = &AunAttachLods;
    h.meshFindStockObject = &AunFindStock;
    h.meshLoadOrFind = &AunLoadOrFind;
    h.textureUploadToSurface = &AunUpload;
    return h;
}

// A synthetic draw block + per-LOD mesh records + per-LOD texture-handle arrays
// describing `lodCount` LODs, each LOD i with texHandles[i] handle slots (some null).
struct DrawFixture {
    std::vector<unsigned char> draw;                 // the node+492 block
    std::vector<std::vector<unsigned char>> lodMesh; // per-LOD mesh record (+480 = count)
    std::vector<std::vector<void*>>          texArr;  // per-LOD handle array

    // Per-LOD-frame slot offsets (the shared render::frame LP64 contract): the engine
    // a2[4] STOCK ptr (resident gate) and a2[5] texSET array, relocated past the two
    // leading native ptrs + the two count dwords. frame_base = drawData + 244 + 384*i;
    // gate = frame_base + 24 (== drawData+268+384*i), texarr = frame_base + 32. On a
    // 32-bit build these are the verbatim engine +260 / +264.
    static constexpr int kFrameBase  = 244;  // render::frame::kLodFrameBase
    static constexpr int kStockSlot  = 24;   // render::frame::kStockSlot  (a2[4])
    static constexpr int kTexSetSlot = 32;   // render::frame::kTexSetSlot (a2[5])
    static constexpr int kGate = kFrameBase + kStockSlot;   // == 268
    static constexpr int kTex  = kFrameBase + kTexSetSlot;  // == 276

    // handles[i] = list of handle values for LOD i (0 means a null slot, skipped).
    void Build(const std::vector<std::vector<std::intptr_t>>& handles) {
        const int lodCount = static_cast<int>(handles.size());
        // big enough to hold lodCount frames at stride 384 (gate slot at +kGate),
        // plus the +2316 count byte.
        const int span = kTex + 8 + lodCount * 384;
        draw.assign(static_cast<size_t>(span > 2317 ? span : 2317), 0);
        draw[2316] = static_cast<unsigned char>(lodCount);   // LOD count

        lodMesh.resize(lodCount);
        texArr.resize(lodCount);
        for (int i = 0; i < lodCount; ++i) {
            const int texCount = static_cast<int>(handles[i].size());
            // Stock-object stand-in: needs +480 (material/texture count). Size it past +484.
            lodMesh[i].assign(484, 0);
            std::int32_t tc = texCount;
            std::memcpy(lodMesh[i].data() + 480, &tc, 4);

            // texture-handle array: texCount native-width pointer slots.
            texArr[i].assign(static_cast<size_t>(texCount), nullptr);
            for (int j = 0; j < texCount; ++j)
                texArr[i][j] = reinterpret_cast<void*>(handles[i][j]);

            // Wire frame i (stride 384): the STOCK ptr at the gate slot (a2[4]) and the
            // texSET array at a2[5] — the shared LP64 layout AttachToUniverseNode reads.
            const int base = i * 384;
            SetBlockPtr(draw.data(), base + kGate, lodMesh[i].data());
            SetBlockPtr(draw.data(), base + kTex, texArr[i].data());
        }
    }
};

}  // namespace

// ---------------------------------------------------------------------------
// (a) spawn fails => returns null, nothing else happens.
// ---------------------------------------------------------------------------
TEST(AttachUniverseNode, SpawnFailsReturnsNullNothingElse) {
    g_aun = AunRec{};
    g_aunSpawned = nullptr;
    ObjLife10Hooks h = MakeFullHooks();
    ObjLife10SetHooks(h);

    void* out = ObjectAttachToUniverseNode(nullptr, nullptr, "x", nullptr, nullptr);
    CHECK(out == nullptr);
    CHECK_EQ(g_aun.spawnCalls, 1);
    CHECK_EQ(g_aun.setParentCalls, 0);
    CHECK_EQ(g_aun.attachLodsCalls, 0);
    CHECK_EQ(g_aun.findStockCalls, 0);
    CHECK_EQ(g_aun.disposeCalls, 0);
    CHECK_EQ(g_aun.linkSceneCalls, 0);
    CHECK_EQ(static_cast<int>(g_aun.uploads.size()), 0);

    ObjLife10ResetHooks();
}

// ---------------------------------------------------------------------------
// (b) mesh-load fails (no draw block / no first LOD mesh) => Dispose, returns 0.
// ---------------------------------------------------------------------------
TEST(AttachUniverseNode, NoDrawBlockDisposesReturnsNull) {
    SceneNode10 node;                 // node+492 left null
    g_aun = AunRec{};
    g_aunSpawned = &node;
    ObjLife10Hooks h = MakeFullHooks();
    ObjLife10SetHooks(h);

    void* out = ObjectAttachToUniverseNode(nullptr, nullptr, "house", nullptr, nullptr);
    CHECK(out == nullptr);
    CHECK_EQ(g_aun.attachLodsCalls, 1);
    CHECK_EQ(g_aun.disposeCalls, 1);
    CHECK_EQ(g_aun.linkSceneCalls, 0);
    CHECK_EQ(g_aun.setPositionCalls, 0);
    CHECK_EQ(static_cast<int>(g_aun.uploads.size()), 0);

    ObjLife10ResetHooks();
}

TEST(AttachUniverseNode, DrawBlockButNoFirstLodDisposesReturnsNull) {
    SceneNode10 node;
    std::vector<unsigned char> draw(2317, 0);   // gate (+268) == 0 => first LOD absent
    node.p(n10::kDrawData) = draw.data();
    g_aun = AunRec{};
    g_aunSpawned = &node;
    ObjLife10Hooks h = MakeFullHooks();
    ObjLife10SetHooks(h);

    void* out = ObjectAttachToUniverseNode(nullptr, nullptr, "house", nullptr, nullptr);
    CHECK(out == nullptr);
    CHECK_EQ(g_aun.disposeCalls, 1);
    CHECK_EQ(g_aun.linkSceneCalls, 0);

    ObjLife10ResetHooks();
}

// ---------------------------------------------------------------------------
// FindStockObject==0 => LoadOrFindByName fires (mesh find-or-load gate).
// ---------------------------------------------------------------------------
TEST(AttachUniverseNode, NotInStockTriggersLoadOrFind) {
    SceneNode10 node;
    DrawFixture fx;
    fx.Build({ { 0x10 } });                  // 1 LOD, 1 (non-null) handle
    node.p(n10::kDrawData) = fx.draw.data();
    g_aun = AunRec{};
    g_aunSpawned = &node;
    ObjLife10Hooks h = MakeFullHooks();
    ObjLife10SetHooks(h);

    ObjectAttachToUniverseNode(nullptr, nullptr, "house", nullptr, nullptr);
    CHECK_EQ(g_aun.findStockCalls, 1);
    CHECK_EQ(g_aun.loadOrFindCalls, 1);      // FindStock returned 0 => LoadOrFind

    ObjLife10ResetHooks();
}

// ---------------------------------------------------------------------------
// (c) success: textures uploaded for exactly the NON-NULL handles across the right
// LOD count; SetPosition/SetWorldTranslation called with the passed args; no parent
// => LinkIntoScene; SetParent NOT called; returns the node.
//   LOD 0: [A, 0, B]   (2 non-null of 3)
//   LOD 1: [0, C]      (1 non-null of 2)
// => exactly 3 uploads, with the right lod index and the right handle each.
// ---------------------------------------------------------------------------
TEST(AttachUniverseNode, SuccessUploadsNonNullHandlesAcrossLodsAndSeatsAndLinks) {
    SceneNode10 node;
    DrawFixture fx;
    fx.Build({ {0xA, 0, 0xB}, {0, 0xC} });   // 2 LODs
    node.p(n10::kDrawData) = fx.draw.data();
    g_aun = AunRec{};
    g_aunSpawned = &node;
    ObjLife10Hooks h = MakeFullHooks();
    ObjLife10SetHooks(h);

    float pos[3]   = {1.0f, 2.0f, 3.0f};
    float xlate[3] = {4.0f, 5.0f, 6.0f};
    void* out = ObjectAttachToUniverseNode(nullptr, pos, "house", xlate, nullptr);

    CHECK(out == &node);
    CHECK_EQ(g_aun.spawnCalls, 1);
    CHECK_EQ(g_aun.attachLodsCalls, 1);
    CHECK_EQ(g_aun.setParentCalls, 0);
    CHECK_EQ(g_aun.setPositionCalls, 1);
    CHECK_EQ(g_aun.setWorldXlateCalls, 1);
    CHECK_EQ(g_aun.linkSceneCalls, 1);       // no parent => LinkIntoScene
    CHECK_EQ(g_aun.disposeCalls, 0);
    CHECK_EQ(g_aun.lastPos[0], 1.0f);
    CHECK_EQ(g_aun.lastPos[2], 3.0f);
    CHECK_EQ(g_aun.lastXlate[0], 4.0f);
    CHECK_EQ(g_aun.lastXlate[2], 6.0f);

    // exactly the 3 non-null handles uploaded, in (lod, handle) order.
    CHECK_EQ(static_cast<int>(g_aun.uploads.size()), 3);
    CHECK(g_aun.uploads[0].handle == reinterpret_cast<void*>(static_cast<std::intptr_t>(0xA)));
    CHECK_EQ(g_aun.uploads[0].lod, 0);
    CHECK(g_aun.uploads[1].handle == reinterpret_cast<void*>(static_cast<std::intptr_t>(0xB)));
    CHECK_EQ(g_aun.uploads[1].lod, 0);
    CHECK(g_aun.uploads[2].handle == reinterpret_cast<void*>(static_cast<std::intptr_t>(0xC)));
    CHECK_EQ(g_aun.uploads[2].lod, 1);
    // arrayPtr argument points at the slot holding the handle (Texture_UploadToSurface
    // gets &texArr[j] as its 3rd arg) — check the first upload's slot holds handle A.
    CHECK(BlockPtr(g_aun.uploads[0].arrayPtr, 0)
          == reinterpret_cast<void*>(static_cast<std::intptr_t>(0xA)));

    ObjLife10ResetHooks();
}

// ---------------------------------------------------------------------------
// success WITH a parent: SetParent fires, LinkIntoScene does NOT, node returned.
// ---------------------------------------------------------------------------
TEST(AttachUniverseNode, SuccessWithParentSetsParentNotLink) {
    SceneNode10 node;
    DrawFixture fx;
    fx.Build({ {0xA} });                     // 1 LOD, 1 handle
    node.p(n10::kDrawData) = fx.draw.data();
    g_aun = AunRec{};
    g_aunSpawned = &node;
    ObjLife10Hooks h = MakeFullHooks();
    ObjLife10SetHooks(h);

    int parentDummy = 7;
    void* out = ObjectAttachToUniverseNode(&parentDummy, nullptr, "house", nullptr, nullptr);
    CHECK(out == &node);
    CHECK_EQ(g_aun.setParentCalls, 1);
    CHECK_EQ(g_aun.linkSceneCalls, 0);
    CHECK_EQ(static_cast<int>(g_aun.uploads.size()), 1);

    ObjLife10ResetHooks();
}

// ---------------------------------------------------------------------------
// A LOD whose mesh ptr is null is SKIPPED for texture upload but does NOT abort the
// loop — only the first-LOD (+260) presence gates the whole function. Here LOD0 is
// present (gate passes); LOD1's mesh ptr is null so it contributes no uploads.
// ---------------------------------------------------------------------------
TEST(AttachUniverseNode, NullLodMeshSkippedNoAbort) {
    SceneNode10 node;
    DrawFixture fx;
    fx.Build({ {0xA, 0xB}, {0xC} });         // build 2 valid LODs first
    // null out LOD1's STOCK/mesh ptr (draw + 1*384 + gate) — its texture array stays but
    // the stock record (matCount source) is gone, so the loop skips LOD1 entirely. The
    // gate slot is the shared render::frame stock slot (== 244+24); see DrawFixture.
    SetBlockPtr(fx.draw.data(), 384 + DrawFixture::kGate, nullptr);
    node.p(n10::kDrawData) = fx.draw.data();
    g_aun = AunRec{};
    g_aunSpawned = &node;
    ObjLife10Hooks h = MakeFullHooks();
    ObjLife10SetHooks(h);

    void* out = ObjectAttachToUniverseNode(nullptr, nullptr, "house", nullptr, nullptr);
    CHECK(out == &node);
    // only LOD0's 2 handles uploaded; LOD1 skipped (null mesh ptr).
    CHECK_EQ(static_cast<int>(g_aun.uploads.size()), 2);
    CHECK_EQ(g_aun.uploads[0].lod, 0);
    CHECK_EQ(g_aun.uploads[1].lod, 0);

    ObjLife10ResetHooks();
}
