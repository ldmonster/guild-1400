// Golden-vector tests for the per-frame scene-update pass (wave-21):
//   gilde.exe 0x41ceb4 VIBE_DecompressGameState -> SceneUpdatePass
//   gilde.exe 0x40e50c VIBE_Decompressor_Init   -> DecompressorInit
// Synthetic entities pin the entity-table walk + per-record type dispatch.
#include "tests/framework/test.h"
#include "sim/entity_frame_update.h"

#include <string>
#include <vector>

using namespace guild::sim;

namespace {

// Recording hooks: append a tag per leaf so the dispatch ORDER is assertable.
struct RecScene : SceneFrameHooks {
    std::vector<std::string> trace;
    int lockCount = 0, unlockCount = 0;
    int decompressStateBlob(i32) override { ++lockCount; trace.push_back("LOCK"); return 0; }
    int decompressionFinalize(i32) override { ++unlockCount; trace.push_back("UNLOCK"); return 0; }
    void coordPush(int, int, int, int) override { trace.push_back("push"); }
    void entityChildProcess(i32, i32) override { trace.push_back("entityChild"); }
    void objectReinitialize(int, int, int, int, i32) override { trace.push_back("reinit"); }
    void objectUpdate(i32, i32) override { trace.push_back("objectUpdate"); }
    void buildingUpdate(i32, i32) override { trace.push_back("building"); }
    void animationApply(int, int, int, i32, const char*, u8) override { trace.push_back("animApply"); }
    void entityAnimationUpdate(int, int, int, int, i32) override { trace.push_back("entityAnim"); }
    void entityInteractionLogic(i32, i32) override { trace.push_back("interaction"); }
    void animationBasic(int, int, i32, i32, u8) override { trace.push_back("basic"); }
    void animationAdvanced(int, int, i32, i32, u8) override { trace.push_back("advanced"); }
    void velocityApply(int, int, i32, i32, u8) override { trace.push_back("velocity"); }
    void stateFinalize(i32) override { trace.push_back("finalize"); }
    void resultBroadcast(int, int, int, int, i32, int, int, i32) override { trace.push_back("broadcast"); }
    bool has(const std::string& t) const {
        for (auto& s : trace) if (s == t) return true;
        return false;
    }
    int count(const std::string& t) const {
        int n = 0; for (auto& s : trace) if (s == t) ++n; return n;
    }
};

struct DecHooks : DecompInitHooks {
    int handlers = 0, gray = 0;
    void resultHandlerInteraction(i32, i32, i32, i32, i32, i32, i32, i32) override { ++handlers; }
    void lightSetGrayColorThunk(int, int, i32) override { ++gray; }
};

} // namespace

// --- 0x41ceb4: gate flags must both be set ---------------------------------
TEST(EntityFrameUpdate, GateFlagsBail) {
    DecompRecord rec;
    rec.flag100 = 0; rec.flag102 = 1; rec.blob105 = 42;
    SceneFrameState st; RecScene h;
    int r = SceneUpdatePass(rec, nullptr, 0, nullptr, 0, st, h);
    CHECK_EQ(r, 42);                 // returns the record blob id
    CHECK_EQ(h.lockCount, 0);        // no work when a gate is clear
}

// --- 0x41ceb4: lock/unlock pair always brackets the walk -------------------
TEST(EntityFrameUpdate, LockUnlockBracket) {
    DecompRecord rec;
    rec.flag100 = 1; rec.flag102 = 1; rec.blob105 = 7; rec.childCount = 0;
    SceneFrameState st; RecScene h;
    int r = SceneUpdatePass(rec, nullptr, 0, nullptr, 0, st, h);
    CHECK_EQ(r, 7);
    CHECK_EQ(h.lockCount, 1);
    CHECK_EQ(h.unlockCount, 1);
    // LOCK first, UNLOCK last.
    CHECK(h.trace.front() == "LOCK");
    CHECK(h.trace.back()  == "UNLOCK");
}

// --- 0x41ceb4: an entity body with no child-anim emits a reinit border ------
TEST(EntityFrameUpdate, EntityBodyReinit) {
    RenderNode nodes[2];
    nodes[0].child44 = 0;            // body node: triggers reinit
    i32 childIdx[1] = {0};
    EntityRecord ent[1];
    ent[0].x2 = 10 << 16; ent[0].y = 20 << 16; ent[0].z = 30 << 16; ent[0].w6 = 40 << 16;
    ent[0].childCount = 0;           // no child nodes
    ent[0].childNodes = nullptr;
    ent[0].bodyNode = 0;

    DecompRecord rec;
    rec.flag100 = 1; rec.flag102 = 1; rec.blob105 = 1; rec.childCount = 1;
    rec.childIndex = childIdx; rec.originX = 5; rec.originY = 6;

    SceneFrameState st; RecScene h;
    SceneUpdatePass(rec, ent, 1, nodes, 2, st, h);
    CHECK(h.has("entityChild"));     // EntityChild_Process ran
    CHECK(h.has("reinit"));          // body reinit ran (child44 == 0)
}

// --- 0x41ceb4: type-byte dispatch routing ----------------------------------
static int RunTypeDispatch(u8 type, RecScene& h) {
    static RenderNode nodes[1];
    nodes[0] = RenderNode{};
    nodes[0].meshHandle = 1;         // non-empty so it dispatches
    nodes[0].typeByte = type;
    nodes[0].child44 = 1;            // suppress incidental reinit
    nodes[0].life = 5;
    static i32 kidNodes[1] = {0};
    static EntityRecord ent[1];
    ent[0] = EntityRecord{};
    ent[0].childCount = 1 << 16;     // one child node
    ent[0].childNodes = kidNodes;
    ent[0].bodyNode = 0;
    nodes[0].child44 = 1;            // body has child -> no body reinit
    static i32 childIdx[1] = {0};
    DecompRecord rec;
    rec.flag100 = 1; rec.flag102 = 1; rec.blob105 = 1; rec.childCount = 1;
    rec.childIndex = childIdx;
    SceneFrameState stt;
    return SceneUpdatePass(rec, ent, 1, nodes, 1, stt, h);
}

TEST(EntityFrameUpdate, DispatchType0x41) {
    RecScene h; RunTypeDispatch(0x41, h);
    CHECK(h.has("objectUpdate"));
}
TEST(EntityFrameUpdate, DispatchType0x42) {
    RecScene h; RunTypeDispatch(0x42, h);
    CHECK(h.has("building"));
}
TEST(EntityFrameUpdate, DispatchType0x43) {
    RecScene h; RunTypeDispatch(0x43, h);
    CHECK(h.has("animApply"));       // door/label anim
    CHECK(h.has("finalize"));
}
TEST(EntityFrameUpdate, DispatchType0x45) {
    RecScene h; RunTypeDispatch(0x45, h);
    CHECK(h.has("interaction"));     // Entity_InteractionLogic
    CHECK(h.has("entityAnim"));
}
TEST(EntityFrameUpdate, DispatchType1) {
    RecScene h; RunTypeDispatch(1, h);
    CHECK(h.has("basic"));           // Animation_Basic
}

// --- 0x41ceb4: type 17 countdown decrements life ---------------------------
TEST(EntityFrameUpdate, Type17Countdown) {
    RenderNode nodes[1];
    nodes[0].meshHandle = 1; nodes[0].typeByte = 0x11; nodes[0].life = 3;
    nodes[0].child44 = 1;
    i32 kidNodes[1] = {0};
    EntityRecord ent[1];
    ent[0].childCount = 1 << 16; ent[0].childNodes = kidNodes; ent[0].bodyNode = -1;
    i32 childIdx[1] = {0};
    DecompRecord rec;
    rec.flag100 = 1; rec.flag102 = 1; rec.childCount = 1; rec.childIndex = childIdx;
    SceneFrameState st; st.freezeCountdowns = 0;
    RecScene h;
    SceneUpdatePass(rec, ent, 1, nodes, 1, st, h);
    CHECK_EQ((int)nodes[0].life, 2);  // decremented

    // frozen -> no decrement.
    nodes[0].life = 3; st.freezeCountdowns = 1;
    RecScene h2;
    SceneUpdatePass(rec, ent, 1, nodes, 1, st, h2);
    CHECK_EQ((int)nodes[0].life, 3);
}

// --- 0x41ceb4: empty mesh node is skipped (no dispatch, just clip reset) ----
TEST(EntityFrameUpdate, EmptyMeshSkipped) {
    RenderNode nodes[1];
    nodes[0].meshHandle = 0;          // empty -> skip
    nodes[0].typeByte = 0x41;
    i32 kidNodes[1] = {0};
    EntityRecord ent[1];
    ent[0].childCount = 1 << 16; ent[0].childNodes = kidNodes; ent[0].bodyNode = -1;
    i32 childIdx[1] = {0};
    DecompRecord rec;
    rec.flag100 = 1; rec.flag102 = 1; rec.childCount = 1; rec.childIndex = childIdx;
    SceneFrameState st; RecScene h;
    SceneUpdatePass(rec, ent, 1, nodes, 1, st, h);
    CHECK(!h.has("objectUpdate"));    // never dispatched
}

// --- 0x41ceb4: divider broadcasts at the end -------------------------------
TEST(EntityFrameUpdate, DividerBroadcasts) {
    DecompRecord rec;
    rec.flag100 = 1; rec.flag102 = 1; rec.childCount = 0;
    rec.broadcast428 = 1; rec.broadcast492 = 1;
    SceneFrameState st; RecScene h;
    SceneUpdatePass(rec, nullptr, 0, nullptr, 0, st, h);
    CHECK_EQ(h.count("broadcast"), 2); // right + bottom dividers
}

// --- 0x40e50c: result-handler table dispatch -------------------------------
TEST(DecompressorInit, BoundEntriesDispatch) {
    DecompRecord rec;
    rec.flag100 = 1; rec.flag102 = 1;
    DecompRecord other;

    DecompHandlerEntry tbl[3] = {};
    tbl[0].owner = &rec;     // bound -> dispatch
    tbl[1].owner = &other;   // not bound -> skip
    tbl[2].owner = &rec;     // bound -> dispatch

    DecHooks h;
    int n = DecompressorInit(rec, tbl, 3, h);
    CHECK_EQ(n, 2);
    CHECK_EQ(h.handlers, 2);
    CHECK_EQ(h.gray, 2);
}

TEST(DecompressorInit, GateClearNoDispatch) {
    DecompRecord rec; rec.flag100 = 0; rec.flag102 = 1;
    DecompHandlerEntry tbl[1] = {}; tbl[0].owner = &rec;
    DecHooks h;
    int n = DecompressorInit(rec, tbl, 1, h);
    CHECK_EQ(n, 0);
    CHECK_EQ(h.handlers, 0);
}

TEST(DecompressorInit, MaxSlotsCap) {
    // The original iterates i != 10240 step 20 => 512 max slots. A larger table
    // is capped at 512.
    DecompRecord rec; rec.flag100 = 1; rec.flag102 = 1;
    std::vector<DecompHandlerEntry> tbl(600);
    for (auto& e : tbl) e.owner = &rec;
    DecHooks h;
    int n = DecompressorInit(rec, tbl.data(), (int)tbl.size(), h);
    CHECK_EQ(n, 512);
}
