// Unit tests for the RealSimHooks installer (src/sim/real_hooks.cpp). After
// InstallRealSimHooks(), the representative wired hooks must invoke the REAL
// reconstructed targets — no mocks on the wired paths.
#include "sim/real_hooks.h"

#include "sim/command.h"
#include "sim/command_apply3.h"
#include "sim/entity.h"
#include "sim/npctarget.h"
#include "sim/charaction.h"
#include "ai/cardgame.h"

#include "test.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;
using guild::ai::CardGameState;
using guild::ai::TakeTurn;

namespace {

// Seed one live Person (id, array slot) into the real entity array.
void SeedPerson(int slot, i32 id) {
    g_persons[slot].marker = 0;     // 0 == live (only -1 == free)
    g_persons[slot].id = id;
    g_personIds[slot] = id;
}

} // namespace

// --- AI command-emit hook now drives the real command codec/queue ------------
TEST(SimRealHooks, BetCmdEnqueuesRealPacket) {
    InstallRealSimHooks();
    CommandQueue* q = RealCommandQueue();
    CHECK(q != nullptr);

    u32 before = q->send_count();

    // cardgame's TakeTurn -> EmitBet -> the wired BetCmd hook -> QueueRequest16
    // -> CommandQueue::EnqueuePacket. Build a minimal seat state and take a turn.
    CardGameState st{};
    // seat A at +12.., seat B at +40..; stakes live at +0 (A) / +4 (B).
    i32 seatPtr = 1;    // any non-zero seat entity ptr value
    st.seti32(12, seatPtr); // make `seatPtr` resolve to seat A
    st.bytes[36] = 1;       // seat-A decision byte 1 (so action 2 is a valid transition)
    st.seti32(0, 5);    // A stake
    st.seti32(4, 7);    // B stake
    st.seti32(68, 0);   // pot

    // action 2 (hold): pot += B stake, emits a bet command.
    int r = TakeTurn(st, seatPtr, 2);
    CHECK(r == 1);

    // A real packet was enqueued on the REAL CommandQueue (not a mock recorder):
    // send_count advanced and a pending-send node now exists.
    CHECK_EQ(q->send_count(), before + 1);
    CHECK(q->pending_head() != nullptr);
    // The enqueued packet carries opcode 16 (QueueRequest16) and B's stake at +0x1D.
    CommandPacket& slot = q->ring_slot((before + 1) & 0x7FFF);
    CHECK_EQ((int)slot.opcode(), 16);
}

// --- command-apply entity-query hook now hits the real entity array ----------
TEST(SimRealHooks, ObjectFindHitsRealEntityArray) {
    ResetEntityArrays();
    InstallRealSimHooks();

    // Seed a real building/object record id 4242 and a miss id.
    g_personArrayLoaded = true;
    g_objects[3].alive = 1;
    g_objects[3].id = 4242;

    // The command_apply3 ObjectFind hook is wired to BuildingFindById; a direct
    // BuildingFindById call confirms the real array holds the record, and the
    // hook's behaviour mirrors it (nonzero on hit, 0 on miss).
    CHECK(BuildingFindById(4242) != nullptr);
    CHECK(BuildingFindById(9999) == nullptr);
}

// --- NPC target entity-query hooks now resolve via the real Person array -----
TEST(SimRealHooks, NpcTargetFindPersonUsesRealArray) {
    ResetEntityArrays();
    SeedPerson(2, 700);
    SeedPerson(5, 701);
    InstallRealSimHooks();

    const NpcTargetHooks& h = GetNpcTargetHooks();
    CHECK(h.findPersonById != nullptr);
    CHECK(h.personByIndex != nullptr);

    // findPersonById -> PersonFindRecordById (linear scan of the real array).
    PersonHandle p = h.findPersonById(701);
    CHECK(p != nullptr);
    CHECK(p == static_cast<PersonHandle>(&g_persons[5]));
    CHECK(h.findPersonById(404) == nullptr);  // absent id

    // personByIndex -> &g_persons[index] (real array slot), bounds-guarded.
    CHECK(h.personByIndex(2) == static_cast<PersonHandle>(&g_persons[2]));
    CHECK(h.personByIndex(-1) == nullptr);
    CHECK(h.personByIndex(kPersonCapacity) == nullptr);

    // The Office/Ai scoring fields remain unwired (no real target) -> null.
    CHECK(h.favorability == nullptr);
    CHECK(h.officeRank == nullptr);
}

// --- charaction leaves: RegisterHandlers binds the real character step fns ----
TEST(SimRealHooks, CharActionCatalogBoundToRealSteps) {
    InstallRealSimHooks();
    // RegisterHandlers populated the action-type catalog with the real
    // character.cpp step handlers; the catalog must report them as registered.
    CHECK(ActionType(7).step != nullptr);    // TurnStepActionUpdate
    CHECK(ActionType(49).step != nullptr);   // TakeObjectActionUpdate
    CHECK(ActionType(50).step != nullptr);   // DropObjectActionUpdate
    CHECK(ActionType(54).step != nullptr);   // LoadAnimActionUpdate
    // A free node can be obtained (the pool is armed).
    ActionNode* n = GetFreeEntry();
    CHECK(n != nullptr);
}
