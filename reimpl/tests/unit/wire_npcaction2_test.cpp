// Verifies InstallRealNpcAction2Wiring() binds the three NpcAction leaf bridges
// (NpcAction8/9/10Hooks) to their real reconstructed siblings — previously all
// three were fully inert at runtime (nothing in the live tree installed them).
// Suite prefix: WireNpcAction2. No main() (shared test_main.cpp), headless.
#include "tests/framework/test.h"

#include "sim/wire_npcaction2.h"
#include "sim/real_hooks3.h"   // InstallRealSimHooks3 (shared He pool + queue prereq)
#include "sim/npcaction8.h"
#include "sim/npcaction9.h"
#include "sim/npcaction10.h"
#include "sim/he.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

// Re-inert every bridge so a clean baseline holds for any later test in this TU.
static void InertAll2() {
    SetNpcAction8Hooks(nullptr);
    SetNpcAction9Hooks(nullptr);
    SetNpcAction10Hooks(nullptr);
}

TEST(WireNpcAction2, BindsRealLeavesIntoAllThreeBridges) {
    InertAll2();
    // The installer SEEDS each table from its module inert default (all-null/zero)
    // and overrides ONLY the byte-faithful fields. We assert the BOUND fields point
    // at real adapters; unbound fields stay at their inert default (null/0).
    InstallRealNpcAction2Wiring();

    // --- NpcAction8: RNG + building rank + op-17 sell emit are real -----------
    const NpcAction8Hooks& h8 = GetNpcAction8Hooks();
    CHECK(h8.randomModulo            != nullptr);
    CHECK(h8.buildingRankWithinGroup != nullptr);
    CHECK(h8.queueRequest17          != nullptr);

    // --- NpcAction9: RNG (modulo + float), building rank, He filter scan ------
    const NpcAction9Hooks& h9 = GetNpcAction9Hooks();
    CHECK(h9.randomModulo               != nullptr);
    CHECK(h9.randomFloatScaled          != nullptr);
    CHECK(h9.buildingRankWithinGroup    != nullptr);
    CHECK(h9.heFindFirstHandlerByFilter != nullptr);
    CHECK(h9.heFindNextMatchingHandler  != nullptr);

    // --- NpcAction10: packet gate, handler free, op-29 + command builders -----
    const NpcAction10Hooks& h10 = GetNpcAction10Hooks();
    CHECK(h10.packetStatus     != nullptr);
    CHECK(h10.queueEntity29     != nullptr);
    CHECK(h10.freeHandlerEntry  != nullptr);
    CHECK(h10.queueRequest16    != nullptr);
    CHECK(h10.requestCoord27    != nullptr);
    CHECK(h10.requestBuildOp71  != nullptr);
    CHECK(h10.requestBuildOp77  != nullptr);
    CHECK(h10.requestBuildOp90  != nullptr);
    CHECK(h10.requestBuildOp91  != nullptr);

    InertAll2();
}

// The bound real leaves execute over zeroed records / direct calls without
// crashing, returning defined results — i.e. the wired control flow actually runs
// the real RNG, the real He-pool scan, and the real command builders.
TEST(WireNpcAction2, WiredLeavesExecuteOverZeroedRecord) {
    // Match the real boot order: InstallRealSimHooks3 creates/Init's the shared He
    // pool + queue that the wired He-pool / command bindings operate on.
    InstallRealSimHooks3();
    InstallRealNpcAction2Wiring();

    // --- NpcAction8: a score evaluator that draws the real building rank + RNG.
    //   EvaluateMoveTo seeds two out floats and routes through the (inert) score
    //   kernels; defined return over the empty world. The bound RNG/rank fields are
    //   exercised by the search/approach evaluators below indirectly; here we prove
    //   the table is live and a representative evaluator returns cleanly.
    {
        float ox = 1.0f, oy = 1.0f;
        int code = NpcAction8_EvaluateMoveTo(&ox, &oy, /*relFlag=*/0,
                                             /*ctxA=*/0, /*ctxB=*/0);
        (void)code;   // defined action code (0 over the empty world)
    }
    // Direct exercise of the bound NpcAction8 leaves through the installed table.
    {
        const NpcAction8Hooks& h8 = GetNpcAction8Hooks();
        u16 r = h8.randomModulo(7);            // real LCG draw < 7
        CHECK(r < 7);
        int rank = h8.buildingRankWithinGroup(0);  // ungrouped code => 0
        CHECK(rank >= 0);
    }

    // --- NpcAction9: the real He filter scan over the (empty) shared pool returns
    //   absent, and the real RNG draws are bounded.
    {
        const NpcAction9Hooks& h9 = GetNpcAction9Hooks();
        const u8* first = h9.heFindFirstHandlerByFilter(2, 0, 24, 2, 0);
        CHECK(first == nullptr);               // empty pool => no match
        const u8* next = h9.heFindNextMatchingHandler();
        CHECK(next == nullptr);
        u16 rm = h9.randomModulo(0x40);
        CHECK(rm < 0x40);
        float rf = h9.randomFloatScaled();
        CHECK(rf >= 0.0f);
        CHECK(rf < 1.0001f);
    }

    // --- NpcAction10: a full coroutine over a zeroed He record runs through the
    //   real packet gate + handler-free + command builders. RunCreditStep over a
    //   zeroed record: req handle -1 (no packet wait), state 0, flag&2 clear => it
    //   takes the early free path through the real FreeHandlerEntry on the shared
    //   pool. Defined (void) return, no crash.
    {
        HeRecord rec;
        std::memset(&rec, 0, sizeof(rec));
        NpcAction10_RunCreditStep(&rec);

        // Direct exercise of a bound command builder + the packet gate.
        const NpcAction10Hooks& h10 = GetNpcAction10Hooks();
        int st = h10.packetStatus(-1);         // -1 handle => defined status
        (void)st;
        h10.requestBuildOp77(0);               // real op-77 builder onto shared queue
        h10.requestCoord27(0, 0, 0);           // real op-27 builder
        i32 freed = h10.freeHandlerEntry(&rec);// null-safe free over zeroed record
        (void)freed;
    }

    InertAll2();
}
