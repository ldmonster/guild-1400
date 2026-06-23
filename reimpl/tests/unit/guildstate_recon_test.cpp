// Golden-vector unit tests for src/world/guildstate_recon.{h,cpp}:
//   * VIBE_GameLogic_InitGuardState (gilde.exe 0x4520d0)
//   * VIBE_GameTime_Set            (gilde.exe 0x5831f0, inlined)
//   * VIBE_GameObject_IterNextTree (gilde.exe 0x585488)
//
// Self-contained: a tiny in-test node array stands in for the coupled-leaf
// scene-object array, wired through IterTreeRecordAccess. Vectors are derived
// directly from the decompiled control flow.
#include "tests/framework/test.h"
#include "world/guildstate_recon.h"

#include <vector>

using namespace guild;
using namespace guild::world;

// --------------------------------------------------------------------------
// InitGuardState / GameTimeSet
// --------------------------------------------------------------------------

TEST(GuildStateReconInitGuard, SuccessPathWritesExactTable) {
    GuardState g;
    int r = GameLogicInitGuardState(g, /*aiDataFileLoaded=*/true);
    CHECK_EQ(r, 1);
    CHECK_EQ((int)g.counterA, 0);
    CHECK_EQ((int)g.counterB, 0);
    CHECK(g.tableValid);
    // Game-time stamp seeded to 06:00:00 (hour=6, min=0, sec=0).
    CHECK_EQ((int)g.stamp.hour, 6);
    CHECK_EQ((int)g.stamp.sec, 0);
    CHECK_EQ((int)g.stamp.min, 0);
    CHECK_EQ((int)g.stamp.secDword, 0);
    // Exact sprite/action id constants.
    CHECK_EQ((int)g.wFAE, 340);
    CHECK_EQ((int)g.wFAC, 342);
    CHECK_EQ((int)g.wFB0, 344);
    CHECK_EQ((int)g.wFB2, 370);
    CHECK_EQ((int)g.wFB4, 366);
    CHECK_EQ((int)g.wFB6, 0);
    CHECK_EQ((int)g.wFC8, 372);
    CHECK_EQ((int)g.wFCA, 374);
    CHECK_EQ((int)g.wFCC, 350);
    CHECK_EQ((int)g.wFCE, 352);
    CHECK_EQ((int)g.wFD0, 0);
}

TEST(GuildStateReconInitGuard, FailPathLeavesTableUntouchedReturnsZero) {
    GuardState g;
    // Pre-poison the table to prove the fail path does not write it.
    g.wFAE = 999;
    int r = GameLogicInitGuardState(g, /*aiDataFileLoaded=*/false);
    CHECK_EQ(r, 0);
    CHECK(!g.tableValid);
    CHECK_EQ((int)g.wFAE, 999);  // untouched
    // The counters + time stamp ARE still cleared/seeded before the load check.
    CHECK_EQ((int)g.counterA, 0);
    CHECK_EQ((int)g.counterB, 0);
    CHECK_EQ((int)g.stamp.hour, 6);
}

TEST(GuildStateReconGameTime, SetWritesHourMinSecLikeOriginal) {
    GameTimeStamp s;
    GameTimeSet(s, 6, 0, 0);
    CHECK_EQ((int)s.hour, 6);
    CHECK_EQ((int)s.sec, 0);
    CHECK_EQ((int)s.min, 0);
    // Distinct args to confirm the field routing (hour->+4, sec->+5/+10, min->+6).
    GameTimeStamp t;
    GameTimeSet(t, 13, 45, 7);  // a2=hour=13, a3=sec=45, a4=min=7
    CHECK_EQ((int)t.hour, 13);
    CHECK_EQ((int)t.sec, 45);
    CHECK_EQ((int)t.secDword, 45);
    CHECK_EQ((int)t.min, 7);
}

// --------------------------------------------------------------------------
// IterNextTree — synthetic node array + access hooks
// --------------------------------------------------------------------------
//
// A node is an index >= 1 into g_nodes (0 == kNull). Each node carries its
// prototype word, location field, childHead and sibling links.
namespace {
struct Node {
    u16 proto;
    i32 loc;
    i32 childHead;  // kNull (0) == none
    i32 sibling;    // kNull (0) == none
    u16 kindProto;  // proto used to index the kind table (== proto here)
    i16 typeField;  // ResolveTypeFieldB result
    u8  kind;       // kindByte for this proto
};
std::vector<Node> g_nodes;

Node& N(i32 id) { return g_nodes[id - 1]; }

IterTreeRecordAccess MakeAccess() {
    IterTreeRecordAccess a;
    a.protoWord = [](i32 n) -> u16 { return n ? N(n).proto : 0; };
    a.childHead = [](i32 n) -> i32 { return n ? N(n).childHead : 0; };
    a.sibling   = [](i32 n) -> i32 { return n ? N(n).sibling : 0; };
    a.locField  = [](i32 n) -> i32 { return n ? N(n).loc : -1; };
    a.kindByte  = [](u16 p) -> u8 {
        for (auto& nd : g_nodes) if (nd.kindProto == p) return nd.kind;
        return 0;
    };
    a.resolveTypeField = [](i32 n) -> i16 { return n ? N(n).typeField : (i16)-1; };
    a.inRange = [](i32 n) -> bool { return n != 0; };
    a.nextLinear = [](i32 n) -> i32 {
        // linear walk over the array by index; kNull past the end.
        i32 next = n + 1;
        return (next <= (i32)g_nodes.size()) ? next : 0;
    };
    return a;
}

// Collect every node id returned by repeated IterNextTree calls until exhausted.
std::vector<i32> DrainQuery(IterTreeCursor& c, const IterTreeRecordAccess& a) {
    std::vector<i32> out;
    for (;;) {
        i32 id = GameObjectIterNextTree(c, a);
        if (id == IterTreeCursor::kNull) break;
        out.push_back(id);
        // Guard against runaway loops in case of a regression.
        if (out.size() > 64) break;
    }
    return out;
}
}  // namespace

TEST(GuildStateReconIterTree, SiblingWalkMatchAllVisitsWholeList) {
    // Three siblings: 1 -> 2 -> 3 (no children). matchAll, no filters.
    g_nodes = {
        {100, 7, 0, 2, 100, -1, 0},
        {101, 7, 0, 3, 101, -1, 0},
        {102, 7, 0, 0, 102, -1, 0},
    };
    auto acc = MakeAccess();
    IterTreeCursor c;
    c.cur = 1;            // QueryBegin parked the cursor at the first node
    c.firstStep = 1;
    c.linearMode = 0;     // tree (sibling) mode
    c.recurse = 0;
    c.matchAll = 1;
    c.filterProto = 0x7FFF;
    c.filterLoc = -1;
    c.filterType = -1;
    c.filterKind = -1;
    c.count = 1000;       // generous bound

    auto got = DrainQuery(c, acc);
    CHECK_EQ((int)got.size(), 3);
    CHECK_EQ((int)got[0], 1);
    CHECK_EQ((int)got[1], 2);
    CHECK_EQ((int)got[2], 3);
}

TEST(GuildStateReconIterTree, RecurseDescendsChildrenDepthFirst) {
    // 1 -> sibling 4. node1 has child 2 -> sibling 3. recurse on.
    //   visit order (DFS): 1, then push child 2; sibling 4; pop 2 -> 2, sibling 3.
    g_nodes = {
        {10, 1, 2, 4, 10, -1, 0},   // 1: child=2, sibling=4
        {11, 1, 0, 3, 11, -1, 0},   // 2: sibling=3
        {12, 1, 0, 0, 12, -1, 0},   // 3
        {13, 1, 0, 0, 13, -1, 0},   // 4
    };
    auto acc = MakeAccess();
    IterTreeCursor c;
    c.cur = 1;
    c.firstStep = 1;
    c.linearMode = 0;
    c.recurse = 1;
    c.matchAll = 1;
    c.filterProto = 0x7FFF;
    c.filterLoc = -1;
    c.filterType = -1;
    c.filterKind = -1;
    c.count = 1000;

    auto got = DrainQuery(c, acc);
    // Every node reachable should be visited exactly once.
    CHECK_EQ((int)got.size(), 4);
    // First node is the cursor seed.
    CHECK_EQ((int)got[0], 1);
    // 4 (sibling of 1) precedes the popped child subtree (stack is LIFO; child 2
    // was pushed at the first step, then sibling 4 is walked before popping).
    bool saw1 = false, saw2 = false, saw3 = false, saw4 = false;
    for (i32 id : got) {
        saw1 |= (id == 1); saw2 |= (id == 2); saw3 |= (id == 3); saw4 |= (id == 4);
    }
    CHECK(saw1 && saw2 && saw3 && saw4);
}

TEST(GuildStateReconIterTree, ProtoFilterRejectsNonMatching) {
    // Siblings with mixed proto words; filter proto == 200.
    g_nodes = {
        {200, 5, 0, 2, 200, -1, 0},
        {201, 5, 0, 3, 201, -1, 0},
        {200, 5, 0, 0, 200, -1, 0},
    };
    auto acc = MakeAccess();
    IterTreeCursor c;
    c.cur = 1;
    c.firstStep = 1;
    c.linearMode = 0;
    c.recurse = 0;
    c.matchAll = 0;
    c.filterProto = 200;   // only proto==200 accepted
    c.filterLoc = -1;
    c.filterType = -1;
    c.filterKind = -1;
    c.count = 1000;

    auto got = DrainQuery(c, acc);
    CHECK_EQ((int)got.size(), 2);
    CHECK_EQ((int)got[0], 1);
    CHECK_EQ((int)got[1], 3);
}

TEST(GuildStateReconIterTree, LocationFilterRejectsNonMatching) {
    g_nodes = {
        {1, 42, 0, 2, 1, -1, 0},
        {1, 99, 0, 3, 1, -1, 0},
        {1, 42, 0, 0, 1, -1, 0},
    };
    auto acc = MakeAccess();
    IterTreeCursor c;
    c.cur = 1;
    c.firstStep = 1;
    c.linearMode = 0;
    c.recurse = 0;
    c.matchAll = 0;
    c.filterProto = 0x7FFF;
    c.filterLoc = 42;   // only loc==42 accepted
    c.filterType = -1;
    c.filterKind = -1;
    c.count = 1000;

    auto got = DrainQuery(c, acc);
    CHECK_EQ((int)got.size(), 2);
    CHECK_EQ((int)got[0], 1);
    CHECK_EQ((int)got[1], 3);
}

TEST(GuildStateReconIterTree, NotReadyOrNullCursorExhaustsImmediately) {
    g_nodes = { {1, 0, 0, 0, 1, -1, 0} };
    auto acc = MakeAccess();

    IterTreeCursor c1;
    c1.cur = 1; c1.firstStep = 1; c1.queryReady = false; c1.matchAll = 1;
    c1.count = 10; c1.filterProto = 0x7FFF;
    CHECK_EQ((int)GameObjectIterNextTree(c1, acc), 0);

    IterTreeCursor c2;
    c2.cur = IterTreeCursor::kNull; c2.firstStep = 1; c2.matchAll = 1;
    c2.count = 10; c2.filterProto = 0x7FFF;
    CHECK_EQ((int)GameObjectIterNextTree(c2, acc), 0);
}

TEST(GuildStateReconIterTree, TypeFieldFilterMatches) {
    // Two siblings; only the one whose ResolveTypeFieldB == 55 is accepted.
    g_nodes = {
        {1, 0, 0, 2, 1, 11, 0},
        {1, 0, 0, 0, 1, 55, 0},
    };
    auto acc = MakeAccess();
    IterTreeCursor c;
    c.cur = 1;
    c.firstStep = 1;
    c.linearMode = 0;
    c.recurse = 0;
    c.matchAll = 0;
    c.filterProto = 0x7FFF;
    c.filterLoc = -1;
    c.filterType = 55;   // only typeField==55
    c.filterKind = -1;
    c.count = 1000;

    auto got = DrainQuery(c, acc);
    CHECK_EQ((int)got.size(), 1);
    CHECK_EQ((int)got[0], 2);
}
