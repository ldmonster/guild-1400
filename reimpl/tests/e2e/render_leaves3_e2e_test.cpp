#include "test.h"

// E2E: a single render-tick texture/lighting pass threaded across the
// render_leaves3 cluster, driven by a deterministic synthetic scene:
//   1. SetMipFilterLevel configures the box-filter level (and would rebuild the
//      weight LUT / flush the cache — captured via hooks).
//   2. ScrollUvCoords advances the scrolling-water UV phase banks across N ticks.
//   3. AdvanceAnimFrames re-selects animated-texture frames for the scene's meshes
//      (CRC + tick/divisor) and rebinds matching materials.
//   4. CollectAffectedObject runs a light-cull pass: a compute phase that flags
//      which objects fall inside a light's radius, then an append phase that packs
//      the flagged objects into the light's output list.
//   5. DetachClone tears down a cloned tile and propagates to pool instances.
// This mirrors how the per-frame texture/light update chains these leaves.

#include "render/render_leaves3.h"

#include <cmath>
#include <cstring>

using namespace guild;
using namespace guild::render;

namespace {

bool nearf(float a, float b, float eps = 1e-5f) {
    return std::fabs(a - b) <= eps * (1.0f + std::fabs(b));
}

// Hook-side accounting for the e2e flow.
struct Telemetry {
    int filterRebuilds = 0;
    int cacheResets    = 0;
    int releases       = 0;
};
Telemetry g_tel;

} // namespace

TEST(RenderLeaves3_E2E, SceneTickPass) {
    g_tel = Telemetry{};
    MipState() = MipFilterState{};
    UvState()  = UvScrollState{};

    RenderLeaves3Hooks h{};
    h.computeFilterWeights = +[](float*){ ++g_tel.filterRebuilds; };
    h.textureCacheReset    = +[](){ ++g_tel.cacheResets; return 0; };
    h.releaseEntry         = +[](void*){ ++g_tel.releases; };
    // Deterministic findGroupMember: returns the requested frame byte unchanged
    // (the default), so the e2e asserts on the engine-computed frame selection.
    InstallRenderLeaves3Hooks(h);

    // --- Step 1: configure mip filter to 32 (level shift 6-5 = 1). -----------
    u32 mipRet = SetMipFilterLevel(32);
    CHECK_EQ((int)mipRet, 0);
    CHECK_EQ((int)MipState().size, 32);
    CHECK_EQ((int)MipState().shift, 1);
    CHECK_EQ(g_tel.filterRebuilds, 1);
    CHECK_EQ(g_tel.cacheResets, 1);

    // --- Step 2: scroll water UVs across 3 ticks (0 -> 200 -> 200 -> 1000). ---
    UvState().lastTick = 0;
    ScrollUvCoords(200);
    CHECK_EQ(UvState().lastTick, 200);
    ScrollUvCoords(200);   // same tick -> no further advance
    CHECK_EQ(UvState().lastTick, 200);
    const float afterFirst = UvState().bankU[1];
    ScrollUvCoords(1000);  // advance by 800 more
    CHECK_EQ(UvState().lastTick, 1000);
    // Channel 1 accumulates monotonically below the 1.0 wrap point here.
    CHECK(UvState().bankU[1] > afterFirst);
    CHECK(nearf(UvState().bankU[1], 0.03333333507180214f));  // == rate[1] * 1000

    // --- Step 3: animate two meshes in one group; rebind their materials. -----
    AnimMesh meshA{}; meshA.frames = 8; meshA.speedNibble = 4; meshA.refCount = 1; meshA.groupId = 42;
    AnimMesh meshB{}; meshB.frames = 6; meshB.speedNibble = 2; meshB.refCount = 1; meshB.groupId = 99;
    AnimMesh* meshes[2] = {&meshA, &meshB};

    AnimMaterial mats[3]{};
    mats[0].slot = &meshA; mats[0].boundMember = -1;  // -> group 42
    mats[1].slot = &meshB; mats[1].boundMember = -1;  // -> group 99
    mats[2].slot = &meshA; mats[2].boundMember = -1;  // -> group 42 (second user)

    AnimGroup grp{};
    grp.memberCount = 2; grp.hasBank = true; grp.meshStride = 3;
    grp.meshes = meshes; grp.meshCount = 2; grp.materials = mats;

    i8 animRc = AdvanceAnimFrames(0x12345678u, &grp, 900u, 0);
    CHECK_EQ((int)animRc, 1);
    // meshA: (crc32(handle) + 900/9) % 8 == 6  (python oracle); both 42-materials.
    CHECK_EQ(mats[0].boundMember, 6);
    CHECK_EQ(mats[2].boundMember, 6);
    // meshB: speed 2 -> div 13, 900/13 = 69; (crc + 69) % 6.
    // crc32(0x12345678) == 0xaf6d87d2; (0xaf6d87d2 + 69) % 6:
    {
        unsigned crc = 0xaf6d87d2u;
        int expB = (int)((crc + 900u / 13u) % 6u);
        CHECK_EQ(mats[1].boundMember, expB);
    }

    // --- Step 4: light-cull pass over 3 objects (compute then append). --------
    LightBoundBlock bbRef{}, bb0{}, bb1{}, bb2{};
    LightObject ref{}; ref.bound = &bbRef; ref.cullRadius = 20.0f;
    ref.pos[0] = 0; ref.pos[1] = 0; ref.pos[2] = 0;

    LightObject o0{}; o0.bound = &bb0; o0.radius = 1.0f; o0.srcRadius = 5.0f;
    o0.pos[0] = 3; o0.pos[1] = 4; o0.pos[2] = 0;             // dist 5 -> inside
    LightObject o1{}; o1.bound = &bb1; o1.radius = 1.0f; o1.srcRadius = 2.0f;
    o1.pos[0] = 100; o1.pos[1] = 0; o1.pos[2] = 0;           // dist 100 -> outside
    LightObject o2{}; o2.bound = &bb2; o2.radius = 1.0f; o2.srcRadius = 8.0f;
    o2.pos[0] = 6; o2.pos[1] = 8; o2.pos[2] = 0;             // dist 10 -> inside

    LightObject* objs[3] = {&o0, &o1, &o2};

    // Compute phase: outArray == null -> set the affected flags.
    LightAccumulator compute{}; compute.reference = &ref; compute.outArray = nullptr; compute.count = 0;
    for (auto* o : objs) CollectAffectedObject(o, &compute);
    CHECK_EQ((int)bb0.affected, 1);
    CHECK_EQ((int)bb1.affected, 0);
    CHECK_EQ((int)bb2.affected, 1);
    CHECK_EQ(compute.count, 2);  // two inside

    // Append phase: pack flagged objects into the light's list.
    LightObject* lightList[8] = {};
    LightAccumulator append{}; append.outArray = lightList; append.count = 0;
    for (auto* o : objs) CollectAffectedObject(o, &append);
    CHECK_EQ(append.count, 2);
    CHECK(lightList[0] == &o0);
    CHECK(lightList[1] == &o2);

    // --- Step 5: detach a cloned tile and propagate to a matching instance. ---
    TexRecord pool[2]{};
    pool[0].master = 77; pool[0].groupId = 3; pool[0].tileOwner = 1; pool[0].refCount = 1;
    pool[1].master = 88; pool[1].groupId = 3; pool[1].refCount = 1;  // matches -> recursed
    int detached = DetachClone(&pool[0], /*propagate*/1, pool, 2);
    CHECK_EQ(detached, 77);
    CHECK_EQ(pool[0].master, 0);
    CHECK_EQ(pool[1].master, 0);
    CHECK_EQ(g_tel.releases, 2);  // master + matching instance
}
