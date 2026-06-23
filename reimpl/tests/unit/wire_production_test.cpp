// Verifies InstallRealProductionWiring() binds the three production / personnel-2
// bridges (IProductionSlotHooks, IProductionHooks, PersonPersonnel2Hooks) to their
// real reconstructed leaves — previously all three were inert at runtime (nothing
// in src/ installed them). Suite prefix: WireProduction. Headless, no main().
#include "test.h"

#include "world/wire_production.h"

#include "sim/production_slots.h"     // IProductionSlotHooks / Set/Get + tick
#include "sim/building_production.h"  // IProductionHooks / Set/Get + PlayerBuildingIndex
#include "sim/person_personnel2.h"    // PersonPersonnel2Hooks / Set/Get
#include "sim/gametime.h"             // GameTimeDiffMinutes (the bound leaf)
#include "sim/real_hooks.h"           // RealCommandQueue (the shared emit target)
#include "sim/command.h"             // CommandQueue

#include <cstring>

using namespace guild;
using namespace guild::sim;
using guild::world::InstallRealProductionWiring;

// Re-inert every bridge so a clean baseline can be asserted before install.
static void InertAll() {
    SetProductionSlotHooks(nullptr);
    SetProductionHooks(nullptr);
    SetPersonPersonnel2Hooks(nullptr);
}

// ===========================================================================
// The installer makes all three bridges live (non-null hook objects). For the
// SLOT bridge two leaves get REAL targets; the other two bridges install their
// module default-backed object (live, owned — not dangling).
// ===========================================================================
TEST(WireProduction, InstallsAllThreeBridges) {
    InertAll();
    InstallRealProductionWiring();

    // All three bridge accessors must return a live (non-null) hook object.
    CHECK(ProductionSlotHooks()       != nullptr);
    CHECK(ProductionHooks()           != nullptr);
    CHECK(PersonPersonnel2HooksPtr()  != nullptr);

    InertAll();   // restore for any later test in this TU
}

// ===========================================================================
// The SLOT bridge's DiffMinutes leaf is now the REAL VIBE_GameTime_DiffMinutes:
// the production-timer tick must debit the slot timer by exactly the reconstructed
// minute difference. We drive a completing order and check the timer landed where
// GameTimeDiffMinutes says it should.
// ===========================================================================
TEST(WireProduction, SlotTickUsesRealDiffMinutes) {
    InertAll();
    InstallRealProductionWiring();

    GameTime stamp{};
    stamp.day = 0; stamp.hour = 0; stamp.minute = 0; stamp.second = 0;
    GameTime now{};
    now.day = 0; now.hour = 2; now.minute = 30; now.second = 0;  // +150 minutes

    // VIBE_GameTime_DiffMinutes oracle (the leaf the hook must call).
    const int elapsed = GameTimeDiffMinutes(&stamp, &now);
    CHECK_EQ(elapsed, 150);

    // An order with a 200-minute timer should NOT finish after 150 elapsed.
    ProductionOrder running{};
    running.active = true;
    running.timerMinutes = 200;
    running.lastStamp = stamp;
    running.productType = 42;
    running.personId = 7;
    ProductionTickResult r1 = InventoryTickProductionOrder(running, now, /*ownerTurn=*/true);
    CHECK(r1.finished == false);
    CHECK_EQ(running.timerMinutes, 200 - elapsed);   // 50, via the real DiffMinutes
    CHECK(running.lastStamp.hour == now.hour);        // restamped to now

    InertAll();
}

// ===========================================================================
// The SLOT bridge's EmitProductionFinished leaf is now the REAL
// VIBE_Command_QueueRequest17: a completing order on the owner's turn must enqueue
// a real opcode-17 packet onto the shared RealCommandQueue.
// ===========================================================================
TEST(WireProduction, CompletedOrderEmitsRealCommand) {
    InertAll();
    InstallRealProductionWiring();
    SetPlayerBuildingIndex(0);   // byte_6477A1 -> a defined a5 for QueueRequest17

    CommandQueue* q = RealCommandQueue();
    CHECK(q != nullptr);
    const u32 before = q->GetPacketSeqById(0);  // touch the queue (proves it exists)
    (void)before;

    GameTime stamp{}; stamp.minute = 0;
    GameTime now{};   now.minute = 30;           // +30 minutes elapsed

    // Timer already below the elapsed window => completes this step.
    ProductionOrder order{};
    order.active = true;
    order.timerMinutes = 10;     // 10 - 30 = -20 < 0 -> finished
    order.lastStamp = stamp;
    order.productType = 99;
    order.personId = 1234;

    ProductionTickResult res = InventoryTickProductionOrder(order, now, /*ownerTurn=*/true);
    CHECK(res.finished == true);
    CHECK(res.emitted  == true);     // the real QueueRequest17 fired
    CHECK(order.active  == false);   // active flag cleared on completion

    // Non-owner turn must NOT emit (the IsObjectForTurn gate) — control-flow proof.
    ProductionOrder order2{};
    order2.active = true;
    order2.timerMinutes = 5;
    order2.lastStamp = stamp;
    order2.productType = 99;
    order2.personId = 1234;
    ProductionTickResult res2 = InventoryTickProductionOrder(order2, now, /*ownerTurn=*/false);
    CHECK(res2.finished == true);
    CHECK(res2.emitted  == false);

    InertAll();
}

// ===========================================================================
// The unbound leaves keep their safe inert defaults after install: the SLOT
// bridge's scene queries return "not found", and the PersonPersonnel2 wealth /
// distance leaves return their inert sentinels (proves SEED-FROM-DEFAULTS left
// them intact rather than zeroing the table).
// ===========================================================================
TEST(WireProduction, UnboundLeavesStayInert) {
    InertAll();
    InstallRealProductionWiring();

    // SLOT: QueryObjectNode / FindGridSlot stay inert (no scene node).
    IProductionSlotHooks* sh = ProductionSlotHooks();
    i32 owner = 0, active = 0; i16 type = 0; i32 slotId = 0;
    CHECK(sh->QueryObjectNode(/*container*/0, /*prot*/1, &owner, &active, &type) == false);
    CHECK(sh->FindGridSlot(/*type*/1, &slotId) == false);

    // PERSON-2: distance is the inert "never wins" sentinel; the staff book empty;
    // the wealth leaves 0 (all unbound — no clean reconstructed binding).
    PersonPersonnel2Hooks* pp = PersonPersonnel2HooksPtr();
    CHECK(pp->NearestDistance(0, 0) == 0x7fffffff);
    int n = -1;
    CHECK(pp->StaffBook(&n) == nullptr);
    CHECK_EQ(n, 0);
    CHECK_EQ(pp->SumCurrencyHeld(nullptr), 0);
    CHECK_EQ(pp->ComputeTotalWealth(nullptr), 0);

    InertAll();
}
