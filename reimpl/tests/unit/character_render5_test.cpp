#include "test.h"

// Unit tests for character_render5 — the remaining deterministic Character leaves
// (need decay, queue-readiness predicate, type counts, camera-view dispatch, the
// sample-stop / pending-mesh-flush state transitions, terrain-tile queries). Golden
// vectors for UpdateNeedsDecay were computed with python (struct float32 oracle).
#include "sim/character_render5.h"

#include <cstring>
#include <cstdint>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

// Build a 600-byte person record (real record is 536; oversize per the brief so
// every touched offset — the +480 scale, +404 phase — is inside a real buffer).
struct PersonBuf { u8 b[600]; };

void Wf(u8* p, int off, float v) { std::memcpy(p + off, &v, 4); }
float Rf(const u8* p, int off) { float v; std::memcpy(&v, p + off, 4); return v; }
void Wi(u8* p, int off, int v) { std::memcpy(p + off, &v, 4); }

} // namespace

// --- UpdateNeedsDecay: dominant flips away from the active need ---------------
TEST(CharRender5, NeedsDecayDominantFlip) {
    PersonBuf rec{};
    int active = 3;
    rec.b[kPnLastDominant] = static_cast<u8>(active);   // +304 active index
    for (int k = 0; k < kNeedCount; ++k) {
        Wf(rec.b, kPnNeedBase + 12 * k, 10.0f * k);
        Wf(rec.b, kPnRateBase + 12 * k, 1.0f + 0.1f * k);
        Wf(rec.b, kPnDecayBase + 12 * k, 0.01f);
    }
    Wf(rec.b, kPnPrimaryRate, 5.0f);
    Wf(rec.b, kPnPrimaryDecay, 0.01f);
    Wf(rec.b, kPnPrimaryValue, 200.0f);
    Wf(rec.b, kPnScale, 2.0f);
    Wi(rec.b, kPnPhase, 8);
    Wi(rec.b, kPnBusyFlag, 0);
    rec.b[kPnKind] = 5;

    int result = UpdateNeedsDecay(rec.b, /*seed=*/7);

    // golden: dominant becomes need 12 (reaches the 1000 ceiling); since dom!=active
    // the engine returns 12*active == 36, resets PRIMV to 0, writes LAST=12.
    CHECK_EQ(result, 36);
    CHECK_EQ(Rf(rec.b, kPnPrimaryValue), 0.0f);
    CHECK_EQ(static_cast<int>(static_cast<std::int8_t>(rec.b[kPnLastDominant])), 12);
    CHECK_EQ(Rf(rec.b, kPnNeedBase + 12 * 3), 30.0f);   // need[3] unchanged (==active, skipped)
    // need[12] = 120 + 2.2 - 120*0.01 = 121.0 (the 1000-ceiling is dbl_619110 == 1000.0,
    // NOT the 0.002 eps; only a value reaching 1000 is clamped). gilde.exe 0x452564.
    CHECK_EQ(Rf(rec.b, kPnNeedBase + 12 * 12), 121.0f); // need[12] updated (below ceiling)
    CHECK_EQ(Rf(rec.b, kPnNeedBase + 12 * 1), 0.0f);     // need[1] zeroed (case k==1)
}

// --- UpdateNeedsDecay: active need stays dominant (no flip) -------------------
TEST(CharRender5, NeedsDecayActiveStays) {
    PersonBuf rec{};
    int active = 5;
    rec.b[kPnLastDominant] = static_cast<u8>(active);
    for (int k = 0; k < kNeedCount; ++k) {
        Wf(rec.b, kPnNeedBase + 12 * k, 0.0f);
        Wf(rec.b, kPnRateBase + 12 * k, 0.0f);
        Wf(rec.b, kPnDecayBase + 12 * k, 0.5f);
    }
    Wf(rec.b, kPnNeedBase + 12 * active, 999.0f);  // active dominates
    Wf(rec.b, kPnPrimaryRate, 0.0f);
    Wf(rec.b, kPnPrimaryDecay, 0.0f);
    Wf(rec.b, kPnPrimaryValue, 0.0f);
    Wf(rec.b, kPnScale, 1.0f);
    Wi(rec.b, kPnPhase, 0);
    rec.b[kPnKind] = 5;

    int result = UpdateNeedsDecay(rec.b, /*seed=*/2);

    CHECK_EQ(result, 5);   // dom == active -> returns the index, no pointer math
    CHECK_EQ(static_cast<int>(static_cast<std::int8_t>(rec.b[kPnLastDominant])), 5);
    CHECK_EQ(Rf(rec.b, kPnNeedBase + 12 * active), 999.0f);  // active not updated
}

// --- UpdateNeedsDecay: null guard --------------------------------------------
TEST(CharRender5, NeedsDecayNullGuard) {
    CHECK_EQ(UpdateNeedsDecay(nullptr, 9), 9);
}

// --- UpdateAllNeeds: skips free slots, runs the rest --------------------------
TEST(CharRender5, UpdateAllNeedsSkipsFree) {
    // 3 records back-to-back; middle is free (marker == -1).
    const int N = 3;
    std::vector<u8> arr(static_cast<std::size_t>(N) * kPersonStride, 0);
    for (int i = 0; i < N; ++i) {
        u8* rec = arr.data() + static_cast<std::size_t>(i) * kPersonStride;
        std::int16_t marker = (i == 1) ? static_cast<std::int16_t>(-1) : 0;
        std::memcpy(rec, &marker, 2);
        rec[kPnLastDominant] = 4;
        Wf(rec, kPnNeedBase + 12 * 4, 999.0f);
        Wf(rec, kPnScale, 1.0f);
    }
    int result = UpdateAllNeeds(arr.data(), N, /*seed=*/0);
    // last live record (index 2) has active need 4 dominating -> returns 4.
    CHECK_EQ(result, 4);
}

// --- CheckQueueReady ----------------------------------------------------------
TEST(CharRender5, CheckQueueReadyTrivial) {
    ChActor actor{};
    // no action streams -> ready.
    CHECK(CheckQueueReady(&actor) == true);
    CHECK(CheckQueueReady(nullptr) == true);
}

TEST(CharRender5, CheckQueueReadyNotReady) {
    // primary stream present but cursor < endFrame and loop bit clear -> not ready.
    StreamDesc desc{};
    desc.endFrame = 50;        // +340
    desc.altEnd   = 0;         // +328
    ActionStream stream{};
    stream.cursor = 10;        // +0
    stream.desc   = &desc;     // +104
    stream.flags  = 0;         // +109 (loop bit clear)
    ChActor actor{};
    actor.actionSlot1 = &stream;
    CHECK(CheckQueueReady(&actor) == false);
    // set the loop bit (0x20) -> ready despite cursor < end.
    stream.flags = 0x20;
    CHECK(CheckQueueReady(&actor) == true);
    // cursor reaches end -> ready.
    stream.flags = 0;
    stream.cursor = 50;
    CHECK(CheckQueueReady(&actor) == true);
    // endFrame == -1 -> use altEnd; altEnd 0, cursor 50 >= 0 -> ready.
    stream.cursor = 50; desc.endFrame = -1; desc.altEnd = 0;
    CHECK(CheckQueueReady(&actor) == true);
}

// --- GetIndexThunk (season = day % 4) ----------------------------------------
TEST(CharRender5, GetIndexThunkSeason) {
    CHECK_EQ(GetIndexThunk(0), 0);
    CHECK_EQ(GetIndexThunk(7), 3);
    CHECK_EQ(GetIndexThunk(365), 1);
}

// --- SetCameraViewMode dispatch (default StrCmp == AnimStrCmp) -----------------
namespace {
int g_camMode;
int g_camCalls;
int g_scriptErrs;
void CamHook(void* /*a*/, int m) { g_camMode = m; ++g_camCalls; }
void ErrHook(const char*) { ++g_scriptErrs; }
} // namespace

TEST(CharRender5, SetCameraViewModeDispatch) {
    CharRender5Hooks h{};
    // leave strCmp null -> falls back to default (AnimStrCmp) via GetCharRender5Hooks?
    // No: installing a hooks struct replaces ALL members; fill the ones we use.
    h.setupAttachCamera = CamHook;
    h.scriptError = ErrHook;
    h.strCmp = [](const char* a, const char* b) { return std::strcmp(a, b); };
    SetCharRender5Hooks(&h);

    g_camCalls = 0; g_camMode = -1; g_scriptErrs = 0;
    int dummy = 1;
    CHECK_EQ(SetCameraViewMode(/*actorPtr0=*/dummy, &dummy, "CLOSEUP"), 0);
    CHECK_EQ(g_camMode, 0);
    CHECK_EQ(SetCameraViewMode(dummy, &dummy, "LEFT_SHOULDER"), 0);
    CHECK_EQ(g_camMode, 1);
    CHECK_EQ(SetCameraViewMode(dummy, &dummy, "RIGHT_SHOULDER"), 0);
    CHECK_EQ(g_camMode, 2);
    CHECK_EQ(SetCameraViewMode(dummy, &dummy, "EGO"), 0);
    CHECK_EQ(g_camMode, 3);
    CHECK_EQ(g_camCalls, 4);
    // unknown name -> no camera change, returns 0.
    CHECK_EQ(SetCameraViewMode(dummy, &dummy, "NOPE"), 0);
    CHECK_EQ(g_camCalls, 4);
    // null actor -> script error, returns 1.
    CHECK_EQ(SetCameraViewMode(/*actorPtr0=*/0, nullptr, "EGO"), 1);
    CHECK_EQ(g_scriptErrs, 1);

    SetCharRender5Hooks(nullptr);
}

// --- StopSample state transition ---------------------------------------------
namespace {
int g_touch;
int g_prune;
void TouchHook(void*) { ++g_touch; }
void PruneHook(void*) { ++g_prune; }
} // namespace

TEST(CharRender5, StopSampleClearsFlag) {
    CharRender5Hooks h{};
    h.touchMeshFrames = TouchHook;
    h.pruneAttachments = PruneHook;
    SetCharRender5Hooks(&h);
    g_touch = 0; g_prune = 0;

    MeshRec mesh{};
    mesh.material = 0;        // no material -> touchMeshFrames fires
    MorphRec morph{};
    morph.attached = 1;       // attached -> pruneAttachments fires

    ChActor actor{};
    actor.flagsA = 0x10;      // sample-active set
    actor.mesh = &mesh;
    actor.animMorph0 = 1;     // nonzero raw +116
    actor.animMorph = &morph;

    StopSample(&actor);
    CHECK_EQ(g_touch, 1);
    CHECK_EQ(g_prune, 1);
    CHECK_EQ(actor.flagsA & 0x10, 0);   // flag cleared
    CHECK_EQ(actor.animMorph0, 0);      // morph ptr cleared

    // second call: flag already clear -> no effect.
    StopSample(&actor);
    CHECK_EQ(g_touch, 1);
    CHECK_EQ(g_prune, 1);

    SetCharRender5Hooks(nullptr);
}

// --- QueryTerrainType ---------------------------------------------------------
namespace {
void* g_qtMap;
int g_qtCode;
void* QtResolveMesh(void*) { return g_qtMap; }
int QtWorldToTile(void*, const float*, int* t, float* hh) {
    if (t) { t[0] = 3; t[1] = 4; } if (hh) *hh = 0.0f; return 1;
}
u8 QtTerrainAt(void*, int c, int r) { (void)c; (void)r; return static_cast<u8>(g_qtCode); }
} // namespace

TEST(CharRender5, QueryTerrainTypeReadsCode) {
    CharRender5Hooks h{};
    int mapDummy = 1;
    g_qtMap = &mapDummy;
    h.resolveMesh = QtResolveMesh;
    h.worldToTileWithHeight = QtWorldToTile;
    h.terrainCodeAt = QtTerrainAt;
    SetCharRender5Hooks(&h);

    ChActor actor{};
    actor.mesh = nullptr;  // null mesh ok (world pos defaults to 0)

    g_qtCode = 7;
    CHECK_EQ(QueryTerrainType(&actor, /*force=*/0), 7);   // walkable code returned
    g_qtCode = 13;
    CHECK_EQ(QueryTerrainType(&actor, /*force=*/0), 13);  // blocked code still returned
    g_qtCode = 0;
    CHECK_EQ(QueryTerrainType(&actor, /*force=*/1), 0);   // force path returns code 0

    // no map -> 0.
    SetCharRender5Hooks(nullptr);
    CHECK_EQ(QueryTerrainType(&actor, 0), 0);  // default resolveMesh returns null
}

// --- FlushPendingMesh ---------------------------------------------------------
namespace {
int g_freeAnim;
int g_dayCycle;
void FreeAnimHook(void*) { ++g_freeAnim; }
void DayCycleHook(void*) { ++g_dayCycle; }
} // namespace

TEST(CharRender5, FlushPendingMeshFrees) {
    CharRender5Hooks h{};
    h.freeObjAnimData = FreeAnimHook;
    h.lightUpdateDayCycle = DayCycleHook;
    SetCharRender5Hooks(&h);
    g_freeAnim = 0; g_dayCycle = 0;

    int animObj = 1, lightRec = 2, scene = 3;
    ResetCharRender5();
    g_pendingMesh.pendingAnim = &animObj;
    g_pendingMesh.pendingFlag = 0x20;          // ready-to-free
    g_pendingMesh.pendingLight = &lightRec;
    g_pendingMesh.pendingLightScene = &scene;
    g_pendingMesh.activeScene = &scene;        // belongs to active scene

    FlushPendingMesh();
    CHECK_EQ(g_freeAnim, 1);
    CHECK_EQ(g_dayCycle, 1);
    CHECK(g_pendingMesh.pendingAnim == nullptr);
    CHECK_EQ(g_pendingMesh.flushBusy, 0);

    // flag NOT ready -> no free, busy stays set.
    ResetCharRender5();
    g_pendingMesh.pendingAnim = &animObj;
    g_pendingMesh.pendingFlag = 0;
    g_freeAnim = 0;
    FlushPendingMesh();
    CHECK_EQ(g_freeAnim, 0);
    CHECK_EQ(g_pendingMesh.flushBusy, 1);
    CHECK(g_pendingMesh.pendingAnim == &animObj);

    SetCharRender5Hooks(nullptr);
    ResetCharRender5();
}

// --- CountByType --------------------------------------------------------------
namespace {
CountByTypeDefs g_ctDefs;
u8 CtResolve(int personId, CountByTypeDefs* defs) {
    *defs = g_ctDefs;
    if (personId == 100) return g_ctDefs.wantA;   // matches bucket A
    if (personId == 200) return g_ctDefs.wantB;   // matches bucket B
    return 0xFF;                                   // unresolvable
}
} // namespace

TEST(CharRender5, CountByTypeBuckets) {
    g_ctDefs.wantA = 3;
    g_ctDefs.wantB = 7;
    int ids[3] = { 100, 200, -1 };  // A, B, free-slot
    int outA = -1, outB = -1;
    int scanned = CountByType(ids, 3, CtResolve, &outA, &outB);
    CHECK_EQ(scanned, 3);
    CHECK_EQ(outA, 1);
    CHECK_EQ(outB, 1);

    int ids2[3] = { 100, 100, 100 };
    CountByType(ids2, 3, CtResolve, &outA, &outB);
    CHECK_EQ(outA, 3);
    CHECK_EQ(outB, 0);
}
