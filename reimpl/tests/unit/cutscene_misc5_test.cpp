// Unit tests for the VIBE_Scene_* scene-sync slice (cutscene_misc5).
// Golden vectors computed with python3; control-flow drivers exercised over a
// recording mock hook surface.
#include "test.h"

#include "sim/cutscene_misc5.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

// ---------------------------------------------------------------------------
// Deterministic kernels.
// ---------------------------------------------------------------------------
TEST(CutsceneMisc5, RetZeroIsZero) {
    CHECK_EQ(static_cast<int>(SceneRetZero()), 0);
}

TEST(CutsceneMisc5, CollectMatchingObjectAppendsOnMatch) {
    i32 list[600];
    std::memset(list, 0, sizeof(list));
    // Two matched appends, one non-match.
    CHECK(SceneCollectMatchingObject(list, 111, true));
    CHECK_EQ(list[0], 1);
    CHECK_EQ(list[1], 111);
    CHECK(SceneCollectMatchingObject(list, 222, false));  // no append
    CHECK_EQ(list[0], 1);
    CHECK(SceneCollectMatchingObject(list, 333, true));
    CHECK_EQ(list[0], 2);
    CHECK_EQ(list[2], 333);
}

TEST(CutsceneMisc5, CollectMatchingObjectCapacity) {
    i32 list[600];
    std::memset(list, 0, sizeof(list));
    list[0] = 511;
    // After one more append count==512 -> returns false (full).
    CHECK(!SceneCollectMatchingObject(list, 7, true));
    CHECK_EQ(list[0], 512);
    CHECK_EQ(list[512], 7);
}

TEST(CutsceneMisc5, CollectTorchObjectAppendsAndCaps) {
    i32 list[64];
    std::memset(list, 0, sizeof(list));
    CHECK(SceneCollectTorchObject(list, 5, true));
    CHECK_EQ(list[32], 1);
    CHECK_EQ(list[0], 5);
    CHECK(SceneCollectTorchObject(list, 6, false));  // no append
    CHECK_EQ(list[32], 1);
    CHECK(SceneCollectTorchObject(list, 9, true));
    CHECK_EQ(list[32], 2);
    CHECK_EQ(list[1], 9);
    // Cap at 32.
    list[32] = 31;
    CHECK(!SceneCollectTorchObject(list, 1, true));
    CHECK_EQ(list[32], 32);
}

TEST(CutsceneMisc5, FlagBuildingGateOnlyOnGbAndMatch) {
    u8 b = 0xFF;
    // Not "gb_" -> untouched.
    CHECK_EQ(static_cast<int>(SceneFlagBuildingGate(&b, false, true)), 1);
    CHECK_EQ(static_cast<int>(b), 0xFF);
    // "gb_" but no inner match -> untouched.
    CHECK_EQ(static_cast<int>(SceneFlagBuildingGate(&b, true, false)), 1);
    CHECK_EQ(static_cast<int>(b), 0xFF);
    // "gb_" + match -> (0xFF & 0xF3) | 4 == 0xF7.
    CHECK_EQ(static_cast<int>(SceneFlagBuildingGate(&b, true, true)), 1);
    CHECK_EQ(static_cast<int>(b), 0xF7);
}

TEST(CutsceneMisc5, FlagGateObjectSetsAndClears) {
    u8 lo = 0xFF;
    i32 f536 = -1;
    CHECK_EQ(SceneFlagGateObject(true, &lo, &f536), 1);
    CHECK_EQ(f536, 1);
    CHECK_EQ(static_cast<int>(lo), 0xFE);  // bit 0 cleared
    // No match: +536 = 0, lo untouched.
    u8 lo2 = 0x05;
    i32 f2 = 99;
    CHECK_EQ(SceneFlagGateObject(false, &lo2, &f2), 0);
    CHECK_EQ(f2, 0);
    CHECK_EQ(static_cast<int>(lo2), 0x05);
}

TEST(CutsceneMisc5, ClassifyMeisterRecords) {
    // kinds: index 0=11(B), 1=12(A sub0), 2=12(A sub1), 3=11(B), 4=12(A sub0 dup)
    u8 kinds[]    = {11, 12, 12, 11, 12};
    u8 subState[] = { 0,  0,  1,  0,  0};
    int aA = 99, aB = 99;
    int outB[8];
    int nB = SceneClassifyMeisterRecords(kinds, subState, 5, &aA, &aB, outB, 8);
    CHECK_EQ(aA, 1);   // class-12 sub-0 @1; both found at index 2 -> scan stops
    CHECK_EQ(aB, 2);   // class-12 sub-1 @2 completes v5 == 3
    CHECK_EQ(nB, 2);   // two class-11 records (the second scan runs to `count`)
    CHECK_EQ(outB[0], 0);
    CHECK_EQ(outB[1], 3);
}

// gilde.exe 0x504ce0 — the binary OVERWRITES the anchor candidate on every
// class-12 match (the v4/v3 stores are unconditional) and only stops once BOTH
// anchors were seen (v5 == 3): the LAST qualifying match before the both-found
// point wins; records after that point never update an anchor.
TEST(CutsceneMisc5, ClassifyMeisterOverwritesUntilBothFound) {
    // sub-0 @0, sub-0 @1 (overwrites @0), sub-1 @2 (-> v5 == 3, stop),
    // sub-0 @3 (after the stop: must NOT overwrite).
    u8 kinds[]    = {12, 12, 12, 12};
    u8 subState[] = { 0,  0,  1,  0};
    int aA = 99, aB = 99;
    int outB[4];
    int nB = SceneClassifyMeisterRecords(kinds, subState, 4, &aA, &aB, outB, 4);
    CHECK_EQ(aA, 1);   // LAST sub-0 before both found (index 1, not 0 / not 3)
    CHECK_EQ(aB, 2);
    CHECK_EQ(nB, 0);
}

// Symmetric overwrite on the sub-1 (anchorB) side.
TEST(CutsceneMisc5, ClassifyMeisterOverwritesAnchorB) {
    // sub-1 @0, sub-1 @1 (overwrites @0), sub-0 @2 (-> v5 == 3, stop),
    // sub-1 @3 unreached.
    u8 kinds[]    = {12, 12, 12, 12};
    u8 subState[] = { 1,  1,  0,  1};
    int aA = 99, aB = 99;
    int nB = SceneClassifyMeisterRecords(kinds, subState, 4, &aA, &aB, nullptr, 0);
    CHECK_EQ(aB, 1);   // LAST sub-1 before both found
    CHECK_EQ(aA, 2);
    CHECK_EQ(nB, 0);
}

// When the second anchor kind never appears, v5 never reaches 3, the scan runs
// to the end of the array and the LAST match of the present kind wins outright.
TEST(CutsceneMisc5, ClassifyMeisterLastMatchWinsWhenNeverBothFound) {
    u8 kinds[]    = {12, 11, 12, 12};
    u8 subState[] = { 0,  0,  0,  0};
    int aA = 99, aB = 99;
    int outB[4];
    int nB = SceneClassifyMeisterRecords(kinds, subState, 4, &aA, &aB, outB, 4);
    CHECK_EQ(aA, 3);   // overwritten 0 -> 2 -> 3 (full scan, no early stop)
    CHECK_EQ(aB, -1);
    CHECK_EQ(nB, 1);
    CHECK_EQ(outB[0], 1);
}

TEST(CutsceneMisc5, ClassifyMeisterNoAnchors) {
    u8 kinds[]    = {11, 11, 5};
    u8 subState[] = { 0,  0, 0};
    int aA = 7, aB = 7;
    int outB[4];
    int nB = SceneClassifyMeisterRecords(kinds, subState, 3, &aA, &aB, outB, 4);
    CHECK_EQ(aA, -1);
    CHECK_EQ(aB, -1);
    CHECK_EQ(nB, 2);
}

TEST(CutsceneMisc5, ComputeProductionTickRateGoldens) {
    {
        int prices[] = {100, 50};
        u16 counts[] = {2, 3};
        bool valid[] = {true, true};
        CHECK_EQ(SceneComputeProductionTickRate(prices, counts, valid, 2, false), 45);
        CHECK_EQ(SceneComputeProductionTickRate(prices, counts, valid, 2, true), 45);
    }
    {
        int prices[] = {8000, 8000};
        u16 counts[] = {1, 1};
        bool valid[] = {true, true};
        CHECK_EQ(SceneComputeProductionTickRate(prices, counts, valid, 2, false), 1);
    }
    {
        int prices[] = {20000};
        u16 counts[] = {1};
        bool valid[] = {true};
        CHECK_EQ(SceneComputeProductionTickRate(prices, counts, valid, 1, false), 1);
    }
    {
        int prices[] = {0};
        u16 counts[] = {0};
        bool valid[] = {true};
        CHECK_EQ(SceneComputeProductionTickRate(prices, counts, valid, 1, false), 0);
    }
    {
        int prices[] = {100};
        u16 counts[] = {5};
        bool valid[] = {false};
        CHECK_EQ(SceneComputeProductionTickRate(prices, counts, valid, 1, false), 0);
    }
    {
        // Only the first 4 slots count (loop guard v9 < 4).
        int prices[] = {300, 300, 300, 300, 300};
        u16 counts[] = {1, 1, 1, 1, 1};
        bool valid[] = {true, true, true, true, true};
        CHECK_EQ(SceneComputeProductionTickRate(prices, counts, valid, 5, false), 13);
    }
}

// ---------------------------------------------------------------------------
// Full-flow drivers over a recording mock.
// ---------------------------------------------------------------------------
namespace {
struct Recorder {
    int loadResult = 0;
    // COPY of the load path, not the pointer: SceneLoadStadtScene formats the
    // path into a local stack buffer and passes it to the hook synchronously, so
    // stashing the raw pointer dangles after return (ASAN stack-use-after-return).
    char lastFile[280] = {0};
    bool gotFile = false;
    int traverseMasks[8] = {0};
    int traverseN = 0;
    int q17Count = 0;
    i32 lastQ17Obj = 0, lastQ17Product = 0;
    int cmd15Amt = -1;
    int mulRate = -1;
    int smokeCount = 0;
};
Recorder g_rec;

int RecLoad(const char* f) {
    if (f) { std::strncpy(g_rec.lastFile, f, sizeof(g_rec.lastFile) - 1); g_rec.gotFile = true; }
    return g_rec.loadResult;
}
void RecTraverse(int m) { if (g_rec.traverseN < 8) g_rec.traverseMasks[g_rec.traverseN++] = m; }
void RecQ17(i32 o, i32, int, int p) { g_rec.q17Count++; g_rec.lastQ17Obj = o; g_rec.lastQ17Product = p; }
void RecCmd15(i32, i32 amt) { g_rec.cmd15Amt = amt; }
int  RecMul(int amt, u8 r) { g_rec.mulRate = r; return amt * 2; }  // 500 -> 1000
void RecSmoke(int, int) { g_rec.smokeCount++; }
}  // namespace

TEST(CutsceneMisc5, LoadStadtSceneSuccessRunsTraversal) {
    g_rec = Recorder{};
    g_rec.loadResult = 1;
    SceneSyncHooks h{};
    h.loadFromStream = RecLoad;
    h.traverseTree = RecTraverse;
    SetSceneSyncHooks(&h);

    CHECK_EQ(static_cast<int>(SceneLoadStadtScene("ROM")), 1);
    if (g_rec.gotFile)
        CHECK_EQ(std::strcmp(g_rec.lastFile, "scenes/*stadt_ROM.ed3"), 0);
    CHECK_EQ(g_rec.traverseN, 1);
    CHECK_EQ(g_rec.traverseMasks[0], 6);  // particle-emitter init mask
    SetSceneSyncHooks(nullptr);
}

TEST(CutsceneMisc5, LoadStadtSceneFailureNoTraversal) {
    g_rec = Recorder{};
    g_rec.loadResult = 0;
    SceneSyncHooks h{};
    h.loadFromStream = RecLoad;
    h.traverseTree = RecTraverse;
    SetSceneSyncHooks(&h);

    CHECK_EQ(static_cast<int>(SceneLoadStadtScene("X")), 0);
    CHECK_EQ(g_rec.traverseN, 0);
    SetSceneSyncHooks(nullptr);
}

TEST(CutsceneMisc5, SyncBuildingEntranceBauplatz) {
    g_rec = Recorder{};
    SceneSyncHooks h{};
    h.queueRequest17 = RecQ17;
    h.enqueueCmd15 = RecCmd15;
    h.multiplyByRate = RecMul;
    SetSceneSyncHooks(&h);

    int p = SceneSyncBuildingEntrance(/*typeByte=*/9, /*objId=*/42, /*rate=*/3);
    CHECK_EQ(p, 310);
    CHECK_EQ(g_rec.lastQ17Product, 310);
    CHECK_EQ(g_rec.cmd15Amt, 1000);   // MultiplyByRate(500,3) -> 500*2
    CHECK_EQ(g_rec.mulRate, 3);
    SetSceneSyncHooks(nullptr);
}

TEST(CutsceneMisc5, SyncBuildingEntranceStoreExempt) {
    g_rec = Recorder{};
    SceneSyncHooks h{};
    h.queueRequest17 = RecQ17;
    SetSceneSyncHooks(&h);

    CHECK_EQ(SceneSyncBuildingEntrance(16, 1, 0), -1);  // storage type 16 exempt
    CHECK_EQ(g_rec.q17Count, 0);
    SetSceneSyncHooks(nullptr);
}

TEST(CutsceneMisc5, SyncBuildingEntranceStandardFee) {
    g_rec = Recorder{};
    SceneSyncHooks h{};
    h.queueRequest17 = RecQ17;
    SetSceneSyncHooks(&h);

    CHECK_EQ(SceneSyncBuildingEntrance(7, 5, 0), 308);  // ordinary building
    CHECK_EQ(g_rec.lastQ17Product, 308);
    CHECK_EQ(g_rec.q17Count, 1);
    SetSceneSyncHooks(nullptr);
}

TEST(CutsceneMisc5, RefreshBuildingEffectsSpawnsAndTraverses) {
    g_rec = Recorder{};
    SceneSyncHooks h{};
    h.spawnChimneySmoke = RecSmoke;
    h.traverseTree = RecTraverse;
    SetSceneSyncHooks(&h);

    int data[] = {100, 0, 200, 300};  // 3 with building data, 1 without
    int n = SceneRefreshBuildingEffects(data, 4, /*a1=*/7);
    CHECK_EQ(n, 3);
    CHECK_EQ(g_rec.smokeCount, 3);
    // gate pass (64) then torch pass (192).
    CHECK_EQ(g_rec.traverseN, 2);
    CHECK_EQ(g_rec.traverseMasks[0], 64);
    CHECK_EQ(g_rec.traverseMasks[1], 192);
    SetSceneSyncHooks(nullptr);
}

TEST(CutsceneMisc5, InertDefaultsAreSafe) {
    SetSceneSyncHooks(nullptr);
    // No hooks installed -> load fails, no traversal, no crash.
    CHECK_EQ(static_cast<int>(SceneLoadStadtScene("Z")), 0);
    CHECK_EQ(SceneSyncBuildingEntrance(9, 1, 1), 310);  // still classifies
    CHECK_EQ(SceneRefreshBuildingEffects(nullptr, 0, 0), 0);
}
