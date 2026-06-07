// Unit tests for the walk-on-path motion executor + universe relocation.
//   - AngleToTargetSigned geometry golden (vs a Python reference).
//   - ObjectSetWorldTranslationXYZ field write.
//   - StepMotionQueue return codes (attach / reached / abort / playing).
//   - QueryTileAhead heightmap terrain sample + height snap.
//   - Move2UniverseActionUpdate validation ladder + field clear + visibility.
#include "tests/framework/test.h"

#include "sim/charaction_motion.h"
#include "sim/character_universe.h"
#include "render/heightmap.h"

#include <cmath>
#include <cstring>
#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {
bool Near(float a, float b, float eps = 1e-4f) { return std::fabs(a - b) <= eps; }

// Build a flat heightmap: size x size grid, all height byte = H, type byte = T.
struct TestMap {
    render::Heightmap hm{};
    std::vector<u8> heights;
    std::vector<u8> entries;   // 24-byte records
    TestMap(int size, u8 h, u8 type) {
        heights.assign((size_t)size * size, h);
        entries.assign((size_t)size * size * 24, 0);
        for (size_t i = 0; i < (size_t)size * size; ++i)
            entries[i * 24] = type;
        hm.originX = 0; hm.originY = 0; hm.originZ = 0;
        hm.scaleX = 1.0f; hm.scaleY = 1.0f; hm.scaleZ = 1.0f;
        hm.size = size;
        hm.heights = heights.data();
        hm.entries = entries.data();
    }
    void setType(int x, int y, u8 t) { entries[((size_t)y * hm.size + x) * 24] = t; }
};
} // namespace

// --- AngleToTargetSigned geometry golden ------------------------------------
TEST(SimMotion, AngleGoldenAhead) {
    float facing[3] = {0, 0, 1};
    float pivot[3]  = {0, 0, 0};
    float ahead[3]  = {0, 0, 5};
    CHECK(Near(AngleToTargetSigned(facing, pivot, ahead), 0.0f));
}

TEST(SimMotion, AngleGoldenRightLeftBehindDiag) {
    float facing[3] = {0, 0, 1};
    float pivot[3]  = {0, 0, 0};
    float right[3]  = {5, 0, 0};
    float left[3]   = {-5, 0, 0};
    float behind[3] = {0, 0, -5};
    float diag[3]   = {5, 0, 5};
    CHECK(Near(AngleToTargetSigned(facing, pivot, right),  -1.5707964f));
    CHECK(Near(AngleToTargetSigned(facing, pivot, left),    1.5707964f));
    CHECK(Near(AngleToTargetSigned(facing, pivot, behind), -3.1415927f, 1e-3f));
    CHECK(Near(AngleToTargetSigned(facing, pivot, diag),   -0.7853981f));
}

TEST(SimMotion, AngleHonoursPivotOffset) {
    // Pivot offset must be subtracted from the target before measuring.
    float facing[3] = {0, 0, 1};
    float pivot[3]  = {10, 0, 10};
    float ahead[3]  = {10, 0, 20};   // straight ahead relative to pivot
    CHECK(Near(AngleToTargetSigned(facing, pivot, ahead), 0.0f));
}

// --- ObjectSetWorldTranslationXYZ -------------------------------------------
TEST(SimMotion, SetWorldTranslationWritesTransform) {
    MotionAvatar av{};
    ObjectSetWorldTranslationXYZ(&av, 1.5f, 2.5f, 3.5f);
    CHECK(Near(av.transX, 1.5f));
    CHECK(Near(av.transYaw, 2.5f));   // +136 == heading
    CHECK(Near(av.transZ, 3.5f));
}

// --- StepMotionQueue return codes -------------------------------------------
namespace {
MotionAnim g_attached;
MotionAnim* AttachMotionStub(MotionQueueNode*) { g_attached = MotionAnim{}; return &g_attached; }
int g_pruned = 0;
void PruneStub(MotionQueueNode*) { ++g_pruned; }
}

TEST(SimMotion, StepMotionQueueAttachOnFirstPoll) {
    MotionAnim* handle = nullptr;
    MotionQueueNode n{};
    n.motionHandle = &handle;
    n.pathFlag = 0;
    n.packedResult = 7;
    n.attachMotion = &AttachMotionStub;
    n.pruneAttachment = &PruneStub;
    int r = StepMotionQueue(&n);
    CHECK_EQ(r, 7);              // returns the packed result on first attach
    CHECK(handle != nullptr);   // motion handle was filled
}

TEST(SimMotion, StepMotionQueueReachedReturnsMinusOne) {
    g_pruned = 0;
    MotionAnim anim{};
    anim.flags109 = 0x20;       // reached bit set
    MotionAnim* handle = &anim;
    MotionQueueNode n{};
    n.motionHandle = &handle;
    n.pathFlag = 1;
    n.packedResult = 7;
    n.attachMotion = &AttachMotionStub;
    n.pruneAttachment = &PruneStub;
    n.avatarFlag140 = 0;        // bit1 clear -> prune runs
    int r = StepMotionQueue(&n);
    CHECK_EQ(r, -1);
    CHECK(handle == nullptr);   // cleared
    CHECK_EQ(g_pruned, 1);
}

TEST(SimMotion, StepMotionQueueAbortReturnsMinusOne) {
    MotionAnim anim{};
    MotionAnim* handle = &anim;
    MotionQueueNode n{};
    n.motionHandle = &handle;
    n.pathFlag = 1;
    n.packedResult = 4;
    n.abort = 1;
    n.attachMotion = &AttachMotionStub;
    n.pruneAttachment = &PruneStub;
    n.avatarFlag140 = 2;        // bit1 set -> prune skipped
    int r = StepMotionQueue(&n);
    CHECK_EQ(r, -1);
    CHECK(handle == nullptr);
}

TEST(SimMotion, StepMotionQueuePlayingReturnsPacked) {
    MotionAnim anim{};          // not reached, not aborting
    MotionAnim* handle = &anim;
    MotionQueueNode n{};
    n.motionHandle = &handle;
    n.pathFlag = 1;
    n.packedResult = 5;
    int r = StepMotionQueue(&n);
    CHECK_EQ(r, 5);             // still playing
    CHECK(handle == &anim);     // handle untouched
}

// --- QueryTileAhead ----------------------------------------------------------
TEST(SimMotion, QueryTileAheadSamplesTerrainType) {
    TestMap m(8, /*h=*/10, /*type=*/3);
    m.setType(2, 2, 4);                 // the cell under the avatar -> type 4
    MotionAvatar av{};
    av.posX = 2.5f; av.posY = 10.0f; av.posZ = 2.5f;   // over cell (2,2)
    MotionCharacter ch{};
    ch.avatar = &av;
    ch.mesh = &m.hm;
    ch.universeIndoorFloor = 0;
    int type = QueryTileAhead(&ch);
    CHECK_EQ(type, 4);                  // sampled the (2,2) type byte
}

TEST(SimMotion, QueryTileAheadHeightSnapLerps) {
    // Flat map at height byte 40, scaleY 1 -> world height ~40.5. The avatar starts
    // at posY=0 and gets lerped 25% toward the (lifted, outdoor) target.
    TestMap m(8, /*h=*/40, /*type=*/3);
    MotionAvatar av{};
    av.posX = 3.5f; av.posY = 0.0f; av.posZ = 3.5f;
    MotionCharacter ch{};
    ch.avatar = &av;
    ch.mesh = &m.hm;
    ch.universeIndoorFloor = 0;         // outdoor -> +3.0 lift
    QueryTileAhead(&ch);
    // sampled height ~= 40.5 (bilinear, half-bias), + 3.0 lift = 43.5; lerp 25% from 0.
    float expected = (43.5f - 0.0f) * 0.25f + 0.0f;
    CHECK(Near(av.posY, expected, 0.3f));
    CHECK(av.posY > 0.0f);              // moved toward the terrain
}

TEST(SimMotion, QueryTileAheadNullMeshReturnsZero) {
    MotionAvatar av{};
    MotionCharacter ch{};
    ch.avatar = &av;
    ch.mesh = nullptr;
    CHECK_EQ(QueryTileAhead(&ch), 0);
}

// --- Move2UniverseActionUpdate validation ladder ----------------------------
TEST(SimUniverse, RejectsNegativeUniverse) {
    SetUniverseHooks(nullptr);
    UniverseTransition st{};
    st.dataUniverse = -1;
    CHECK(Move2UniverseActionUpdate(&st) == UniverseResult::kInvalidUniverse);
}

TEST(SimUniverse, RejectsInvalidCombo) {
    SetUniverseHooks(nullptr);
    UniverseTransition a{};
    a.dataUniverse = 0; a.dataId = 5;     // universe 0 but a real id
    CHECK(Move2UniverseActionUpdate(&a) == UniverseResult::kInvalidCombo);
    UniverseTransition b{};
    b.dataUniverse = 3; b.dataId = -1;    // real universe but CH_ID_NONE
    CHECK(Move2UniverseActionUpdate(&b) == UniverseResult::kInvalidCombo);
}

namespace {
int g_moveFail = 0;
int MoveFailHook(UniverseTransition*, int) { return g_moveFail ? 0 : 1; }
}

TEST(SimUniverse, MoveFailurePropagates) {
    UniverseHooks h{};
    h.moveToUniverse = &MoveFailHook;
    h.switchActiveSlot = [](int){};
    h.selectTextureSet = [](UniverseTransition*, int, u8){};
    h.stopSample = [](UniverseTransition*){};
    h.placeAtEntryDummy = [](UniverseTransition*, const char*){ return 1; };
    h.setVisible = [](UniverseTransition* s, int v){ s->visible = v; };
    SetUniverseHooks(&h);
    g_moveFail = 1;
    UniverseTransition st{};
    st.dataUniverse = 0; st.dataId = -1;
    CHECK(Move2UniverseActionUpdate(&st) == UniverseResult::kMoveFailed);
    g_moveFail = 0;
    SetUniverseHooks(nullptr);
}

TEST(SimUniverse, OkClearsScratchAndSetsIds) {
    SetUniverseHooks(nullptr);
    g_univActiveSlot = 7;
    UniverseTransition st{};
    st.dataUniverse = 3;
    st.dataId = 42;
    st.dataSubId = 9;
    st.char56 = st.char60 = st.char64 = 0x1234;   // dirty -> must be cleared
    CHECK(Move2UniverseActionUpdate(&st) == UniverseResult::kOk);
    CHECK_EQ(st.curUniverse, 42);    // +44 = Data1
    CHECK_EQ(st.char48, 9);          // +48 = Data2
    CHECK_EQ(st.char56, 0);
    CHECK_EQ(st.char60, 0);
    CHECK_EQ(st.char64, 0);
    CHECK_EQ(st.prevSlot, 7);        // saved active slot
    CHECK_EQ(st.visible, 1);         // last visibility request (no mismatch hide)
}

TEST(SimUniverse, SlotMismatchHides) {
    SetUniverseHooks(nullptr);
    g_univActiveSlot = 3;                // prevSlot == Data0 -> mismatch check active
    g_univActiveUniverseId = 99;         // != curUniverse (42) -> hide
    g_univActiveSubUniverse = -1;
    UniverseTransition st{};
    st.dataUniverse = 3;
    st.dataId = 42;
    st.dataSubId = 0;
    CHECK(Move2UniverseActionUpdate(&st) == UniverseResult::kOk);
    CHECK_EQ(st.visible, 0);         // hidden by the slot-mismatch rule
    g_univActiveUniverseId = 0;
}

TEST(SimUniverse, NextActionHideForcesHidden) {
    SetUniverseHooks(nullptr);
    g_univActiveSlot = 7;                // prevSlot != Data0 -> no mismatch hide
    g_univActiveUniverseId = 0;
    g_univActiveSubUniverse = -1;
    UniverseTransition st{};
    st.dataUniverse = 3;
    st.dataId = 42;
    st.nextIsHide = true;            // chained action is SetVisible(56) hide
    CHECK(Move2UniverseActionUpdate(&st) == UniverseResult::kOk);
    CHECK_EQ(st.visible, 0);
}
