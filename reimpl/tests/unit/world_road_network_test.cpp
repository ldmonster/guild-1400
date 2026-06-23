// Golden-vector unit tests for the faithful road-network / building-upgrade-tree
// layout solver (gilde.exe 0x592d98 / 0x592c7c).
//
// The inputs mirror the binary's in-memory format exactly:
//   * type-record table  (dword_13CE294): stride 589; record[+34]=entry count,
//     record[+35] = u16[] entry ids (bit15 a flag, masked off), record[+163] =
//     parallel dword[] whose lo16 = parentFrom id, hi16 = childType id.
//   * type-flag table     (dword_13CE27C): stride 65; [0] = category byte; the
//     scan stops on category 2 or 6.
// The expected node coordinates/depths are pinned to the 1:1 translation; each is
// also cross-checked against the structural invariants the algorithm guarantees.
#include "test.h"
#include "world/road_network.h"

#include <cstring>

using namespace guild;
using namespace guild::world;

namespace {

// A reusable synthetic type table. type 0 holds `count` entries; entry 0 is the
// search target, entries 1.. become nodes (until a stop category). Helpers write
// the binary-faithful record fields.
struct SyntheticCity {
    u8 flag[65 * 256];     // typeFlagBase
    u8 table[589 * 4];     // typeTableBase (4 type slots)

    SyntheticCity() {
        std::memset(flag, 0, sizeof(flag));
        std::memset(table, 0, sizeof(table));
    }
    u8* rec(int type) { return table + 589 * type; }
    void setCount(int type, int n) { rec(type)[34] = static_cast<u8>(n); }
    void setEntry(int type, int idx, u16 id) {
        *reinterpret_cast<u16*>(rec(type) + 35 + 2 * idx) = id;
    }
    void setLink(int type, int idx, u16 parentFrom, u16 childType) {
        *reinterpret_cast<u16*>(rec(type) + 163 + 4 * idx) = parentFrom;
        *reinterpret_cast<u16*>(rec(type) + 163 + 4 * idx + 2) = childType;
    }
    void setCategory(u16 id, u8 cat) { flag[65 * id] = cat; }
};

} // namespace

// --- chain depth (0x592c7c) -------------------------------------------------
TEST(RoadNetwork, ChainDepthRootAndCache) {
    RoadLayoutState st{};
    st.nodeCount = 3;
    // node0 root (parentFrom 0); node1 child of node0; node2 child of node1.
    st.nodes[0] = {}; st.nodes[0].nodeId = 11; st.nodes[0].parentFromId = 0;
    st.nodes[0].depth = 0xFFFF;
    st.nodes[1] = {}; st.nodes[1].nodeId = 12; st.nodes[1].parentFromId = 11;
    st.nodes[1].depth = 0xFFFF;
    st.nodes[2] = {}; st.nodes[2].nodeId = 13; st.nodes[2].parentFromId = 12;
    st.nodes[2].depth = 0xFFFF;
    CHECK_EQ(RoadComputeChainDepth(st, 0), 0);   // root
    CHECK_EQ(RoadComputeChainDepth(st, 1), 1);   // one hop
    CHECK_EQ(RoadComputeChainDepth(st, 2), 2);   // two hops
}

TEST(RoadNetwork, ChainDepthCachedShortCircuit) {
    RoadLayoutState st{};
    st.nodeCount = 1;
    st.nodes[0] = {}; st.nodes[0].nodeId = 5; st.nodes[0].parentFromId = 99;
    st.nodes[0].depth = 7;   // pre-cached, != 0xFFFF
    CHECK_EQ(RoadComputeChainDepth(st, 0), 7);
}

// --- full layout (0x592d98) -------------------------------------------------
TEST(RoadNetwork, LayoutTargetNotPresentReturnsOne) {
    SyntheticCity c;
    c.setCount(0, 3);
    c.setEntry(0, 0, 10);
    c.setEntry(0, 1, 11);
    c.setEntry(0, 2, 12);
    i8 typeRec[1] = {0};
    i16 target = 999;   // not among the entries
    RoadLayoutState st{};
    CHECK_EQ(RoadComputeNetworkLayout(st, c.table, c.flag, typeRec, &target, 800, 600), 1);
}

TEST(RoadNetwork, LayoutEmptyAfterTargetReturnsOne) {
    SyntheticCity c;
    c.setCount(0, 1);
    c.setEntry(0, 0, 10);   // target is the only entry -> no nodes after it
    i8 typeRec[1] = {0};
    i16 target = 10;
    RoadLayoutState st{};
    CHECK_EQ(RoadComputeNetworkLayout(st, c.table, c.flag, typeRec, &target, 800, 600), 1);
}

TEST(RoadNetwork, LayoutStopsAtStopCategory) {
    SyntheticCity c;
    c.setCount(0, 4);
    c.setEntry(0, 0, 10);   // target
    c.setEntry(0, 1, 11);   // node
    c.setEntry(0, 2, 12);   // stop category -> populate halts BEFORE this
    c.setEntry(0, 3, 13);
    c.setLink(0, 1, 0, 0);
    c.setCategory(12, kRoadStopCategoryA);   // category 2
    i8 typeRec[1] = {0};
    i16 target = 10;
    RoadLayoutState st{};
    int rc = RoadComputeNetworkLayout(st, c.table, c.flag, typeRec, &target, 800, 600);
    CHECK_EQ(rc, 0);
    CHECK_EQ(st.nodeCount, 1);          // only id 11 was added
    CHECK_EQ(st.nodes[0].nodeId, 11);
}

// The headline golden vector: a 4-node, 3-level upgrade graph. Pins the recovered
// depths, level boundaries, and final X/Y placements byte-for-byte against the
// 1:1 translation of VIBE_Map_ComputeRoadNetworkLayout.
TEST(RoadNetwork, LayoutGoldenFourNodeTree) {
    SyntheticCity c;
    c.setCount(0, 5);
    c.setEntry(0, 0, 10);   // target (entry 0)
    c.setEntry(0, 1, 11);   // node0 id 11
    c.setEntry(0, 2, 12);   // node1 id 12
    c.setEntry(0, 3, 13);   // node2 id 13
    c.setEntry(0, 4, 14);   // node3 id 14
    // links: lo16 = parentFrom, hi16 = childType (childType aliases the next
    // node's parentTo in the binary; pinned values reflect that quirk).
    c.setLink(0, 1, 0,  100);   // id11 root
    c.setLink(0, 2, 11, 101);   // id12 child of 11
    c.setLink(0, 3, 11, 102);   // id13 child of 11
    c.setLink(0, 4, 12, 103);   // id14 child of 12
    i8 typeRec[1] = {0};
    i16 target = 10;
    RoadLayoutState st{};

    int rc = RoadComputeNetworkLayout(st, c.table, c.flag, typeRec, &target, 800, 600);
    CHECK_EQ(rc, 0);
    CHECK_EQ(st.nodeCount, 4);
    CHECK_EQ(st.levelCount, 3);

    // level boundaries
    CHECK_EQ((int)st.levelStart[0], 0);
    CHECK_EQ((int)st.levelStart[1], 1);
    CHECK_EQ((int)st.levelStart[2], 3);
    CHECK_EQ((int)st.levelStart[3], 4);

    // depths (sorted ascending by the bubble pass)
    CHECK_EQ((int)st.nodes[0].depth, 0);
    CHECK_EQ((int)st.nodes[1].depth, 1);
    CHECK_EQ((int)st.nodes[2].depth, 1);
    CHECK_EQ((int)st.nodes[3].depth, 2);

    // node ids preserved through the sort
    CHECK_EQ(st.nodes[0].nodeId, 11);
    CHECK_EQ(st.nodes[1].nodeId, 12);
    CHECK_EQ(st.nodes[2].nodeId, 13);
    CHECK_EQ(st.nodes[3].nodeId, 14);

    // golden Y (depth * 96 spread, clamped to width-16, +16 top margin)
    CHECK_EQ(st.nodes[0].coordY, 16);
    CHECK_EQ(st.nodes[1].coordY, 112);
    CHECK_EQ(st.nodes[2].coordY, 112);
    CHECK_EQ(st.nodes[3].coordY, 208);

    // golden X (level-0 spread + per-level relaxation + float rescale)
    CHECK_EQ(st.nodes[0].coordX, 266);
    CHECK_EQ(st.nodes[1].coordX, 22);
    CHECK_EQ(st.nodes[2].coordX, 510);
    CHECK_EQ(st.nodes[3].coordX, 22);
}

// The *a2 == 253 special target path: the search still runs and (because the
// target is not a real entry) the drain loop leaves no nodes -> returns 1.
TEST(RoadNetwork, LayoutSpecialTarget253) {
    SyntheticCity c;
    c.setCount(0, 3);
    c.setEntry(0, 0, 11);
    c.setEntry(0, 1, 12);
    c.setEntry(0, 2, 13);
    i8 typeRec[1] = {0};
    i16 target = 253;
    RoadLayoutState st{};
    // 253 is not present as an entry -> the initial search misses -> returns 1.
    CHECK_EQ(RoadComputeNetworkLayout(st, c.table, c.flag, typeRec, &target, 800, 600), 1);
}
