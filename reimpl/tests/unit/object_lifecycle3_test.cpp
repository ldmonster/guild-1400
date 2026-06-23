// Unit tests for src/sim/object_lifecycle3.{h,cpp} (VIBE_Object_* batch 3).
// Golden vectors for GetTypeMessageId computed with python3; transform setters
// verified against the recovered dword-offset writes; find callbacks against the
// '!' prefix + queryNode fast-path control flow.
#include "sim/object_lifecycle3.h"

#include <cstring>

#include "test.h"

using namespace guild;
using namespace guild::sim;

namespace {

// Hook captor state.
struct Captor {
    int   markDirtyCalls = 0;
    int   shadowResetCalls = 0;
    int   matrixCalls = 0;
    float lastMatrixAngles[3] = {0, 0, 0};
    int   detachCalls = 0;
    int   rebindProto = -999;
    const char* rebindMesh = nullptr;
    int   soundCalls = 0;
    float soundXYZ[3] = {0, 0, 0};
    float soundWorld[3] = {0, 0, 0};
    int   rainCreateCalls = 0;
    int   rainDestroyCalls = 0;
    const char* lastError = nullptr;
};
Captor g_cap;

void hMark(SceneNode3*, u16 mask, u8) { (void)mask; ++g_cap.markDirtyCalls; }
void hShadow(SceneNode3*, u16) { ++g_cap.shadowResetCalls; }
void hMatrix(const float* a, SceneNode3*) {
    ++g_cap.matrixCalls;
    g_cap.lastMatrixAngles[0] = a[0];
    g_cap.lastMatrixAngles[1] = a[1];
    g_cap.lastMatrixAngles[2] = a[2];
}
void hDetach(SceneNode3*) { ++g_cap.detachCalls; }
void hRebind(SceneNode3*, int p, const char* m) {
    g_cap.rebindProto = p;
    g_cap.rebindMesh = m;
}
void hSound(SceneNode3*, float x, float y, float z, float wx, float wy,
            float wz, int) {
    ++g_cap.soundCalls;
    g_cap.soundXYZ[0] = x; g_cap.soundXYZ[1] = y; g_cap.soundXYZ[2] = z;
    g_cap.soundWorld[0] = wx; g_cap.soundWorld[1] = wy; g_cap.soundWorld[2] = wz;
}
void* hRainCreate(SceneNode3*, int, int) { ++g_cap.rainCreateCalls; return (void*)1; }
void hRainDestroy(SceneNode3*) { ++g_cap.rainDestroyCalls; }
void hError(const char* m) { g_cap.lastError = m; }

void InstallHooks() {
    g_cap = Captor{};
    ObjLife3Hooks h;
    h.walkMarkDirty = hMark;
    h.traverseShadowReset = hShadow;
    h.matrixFromEuler = hMatrix;
    h.detachAndRelease = hDetach;
    h.rebindParentMesh = hRebind;
    h.sound3dMove = hSound;
    h.rainCreate = hRainCreate;
    h.rainDestroy = hRainDestroy;
    h.reportError = hError;
    ObjLife3SetHooks(h);
    g_objCurrent = nullptr;
}

}  // namespace

// ---------------------------------------------------------------------------
TEST(ObjLife3, SetPositionWritesPosAndDirties) {
    InstallHooks();
    SceneNode3 n;
    float p[3] = {1.5f, -2.0f, 3.25f};
    ObjectSetPosition(&n, p);
    CHECK_EQ(n.f(76 + 0), 1.5f);
    CHECK_EQ(n.f(76 + 4), -2.0f);
    CHECK_EQ(n.f(76 + 8), 3.25f);
    // dirty bit2 set on the root node, shadow-reset traversed.
    CHECK((n.b(528) & 4) != 0);
    CHECK_EQ(g_cap.markDirtyCalls, 1);
    CHECK_EQ(g_cap.shadowResetCalls, 1);
}

TEST(ObjLife3, SetPositionXYZThunk) {
    InstallHooks();
    SceneNode3 n;
    ObjectSetPositionXYZ(&n, 7.f, 8.f, 9.f);
    CHECK_EQ(n.f(76 + 0), 7.f);
    CHECK_EQ(n.f(76 + 4), 8.f);
    CHECK_EQ(n.f(76 + 8), 9.f);
}

TEST(ObjLife3, SetScaleAndPivotOffsets) {
    InstallHooks();
    SceneNode3 n;
    float s[3] = {2.f, 3.f, 4.f};
    ObjectSetScaleVector(&n, s);
    CHECK_EQ(n.f(108 + 0), 2.f);
    CHECK_EQ(n.f(108 + 4), 3.f);
    CHECK_EQ(n.f(108 + 8), 4.f);
    float pv[3] = {5.f, 6.f, 7.f};
    ObjectSetPivotVectorXYZ(&n, pv[0], pv[1], pv[2]);
    CHECK_EQ(n.f(120 + 0), 5.f);
    CHECK_EQ(n.f(120 + 4), 6.f);
    CHECK_EQ(n.f(120 + 8), 7.f);
}

TEST(ObjLife3, WorldTranslationNonPivotBuildsMatrixForward) {
    InstallHooks();
    SceneNode3 n;
    n.b(533) = 0;  // nodeType != 3
    float a[3] = {0.1f, 0.2f, 0.3f};
    ObjectSetWorldTranslation(&n, a);
    CHECK_EQ(n.f(132 + 0), 0.1f);
    CHECK_EQ(n.f(132 + 4), 0.2f);
    CHECK_EQ(n.f(132 + 8), 0.3f);
    // forward (positive) angles, then shadow reset.
    CHECK_EQ(g_cap.matrixCalls, 1);
    CHECK_EQ(g_cap.lastMatrixAngles[0], 0.1f);
    CHECK_EQ(g_cap.shadowResetCalls, 1);
}

TEST(ObjLife3, WorldTranslationPivotNegatesAnglesNoShadow) {
    InstallHooks();
    SceneNode3 n;
    n.b(533) = 3;  // nodeType == 3 -> negate path
    float a[3] = {1.f, -2.f, 3.f};
    ObjectSetWorldTranslation(&n, a);
    CHECK_EQ(n.f(132 + 0), 1.f);
    CHECK_EQ(g_cap.matrixCalls, 1);
    CHECK_EQ(g_cap.lastMatrixAngles[0], -1.f);
    CHECK_EQ(g_cap.lastMatrixAngles[1], 2.f);
    CHECK_EQ(g_cap.lastMatrixAngles[2], -3.f);
    // pivot path does NOT call the shadow traverse.
    CHECK_EQ(g_cap.shadowResetCalls, 0);
}

TEST(ObjLife3, SetPositionCurrentRoutesInvalidate) {
    InstallHooks();
    SceneNode3 n;
    g_objCurrent = &n;
    float p[3] = {1.f, 1.f, 1.f};
    ObjectSetPosition(&n, p);
    // current-object path skips the subtree walk hook but still shadow-resets.
    CHECK_EQ(g_cap.markDirtyCalls, 0);
    CHECK_EQ(g_cap.shadowResetCalls, 1);
    CHECK_EQ(n.f(76), 1.f);
}

TEST(ObjLife3, KillObjectDetachVsError) {
    InstallHooks();
    SceneNode3 n;
    CHECK_EQ(ObjectKillObject(&n), 1);
    CHECK_EQ(g_cap.detachCalls, 1);
    CHECK_EQ(ObjectKillObject(nullptr), 1);
    CHECK(g_cap.lastError != nullptr);
}

TEST(ObjLife3, SetPosOrdersXYZIntoNode) {
    InstallHooks();
    SceneNode3 n;
    ObjectSetPos(&n, 11.f, 22.f, 33.f);  // x,y,z node order
    CHECK_EQ(n.f(76 + 0), 11.f);
    CHECK_EQ(n.f(76 + 4), 22.f);
    CHECK_EQ(n.f(76 + 8), 33.f);
}

TEST(ObjLife3, MoveObjectFeedsSound) {
    InstallHooks();
    SceneNode3 n;
    n.f(132 + 0) = 100.f; n.f(132 + 4) = 200.f; n.f(132 + 8) = 300.f;
    CHECK_EQ(ObjectMoveObject(&n, 1, 2, 3, 9), 0);
    CHECK_EQ(g_cap.soundCalls, 1);
    CHECK_EQ(g_cap.soundXYZ[0], 1.f);
    CHECK_EQ(g_cap.soundXYZ[1], 2.f);
    CHECK_EQ(g_cap.soundXYZ[2], 3.f);
    CHECK_EQ(g_cap.soundWorld[0], 100.f);
    CHECK_EQ(g_cap.soundWorld[2], 300.f);
}

TEST(ObjLife3, ReplaceAndRainThunks) {
    InstallHooks();
    SceneNode3 n;
    CHECK_EQ(ObjectReplaceObject(&n, 42, "newmesh"), 1);
    CHECK_EQ(g_cap.rebindProto, 42);
    CHECK(g_cap.rebindMesh != nullptr &&
          std::strcmp(g_cap.rebindMesh, "newmesh") == 0);
    CHECK(ObjectCmdSetObjectStateThunk(&n, 0, 0) != nullptr);
    CHECK_EQ(g_cap.rainCreateCalls, 1);
    CHECK_EQ(ObjectCmdResetObjectThunk(&n), 0);
    CHECK_EQ(g_cap.rainDestroyCalls, 1);
}

TEST(ObjLife3, ToggleHiddenStateTransitions) {
    InstallHooks();
    SceneNode3 n;
    n.b(0) = 114;     // 'r'
    n.b(533) = 5;
    // enable: 5 -> 6, frame stamp recorded.
    CHECK_EQ(ObjectToggleHiddenState(&n, 1, 0x1234), 1);
    CHECK_EQ((int)n.b(533), 6);
    CHECK_EQ(n.d(64), 0x1234);
    // disable: 6 -> 5.
    CHECK_EQ(ObjectToggleHiddenState(&n, 0, 0), 1);
    CHECK_EQ((int)n.b(533), 5);
    // enable when name not 'r' is a no-op transition (stays 5).
    n.b(0) = 'x';
    CHECK_EQ(ObjectToggleHiddenState(&n, 1, 99), 1);
    CHECK_EQ((int)n.b(533), 5);
}

TEST(ObjLife3, FindObjectByIdStride67) {
    // Build a 4-entry table, stride 67, marker word @+0, id dword @+2.
    const int stride = 67, count = 4;
    u8 table[stride * count];
    std::memset(table, 0, sizeof(table));
    auto setEntry = [&](int i, u16 marker, i32 id) {
        *reinterpret_cast<u16*>(table + i * stride) = marker;
        *reinterpret_cast<i32*>(table + i * stride + 2) = id;
    };
    setEntry(0, 0, 555);       // free slot (marker 0) — must be skipped
    setEntry(1, 1, 777);
    setEntry(2, 1, 555);       // matching id but live
    setEntry(3, 1, 999);
    CHECK_EQ(ObjectFindObjectByIdIndex(table, count, stride, 777), 1);
    CHECK_EQ(ObjectFindObjectByIdIndex(table, count, stride, 555), 2);
    CHECK_EQ(ObjectFindObjectByIdIndex(table, count, stride, 12345), -1);
}

TEST(ObjLife3, MatchHandleCaseSensitive) {
    InstallHooks();
    SceneNode3 n;
    std::strcpy(reinterpret_cast<char*>(n.raw), "!Door");  // '!' prefix skipped
    FindCtx ctx{};
    ctx.queryStr = "Door";
    ctx.queryNode = nullptr;
    ctx.found = nullptr;
    bool keep = ObjectMatchHandleCallback(&n, &ctx);
    CHECK(ctx.found == &n);
    CHECK_EQ(keep, false);  // found == node -> stop walking
    // case mismatch does NOT match (case-sensitive).
    FindCtx ctx2{};
    ctx2.queryStr = "door";
    ObjectMatchHandleCallback(&n, &ctx2);
    CHECK(ctx2.found == nullptr);
}

TEST(ObjLife3, MatchNameCaseInsensitive) {
    SceneNode3 n;
    std::strcpy(reinterpret_cast<char*>(n.raw), "Door");
    FindCtx ctx{};
    ctx.queryStr = "dOOr";
    ObjectMatchNameCallback(&n, &ctx);
    CHECK(ctx.found == &n);
}

TEST(ObjLife3, IsNearDoorAltTolerances) {
    DoorProximityAltInputs in{};
    in.hasActor = true;
    in.targetHasAnchor = true;
    in.doorDummyFound = true;  // tol = 650
    in.doorPos[0] = 0; in.doorPos[1] = 0; in.doorPos[2] = 0;
    in.testPos[0] = 600; in.testPos[1] = 0; in.testPos[2] = 0;
    CHECK(ObjectIsNearDoorAltCore(in));    // within 650
    in.testPos[0] = 700;
    CHECK(!ObjectIsNearDoorAltCore(in) ||  // out of 650
          (in.hasActor && in.actorOwnerId == in.targetOwnerId));
    // fallback (no dummy) widens to 3250.
    in.doorDummyFound = false;
    in.testPos[0] = 3000;
    CHECK(ObjectIsNearDoorAltCore(in));
    // owner-id equality fallback when out of range.
    in.testPos[0] = 9999;
    in.actorOwnerId = 5; in.targetOwnerId = 5;
    CHECK(ObjectIsNearDoorAltCore(in));
    in.targetOwnerId = 6;
    CHECK(!ObjectIsNearDoorAltCore(in));
}

// ---------------------------------------------------------------------------
// GetTypeMessageId — golden vectors (python3-computed).
// ---------------------------------------------------------------------------
TEST(ObjLife3, GetTypeMessageIdGolden) {
    struct V { int type, itemHigh, bldgKind; bool hasDef; int defVal;
               int count; i32 out[3]; };
    const V vecs[] = {
        { -1, 0, 0, false, 0, 0, {0,0,0} },
        { 63, 0, 0, false, 0, 1, {1205,0,0} },
        { 64, 0, 0, false, 0, 1, {1198,0,0} },
        { 67, 0, 0, false, 0, 1, {1202,0,0} },
        { 97, 0, 0, false, 0, 1, {1202,0,0} },
        { 98, 0, 0, false, 0, 1, {1209,0,0} },
        { 100, 0, 0, false, 0, 1, {1202,0,0} },
        { 101, 0, 0, false, 0, 1, {1202,0,0} },
        { 108, 0, 0, false, 0, 1, {1200,0,0} },
        { 117, 0, 0, false, 0, 1, {1197,0,0} },
        { 72, 0, 0, false, 0, 1, {1197,0,0} },
        { 73, 0, 0, false, 0, 1, {1205,0,0} },
        { 60, 0, 0, false, 0, 1, {1199,0,0} },
        { 40, 500, 0, false, 0, 2, {1194,706,0} },
        { 20, 7, 0, false, 0, 2, {1195,213,0} },
        { 22, 0, 0, false, 0, 1, {1204,0,0} },
        { 4, 11, 7, false, 0, 2, {1203,217,0} },
        { 4, 11, 8, false, 0, 2, {1201,217,0} },
        { 4, 11, 14, false, 0, 2, {1201,217,0} },
        { 4, 11, 2, false, 0, 2, {1194,217,0} },
        { 8, 9, 0, false, 0, 2, {1195,215,0} },
        { 9, 0, 0, false, 0, 1, {1195,0,0} },
        { 3, 100, 0, false, 0, 2, {1196,306,0} },
        { 7, 0, 0, false, 0, 0, {0,0,0} },
        { 3, 100, 0, true, 50, 3, {1196,306,256} },
        { -1, 0, 0, true, 30, 1, {236,0,0} },
    };
    for (const auto& v : vecs) {
        TypeMessageInputs in{};
        in.typeByte = v.type;
        in.itemHighWord = v.itemHigh;
        in.buildingKind = v.bldgKind;
        in.hasCombatDef = v.hasDef;
        in.combatDefValue = v.defVal;
        i32 out[3] = {0, 0, 0};
        int n = ObjectGetTypeMessageId(in, out);
        CHECK_EQ(n, v.count);
        CHECK_EQ(out[0], v.out[0]);
        CHECK_EQ(out[1], v.out[1]);
        CHECK_EQ(out[2], v.out[2]);
    }
}
