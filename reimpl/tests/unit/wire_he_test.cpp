// Verifies InstallRealHeWiring() binds the three He (handler-pool) leaf bridges —
// HeEntityQueryHooks, HeHandlerHooks, GroupInteractHooks — to their real
// reconstructed leaves (previously HeHandlerHooks/GroupInteractHooks were fully
// inert at runtime; HeEntityQueryHooks had only its personFind default).
// Suite prefix: WireHe.
#include "tests/framework/test.h"

#include "sim/wire_he.h"
#include "sim/real_hooks3.h"   // InstallRealSimHooks3 — binds the sibling He pool + queue
#include "sim/he_entity_query.h"
#include "sim/he_handlers.h"
#include "sim/charaction_misc.h"
#include "sim/he.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

// Re-inert the two bridges that own a settable pointer so a clean baseline holds
// before/after. (HeEntityQueryHooks(nullptr) restores its real-default table.)
static void InertAll() {
    SetHeEntityQueryHooks(nullptr);
    SetHeHandlerHooks(nullptr);
    SetGroupInteractHooks(nullptr);
}

TEST(WireHe, BindsRealLeavesIntoAllThreeBridges) {
    InertAll();
    // The installer SEEDS each table from its module defaults and overrides only the
    // wireable fields (GroupInteractStep invokes hook fields WITHOUT a null-check),
    // so unbound fields keep their inert (non-null) stubs. We assert the BOUND
    // fields point at real adapters; the execute test proves real behaviour.
    InstallRealHeWiring();

    // --- HeEntityQueryHooks: person resolve + op35 emit are bound --------------
    const HeEntityQueryHooks& heq = GetHeEntityQueryHooks();
    CHECK(heq.personFind         != nullptr);
    CHECK(heq.queueRequestPair35 != nullptr);
    // notifyRivalEvent: no reconstructed History leaf -> inert (stays null).

    // --- HeHandlerHooks: free / dispatch / packet-status / cmd29 are bound -----
    const HeHandlerHooks& heh = GetHeHandlerHooks();
    CHECK(heh.freeHandlerEntry     != nullptr);
    CHECK(heh.npcActionDispatch    != nullptr);
    CHECK(heh.packetStatus         != nullptr);
    CHECK(heh.queueRequestEntity29 != nullptr);
    // charActionTick / eventTick / buildingTick: faithful 0-returning stubs -> inert.

    // --- GroupInteractHooks: person / ready / free / op8-interaction are bound -
    const GroupInteractHooks& gi = GetGroupInteractHooks();
    CHECK(gi.findPersonById           != nullptr);
    CHECK(gi.personReady              != nullptr);
    CHECK(gi.freeHandlerEntry         != nullptr);
    CHECK(gi.enqueueObjectInteraction != nullptr);
    // queueRequest39: no reconstructed op39 builder -> inert (but seeded non-null
    // from the module default, since the step calls it without a null-check).
    CHECK(gi.queueRequest39           != nullptr);

    InertAll();
}

// A representative action from each bridge runs over zeroed records through the
// installed real leaves without crashing, returning a defined result — the wired
// control flow actually executes against the real shared He pool / command queue.
TEST(WireHe, WiredLeavesExecuteOverZeroedRecords) {
    // Match the real boot order: InstallRealSimHooks3 creates/Init's the shared He
    // pool + command queue (the same RealHandlerTable()/RealCommandQueue() our
    // leaves operate on), so free/find/emit resolve against a live pool.
    InstallRealSimHooks3();
    InstallRealHeWiring();

    // --- HeEntityQuery: a pure table reset (returns 16384) --------------------
    int r = He_ResetEntityTables();
    CHECK_EQ(r, 16384);
    // The rival-pair scan over the (now-empty) tables: personFind(0) resolves to no
    // record -> the scan requests nothing. Defined zero return; exercises the real
    // personFind + op35 emit wiring path without faulting.
    int reqs = He_RequestRivalEntityPairs(/*personId=*/0, /*limit=*/1);
    CHECK(reqs >= 0);

    // --- HeHandler: the NpcAction step (dispatch on h+172, then free) ---------
    HeRecord rec;
    std::memset(&rec, 0, sizeof(rec));
    i32 a = He_NpcActionHandler(&rec);   // state 0 -> dispatch (no-op) -> free
    (void)a;

    // The cmd29-request handler: zeroed handle (+132) => none pending; state 0 with
    // the needs-cmd29 flag clear => passthrough. Exercises packetStatus wiring.
    std::memset(&rec, 0, sizeof(rec));
    i32 b = He_Entity29RequestHandler(&rec);
    (void)b;

    // --- GroupInteract: a zeroed leader + partnerId 0 => findPersonById(0) finds
    // no record => the "not ready" free path. Exercises findPersonById + free.
    std::memset(&rec, 0, sizeof(rec));
    GroupLeader leader;
    std::memset(&leader, 0, sizeof(leader));
    GroupInteractStep(&rec, &leader, /*partnerId=*/0);

    InertAll();
}
