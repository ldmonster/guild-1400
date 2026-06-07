#include "test.h"

// End-to-end: a full build-plot placement flow — walk the scene to collect free
// "bk_" plots, drop the blocked ones, then pick the plot nearest a target point —
// followed by a gate reset/sync cycle, all through building5's public surface.

#include "sim/building5.h"

#include <cstdint>
#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {

struct Node {
    std::uint8_t bytes[600];
    Node() { std::memset(bytes, 0, sizeof bytes); }
    void setName(const char* s) { std::strncpy(reinterpret_cast<char*>(bytes), s, 32); }
};

struct FlowHooks : Building5Hooks {
    const std::uint8_t* nodes[8] = {};
    int n = 0;
    int advances = 0;
    const std::uint8_t* SceneNode(int index) override {
        return index < n ? nodes[index] : nullptr;
    }
    std::int32_t GameTimeAdvance(std::uint8_t*, int, int, int) override {
        ++advances; return 11;
    }
};

}  // namespace

TEST(Building5E2E, PlacementThenGateCycle) {
    FlowHooks h;
    SetBuilding5Hooks(&h);

    // 1) Three candidate plots; only the two "bk_" ones are free plots.
    Node p1, p2, p3;
    p1.setName("bk_north");
    p2.setName("bk_south");
    p3.setName("road");           // not a plot
    h.nodes[0] = p1.bytes; h.nodes[1] = p2.bytes; h.nodes[2] = p3.bytes;
    h.n = 3;

    const std::uint8_t* frame[8] = {};
    std::int32_t free = Building_FilterBlockedBauplatze(0, frame, 8);
    CHECK_EQ(free, 2);
    CHECK(frame[0] == p1.bytes);
    CHECK(frame[1] == p2.bytes);

    // 2) Build a plot pointer array (the two surviving plots, as frames) and pick
    //    the nearest one. Each plot's frame is the node base; with zero parent the
    //    world pos == frame[19..21] + frame[30..32] (all zero -> origin), so the
    //    first non-null plot within the cutoff wins.
    const std::uint8_t* plots[256] = {};
    plots[0] = frame[0];
    plots[1] = frame[1];
    float target[3] = {0.0f, 0.0f, 0.0f};
    const std::uint8_t* nearest = Building_FindNearestPlotByDistance(target, plots);
    CHECK(nearest != nullptr);
    CHECK(nearest == p1.bytes);   // both at origin -> first wins

    // 3) Gate cycle on a building record: reset then one flag-sync tick.
    std::uint8_t rec[256];
    std::memset(rec, 0, sizeof rec);
    std::int32_t hr = Building_ResetGateState(rec);
    CHECK_EQ(hr, 11);
    int before = h.advances;
    Building_RequestGateFlagSync(rec);
    CHECK_EQ(h.advances, before + 1);   // inert sync schedules exactly one tick

    SetBuilding5Hooks(nullptr);
}

TEST(Building5E2E, RegisterAndDeselect) {
    FlowHooks h;
    SetBuilding5Hooks(&h);
    // All gate handlers register successfully via the inert hook.
    CHECK_EQ(Building_RegisterGateHandlers(), 0);
    // Deselect + stubs are safe no-ops.
    Building_DeselectThunk();
    Building_GateCallbackStub();
    CHECK_EQ(Building_HandlerStub(), 0);
    SetBuilding5Hooks(nullptr);
}
