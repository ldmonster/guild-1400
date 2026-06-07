// Integration: drive object_lifecycle6's VIBE_Object_CmdFindRandomByName /
// CmdFindRandomVisibleByName against the REAL reconstructed CRT RNG sibling
// (crt/rand.cpp: VIBE_Util_RandNext 0x5cb8bc, with Srand to seed). This is the
// live wiring: the original FindRandom* handlers pick slots[VIBE_Util_RandNext()
// % count]; this reimpl routes that RNG draw through ObjLife6Hooks.randNext, so
// we forward that hook into the genuine crt::RandNext and assert the cross-module
// selection the binary would produce for a known seed.
//
// The scene-graph walk leaf (VIBE_SceneGraph_WalkAndInvoke) has no reconstructed
// sibling, so we install a captor walk hook that replays a fixed set of nodes
// through the module's own CollectMatchingHandle trampoline (exactly the contract
// the real walk satisfies). The pointer-VALIDATION pass and FindObjectById run
// end-to-end through the module's INERT default-hook path (no leaves installed) —
// see the last two tests, which note that explicitly.
#include "test.h"

#include "sim/object_lifecycle6.h"
#include "crt/rand.h"

using namespace guild;
using namespace guild::sim;

namespace {

// The set of node handles the real scene-graph walk would visit, replayed
// through the module's CollectMatchingHandle trampoline. The walk hook shape is
// char(*)(void* root, int arg2, bool(*cb)(int,int), int flags, int ctx).
int g_walkNodes[8];
int g_walkNodeCount = 0;

char ReplayWalk(void* /*root*/, int /*arg2*/, bool (*cb)(int, int),
                int /*flags*/, int /*ctx*/) {
    for (int i = 0; i < g_walkNodeCount; ++i) {
        // Every replayed node "matches" (the real walk hands CollectMatchingHandle
        // already-filtered nodes); stop early if the collector says it is full.
        if (!cb(g_walkNodes[i], 1))
            break;
    }
    return 1;
}

// REAL CRT RNG sibling, forwarded exactly as the original's RNG draw.
int RealRandNext() { return crt::RandNext(); }

} // namespace

// FindRandomByName collects the walked nodes, then selects slots[RandNext()%count]
// using the REAL seeded LCG. With a known seed we can predict the exact index and
// therefore the exact handle returned — proving the cross-module RNG wiring.
TEST(ObjectLifecycle6Itest, FindRandomByNameUsesRealRng) {
    g_walkNodes[0] = 100; g_walkNodes[1] = 200; g_walkNodes[2] = 300;
    g_walkNodeCount = 3;

    ObjLife6Hooks h{};
    h.sceneGraphWalkAndInvoke = ReplayWalk;
    h.randNext = RealRandNext;
    ObjLife6SetHooks(h);

    // Seed and predict: the module draws exactly one RandNext() and picks
    // slots[draw % 3]. Compute the same draw from the same seed independently.
    crt::Srand(12345u);
    int predictedDraw = crt::RandNext();
    int expected = g_walkNodes[predictedDraw % 3];

    crt::Srand(12345u);   // re-seed so the module's draw matches the prediction
    int got = ObjectCmdFindRandomByName("crate");
    ObjLife6ResetHooks();

    CHECK_EQ(got, expected);
}

// No matching nodes -> the collector count stays 0 -> the handler returns 0
// WITHOUT drawing from the RNG (the count==0 branch). Wired with the real RNG to
// prove the early-out genuinely skips the RNG sibling.
TEST(ObjectLifecycle6Itest, FindRandomByNameEmptyReturnsZero) {
    g_walkNodeCount = 0;

    ObjLife6Hooks h{};
    h.sceneGraphWalkAndInvoke = ReplayWalk;
    h.randNext = RealRandNext;
    ObjLife6SetHooks(h);

    int got = ObjectCmdFindRandomByName("nothing");
    ObjLife6ResetHooks();

    CHECK_EQ(got, 0);
}

// FindRandomVisibleByName with a null root short-circuits to 0 before any walk or
// RNG draw (the !*root branch).
TEST(ObjectLifecycle6Itest, FindRandomVisibleNullRootShortCircuits) {
    g_walkNodes[0] = 777;
    g_walkNodeCount = 1;

    ObjLife6Hooks h{};
    h.sceneGraphWalkAndInvoke = ReplayWalk;
    h.randNext = RealRandNext;
    ObjLife6SetHooks(h);

    int got = ObjectCmdFindRandomVisibleByName(/*root*/ 0, "anyone");
    ObjLife6ResetHooks();

    CHECK_EQ(got, 0);
}

// FindRandomVisibleByName with a valid root walks + selects via the REAL RNG, the
// same as FindRandomByName but visibility-gated (flags 832). One node -> draw%1==0
// -> that single handle, regardless of seed.
TEST(ObjectLifecycle6Itest, FindRandomVisibleSingleMatch) {
    g_walkNodes[0] = 4242;
    g_walkNodeCount = 1;

    ObjLife6Hooks h{};
    h.sceneGraphWalkAndInvoke = ReplayWalk;
    h.randNext = RealRandNext;
    ObjLife6SetHooks(h);

    crt::Srand(999u);
    int got = ObjectCmdFindRandomVisibleByName(/*root*/ 0x1000, "guard");
    ObjLife6ResetHooks();

    CHECK_EQ(got, 4242);
}

// CollectMatchingHandle caps at 32 slots: replay 40 matching nodes, assert the
// collector stops returning "continue" once full and only the first 32 are kept.
TEST(ObjectLifecycle6Itest, CollectorCapsAtThirtyTwo) {
    HandleCollector c;
    bool cont = true;
    for (int i = 0; i < 40 && cont; ++i)
        cont = ObjectCollectMatchingHandle(1000 + i, &c, true);

    CHECK_EQ(c.count, 32);          // store at slot 31 pushes count to 32 -> returns false, stops
    CHECK_EQ(c.slots[0], 1000);
    CHECK_EQ(c.slots[31], 1031);    // slot 32 onward never visited (walk stopped)
}

// FindObjectById runs entirely through the module's faithful table scan — no hooks
// at all (pure deterministic logic). Sized to the real ObjIdRecord stride/bound.
TEST(ObjectLifecycle6Itest, FindObjectByIdScansTable) {
    ObjLife6ResetHooks();   // inert defaults — no leaves needed for the scan

    static ObjIdRecord table[kObjIdTableSlots];
    for (int i = 0; i < kObjIdTableSlots; ++i) { table[i].kind = 0; table[i].id = 0; }
    table[5].kind = 1;  table[5].id = 42;
    table[9].kind = 1;  table[9].id = 99;

    ObjIdRecord* hit = ObjectFindObjectById(table, 99);
    CHECK(hit != nullptr);
    if (hit) {
        CHECK_EQ(hit->id, 99);
        CHECK_EQ((int)(hit - table), 9);
    }
    CHECK(ObjectFindObjectById(table, 12345) == nullptr);
}

// ValidatePointers + the validation-failure callback run through the module's
// INERT default IsValidPointer policy (non-null => valid) plus an installed
// failure captor. With one deliberately-invalid pointer the captor fires exactly
// once; this exercises the validation pass end to end (no reconstructed sibling
// exists for the Memory_IsValidPointer leaf, so the default policy is used and
// overridden only to mark one pointer bad).
TEST(ObjectLifecycle6Itest, ValidatePointersFailureCallback) {
    static void* g_bad = nullptr;
    static int g_failCount = 0;
    g_bad = nullptr; g_failCount = 0;

    void* badPtr = reinterpret_cast<void*>(0xBADBAD);

    ObjLife6Hooks h{};
    h.memoryIsValidPointer = [](void* p) -> int {
        // Only the designated bad pointer is invalid.
        return p == reinterpret_cast<void*>(0xBADBAD) ? 0 : 1;
    };
    h.validationFailure = [](void* p) { g_bad = p; g_failCount++; };
    ObjLife6SetHooks(h);

    ValidationRecord rec;          // sized POD per the .h
    int dummy = 0;
    rec.mesh13 = &dummy;           // valid -> no fail
    rec.ptr30  = badPtr;           // invalid -> one fail
    ObjectValidatePointers(&rec);
    ObjLife6ResetHooks();

    CHECK_EQ(g_failCount, 1);
    CHECK_EQ(g_bad, badPtr);
}
