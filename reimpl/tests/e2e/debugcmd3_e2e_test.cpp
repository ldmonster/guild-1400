// E2E flow for the third wave of VIBE_DebugCmd_* handlers (debugcmd3.cpp).
// Drives a small "game tick" that routes several NpcAction command codes through
// DebugCmd3NpcTableEntry into the real handlers, against one shared scripted
// world (a person, an HE handler list, a nearby entity, a query list), asserting
// the cumulative observable effects (return codes, command emits, messages) and
// the deterministic RNG progression across the whole sequence.
#include "test.h"
#include "sim/debugcmd3.h"
#include "crt/rand.h"

#include <vector>

using namespace guild;
using namespace guild::sim;

namespace {

struct HandlerSpec {
    DebugCmd3Handler hnd;
    DebugCmd3Entity owner, bA, bB;
};

struct World {
    DebugCmdPerson person;
    std::vector<HandlerSpec> handlers;
    int iter = 0;
    std::vector<i32> queryIds;
    bool nearestFound = false; i32 nearestId = 0;

    int q16Calls = 0; i32 lastAmount = 0;
    int msgCalls = 0;
    i32 totalQueued = 0;
};
World* g_w = nullptr;

DebugCmd3Entity Resolve(i32 id) {
    if (!g_w) return {};
    for (auto& s : g_w->handlers) {
        if (id == s.hnd.ownerEntityId) return s.owner;
        if (id == s.hnd.buildingIdA)   return s.bA;
        if (id == s.hnd.buildingIdB)   return s.bB;
    }
    if (g_w->nearestFound && id == g_w->nearestId) {
        DebugCmd3Entity e; e.valid = true; return e;
    }
    return {};
}

bool Find(i32 id, DebugCmdPerson* out) { *out = g_w->person; out->id = id; return true; }
void Msg(i32, i32, i32) { g_w->msgCalls++; }
void Q16(i32, i32, i32 amount, u8) { g_w->q16Calls++; g_w->lastAmount = amount; g_w->totalQueued += amount; }
bool First(i32, DebugCmd3Handler* o) { g_w->iter = 0; if (g_w->handlers.empty()) return false; *o = g_w->handlers[0].hnd; return true; }
bool Next(DebugCmd3Handler* o) { g_w->iter++; if (g_w->iter >= (int)g_w->handlers.size()) return false; *o = g_w->handlers[g_w->iter].hnd; return true; }
bool Near(const DebugCmdPerson*, i32* o) { if (!g_w->nearestFound) return false; *o = g_w->nearestId; return true; }
int Query(const DebugCmdPerson*, i32* o, int cap) { int n = 0; for (i32 id : g_w->queryIds) { if (n >= cap) break; o[n++] = id; } return n; }

HandlerSpec Mk(int id, i32 kw, u8 ca, u8 cb) {
    HandlerSpec s;
    s.hnd.ownerEntityId = id * 10 + 1;
    s.hnd.buildingIdA = id * 10 + 2;
    s.hnd.buildingIdB = id * 10 + 3;
    s.owner.valid = true; s.owner.kindWord = kw;
    s.bA.valid = true; s.bA.production = true; s.bA.buildingCode = ca;
    s.bB.valid = true; s.bB.production = true; s.bB.buildingCode = cb;
    return s;
}

DebugCmdHooks g_base;
DebugCmd3Hooks g_h3;
void Install(World& w, u32 seed) {
    g_w = &w; crt::Srand(seed);
    g_base = DebugCmdHooks{};
    g_base.findPerson = Find; g_base.sendEntityMessage = Msg;
    g_base.queueRequest16 = Q16; g_base.market = 3;
    SetDebugCmdHooks(&g_base);
    g_h3 = DebugCmd3Hooks{};
    g_h3.heFindFirst = First; g_h3.heFindNext = Next; g_h3.resolveEntity = Resolve;
    g_h3.findNearestEntity = Near; g_h3.queryEntityList = Query;
    SetDebugCmd3Hooks(&g_h3);
}

i32 RunByIndex(int idx, i32 personId) {
    auto fn = DebugCmd3NpcTableEntry(idx);
    return fn ? fn(personId) : -777;
}

} // namespace

// --------------------------------------------------------------------------
// A full dispatch sweep: every wave-3 index routes to a live handler; with a
// rich world they all succeed (0); the broadcast variants send messages, the
// scaled/spawn variants queue commands.
// --------------------------------------------------------------------------
TEST(DebugCmd3E2E, FullDispatchSweepSucceeds) {
    World w; w.person.kindWord = 5; w.person.wealth = 20000;
    // A handler list that satisfies every variant's collect gate at once:
    // one with both codes 71 and one with neither -> every gate finds >=1 match.
    w.handlers.push_back(Mk(1, 5, 71, 71));
    w.handlers.push_back(Mk(2, 5, 0, 0));
    w.queryIds = {11, 22, 33};
    w.nearestFound = true; w.nearestId = 900;
    Install(w, 555);

    const int idxs[] = {7, 17, 34, 36, 37, 38, 39, 41, 42};
    for (int idx : idxs) {
        i32 r = RunByIndex(idx, 7);
        CHECK_EQ(r, 0);
    }
    // 9 handlers all reached the message tail (NearNearest + FromHandlerList +
    // QueueScaled go through SendTail too).
    CHECK_EQ(w.msgCalls, 9);
    // The three command-emitting handlers (FromHandlerList, NearNearest,
    // QueueScaled) each queued exactly one QR16.
    CHECK_EQ(w.q16Calls, 3);
}

// --------------------------------------------------------------------------
// Barren world: no handlers, no nearby entity, empty query list. Every handler
// that depends on a list/search returns 1024; nothing is emitted.
// --------------------------------------------------------------------------
TEST(DebugCmd3E2E, BarrenWorldAllIneligible) {
    World w; w.person.kindWord = 5; w.person.wealth = 20000;
    Install(w, 9);

    const int idxs[] = {7, 17, 34, 36, 37, 38, 39, 41, 42};
    for (int idx : idxs)
        CHECK_EQ(RunByIndex(idx, 7), 1024);

    CHECK_EQ(w.msgCalls, 0);
    CHECK_EQ(w.q16Calls, 0);
}

// --------------------------------------------------------------------------
// Cross-handler determinism: two identical sweeps from the same seed produce the
// identical queued totals (the RNG drives every roll/pick deterministically).
// --------------------------------------------------------------------------
TEST(DebugCmd3E2E, DeterministicAcrossRuns) {
    auto runSweep = [](u32 seed) -> i32 {
        World w; w.person.kindWord = 5; w.person.wealth = 33333;
        w.handlers.push_back(Mk(1, 5, 71, 0));
        w.queryIds = {1, 2, 3, 4};
        w.nearestFound = true; w.nearestId = 900;
        Install(w, seed);
        const int idxs[] = {37, 7, 17, 37, 7, 17};
        for (int idx : idxs) RunByIndex(idx, 7);
        return w.totalQueued;
    };
    i32 a = runSweep(8675309);
    i32 b = runSweep(8675309);
    CHECK_EQ(a, b);            // identical seed -> identical queued total
    i32 a2 = runSweep(8675309);
    CHECK_EQ(a, a2);           // and stable across a third identical run
}

// --------------------------------------------------------------------------
// Mixed eligibility: a handler that satisfies GateA but not GateC interleaved
// with the search handlers. Confirms each variant's gate is independent.
// --------------------------------------------------------------------------
TEST(DebugCmd3E2E, GateSelectivityAcrossVariants) {
    World w; w.person.kindWord = 5; w.person.wealth = 1000;
    // Single handler, both codes 71: GateA/E/Q collect it; GateC (needs a !=71)
    // rejects it -> BroadcastC must return 1024 while A/E/D succeed.
    w.handlers.push_back(Mk(1, 5, 71, 71));
    Install(w, 42);

    CHECK_EQ(RunByIndex(36, 7), 0);      // BroadcastA  (71||71)
    CHECK_EQ(RunByIndex(39, 7), 1024);   // BroadcastC  (need a !=71) -> none
    CHECK_EQ(RunByIndex(41, 7), 0);      // BroadcastD  (a==71) collect
    CHECK_EQ(RunByIndex(42, 7), 0);      // BroadcastE  (71||71)
    CHECK_EQ(RunByIndex(34, 7), 0);      // QueueState  (GateQ a==71)
}
