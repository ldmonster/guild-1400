// Unit tests for guild::render animation/mesh loader + AABB leaves.
#include "test.h"
#include "render/animation_mesh.h"

#include <cstring>
#include <string>

using namespace guild;
using namespace guild::render;

// ---------------------------------------------------------------------------
// 0x5D9774 — VIBE_Animation_GetPtr.
// ---------------------------------------------------------------------------
TEST(AnimMeshGetPtr, MatchesCaseInsensitiveAndSkipsEmpty) {
    AnimRegistryEntry tab[4];
    std::memset(tab, 0, sizeof(tab));
    std::strcpy(tab[0].name, "Walk");   tab[0].frameCount = 0;   // empty -> skip
    std::strcpy(tab[1].name, "RUN");    tab[1].frameCount = 8;
    std::strcpy(tab[2].name, "idle");   tab[2].frameCount = 4;
    std::strcpy(tab[3].name, "jump");   tab[3].frameCount = 2;

    // "Walk" has frameCount 0 -> treated as empty -> not found.
    CHECK(AnimationGetPtr("walk", tab, 4) == nullptr);
    // case-insensitive match on a valid entry.
    AnimRegistryEntry* r = AnimationGetPtr("run", tab, 4);
    CHECK(r == &tab[1]);
    r = AnimationGetPtr("IDLE", tab, 4);
    CHECK(r == &tab[2]);
    // missing name.
    CHECK(AnimationGetPtr("crouch", tab, 4) == nullptr);
    // empty registry.
    CHECK(AnimationGetPtr("run", tab, 0) == nullptr);
}

// ---------------------------------------------------------------------------
// 0x5D89BC — VIBE_Animation_Advanced (forces frame mode to 3 across process).
// ---------------------------------------------------------------------------
static int g_seenMode = -1;
static void CaptureFrameMode(void* frame) {
    g_seenMode = ((AdvancedFrame*)frame)->mode;
}

TEST(AnimMeshAdvanced, ForcesModeThreeAndRestores) {
    AnimMeshResetHooks();
    AnimMeshHooks h;
    h.frameProcess = &CaptureFrameMode;
    AnimMeshSetHooks(h);

    AdvancedFrame f0{};
    f0.mode = 7;
    AdvancedClip clip{};
    clip.frameCount = 4;
    clip.frames[2] = &f0;

    g_seenMode = -1;
    int observed = AnimationAdvanced(clip, 2);
    CHECK_EQ(observed, 3);          // the processor saw mode == 3
    CHECK_EQ(g_seenMode, 3);
    CHECK_EQ((int)f0.mode, 7);      // restored afterward

    // out-of-range index -> -1, processor not invoked again.
    int before = AnimMeshFrameProcessCalls();
    CHECK_EQ(AnimationAdvanced(clip, 99), -1);
    CHECK_EQ(AnimMeshFrameProcessCalls(), before);
    AnimMeshResetHooks();
}

// ---------------------------------------------------------------------------
// 0x5D1034 — VIBE_Mesh_BuildTexturePath ("*"+a1+a2).
// ---------------------------------------------------------------------------
static const char* g_lastResolve = nullptr;
static const char* CaptureResolve(const char* p) { g_lastResolve = p; return p; }

TEST(AnimMeshBuildTexturePath, AssemblesStarPrefixedName) {
    AnimMeshResetHooks();
    AnimMeshHooks h;
    h.resolvePath = &CaptureResolve;
    AnimMeshSetHooks(h);

    char out[272];
    const char* r = MeshBuildTexturePath(out, "house", ".bgf");
    CHECK(std::strcmp(out, "*house.bgf") == 0);
    CHECK(r == out);                 // default-ish capture returns the buffer
    CHECK(g_lastResolve != nullptr && std::strcmp(g_lastResolve, "*house.bgf") == 0);
    AnimMeshResetHooks();
}

// ---------------------------------------------------------------------------
// 0x5D1020 — VIBE_Mesh_SetActiveTexturePath.
// ---------------------------------------------------------------------------
static int g_normRet = 0;
static int g_normCalls = 0;
static int CaptureNorm(const char*) { ++g_normCalls; return g_normRet; }

TEST(AnimMeshSetActiveTexturePath, ReturnsNormalizeResult) {
    AnimMeshResetHooks();
    AnimMeshHooks h;
    g_normRet = 1234; g_normCalls = 0;
    h.normalizeDirPath = &CaptureNorm;
    AnimMeshSetHooks(h);
    CHECK_EQ(MeshSetActiveTexturePath("textures/"), 1234);
    CHECK_EQ(g_normCalls, 1);
    AnimMeshResetHooks();
}

// ---------------------------------------------------------------------------
// 0x5D15FC — VIBE_Mesh_BuildLodFileName.
// ---------------------------------------------------------------------------
TEST(AnimMeshBuildLodFileName, BaseRequestAppendsSuffix) {
    char out[272], out2[272];
    // a4 < 0 with negative lodMode -> base + "_s".
    char r = MeshBuildLodFileName("door", "frame", out, -1, out2, (signed char)0x80);
    CHECK_EQ((int)r, 1);
    CHECK(std::strcmp(out, "door_s") == 0);
    CHECK(std::strcmp(out2, "frame_s") == 0);

    // a4 < 0 but lodMode >= 0 -> rejected (0).
    r = MeshBuildLodFileName("door", nullptr, out, -1, nullptr, (signed char)0);
    CHECK_EQ((int)r, 0);
}

TEST(AnimMeshBuildLodFileName, PositiveLodFormatsIndex) {
    char out[272], out2[272];
    // lod > 0, v6 != 2 -> idx = lod - 1.
    char r = MeshBuildLodFileName("wall", "uv", out, 3, out2, (signed char)1);
    CHECK_EQ((int)r, 1);
    CHECK(std::strcmp(out, "wall_2") == 0);
    CHECK(std::strcmp(out2, "uv_2") == 0);

    // lod > 0, v6 == 2 -> lod = 2 - lod, idx = (2-lod)-1.
    r = MeshBuildLodFileName("wall", nullptr, out, 1, nullptr, (signed char)2);
    CHECK_EQ((int)r, 1);
    // 2-1=1, idx=0 -> "wall_0"
    CHECK(std::strcmp(out, "wall_0") == 0);
}

TEST(AnimMeshBuildLodFileName, ZeroLodPlainCopyWhenNotMode2) {
    char out[272];
    char r = MeshBuildLodFileName("rock", nullptr, out, 0, nullptr, (signed char)0);
    CHECK_EQ((int)r, 1);
    CHECK(std::strcmp(out, "rock") == 0);
}

// mode==2 probe: resolvePath returns non-null on the FIRST attempted name.
static std::string g_probeMatch;
static const char* ProbeResolve(const char* p) {
    // p is "*<name>.bgf"; succeed only when <name> matches g_probeMatch.
    return (g_probeMatch.size() &&
            std::string(p) == "*" + g_probeMatch + ".bgf") ? p : nullptr;
}

TEST(AnimMeshBuildLodFileName, ZeroLodMode2ProbesIndices) {
    AnimMeshResetHooks();
    AnimMeshHooks h;
    h.resolvePath = &ProbeResolve;
    AnimMeshSetHooks(h);

    char out[272];
    // first probe is "%s_%i" with index v7-1 == 1 -> "tree_1"; make that resolve.
    g_probeMatch = "tree_1";
    char r = MeshBuildLodFileName("tree", nullptr, out, 0, nullptr, (signed char)2);
    CHECK_EQ((int)r, 1);
    CHECK(std::strcmp(out, "tree_1") == 0);
    AnimMeshResetHooks();
}

// ---------------------------------------------------------------------------
// 0x5B4944 — VIBE_Mesh_MarkAllFramesDirty.
// ---------------------------------------------------------------------------
TEST(AnimMeshMarkAllFramesDirty, PrecachesAndSetsBit) {
    AnimMeshResetHooks();
    FrameEntry f[4] = {};
    f[0].texture = 0;     f[0].flags = 0;       // no texture -> skip
    f[1].texture = 99;    f[1].flags = 0;       // precache + set bit7
    f[2].texture = 50;    f[2].flags = 0x80;    // already dirty (bit7) -> skip
    f[3].texture = 7;     f[3].flags = 0x01;    // precache + set bit7

    MeshMarkAllFramesDirty(f, 4);
    CHECK_EQ((int)f[0].flags, 0);
    CHECK_EQ((int)f[1].flags, 0x80);
    CHECK_EQ((int)f[2].flags, 0x80);
    CHECK_EQ((int)f[3].flags, 0x81);
    CHECK_EQ(AnimMeshPrecacheCalls(), 2);
    AnimMeshResetHooks();
}

// ---------------------------------------------------------------------------
// 0x429070 — VIBE_Mesh_ApplyTransformRecursive (DFS inflate count).
// ---------------------------------------------------------------------------
TEST(AnimMeshApplyTransform, VisitsEveryNode) {
    AnimMeshResetHooks();
    // tree: root -> {a -> {a1}, b}
    AabbNode a1, a, b, root;
    a.firstChild = &a1;  a.nextSibling = &b;
    root.firstChild = &a;
    MeshApplyTransformRecursive(&root);
    CHECK_EQ(AnimMeshInflateCalls(), 4);   // root,a,a1,b
    AnimMeshResetHooks();
}

// ---------------------------------------------------------------------------
// 0x4283AC — VIBE_Mesh_AccumulateAabbRecursive.
// ---------------------------------------------------------------------------
static void FillCorners(AabbNode& n, const float c[8][3]) {
    n.hasMesh = true;
    for (int i = 0; i < 8; ++i) {
        n.corners[i].x = c[i][0];
        n.corners[i].y = c[i][1];
        n.corners[i].z = c[i][2];
    }
}

TEST(AnimMeshAccumulateAabb, MinMaxOverCorners) {
    const float c[8][3] = {
        {-1,-2,-3},{4,5,6},{0,0,0},{1,1,1},
        {-5,2,2},{3,-3,3},{2,2,-4},{0,7,0}};
    AabbNode n;
    FillCorners(n, c);

    Aabb box;
    box.mn[0] = box.mn[1] = box.mn[2] = kAabbSeedMin;
    box.mx[0] = box.mx[1] = box.mx[2] = kAabbSeedMax;
    MeshAccumulateAabbRecursive(box, &n);

    // python oracle: mn[-5,-3,-4] mx[4,7,6]
    CHECK_EQ(box.mn[0], -5.f); CHECK_EQ(box.mn[1], -3.f); CHECK_EQ(box.mn[2], -4.f);
    CHECK_EQ(box.mx[0],  4.f); CHECK_EQ(box.mx[1],  7.f); CHECK_EQ(box.mx[2],  6.f);
}

// ---------------------------------------------------------------------------
// 0x427820 — VIBE_Mesh_TestAabbOverlapRecursive.
// ---------------------------------------------------------------------------
TEST(AnimMeshTestOverlap, MeshNodeReturnsZeroAndComputesBox) {
    const float c[8][3] = {
        {0,0,0},{2,0,0},{0,2,0},{0,0,2},
        {2,2,0},{2,0,2},{0,2,2},{2,2,2}};
    AabbNode n;
    FillCorners(n, c);

    Aabb q;
    q.mn[0]=q.mn[1]=q.mn[2]=-10.f;
    q.mx[0]=q.mx[1]=q.mx[2]= 10.f;
    Aabb out;
    out.mn[0]=out.mn[1]=out.mn[2]=kAabbSeedMin;
    out.mx[0]=out.mx[1]=out.mx[2]=kAabbSeedMax;

    int r = MeshTestAabbOverlapRecursive(q, &n, &out);
    CHECK_EQ(r, 0);                 // a mesh node consumes the test
    CHECK_EQ(out.mn[0], 0.f); CHECK_EQ(out.mx[0], 2.f);
    CHECK_EQ(out.mn[2], 0.f); CHECK_EQ(out.mx[2], 2.f);
}

TEST(AnimMeshTestOverlap, NonMeshLeafReturnsOne) {
    AabbNode n;                      // hasMesh = false
    Aabb q; q.mn[0]=q.mn[1]=q.mn[2]=0; q.mx[0]=q.mx[1]=q.mx[2]=1;
    int r = MeshTestAabbOverlapRecursive(q, &n, nullptr);
    CHECK_EQ(r, 1);
}

// ---------------------------------------------------------------------------
// 0x5F5628 — VIBE_Mesh_AccumulateMemoryCallback.
// ---------------------------------------------------------------------------
TEST(AnimMeshAccumulateMemory, AddsNodeSize) {
    int acc = 100;
    char r = MeshAccumulateMemoryCallback(&acc, 40);
    CHECK_EQ((int)r, 1);
    CHECK_EQ(acc, 140);
    r = MeshAccumulateMemoryCallback(&acc, 5);
    CHECK_EQ(acc, 145);
    // null accumulator is a no-op but still returns 1.
    CHECK_EQ((int)MeshAccumulateMemoryCallback(nullptr, 7), 1);
}

// ---------------------------------------------------------------------------
// .TXS round-trip (0x5D20DC / 0x5D2240).
// ---------------------------------------------------------------------------
TEST(AnimMeshTextureSet, SaveLoadRoundTrip) {
    MeshTextureSet ts;
    ts.rows = 2; ts.cols = 3;
    for (int r = 0; r < 2; ++r)
        for (int c = 0; c < 3; ++c) {
            std::memset(ts.at(r, c), 0, 64);
            std::snprintf(ts.at(r, c), 64, "tex_%d_%d", r, c);
        }

    u8 buf[4096];
    std::size_t n = SaveTextureSet(ts, buf, sizeof(buf));
    CHECK_EQ((int)n, 12 + 2 * 3 * 64);
    // golden header bytes (magic BE then rows then cols).
    CHECK_EQ((int)buf[0], 0x23); CHECK_EQ((int)buf[1], 0xF2);
    CHECK_EQ((int)buf[2], 0x09); CHECK_EQ((int)buf[3], 0xAE);
    CHECK_EQ((int)buf[7], 2);    // rows
    CHECK_EQ((int)buf[11], 3);   // cols

    MeshTextureSet ld;
    bool ok = LoadTextureSet(buf, n, ld);
    CHECK(ok);
    CHECK_EQ(ld.rows, 2);
    CHECK_EQ(ld.cols, 3);
    for (int r = 0; r < 2; ++r)
        for (int c = 0; c < 3; ++c)
            CHECK(std::strcmp(ld.at(r, c), ts.at(r, c)) == 0);
}

TEST(AnimMeshTextureSet, RejectsBadMagicAndEmpty) {
    u8 buf[64] = {};
    MeshTextureSet ld;
    CHECK(!LoadTextureSet(buf, sizeof(buf), ld));   // magic 0
    CHECK(!LoadTextureSet(buf, 4, ld));             // too short

    MeshTextureSet empty;                               // rows/cols 0
    u8 obuf[64];
    CHECK_EQ((int)SaveTextureSet(empty, obuf, sizeof(obuf)), 0);
}
