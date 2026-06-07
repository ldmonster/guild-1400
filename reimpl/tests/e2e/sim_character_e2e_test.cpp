// End-to-end test for the per-actor action system: give a character a small
// action script (turn -> load-anim ("walk") -> wait -> drop), then step the
// simulation tick-by-tick via Character_Update and verify the sequence executes
// in order with the correct per-tick state.
#include "sim/character.h"
#include "sim/charaction.h"
#include "sim/actionqueue.h"

#include "tests/framework/test.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {

// Hooks that make each animation last a fixed number of ticks so we can observe
// the coroutine advancing one action at a time.
struct E2EState {
    int animTicksRemaining = 0;
    int turnTicksRemaining = 0;
    int carried = 0;
};
E2EState g_e2e;

void* E2EAttach(Character*, const char* name, int) {
    // "walk"/load-anim and take/drop attach: 2-tick animations.
    g_e2e.animTicksRemaining = 2;
    (void)name;
    return reinterpret_cast<void*>(1);
}
int E2EAnimDone(Character*) {
    if (g_e2e.animTicksRemaining > 0) --g_e2e.animTicksRemaining;
    return g_e2e.animTicksRemaining == 0 ? 1 : 0;
}
void E2ESetCarried(Character*, int, int carried) { g_e2e.carried = carried; }
int E2ETurnStep(Character*, ActionNode*) {
    if (g_e2e.turnTicksRemaining > 0) --g_e2e.turnTicksRemaining;
    return g_e2e.turnTicksRemaining == 0 ? 1 : 0;
}
void E2ESetVisible(Character*, int) {}

const CharActionHooks kE2EHooks = {
    E2EAttach, E2EAnimDone, E2ESetCarried, E2ETurnStep, E2ESetVisible,
};

} // namespace

TEST(SimCharacterE2E, TurnWalkWaitDropScript) {
    RegisterHandlers();
    ResetCharacters();
    g_gameTick = 0;
    g_e2e = E2EState{};
    g_e2e.turnTicksRemaining = 2;     // the turn takes 2 advance steps
    SetCharActionHooks(&kE2EHooks);

    Character ch;
    std::memset(&ch, 0, sizeof(ch));
    ch.carried = 1;                   // starts carrying something (bone 0 -> 1)
    g_characters[0] = &ch;
    g_characterCount = 1;

    // Build the script in order: turn (7), walk/load-anim (54), wait (59),
    // drop (50). Each InsertActionVararg appends at the tail, so queue order is
    // exactly the insertion order.
    i32 turnArgs[2] = { 90, 90 };
    ActionNode* nTurn = InsertActionVararg(&ch, kActTurnAnim, turnArgs, 2);
    ActionNode* nWalk = InsertActionVararg(&ch, kActLoadAnim, nullptr, 0);
    i32 waitArgs[1] = { 3 };          // wait 3 ticks
    ActionNode* nWait = InsertActionVararg(&ch, kActWaitDuration, waitArgs, 1);
    i32 dropArgs[3] = { 0, 0, (2 << 24) };
    ActionNode* nDrop = InsertActionVararg(&ch, kActDropObject, dropArgs, 3);
    nDrop->args[2] = (2 << 24);       // bone 2 (see take/drop note in unit test)

    CHECK(nTurn && nWalk && nWait && nDrop);
    CHECK_EQ(ch.actions, nTurn);
    CHECK_EQ(nTurn->next_link, nWalk);
    CHECK_EQ(nWalk->next_link, nWait);
    CHECK_EQ(nWait->next_link, nDrop);

    // --- Phase 1: TURN (attach on tick 0, then 2 advance steps) -------------
    g_gameTick = 1;
    CharacterUpdate();                          // tick: attach turn anim, latch
    CHECK_EQ(ch.actions, nTurn);
    CHECK_EQ(ch.turnTarget, 90);

    g_gameTick = 2;
    CharacterUpdate();                          // advance 1/2
    CHECK_EQ(ch.actions, nTurn);

    g_gameTick = 3;
    CharacterUpdate();                          // advance 2/2 -> turn done
    CHECK_EQ(ch.actions, nWalk);                // advanced to walk

    // --- Phase 2: WALK (load-anim; 2-tick animation) ------------------------
    g_gameTick = 4;
    CharacterUpdate();                          // attach walk anim
    CHECK_EQ(ch.actions, nWalk);
    CHECK(ch.motion != nullptr);                // motion handle latched

    g_gameTick = 5;
    CharacterUpdate();                          // anim tick 1 (not done)
    CHECK_EQ(ch.actions, nWalk);

    g_gameTick = 6;
    CharacterUpdate();                          // anim tick 2 -> done, free
    CHECK_EQ(ch.actions, nWait);                // advanced to wait
    CHECK(ch.motion == nullptr);

    // --- Phase 3: WAIT (duration 3) -----------------------------------------
    // nWalk completed on the tick-6 update and advanced to nWait WITHOUT
    // dispatching it that tick (DispatchCurrent runs the head once per call), so
    // nWait's first dispatch — which stamps its start tick — is on tick 7.
    g_gameTick = 7;
    CharacterUpdate();                           // wait first dispatch: stamp 7
    CHECK_EQ(nWait->args[2], 7);                 // start tick stamped
    CHECK_EQ(ch.actions, nWait);
    g_gameTick = 8; CharacterUpdate(); CHECK_EQ(ch.actions, nWait);  // 3+7<8 false
    g_gameTick = 9; CharacterUpdate(); CHECK_EQ(ch.actions, nWait);  // 3+7<9 false
    g_gameTick = 10; CharacterUpdate(); CHECK_EQ(ch.actions, nWait); // 3+7<10 false
    g_gameTick = 11; CharacterUpdate();          // 3+7 < 11 -> expires
    CHECK_EQ(ch.actions, nDrop);                 // advanced to drop

    // --- Phase 4: DROP (attach, then drop carried item) ---------------------
    CHECK_EQ(ch.carried, 1);                     // still carrying before drop
    g_gameTick = 12;
    CharacterUpdate();                           // attach drop anim
    CHECK_EQ(ch.actions, nDrop);

    g_gameTick = 13;
    CharacterUpdate();                           // anim tick 1
    CHECK_EQ(ch.actions, nDrop);

    g_gameTick = 14;
    CharacterUpdate();                           // anim tick 2 -> drop completes
    CHECK_EQ(ch.carried, 0);                     // carried flag cleared
    CHECK_EQ(g_e2e.carried, 0);
    CHECK(ch.actions == nullptr);                // script fully drained
}
