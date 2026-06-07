// Unit tests for the per-actor action system (charaction / actionqueue /
// character). Exercises the node pool lifecycle, the coroutine dispatch, the
// duration/wait handler, the turn handler, and take/drop carried-flag toggling.
#include "sim/character.h"
#include "sim/charaction.h"
#include "sim/actionqueue.h"

#include "tests/framework/test.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {

// Recording mock hooks: turnStep finishes after N calls; animDone finishes after
// M calls; carried toggles are recorded.
struct MockState {
    int turnStepCalls = 0;
    int turnStepsToFinish = 1;
    int animDoneCalls = 0;
    int animDoneToFinish = 1;
    int lastBone = -1;
    int lastCarried = -1;
    int attachCalls = 0;
};
MockState g_mock;

void* MockAttach(Character*, const char*, int) { ++g_mock.attachCalls; return reinterpret_cast<void*>(1); }
int   MockAnimDone(Character*) { return (++g_mock.animDoneCalls >= g_mock.animDoneToFinish) ? 1 : 0; }
void  MockSetCarried(Character*, int bone, int carried) { g_mock.lastBone = bone; g_mock.lastCarried = carried; }
int   MockTurnStep(Character*, ActionNode*) { return (++g_mock.turnStepCalls >= g_mock.turnStepsToFinish) ? 1 : 0; }
void  MockSetVisible(Character*, int) {}

const CharActionHooks kMockHooks = {
    MockAttach, MockAnimDone, MockSetCarried, MockTurnStep, MockSetVisible,
};

// Build a fresh world: register the catalog, reset clock/array/mock, install hooks.
Character MakeCharacter() {
    Character c;
    std::memset(&c, 0, sizeof(c));
    return c;
}

void Setup() {
    RegisterHandlers();
    ResetCharacters();
    g_gameTick = 0;
    g_mock = MockState{};
    SetCharActionHooks(&kMockHooks);
}

} // namespace

// --- pool lifecycle --------------------------------------------------------

TEST(SimCharActionPool, GetFreeAndUnlink) {
    Setup();
    Character ch = MakeCharacter();

    ActionNode* a = QueueInsertEntry(&ch);
    CHECK(a != nullptr);
    CHECK_EQ(a->owner, &ch);
    CHECK_EQ(ch.actions, a);          // becomes the head

    ActionNode* b = QueueInsertEntry(&ch);
    CHECK(b != nullptr);
    CHECK(b != a);
    CHECK_EQ(ch.actions, a);          // still the head
    CHECK_EQ(a->next_link, b);        // b appended at tail
    CHECK_EQ(b->prev, a);

    // Unlink the head: b is promoted.
    CHECK_EQ(UnlinkEntry(a), 1);
    CHECK_EQ(ch.actions, b);
    CHECK(b->prev == nullptr);

    CHECK_EQ(UnlinkEntry(b), 1);
    CHECK(ch.actions == nullptr);

    CHECK_EQ(UnlinkEntry(nullptr), 0); // null is a no-op returning 0
}

TEST(SimCharActionPool, InsertVarargFillsRegistry) {
    Setup();
    Character ch = MakeCharacter();
    i32 args[2] = { 42, 7 };
    ActionNode* n = InsertActionVararg(&ch, kActWaitDuration, args, 1);
    CHECK(n != nullptr);
    CHECK_EQ((int)n->type, (int)kActWaitDuration);
    CHECK(n->step != nullptr);        // step pulled from the catalog
    // The vararg copy lands the first arg at +48 (args[1]) — see FillActionNode.
    CHECK_EQ(n->args[1], 42);         // duration arg copied (argCount==1)

    // Unknown type coerces to 0 (RunActionOrFree base action).
    ActionNode* m = InsertActionVararg(&ch, 60, nullptr, 0);
    CHECK(m != nullptr);
    CHECK_EQ((int)m->type, 0);
}

// --- duration / wait handler ----------------------------------------------

TEST(SimCharActionWait, ExpiresAfterDuration) {
    Setup();
    Character ch = MakeCharacter();
    g_characters[0] = &ch;
    g_characterCount = 1;

    i32 dur[1] = { 5 };               // duration 5 ticks
    ActionNode* n = InsertActionVararg(&ch, kActWaitDuration, dur, 1);
    CHECK(n != nullptr);

    // Tick 0: first dispatch stamps the start tick; node persists.
    CharacterUpdate();
    CHECK(ch.actions != nullptr);
    CHECK_EQ(n->args[2], 0);          // start tick stamped at g_gameTick==0

    // Ticks 1..5: still within duration (5 + 0 < tick is false until tick 6).
    for (g_gameTick = 1; g_gameTick <= 5; ++g_gameTick) {
        CharacterUpdate();
        CHECK(ch.actions != nullptr);
    }
    // Tick 6: 5 + 0 < 6 -> expires and frees.
    g_gameTick = 6;
    CharacterUpdate();
    CHECK(ch.actions == nullptr);
}

TEST(SimCharActionWait, AbortFlagFinishesImmediately) {
    Setup();
    Character ch = MakeCharacter();
    i32 dur[1] = { 100 };
    ActionNode* n = InsertActionVararg(&ch, kActWaitDuration, dur, 1);
    CHECK(n != nullptr);
    CheckDurationExpiry(n);           // stamp
    ch.abort = 1;                     // force finish
    CHECK_EQ(CheckDurationExpiry(n), 0);
    CHECK(ch.actions == nullptr);
}

// --- turn handler ----------------------------------------------------------

TEST(SimCharActionTurn, RotatesPerTickToTarget) {
    Setup();
    Character ch = MakeCharacter();
    g_characters[0] = &ch;
    g_characterCount = 1;
    g_mock.turnStepsToFinish = 3;     // completes after 3 advance steps

    i32 ta[2] = { 90, 90 };
    ActionNode* n = InsertActionVararg(&ch, kActTurnAnim, ta, 2);
    CHECK(n != nullptr);

    // Tick 0: first call attaches the anim, latches the target, no step yet.
    CharacterUpdate();
    CHECK(ch.actions != nullptr);
    CHECK_EQ(ch.turnTarget, 90);
    CHECK_EQ(g_mock.attachCalls, 1);
    CHECK_EQ(g_mock.turnStepCalls, 0);

    // Next dispatches advance one quantum each.
    CharacterUpdate(); CHECK_EQ(g_mock.turnStepCalls, 1); CHECK(ch.actions != nullptr);
    CharacterUpdate(); CHECK_EQ(g_mock.turnStepCalls, 2); CHECK(ch.actions != nullptr);
    CharacterUpdate(); CHECK_EQ(g_mock.turnStepCalls, 3); // reaches target -> frees
    CHECK(ch.actions == nullptr);
}

// --- take / drop carried flag ---------------------------------------------

TEST(SimCharActionTakeDrop, TogglesCarriedFlag) {
    Setup();
    Character ch = MakeCharacter();
    g_characters[0] = &ch;
    g_characterCount = 1;
    g_mock.animDoneToFinish = 2;      // anim completes on the 2nd poll

    // bone id 5 encoded in HIBYTE of args[2].
    i32 takeArgs[3] = { 0, 0, (5 << 24) };
    ActionNode* n = InsertActionVararg(&ch, kActTakeObject, takeArgs, 3);
    // type 49 has argCount 0 in the catalog, so args are not auto-copied; set
    // the encoded bone directly on the node (matches the original which reads
    // the node's +53 byte filled by the take-action builder).
    n->args[2] = (5 << 24);
    CHECK(n != nullptr);

    CharacterUpdate();                // tick 0: attach take anim
    CHECK_EQ(ch.carried, 0);
    CHECK(ch.actions != nullptr);
    CharacterUpdate();                // animDone poll 1 -> not done
    CHECK_EQ(ch.carried, 0);
    CHECK(ch.actions != nullptr);
    CharacterUpdate();                // animDone poll 2 -> take completes
    CHECK_EQ(ch.carried, 6);          // bone 5 -> carried == bone+1
    CHECK_EQ(g_mock.lastCarried, 1);
    CHECK(ch.actions == nullptr);

    // Now drop it.
    g_mock = MockState{};
    g_mock.animDoneToFinish = 1;
    SetCharActionHooks(&kMockHooks);
    i32 dropArgs[3] = { 0, 0, (5 << 24) };
    ActionNode* d = InsertActionVararg(&ch, kActDropObject, dropArgs, 3);
    d->args[2] = (5 << 24);
    CHECK(d != nullptr);
    CharacterUpdate();                // attach drop anim
    CharacterUpdate();                // animDone -> drop completes
    CHECK_EQ(ch.carried, 0);
    CHECK_EQ(g_mock.lastCarried, 0);
    CHECK(ch.actions == nullptr);
}

// --- completion frees node + advances to next ------------------------------

TEST(SimCharActionAdvance, CompletionAdvancesToNextAction) {
    Setup();
    Character ch = MakeCharacter();
    g_characters[0] = &ch;
    g_characterCount = 1;
    g_mock.turnStepsToFinish = 1;     // turn completes after 1 step

    // Queue: turn (type 7) then wait (type 59).
    i32 ta[2] = { 45, 45 };
    ActionNode* t = InsertActionVararg(&ch, kActTurnAnim, ta, 2);
    i32 dur[1] = { 2 };
    ActionNode* w = InsertActionVararg(&ch, kActWaitDuration, dur, 1);
    CHECK_EQ(ch.actions, t);
    CHECK_EQ(t->next_link, w);

    CharacterUpdate();                // turn tick 0: attach, latch
    CHECK_EQ(ch.actions, t);
    CharacterUpdate();                // turn tick 1: completes, frees t
    CHECK_EQ(ch.actions, w);          // advanced to the wait action
    CHECK_EQ(w->args[2], (i32)g_gameTick); // wait stamps its start on first run
}

// --- Character_Update walks the live array ---------------------------------

TEST(SimCharacterUpdate, WalksLiveArrayAndSkips) {
    Setup();
    Character a = MakeCharacter();
    Character b = MakeCharacter();
    b.flagsA = 0x04;                  // skip-update flag
    g_characters[3] = &a;
    g_characters[9] = &b;
    g_characterCount = 2;

    g_mock.turnStepsToFinish = 1;
    i32 ta[2] = { 10, 10 };
    InsertActionVararg(&a, kActTurnAnim, ta, 2);
    InsertActionVararg(&b, kActTurnAnim, ta, 2);

    CHECK_EQ(CharacterUpdate(), 1);   // ran (count > 0)
    // a was dispatched (attach happened); b was skipped.
    CHECK_EQ(g_mock.attachCalls, 1);
    CHECK(b.actions != nullptr);      // b's action untouched

    // Zero live count -> Update bails.
    g_characterCount = 0;
    CHECK_EQ(CharacterUpdate(), 0);
}
