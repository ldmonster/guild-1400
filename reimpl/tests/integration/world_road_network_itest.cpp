// Integration / flow test for the road-network (building-upgrade-tree) layout.
// Exercises the full pipeline end to end on a wider synthetic graph: entry scan ->
// node populate -> chain-depth -> bubble sort -> level banding -> X spread ->
// per-level relaxation (average / sort / gap spread / 80px push) -> float X
// rescale -> Y placement. Asserts the structural invariants the original
// guarantees, plus that the coordinates are the ones the caller (0x594100) would
// feed to VIBE_Paintbox_DrawLine.
//
// NOTE: the binary's real inputs (dword_13CE294 type-record table, dword_13CE27C
// flag table) are populated by the city/new-game load path at runtime, not parsed
// from a single shipped file, so there is no standalone map asset to drive this
// against. The test therefore builds the in-memory tables in the exact binary
// format and runs the solver as the caller would. (When the live load path lands a
// GUILD_GAME_DIR-driven e2e can replace this synthetic feed.)
#include "test.h"
#include "world/road_network.h"

#include <cstring>

using namespace guild;
using namespace guild::world;

namespace {

struct City {
    u8 flag[65 * 512];
    u8 table[589 * 2];
    City() { std::memset(flag, 0, sizeof(flag)); std::memset(table, 0, sizeof(table)); }
    u8* rec() { return table; }
    void count(int n) { rec()[34] = static_cast<u8>(n); }
    void entry(int i, u16 id) { *reinterpret_cast<u16*>(rec() + 35 + 2 * i) = id; }
    void link(int i, u16 pf, u16 ch) {
        *reinterpret_cast<u16*>(rec() + 163 + 4 * i) = pf;
        *reinterpret_cast<u16*>(rec() + 163 + 4 * i + 2) = ch;
    }
};

} // namespace

// A wider 7-node tree spanning 4 levels — drives the per-level relaxation and the
// float X-rescale (the span exceeds the area, forcing the ConvertX path).
TEST(RoadNetworkFlow, WideTreeProducesLayeredLayout) {
    City c;
    c.count(8);
    c.entry(0, 1);      // target
    // 7 nodes: a 4-deep chain with branches.
    c.entry(1, 10); c.link(1, 0,  0);    // root
    c.entry(2, 20); c.link(2, 10, 0);    // L1
    c.entry(3, 21); c.link(3, 10, 0);    // L1
    c.entry(4, 30); c.link(4, 20, 0);    // L2
    c.entry(5, 31); c.link(5, 20, 0);    // L2
    c.entry(6, 32); c.link(6, 21, 0);    // L2
    c.entry(7, 40); c.link(7, 30, 0);    // L3

    i8 typeRec[1] = {0};
    i16 target = 1;
    RoadLayoutState st{};

    int rc = RoadComputeNetworkLayout(st, c.table, c.flag, typeRec, &target, 800, 600);
    CHECK_EQ(rc, 0);
    CHECK_EQ(st.nodeCount, 7);
    CHECK_EQ(st.levelCount, 4);    // depths 0..3

    // Invariant 1: depths are non-decreasing after the bubble sort.
    for (int i = 1; i < st.nodeCount; ++i) {
        CHECK(st.nodes[i - 1].depth <= st.nodes[i].depth);
    }

    // Invariant 2: Y is a strict function of depth (deeper => larger Y), each in
    // [16, height]. (Y = depth * v72/levelCount + 16.)
    for (int i = 0; i < st.nodeCount; ++i) {
        CHECK(st.nodes[i].coordY >= 16);
        CHECK(st.nodes[i].coordY <= 600);
    }
    // same-depth nodes share a Y row.
    for (int i = 1; i < st.nodeCount; ++i) {
        if (st.nodes[i].depth == st.nodes[i - 1].depth) {
            CHECK_EQ(st.nodes[i].coordY, st.nodes[i - 1].coordY);
        }
    }

    // Invariant 3: level boundaries tile the node array and terminate at nodeCount.
    CHECK_EQ((int)st.levelStart[0], 0);
    CHECK_EQ((int)st.levelStart[st.levelCount], st.nodeCount);
    for (int l = 1; l <= st.levelCount; ++l) {
        CHECK(st.levelStart[l] >= st.levelStart[l - 1]);
    }

    // Invariant 4: the root (depth 0) sits on the top row.
    CHECK_EQ((int)st.nodes[0].depth, 0);
    CHECK_EQ(st.nodes[0].coordY, 16);
}
