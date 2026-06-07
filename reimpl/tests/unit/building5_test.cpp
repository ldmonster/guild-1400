#include "test.h"

// Unit tests for building5: the Bauplatz (build-plot) finder family and the gate
// handlers. The plot finders call the REAL util siblings (StrCmpNoCaseN /
// VectorWithinTolerance / PointThroughBoneChain); only the scene-walk / person
// iterator / render / time leaves are hooked.

#include "sim/building5.h"

#include <cstdint>
#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {

// A scene-graph node large enough for the fields the finders read:
//   name string at +0, link +97, next +496, frame ptr +504, child +508.
// We allocate 600 bytes so every read is in-bounds.
struct Node {
    std::uint8_t bytes[600];
    Node() { std::memset(bytes, 0, sizeof bytes); }
    void setName(const char* s) { std::strncpy(reinterpret_cast<char*>(bytes), s, 32); }
};

// A 128-float frame used by FindNearestPlotByDistance. Byte 504 (float 126) is the
// parent link -> keep it 0 so PointThroughBoneChain just adds frame[19..21] +
// frame[30..32] to the point (which is frame+19).
struct Frame {
    float f[128];
    Frame() { std::memset(f, 0, sizeof f); }
};

// Scene source: hands out a fixed set of nodes to the default walk.
struct SceneHooks : Building5Hooks {
    const std::uint8_t* nodes[8] = {};
    int n = 0;
    const std::uint8_t* SceneNode(int index) override {
        return index < n ? nodes[index] : nullptr;
    }
};

// Person iterator that yields a single person sitting near a plot.
struct OccupiedHooks : SceneHooks {
    const std::uint8_t* person = nullptr;
    bool handed = false;
    const std::uint8_t* PersonQueryBegin(const std::uint8_t*, int, int) override {
        handed = false;
        return nullptr;   // tests override per case via `person` below
    }
};

}  // namespace

// ---------------------------------------------------------------------------
TEST(Building5, CollectCandidate_AcceptsBkPlot) {
    SceneHooks h;
    SetBuilding5Hooks(&h);   // no persons -> never occupied

    Node plot;
    plot.setName("bk_001");
    // Drive the collector directly with the result frame set up by Filter.
    const std::uint8_t* frame[4] = {};
    // Simulate FilterBlockedBauplatze setting up the result frame: use Filter.
    h.nodes[0] = plot.bytes;
    h.n = 1;
    std::int32_t count = Building_FilterBlockedBauplatze(0, frame, 4);
    CHECK_EQ(count, 1);
    CHECK(frame[0] == plot.bytes);
    SetBuilding5Hooks(nullptr);
}

TEST(Building5, CollectCandidate_RejectsNonBkPlot) {
    SceneHooks h;
    SetBuilding5Hooks(&h);
    Node plot;
    plot.setName("house");     // not "bk_" -> skipped
    h.nodes[0] = plot.bytes;
    h.n = 1;
    const std::uint8_t* frame[4] = {};
    CHECK_EQ(Building_FilterBlockedBauplatze(0, frame, 4), 0);
    SetBuilding5Hooks(nullptr);
}

TEST(Building5, Filter_MultipleFreePlots) {
    SceneHooks h;
    SetBuilding5Hooks(&h);
    Node a, b, c;
    a.setName("bk_a");
    b.setName("bk_b");
    c.setName("vg_c");          // "vg_" is not "bk_" -> rejected by the bk filter
    h.nodes[0] = a.bytes; h.nodes[1] = b.bytes; h.nodes[2] = c.bytes;
    h.n = 3;
    const std::uint8_t* frame[8] = {};
    CHECK_EQ(Building_FilterBlockedBauplatze(0, frame, 8), 2);
    CHECK(frame[0] == a.bytes);
    CHECK(frame[1] == b.bytes);
    SetBuilding5Hooks(nullptr);
}

// A person sitting exactly on the plot makes Filter drop it in the second pass.
namespace {
struct BlockedHooks : SceneHooks {
    std::uint8_t personRec[600];
    std::uint8_t personFrame[600];
    std::uint8_t plotFrame[600];
    BlockedHooks() {
        std::memset(personRec, 0, sizeof personRec);
        std::memset(personFrame, 0, sizeof personFrame);
        std::memset(plotFrame, 0, sizeof plotFrame);
        // person link at +97 -> personFrame.
        const std::uint8_t* pf = personFrame;
        std::memcpy(personRec + 97, &pf, sizeof pf);
    }
    bool yielded = false;
    const std::uint8_t* PersonQueryBegin(const std::uint8_t*, int, int) override {
        yielded = false;
        return personRec;
    }
    const std::uint8_t* PersonIterNext() override {
        if (yielded) return nullptr;
        yielded = true;
        return nullptr;
    }
};
}  // namespace

TEST(Building5, Filter_DropsPlotBlockedByPerson) {
    BlockedHooks h;
    Node plot;
    plot.setName("bk_x");
    // plot frame pointer at +504 -> plotFrame (so the 2nd pass transforms it).
    const std::uint8_t* pf = h.plotFrame;
    std::memcpy(plot.bytes + 504, &pf, sizeof pf);
    h.nodes[0] = plot.bytes;
    h.n = 1;
    SetBuilding5Hooks(&h);

    // Both the plot (as node) and the person frame transform to the origin (all
    // zero), so the candidate collector itself sees the plot as occupied and never
    // adds it -> the surviving count is 0.
    const std::uint8_t* frame[4] = {};
    std::int32_t count = Building_FilterBlockedBauplatze(0, frame, 4);
    CHECK_EQ(count, 0);
    SetBuilding5Hooks(nullptr);
}

// ---------------------------------------------------------------------------
TEST(Building5, FindNearestPlotByDistance_PicksClosest) {
    SetBuilding5Hooks(nullptr);   // PointThroughBoneChain is the real sibling.

    Frame plotA, plotB;
    // PointThroughBoneChain(frame, frame+19, out): with null parent, world pos =
    // frame[19..21] + frame[30..32].
    plotA.f[30] = 10.0f;          // pos (10,0,0)
    plotB.f[30] = 50.0f;          // pos (50,0,0)

    const std::uint8_t* plots[256] = {};
    plots[0] = reinterpret_cast<const std::uint8_t*>(plotA.f);
    plots[1] = reinterpret_cast<const std::uint8_t*>(plotB.f);

    float p[3] = {12.0f, 0.0f, 0.0f};
    const std::uint8_t* best = Building_FindNearestPlotByDistance(p, plots);
    CHECK(best == reinterpret_cast<const std::uint8_t*>(plotA.f));   // dist 2 < 38

    // Move the query closer to B.
    p[0] = 48.0f;
    best = Building_FindNearestPlotByDistance(p, plots);
    CHECK(best == reinterpret_cast<const std::uint8_t*>(plotB.f));
}

TEST(Building5, FindNearestPlot_OutsideCutoffReturnsNull) {
    SetBuilding5Hooks(nullptr);
    Frame plot;
    plot.f[30] = 1000.0f;         // pos (1000,0,0) -> 1000 units from origin
    const std::uint8_t* plots[256] = {};
    plots[0] = reinterpret_cast<const std::uint8_t*>(plot.f);
    float p[3] = {0.0f, 0.0f, 0.0f};
    CHECK(Building_FindNearestPlotByDistance(p, plots) == nullptr);  // > 100 cutoff
}

// ---------------------------------------------------------------------------
// Gate handlers.
namespace {
struct GateHooks : Building5Hooks {
    int advanceCalls = 0;
    int lastDays = 0, lastMin = 0, lastSec = 0;
    std::int32_t GameTimeAdvance(std::uint8_t*, int d, int s, int m) override {
        ++advanceCalls; lastDays = d; lastSec = s; lastMin = m;
        return 7;   // pretend hour-of-day 7
    }
    bool failRegister = false;
    std::int32_t HeRegisterHandlerByType(std::uint32_t, void*, void*) override {
        return failRegister ? 1 : 0;
    }
    std::int32_t restoreFlag = 0;
    std::int32_t UniverseRestoreObjectStates(const std::uint8_t*, std::uint8_t f) override {
        restoreFlag = f; return 0;
    }
};
}  // namespace

TEST(Building5, ResetGateState_CopiesAndSchedules) {
    GateHooks h;
    SetBuilding5Hooks(&h);
    std::uint8_t rec[256];
    std::memset(rec, 0, sizeof rec);
    // Seed the source time block at +68 (14 bytes): day=5, hour=9, ...
    std::int32_t day = 5; std::uint16_t hour = 9;
    std::memcpy(rec + 68, &day, 4);
    std::memcpy(rec + 72, &hour, 2);          // partial: just check the copy
    rec[112] = 0xAB; rec[172] = 0x00;

    std::int32_t r = Building_ResetGateState(rec);
    CHECK_EQ(r, 7);                            // forwarded GameTimeAdvance result
    // +82 block must equal +68 block (14 bytes).
    CHECK_EQ(std::memcmp(rec + 82, rec + 68, 14), 0);
    // +96 mirrors +82.
    CHECK_EQ(std::memcmp(rec + 96, rec + 82, 14), 0);
    // +112 cleared to 0.
    std::uint32_t v112; std::memcpy(&v112, rec + 112, 4);
    CHECK_EQ((int)v112, 0);
    // +172 set to -1.
    std::uint32_t v172; std::memcpy(&v172, rec + 172, 4);
    CHECK_EQ(v172, 0xFFFFFFFFu);
    // Scheduled +10 minutes.
    CHECK_EQ(h.lastMin, 10);
    CHECK_EQ(h.lastDays, 0);
    SetBuilding5Hooks(nullptr);
}

TEST(Building5, RegisterGateHandlers_AllSucceed) {
    GateHooks h;
    SetBuilding5Hooks(&h);
    CHECK_EQ(Building_RegisterGateHandlers(), 0);
    h.failRegister = true;
    CHECK_EQ(Building_RegisterGateHandlers(), 1);
    SetBuilding5Hooks(nullptr);
}

TEST(Building5, RequestGateFlagSync_NoMatchSchedules30) {
    GateHooks h;
    SetBuilding5Hooks(&h);
    std::uint8_t rec[256];
    std::memset(rec, 0, sizeof rec);
    Building_RequestGateFlagSync(rec);
    // Inert flag table -> no match (index 128) -> +30 min, single advance.
    CHECK_EQ(h.advanceCalls, 1);
    CHECK_EQ(h.lastMin, 30);
    SetBuilding5Hooks(nullptr);
}

TEST(Building5, GateStubs_AreNoOps) {
    SetBuilding5Hooks(nullptr);
    Building_GateCallbackStub();
    Building_EmptyCallbackStub();
    CHECK_EQ(Building_HandlerStub(), 0);
    Building_DeselectThunk();        // routes to the (inert) light thunk
    CHECK(true);
}

TEST(Building5, ForEachBauplatzReserve_RestoresFlag) {
    GateHooks h;
    // Reuse GateHooks but supply scene nodes via a small adapter.
    struct RH : GateHooks {
        const std::uint8_t* node = nullptr;
        const std::uint8_t* SceneNode(int i) override { return i == 0 ? node : nullptr; }
    } rh;
    std::uint8_t node[8] = {0};
    rh.node = node;
    SetBuilding5Hooks(&rh);
    // reserve != 0 -> flag 1.
    std::int32_t r = Building_ForEachBauplatzReserve(1, 0);
    CHECK_EQ(r, 0);
    CHECK_EQ((int)rh.restoreFlag, 1);
    // reserve == 0 -> flag 0.
    Building_ForEachBauplatzReserve(0, 0);
    CHECK_EQ((int)rh.restoreFlag, 0);
    SetBuilding5Hooks(nullptr);
}
