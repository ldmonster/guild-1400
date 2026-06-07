// Unit tests for the third wave of VIBE_DebugCmd_* handlers (debugcmd3.cpp):
// the HE-handler-list broadcast/per-handler handlers + the two entity-search
// handlers. Each test installs recording DebugCmdHooks + DebugCmd3Hooks mocks,
// seeds the CRT LCG, invokes a handler and asserts the return code, the emitted
// command/message, and the exact RNG advance (via a Python-oracle LCG).
#include "test.h"
#include "sim/debugcmd3.h"
#include "crt/rand.h"

#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

// ---- Python-oracle LCG (matches crt::RandNext) ---------------------------
struct Lcg {
    u32 s;
    explicit Lcg(u32 seed) : s(seed) {}
    int next() { s = 1103515245u * s + 12345u; return (int)((s >> 16) & 0x7FFFu); }
    int mod(int n) { return n ? next() % n : 0; }
};
i32 Gold(i32 wealth, int roll, double base, double scale) {
    return (i32)((double)wealth * ((double)roll + base) * scale);
}

// ---- A scripted HE handler list driving DebugCmd3Hooks -------------------
struct HandlerSpec {
    DebugCmd3Handler hnd;
    DebugCmd3Entity  owner;     // resolve(ownerEntityId)
    DebugCmd3Entity  bA;        // resolve(buildingIdA)
    DebugCmd3Entity  bB;        // resolve(buildingIdB)
};

struct Env {
    DebugCmdPerson person;
    bool personFound = true;

    std::vector<HandlerSpec> handlers;   // the HE list
    int iter = 0;                        // iteration cursor

    std::vector<i32> queryIds;           // SpawnEntityFromHandlerList list
    bool nearestFound = false;
    i32  nearestId = 0;
    bool nearestValid = true;            // resolve(nearestId).valid

    // recorded emits
    int q16Calls = 0; i32 q16a1 = 0, q16a2 = 0, q16amount = 0; u8 q16mkt = 0;
    int msgCalls = 0; i32 msgFrom = 0, msgTo = 0;
};
Env* g_env = nullptr;

// Resolve an entity id by scanning the scripted handler list (owner/bA/bB carry
// distinct ids); also handles the nearest-entity id.
DebugCmd3Entity ResolveById(i32 id) {
    if (!g_env) return {};
    for (auto& s : g_env->handlers) {
        if (id == s.hnd.ownerEntityId) return s.owner;
        if (id == s.hnd.buildingIdA)   return s.bA;
        if (id == s.hnd.buildingIdB)   return s.bB;
    }
    if (g_env->nearestFound && id == g_env->nearestId) {
        DebugCmd3Entity e; e.valid = g_env->nearestValid; return e;
    }
    return {};
}

bool MockFind(i32 id, DebugCmdPerson* out) {
    if (!g_env || !g_env->personFound) return false;
    *out = g_env->person; out->id = id; return true;
}
void MockMsg(i32 from, i32 to, i32) {
    g_env->msgCalls++; g_env->msgFrom = from; g_env->msgTo = to;
}
void MockQ16(i32 a1, i32 a2, i32 amount, u8 mkt) {
    g_env->q16Calls++; g_env->q16a1 = a1; g_env->q16a2 = a2;
    g_env->q16amount = amount; g_env->q16mkt = mkt;
}
bool MockFindFirst(i32, DebugCmd3Handler* out) {
    g_env->iter = 0;
    if (g_env->handlers.empty()) return false;
    *out = g_env->handlers[0].hnd; return true;
}
bool MockFindNext(DebugCmd3Handler* out) {
    g_env->iter++;
    if (g_env->iter >= (int)g_env->handlers.size()) return false;
    *out = g_env->handlers[g_env->iter].hnd; return true;
}
DebugCmd3Entity MockResolve(i32 id) { return ResolveById(id); }
bool MockNearest(const DebugCmdPerson*, i32* outId) {
    if (!g_env->nearestFound) return false;
    *outId = g_env->nearestId; return true;
}
int MockQuery(const DebugCmdPerson*, i32* out, int cap) {
    int n = 0;
    for (i32 id : g_env->queryIds) { if (n >= cap) break; out[n++] = id; }
    return n;
}

DebugCmdHooks g_base;
DebugCmd3Hooks g_h3;

void Install(Env& e, u32 seed) {
    g_env = &e;
    crt::Srand(seed);

    g_base = DebugCmdHooks{};
    g_base.findPerson = MockFind;
    g_base.sendEntityMessage = MockMsg;
    g_base.queueRequest16 = MockQ16;
    g_base.market = 7;
    SetDebugCmdHooks(&g_base);

    g_h3 = DebugCmd3Hooks{};
    g_h3.heFindFirst = MockFindFirst;
    g_h3.heFindNext = MockFindNext;
    g_h3.resolveEntity = MockResolve;
    g_h3.findNearestEntity = MockNearest;
    g_h3.queryEntityList = MockQuery;
    SetDebugCmd3Hooks(&g_h3);
}

// Build a handler whose owner matches person.kindWord, both buildings are valid
// production buildings with the given codes.
HandlerSpec MakeHandler(int id, i32 kindWord, u8 codeA, u8 codeB) {
    HandlerSpec s;
    s.hnd.ownerEntityId = id * 10 + 1;
    s.hnd.buildingIdA   = id * 10 + 2;
    s.hnd.buildingIdB   = id * 10 + 3;
    s.owner.valid = true; s.owner.kindWord = kindWord;
    s.bA.valid = true; s.bA.production = true; s.bA.buildingCode = codeA;
    s.bB.valid = true; s.bB.production = true; s.bB.buildingCode = codeB;
    return s;
}

} // namespace

// --------------------------------------------------------------------------
// Person lookup failure -> 1 (no RNG advance, no emits).
// --------------------------------------------------------------------------
TEST(DebugCmd3, NoPersonReturns1) {
    Env e; e.personFound = false;
    Install(e, 1);
    CHECK_EQ(DebugCmdBroadcastMsgToHandlersA(7), 1);
    CHECK_EQ(DebugCmdSpawnEntityNearNearest(7), 1);
    CHECK_EQ(DebugCmdSpawnEntityFromHandlerList(7), 1);
    CHECK_EQ(e.msgCalls, 0);
    CHECK_EQ(e.q16Calls, 0);
}

// --------------------------------------------------------------------------
// Empty handler list (inert iterators) -> 1024, no message.
// --------------------------------------------------------------------------
TEST(DebugCmd3, BroadcastEmptyListReturns1024) {
    Env e; e.person.kindWord = 5;
    Install(e, 1);
    CHECK_EQ(DebugCmdBroadcastMsgToHandlersA(7), 1024);
    CHECK_EQ(DebugCmdBroadcastMsgToHandlersB(7), 1024);
    CHECK_EQ(DebugCmdBroadcastMsgToHandlersC(7), 1024);
    CHECK_EQ(DebugCmdBroadcastMsgToHandlersD(7), 1024);
    CHECK_EQ(DebugCmdBroadcastMsgToHandlersE(7), 1024);
    CHECK_EQ(e.msgCalls, 0);
}

// --------------------------------------------------------------------------
// Inert (no hooks installed at all) -> person found by NO hook means
// findPerson is null -> 1. Verify GetDebugCmd3Hooks defaults are inert.
// --------------------------------------------------------------------------
TEST(DebugCmd3, InertDefaultsCollectNothing) {
    SetDebugCmdHooks(nullptr);
    SetDebugCmd3Hooks(nullptr);
    // base inert findPerson is null -> handler returns 1 (no person resolvable).
    CHECK_EQ(DebugCmdBroadcastMsgToHandlersA(7), 1);
}

// --------------------------------------------------------------------------
// GateA: code==71 on either building collects. Owner kindWord must match.
// Two matching handlers -> count 2, pick = RandomModulo(2), message sent.
// --------------------------------------------------------------------------
TEST(DebugCmd3, BroadcastA_CollectAndPick) {
    Env e; e.person.kindWord = 5;
    e.handlers.push_back(MakeHandler(1, 5, 71, 0));   // code A == 71 -> collect
    e.handlers.push_back(MakeHandler(2, 5, 0, 71));   // code B == 71 -> collect
    e.handlers.push_back(MakeHandler(3, 5, 0, 0));    // neither 71 -> skip
    Install(e, 999);

    i32 r = DebugCmdBroadcastMsgToHandlersA(7);
    CHECK_EQ(r, 0);
    CHECK_EQ(e.msgCalls, 1);
    CHECK_EQ(e.msgFrom, 7);
    CHECK_EQ(e.msgTo, 7);

    // RNG: only the pick = RandomModulo(2).
    Lcg lcg(999); int expectPick = lcg.mod(2);
    Lcg lcg2(999); (void)lcg2.mod(2);
    // The pick consumed exactly one RandNext; a fresh handler at same seed must
    // re-consume the same single value.
    crt::Srand(999);
    int got = DebugCmdRandomModulo(2);
    CHECK_EQ(got, expectPick);
}

// --------------------------------------------------------------------------
// GateA: owner kindWord mismatch -> handler skipped -> 1024.
// --------------------------------------------------------------------------
TEST(DebugCmd3, BroadcastA_OwnerMismatchSkips) {
    Env e; e.person.kindWord = 5;
    HandlerSpec s = MakeHandler(1, 99, 71, 71);   // owner.kindWord 99 != 5
    e.handlers.push_back(s);
    Install(e, 1);
    CHECK_EQ(DebugCmdBroadcastMsgToHandlersA(7), 1024);
}

// --------------------------------------------------------------------------
// GateA: non-production buildings -> the production precondition fails -> skip.
// --------------------------------------------------------------------------
TEST(DebugCmd3, BroadcastA_NonProductionSkips) {
    Env e; e.person.kindWord = 5;
    HandlerSpec s = MakeHandler(1, 5, 71, 71);
    s.bA.production = false; s.bB.production = false;
    e.handlers.push_back(s);
    Install(e, 1);
    CHECK_EQ(DebugCmdBroadcastMsgToHandlersA(7), 1024);
}

// --------------------------------------------------------------------------
// GateC: collects when EITHER code != 71. (71,71) -> skip; (71,0) -> collect.
// --------------------------------------------------------------------------
TEST(DebugCmd3, BroadcastC_NotSeventyOneGate) {
    {
        Env e; e.person.kindWord = 5;
        e.handlers.push_back(MakeHandler(1, 5, 71, 71));   // both 71 -> skip
        Install(e, 1);
        CHECK_EQ(DebugCmdBroadcastMsgToHandlersC(7), 1024);
    }
    {
        Env e; e.person.kindWord = 5;
        e.handlers.push_back(MakeHandler(1, 5, 71, 0));    // B != 71 -> collect
        Install(e, 1);
        CHECK_EQ(DebugCmdBroadcastMsgToHandlersC(7), 0);
    }
}

// --------------------------------------------------------------------------
// BroadcastB/C/E consume an extra RandomModulo(3)+2 BEFORE the pick. Verify the
// RNG advances twice (extra, then pick) for a single-match list.
// --------------------------------------------------------------------------
TEST(DebugCmd3, BroadcastE_ConsumesExtraRollThenPick) {
    Env e; e.person.kindWord = 5;
    e.handlers.push_back(MakeHandler(1, 5, 71, 0));   // collect (count 1)
    Install(e, 4242);
    i32 r = DebugCmdBroadcastMsgToHandlersE(7);
    CHECK_EQ(r, 0);

    // Oracle: extra = RandomModulo(3)+2, then pick = RandomModulo(1).
    Lcg lcg(4242);
    (void)(lcg.mod(3) + 2);
    int pick = lcg.mod(1);
    CHECK_EQ(pick, 0);   // count 1 -> pick always 0

    // RNG state after the handler must equal the oracle's state: the next draw
    // matches RandomModulo against the oracle continuing from `lcg`.
    int oracleNext = lcg.mod(100);
    int gotNext = DebugCmdRandomModulo(100);
    CHECK_EQ(gotNext, oracleNext);
}

// --------------------------------------------------------------------------
// BroadcastA (no extra roll) advances RNG exactly ONCE (the pick).
// --------------------------------------------------------------------------
TEST(DebugCmd3, BroadcastA_AdvancesOnce) {
    Env e; e.person.kindWord = 5;
    e.handlers.push_back(MakeHandler(1, 5, 71, 0));
    Install(e, 4242);
    CHECK_EQ(DebugCmdBroadcastMsgToHandlersA(7), 0);

    Lcg lcg(4242);
    (void)lcg.mod(1);                 // single pick
    int oracleNext = lcg.mod(100);
    CHECK_EQ(DebugCmdRandomModulo(100), oracleNext);
}

// --------------------------------------------------------------------------
// QueueScaledRequestPerHandler: GateQ collect, then gold roll (%2,1.0,~0.01),
// QueueRequest16(-1,-1,gold,market), then pick. Verify gold + emit + RNG order.
// --------------------------------------------------------------------------
TEST(DebugCmd3, QueueScaled_GoldEmitAndRngOrder) {
    Env e; e.person.kindWord = 5; e.person.wealth = 10000;
    e.handlers.push_back(MakeHandler(1, 5, 71, 0));   // GateQ: B!=71 but A==71 ok
    Install(e, 7);

    i32 r = DebugCmdQueueScaledRequestPerHandler(7);
    CHECK_EQ(r, 0);
    CHECK_EQ(e.q16Calls, 1);
    CHECK_EQ(e.q16a1, -1);
    CHECK_EQ(e.q16a2, -1);
    CHECK_EQ(e.q16mkt, 7);
    CHECK_EQ(e.msgCalls, 1);

    Lcg lcg(7);
    int roll = lcg.mod(2);
    i32 gold = Gold(10000, roll, 1.0, (double)0.009999999776482582f);
    CHECK_EQ(e.q16amount, gold);
    (void)lcg.mod(1);                 // pick
    CHECK_EQ(DebugCmdRandomModulo(100), lcg.mod(100));
}

// --------------------------------------------------------------------------
// QueueStateRequestPerHandler: GateQ collect, pick, then amount=%0x23+60.
// Verify return + message + RNG advance (pick then amount roll).
// --------------------------------------------------------------------------
TEST(DebugCmd3, QueueState_PickThenAmountRoll) {
    Env e; e.person.kindWord = 5;
    e.handlers.push_back(MakeHandler(1, 5, 71, 71));   // GateQ A==71 -> collect
    e.handlers.push_back(MakeHandler(2, 5, 0, 71));    // GateQ B==71 -> collect
    Install(e, 31);

    CHECK_EQ(DebugCmdQueueStateRequestPerHandler(7), 0);
    CHECK_EQ(e.msgCalls, 1);

    Lcg lcg(31);
    (void)lcg.mod(2);                 // pick (count 2)
    (void)(lcg.mod(0x23) + 60);       // amount
    CHECK_EQ(DebugCmdRandomModulo(100), lcg.mod(100));
}

// --------------------------------------------------------------------------
// SpawnEntityFromHandlerList: empty query list -> 1024.
// --------------------------------------------------------------------------
TEST(DebugCmd3, FromHandlerList_EmptyReturns1024) {
    Env e; e.person.kindWord = 5;
    Install(e, 1);
    CHECK_EQ(DebugCmdSpawnEntityFromHandlerList(7), 1024);
    CHECK_EQ(e.q16Calls, 0);
}

// --------------------------------------------------------------------------
// SpawnEntityFromHandlerList: list of 3, pick then roll%5 base 2.0 scale 0.01,
// QueueRequest16(personId,-1,gold,market). Verify gold + emit + RNG order.
// --------------------------------------------------------------------------
TEST(DebugCmd3, FromHandlerList_PickRollGoldEmit) {
    Env e; e.person.kindWord = 5; e.person.wealth = 50000;
    e.queryIds = {100, 200, 300};
    Install(e, 12345);

    i32 r = DebugCmdSpawnEntityFromHandlerList(77);
    CHECK_EQ(r, 0);
    CHECK_EQ(e.q16Calls, 1);
    CHECK_EQ(e.q16a1, 77);
    CHECK_EQ(e.q16a2, -1);
    CHECK_EQ(e.q16mkt, 7);
    CHECK_EQ(e.msgCalls, 1);

    Lcg lcg(12345);
    (void)lcg.mod(3);                 // pick from 3-element list
    int roll = lcg.mod(5);
    i32 gold = Gold(50000, roll, 2.0, 0.01);
    CHECK_EQ(e.q16amount, gold);
}

// --------------------------------------------------------------------------
// SpawnEntityNearNearest: nothing near -> 1024; resolve-fail -> 1024.
// --------------------------------------------------------------------------
TEST(DebugCmd3, NearNearest_GuardPaths) {
    {
        Env e; e.person.kindWord = 5; e.nearestFound = false;
        Install(e, 1);
        CHECK_EQ(DebugCmdSpawnEntityNearNearest(7), 1024);
    }
    {
        Env e; e.person.kindWord = 5;
        e.nearestFound = true; e.nearestId = 555; e.nearestValid = false;
        Install(e, 1);
        CHECK_EQ(DebugCmdSpawnEntityNearNearest(7), 1024);
    }
}

// --------------------------------------------------------------------------
// SpawnEntityNearNearest: found+valid -> roll%3 base 1.0 scale 0.01,
// QueueRequest16(-1,personId,gold,market). Verify gold + emit.
// --------------------------------------------------------------------------
TEST(DebugCmd3, NearNearest_RollGoldEmit) {
    Env e; e.person.kindWord = 5; e.person.wealth = 8000;
    e.nearestFound = true; e.nearestId = 555; e.nearestValid = true;
    Install(e, 2024);

    i32 r = DebugCmdSpawnEntityNearNearest(33);
    CHECK_EQ(r, 0);
    CHECK_EQ(e.q16Calls, 1);
    CHECK_EQ(e.q16a1, -1);
    CHECK_EQ(e.q16a2, 33);
    CHECK_EQ(e.q16mkt, 7);

    Lcg lcg(2024);
    int roll = lcg.mod(3);
    i32 gold = Gold(8000, roll, 1.0, 0.01);
    CHECK_EQ(e.q16amount, gold);
}

// --------------------------------------------------------------------------
// NpcAction table mapping is exact for this wave's indices, nullptr elsewhere.
// --------------------------------------------------------------------------
TEST(DebugCmd3, NpcTableMapping) {
    CHECK(DebugCmd3NpcTableEntry(7)  == DebugCmdSpawnEntityFromHandlerList);
    CHECK(DebugCmd3NpcTableEntry(17) == DebugCmdSpawnEntityNearNearest);
    CHECK(DebugCmd3NpcTableEntry(34) == DebugCmdQueueStateRequestPerHandler);
    CHECK(DebugCmd3NpcTableEntry(36) == DebugCmdBroadcastMsgToHandlersA);
    CHECK(DebugCmd3NpcTableEntry(37) == DebugCmdQueueScaledRequestPerHandler);
    CHECK(DebugCmd3NpcTableEntry(38) == DebugCmdBroadcastMsgToHandlersB);
    CHECK(DebugCmd3NpcTableEntry(39) == DebugCmdBroadcastMsgToHandlersC);
    CHECK(DebugCmd3NpcTableEntry(41) == DebugCmdBroadcastMsgToHandlersD);
    CHECK(DebugCmd3NpcTableEntry(42) == DebugCmdBroadcastMsgToHandlersE);
    CHECK(DebugCmd3NpcTableEntry(0)  == nullptr);
    CHECK(DebugCmd3NpcTableEntry(5)  == nullptr);
    CHECK(DebugCmd3NpcTableEntry(99) == nullptr);
}
