// End-to-end: drive a synthetic NPC carrier through a full plague-spread routine
// tick-by-tick (select target -> phase 0 -> phase 1 -> phase 2 x N -> phase 3 ->
// teardown) and verify the He state progression + the emitted (mock) commands
// against a hand-computed reference.
//
// The host re-dispatches the coroutine when each QueueRequestEntity29 packet is
// "applied" (we model that by clearing the +132 handle to -1 between ticks, and
// stamping the new state the previous tick's QueueRequestEntity29(arg) implies).
// In the engine the arg N is the next state the entity-request carries; this
// driver mirrors that: arg -> next state (with -1 meaning teardown via state -1).
#include "tests/framework/test.h"

#include "sim/npcaction2.h"
#include "sim/npcaction.h"
#include "sim/he.h"
#include "crt/rand.h"

#include <cstring>
#include <vector>

using namespace guild::sim;
using guild::i32;
using guild::u8;

namespace {

struct E2E {
    std::vector<int> aiType = std::vector<int>(256, 0);
    std::vector<char> susceptible = std::vector<char>(256, 0);
    std::vector<int> e29;            // QueueRequestEntity29 args in order
    int e29Counter = 0;
    int infectCalls = 0;
    int outbreak = -1;
    int spreadDone = -1;
    int named53 = 0;
    bool nearDoor = true;
};
E2E s;

i32 e_ent29(int arg, HeRecord*) { s.e29.push_back(arg); return ++s.e29Counter; }
i32 e_status(i32 h) { return h >= 0 ? 1 : 0; }
i32 e_seq(i32 h) { return h + 1; }
i32 e_free(HeRecord*) { return 0; }
u8  e_ai(int slot) { return (slot >= 0 && slot < 256) ? (u8)s.aiType[slot] : 0; }
i32 e_oid(int slot) { return 1000 + slot; }
bool e_susc(int slot) { return slot >= 0 && slot < 256 && s.susceptible[slot]; }
bool e_resolve(i32) { return true; }
bool e_present(i32) { return true; }
bool e_door(i32, i32) { return s.nearDoor; }
void e_single(i32) {}
void e_named(i32, i32, int, int) { s.named53++; }
void e_pair(i32, int) {}
i32  e_op73(i32) { return 300; }
void e_slot(i32) {}
void e_out(i32 o) { s.outbreak = o; }
void e_spread(i32 o) { s.spreadDone = o; }
int  e_infect(i32) { s.infectCalls++; return 1; }

NpcAction2Hooks Hooks() {
    NpcAction2Hooks h{};
    h.queueRequestEntity29 = e_ent29;
    h.packetStatus = e_status;
    h.packetSeq = e_seq;
    h.freeHandlerEntry = e_free;
    h.objectAiPlayerType = e_ai;
    h.objectId = e_oid;
    h.objectSusceptible = e_susc;
    h.resolveObjectId = e_resolve;
    h.personPresent = e_present;
    h.personNearDoor = e_door;
    h.queueSingle49 = e_single;
    h.queueNamedObject53 = e_named;
    h.queuePair33 = e_pair;
    h.requestBuildOp73Str = e_op73;
    h.beginSlotResetPacket = e_slot;
    h.broadcastOutbreak = e_out;
    h.broadcastSpreadDone = e_spread;
    h.plagueInfectNearby = e_infect;
    return h;
}

// One host tick: the prior QueueRequestEntity29(arg) carries the next state; we
// set state := (arg < 0 ? -1 : arg), clear the pending handle, then re-dispatch.
void DriveTick(HeRecord& h) {
    int arg = s.e29.back();
    He_State(&h) = (arg < 0) ? -1 : arg;
    He_ReqHandle(&h) = -1;
    NpcAction2_PlagueSpreadStep(&h);
}

} // namespace

TEST(SimNpcAction2E2E, FullPlagueRoutine) {
    s = E2E{};
    guild::crt::Srand(1);
    GameTime t{}; t.day = 10; t.hour = 9; t.minute = 0; t.second = 0;
    SetNpcClock(t);

    NpcAction2Hooks hk = Hooks();
    SetNpcAction2Hooks(&hk);

    // Scene: object slot 198 is the townsfolk source (type 7); slot 50 is a
    // susceptible spread target. Carrier counter = 2 spread iterations.
    s.aiType[198] = 7;

    HeRecord h{};
    std::memset(&h, 0, sizeof(h));

    // --- tick 1: select target (state 5 path triggers PlagueSelectTarget) ---
    // We call SelectTarget directly here, matching the dispatcher's state-5 entry.
    NpcAction2_PlagueSelectTarget(&h);
    // It chose source object 1198, queued entity29(0), broadcast the outbreak.
    CHECK_EQ(He_PlagueSource(&h), 1198);
    CHECK_EQ(s.outbreak, 1198);
    CHECK_EQ(s.e29.back(), 0);

    // Set up the spread cursor to immediately hit slot 50 in phase 1, and arm 2
    // spread iterations.
    He_TargetObjId(&h) = 50;
    He_ScanStep(&h) = 1;
    s.susceptible[50] = 1;
    He_Counter172(&h) = 2;

    // --- tick 2: phase 0 (refresh member ids, re-arm phase 1) ---
    DriveTick(h);                 // state := 0
    CHECK_EQ(He_State(&h), 0);
    CHECK_EQ(s.e29.back(), 1);    // re-armed phase 1

    // --- tick 3: phase 1 (find target slot 50 -> object 1050, re-arm phase 2) ---
    DriveTick(h);                 // state := 1
    CHECK_EQ(He_PlagueTarget(&h), 1050);
    CHECK_EQ(s.e29.back(), 2);
    CHECK_EQ(s.named53, 4);       // 4 carriers sent to the target

    // --- tick 4: phase 2 (all at door -> infect, counter 2->1, re-arm phase 1) ---
    DriveTick(h);                 // state := 2
    CHECK_EQ(s.infectCalls, 1);
    CHECK_EQ(He_Counter172(&h), 1);
    CHECK_EQ(s.e29.back(), 1);    // back to phase 1

    // --- tick 5: phase 1 again (cursor advanced; slot 50 still susceptible) ---
    DriveTick(h);                 // state := 1
    CHECK_EQ(s.e29.back(), 2);

    // --- tick 6: phase 2 last iteration (counter 1->0 -> send home, re-arm 3) ---
    DriveTick(h);                 // state := 2
    CHECK_EQ(s.infectCalls, 2);
    CHECK_EQ(He_Counter172(&h), 0);
    CHECK_EQ(s.e29.back(), 3);    // re-arm phase 3 (carriers go home)

    // --- tick 7: phase 3 (all home at door -> broadcast spread-done, free) ---
    DriveTick(h);                 // state := 3
    CHECK_EQ(s.spreadDone, 1198); // home object
    CHECK_EQ(s.e29.back(), -1);   // teardown request

    // --- tick 8: teardown (state -1) frees the handler ---
    DriveTick(h);                 // state := -1
    CHECK_EQ(He_State(&h), -1);
    // No further entity29 from teardown (flag 0x02 not set on this record).
    CHECK_EQ(s.e29.back(), -1);
}
