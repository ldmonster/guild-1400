#include "test.h"

// E2E: a "turn tick" flow across character_render5 — decay every person's needs,
// then for one actor whose action queue has drained, query its terrain and stop any
// running sample, then flush the pending mesh. Exercises UpdateAllNeeds ->
// CheckQueueReady -> QueryTerrainType -> StopSample -> FlushPendingMesh as one
// chain with installed hooks, asserting the cross-function state ends up consistent.
#include "sim/character_render5.h"

#include <cstring>
#include <cstdint>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

void Wf(u8* p, int off, float v) { std::memcpy(p + off, &v, 4); }

// flow effect counters
int g_touch, g_prune, g_freeAnim, g_terrainReads;
void TouchHook(void*) { ++g_touch; }
void PruneHook(void*) { ++g_prune; }
void FreeAnimHook(void*) { ++g_freeAnim; }
int  mapDummy = 1;
void* ResolveMesh(void*) { return &mapDummy; }
int  WorldToTile(void*, const float*, int* t, float* h) { if (t){t[0]=2;t[1]=2;} if(h)*h=0; return 1; }
u8   TerrainAt(void*, int, int) { ++g_terrainReads; return 9; }  // walkable

} // namespace

TEST(CharRender5E2E, TurnTickFlow) {
    CharRender5Hooks h{};
    h.touchMeshFrames = TouchHook;
    h.pruneAttachments = PruneHook;
    h.freeObjAnimData = FreeAnimHook;
    h.resolveMesh = ResolveMesh;
    h.worldToTileWithHeight = WorldToTile;
    h.terrainCodeAt = TerrainAt;
    SetCharRender5Hooks(&h);
    g_touch = g_prune = g_freeAnim = g_terrainReads = 0;

    // 1) decay needs for a 2-person array.
    const int N = 2;
    std::vector<u8> persons(static_cast<std::size_t>(N) * kPersonStride, 0);
    for (int i = 0; i < N; ++i) {
        u8* rec = persons.data() + static_cast<std::size_t>(i) * kPersonStride;
        std::int16_t marker = 0; std::memcpy(rec, &marker, 2);
        rec[kPnLastDominant] = 6;
        Wf(rec, kPnNeedBase + 12 * 6, 999.0f);  // need 6 dominates and stays
        Wf(rec, kPnScale, 1.0f);
    }
    int dom = UpdateAllNeeds(persons.data(), N, /*seed=*/0);
    CHECK_EQ(dom, 6);

    // 2) an actor with NO action streams -> queue ready.
    ChActor actor{};
    CHECK(CheckQueueReady(&actor) == true);

    // 3) query its terrain (walkable code).
    int code = QueryTerrainType(&actor, /*force=*/0);
    CHECK_EQ(code, 9);
    CHECK_EQ(g_terrainReads, 1);

    // 4) it had a running sample with an attached morph -> stop it.
    MeshRec mesh{};       // no material -> touch path
    MorphRec morph{};
    morph.attached = 1;   // attached -> prune path
    actor.flagsA = 0x10;
    actor.mesh = &mesh;
    actor.animMorph0 = 1;
    actor.animMorph = &morph;
    StopSample(&actor);
    CHECK_EQ(g_touch, 1);
    CHECK_EQ(g_prune, 1);
    CHECK_EQ(actor.flagsA & 0x10, 0);

    // 5) flush the pending mesh queued during the tick.
    int animObj = 1;
    ResetCharRender5();
    g_pendingMesh.pendingAnim = &animObj;
    g_pendingMesh.pendingFlag = 0x20;
    FlushPendingMesh();
    CHECK_EQ(g_freeAnim, 1);
    CHECK(g_pendingMesh.pendingAnim == nullptr);

    SetCharRender5Hooks(nullptr);
    ResetCharRender5();
}

// A second flow: a season-driven camera dispatch using the default (real) StrCmp.
namespace {
int g_e2eMode, g_e2eErr;
void CamHook2(void*, int m) { g_e2eMode = m; }
void ErrHook2(const char*) { ++g_e2eErr; }
} // namespace

TEST(CharRender5E2E, SeasonAndCameraDispatch) {
    // season index for a few days (day % 4).
    CHECK_EQ(GetIndexThunk(3), 3);
    CHECK_EQ(GetIndexThunk(4), 0);

    CharRender5Hooks h{};
    h.setupAttachCamera = CamHook2;
    h.scriptError = ErrHook2;
    h.strCmp = [](const char* a, const char* b) { return std::strcmp(a, b); };
    SetCharRender5Hooks(&h);
    g_e2eMode = -1; g_e2eErr = 0;

    int actor = 1;
    CHECK_EQ(SetCameraViewMode(actor, &actor, "RIGHT_SHOULDER"), 0);
    CHECK_EQ(g_e2eMode, 2);
    CHECK_EQ(SetCameraViewMode(0, nullptr, "EGO"), 1);
    CHECK_EQ(g_e2eErr, 1);

    SetCharRender5Hooks(nullptr);
}
