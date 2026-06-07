// End-to-end test for the RealSimHooks installer. Builds a small world (entities
// + a character with an action queue + an AI agent), installs the REAL hooks,
// runs several simulation ticks, and verifies real cross-module effects occurred
// with no mocks on the wired paths:
//   * the AI agent emits commands that actually enqueue real packets on the real
//     CommandQueue (via the wired BetCmd -> QueueRequest16 -> EnqueuePacket path);
//   * an entity query resolves against the real Person/Object arrays;
//   * a character's action queue advances real charaction/actionqueue state across
//     ticks until the queued action completes and unlinks itself.
#include "sim/real_hooks.h"

#include "sim/command.h"
#include "sim/entity.h"
#include "sim/character.h"
#include "sim/charaction.h"
#include "sim/actionqueue.h"
#include "sim/npctarget.h"
#include "ai/cardgame.h"

#include "test.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;
using guild::ai::CardGameState;
using guild::ai::TakeTurn;

namespace {

void ResetWorld() {
    ResetEntityArrays();
    ResetCharacters();
}

} // namespace

TEST(SimRealHooksE2E, FullTickFlowCrossModuleEffects) {
    ResetWorld();
    InstallRealSimHooks();
    CommandQueue* q = RealCommandQueue();
    CHECK(q != nullptr);

    // --- build a small world -------------------------------------------------
    // Two persons and one object/building in the REAL entity arrays.
    g_personArrayLoaded = true;
    g_persons[0].marker = 0; g_persons[0].id = 1001; g_personIds[0] = 1001;
    g_persons[1].marker = 0; g_persons[1].id = 1002; g_personIds[1] = 1002;
    g_objects[0].alive = 1;  g_objects[0].id = 5005;

    // A live character with an action queue: enqueue a type-59 wait/duration
    // action lasting 5 ticks. RegisterHandlers (run by InstallRealSimHooks) armed
    // the pool and bound the real CheckDurationExpiry step.
    Character ch{};
    g_characters[0] = &ch;
    g_characterCount = 1;

    g_gameTick = 100;
    i32 durArgs[1] = {5};                 // landed at node->args[1] (duration)
    ActionNode* node = InsertActionVararg(&ch, kActWaitDuration /*59*/, durArgs, 1);
    CHECK(node != nullptr);
    CHECK(ch.actions == node);            // queue head set
    CHECK(node->step != nullptr);         // real step bound via the catalog

    // --- run several sim ticks -----------------------------------------------
    // Tick 1: dispatch stamps the start tick (==100) and keeps the node alive.
    CHECK_EQ(CharacterUpdate(), 1);
    CHECK(ch.actions == node);            // still waiting
    CHECK_EQ(node->args[2], 100);         // start tick stamped by real handler
    CHECK(node->callCount >= 1);

    // Advance the clock past start+duration (100+5) and tick again: the real
    // CheckDurationExpiry unlinks the node, emptying the character's queue.
    g_gameTick = 110;
    CHECK_EQ(CharacterUpdate(), 1);
    CHECK(ch.actions == nullptr);         // action completed -> queue advanced

    // --- AI agent emits real commands across several decisions ---------------
    u32 sendBefore = q->send_count();
    CardGameState st{};
    i32 seatPtr = 7;
    st.seti32(12, seatPtr);               // seat A entity ptr
    st.bytes[36] = 1;                     // decision 1
    st.seti32(0, 9);                      // A stake
    st.seti32(4, 4);                      // B stake

    // A sequence of valid transitions (1 --hold--> 2 --hold--> ... ) each emits a
    // bet command that flows through the real codec onto the real CommandQueue.
    int emitted = 0;
    for (int i = 0; i < 3; ++i) {
        if (TakeTurn(st, seatPtr, 2) == 1)
            ++emitted;
    }
    CHECK(emitted >= 1);
    CHECK_EQ(q->send_count(), sendBefore + (u32)emitted);  // real packets enqueued
    CHECK(q->pending_head() != nullptr);
    // Last enqueued packet is a real opcode-16 request packet.
    CommandPacket& last = q->ring_slot(q->send_count() & 0x7FFF);
    CHECK_EQ((int)last.opcode(), 16);

    // --- entity queries resolve against the real arrays ----------------------
    const NpcTargetHooks& h = GetNpcTargetHooks();
    PersonHandle p = h.findPersonById(1002);          // real PersonFindRecordById
    CHECK(p == static_cast<PersonHandle>(&g_persons[1]));
    CHECK(BuildingFindById(5005) != nullptr);         // real object array hit
    CHECK(h.findPersonById(7777) == nullptr);         // real miss

    // cleanup
    g_characters[0] = nullptr;
    g_characterCount = 0;
}
