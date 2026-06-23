// Wave-12 hardening — boundary / degenerate-input tests for the action-queue
// coroutine driver (actionqueue.cpp) and the node-pool lifecycle it runs over
// (charaction.cpp pool helpers). These exercise the capacity boundary, the
// dequeue-empty path, and the duration/visibility/motion step handlers at their
// edges. Built under ASAN+UBSAN by the wave-12 cluster harness.
#include "test.h"

#include "sim/actionqueue.h"
#include "sim/charaction.h"
#include "sim/character.h"

#include <string>

using namespace guild;
using namespace guild::sim;

namespace {
// Recording CharAction hooks so FinishSetVisible / RunActionOrFree observe their
// leaf effects without a render bridge.
int   g_animDone = 0;
int   g_setVisibleCalls = 0;
int   g_lastVisible = -99;
void* HAttach(Character*, const char*, int) { return reinterpret_cast<void*>(1); }
int   HAnimDone(Character*) { return g_animDone; }
void  HSetCarried(Character*, int, int) {}
int   HTurn(Character*, ActionNode*) { return 1; }
// Stands in for VIBE_Character_SetVisible @0x401894 (the leaf the original calls);
// FinishSetVisible itself performs no inline visibility write.
void  HSetVisible(Character* ch, int v) { ++g_setVisibleCalls; g_lastVisible = v; if (ch) ch->visible = v; }

CharActionHooks MakeHooks() {
    CharActionHooks h{};
    h.attachAnim = &HAttach;
    h.animDone   = &HAnimDone;
    h.setCarried = &HSetCarried;
    h.turnStep   = &HTurn;
    h.setVisible = &HSetVisible;
    return h;
}
} // namespace

// --- DispatchCurrent: degenerate heads -------------------------------------
TEST(ActionQueueBoundary, DispatchNullHead) {
    Character ch{};
    ch.actions = nullptr;
    CHECK_EQ(DispatchCurrent(&ch), 0);   // no node -> 0
}

TEST(ActionQueueBoundary, DispatchNoOwnerNode) {
    // 0x40477c: cmp dword ptr [edx+14h],0 ; jz -> return 1. [edx+14h] is owner
    // (the decompile's v1[5] under 4-byte stride), not the +8 ready byte.
    Character ch{};
    ActionNode node{};
    node.owner = nullptr;                // owner gate -> bail (return 1)
    node.step  = [](ActionNode*) {};
    ch.actions = &node;
    CHECK_EQ(DispatchCurrent(&ch), 1);
}

TEST(ActionQueueBoundary, DispatchNullStepFn) {
    Character ch{};
    ActionNode node{};
    node.owner = &ch;                    // pass the owner gate (+0x14)
    node.ready = 1;
    node.step  = nullptr;                // [edx]==0 -> bail (return 1)
    ch.actions = &node;
    CHECK_EQ(DispatchCurrent(&ch), 1);
}

TEST(ActionQueueBoundary, DispatchStepReplacedHeadStopsSafely) {
    // If the step fn unlinks/replaces the head, DispatchCurrent must not touch the
    // stale node (memory-safe early return).
    Character ch{};
    static ActionNode node;
    node = ActionNode{};
    node.ready = 1;
    node.owner = &ch;
    node.step = [](ActionNode* n) { n->owner->actions = nullptr; }; // self-unlink
    node.chained = [](ActionNode*) {};   // would run if head were unchanged
    ch.actions = &node;
    CHECK_EQ(DispatchCurrent(&ch), 1);   // head changed -> stop, no chained call
}

// --- CheckDurationExpiry: tick boundary ------------------------------------
TEST(ActionQueueBoundary, DurationExpiryStampsAndExpiresAtBoundary) {
    auto hooks = MakeHooks();
    SetCharActionHooks(&hooks);

    Character ch{};
    static ActionNode node;
    node = ActionNode{};
    node.owner = &ch;
    node.callCount = 0;
    node.args[1] = 5;                    // duration = 5 ticks
    ch.actions = &node;

    g_gameTick = 100;
    // First call stamps start = 100, not yet expired (100+5 not < 100).
    CHECK_EQ(CheckDurationExpiry(&node), 1);
    CHECK_EQ(node.args[2], 100);        // start stamp

    node.callCount = 1;                 // simulate a later dispatch
    g_gameTick = 105;                   // duration+start == gameTick (not <) -> live
    CHECK_EQ(CheckDurationExpiry(&node), 1);

    g_gameTick = 106;                   // 5+100 < 106 -> expire (frees node)
    CHECK_EQ(CheckDurationExpiry(&node), 0);
    CHECK_EQ(ch.actions, nullptr);     // UnlinkEntry cleared the head

    SetCharActionHooks(nullptr);
}

TEST(ActionQueueBoundary, DurationExpiryAbortFlagFrees) {
    auto hooks = MakeHooks();
    SetCharActionHooks(&hooks);

    Character ch{};
    static ActionNode node;
    node = ActionNode{};
    node.owner = &ch;
    node.callCount = 1;
    node.args[1] = 1000000;            // huge duration
    node.args[2] = 0;
    ch.abort = 1;                       // abort -> immediate free
    ch.actions = &node;
    g_gameTick = 1;
    CHECK_EQ(CheckDurationExpiry(&node), 0);
    CHECK_EQ(ch.actions, nullptr);

    SetCharActionHooks(nullptr);
}

// --- FinishSetVisible -------------------------------------------------------
TEST(ActionQueueBoundary, FinishSetVisibleTogglesAndFrees) {
    auto hooks = MakeHooks();
    SetCharActionHooks(&hooks);
    g_setVisibleCalls = 0; g_lastVisible = -99;

    // 0x40b974: if (callCount==0) { SetVisible(owner, args[1]); Unlink(); }
    Character ch{};
    static ActionNode node;
    node = ActionNode{};
    node.owner = &ch;
    node.args[1] = 1;                  // result[12] -> visibility flag
    node.callCount = 0;               // result[3] == 0 -> fire
    ch.actions = &node;
    FinishSetVisible(&node);
    CHECK_EQ(g_setVisibleCalls, 1);
    CHECK_EQ(g_lastVisible, 1);       // SetVisible(owner, args[1])
    CHECK_EQ(ch.visible, 1);          // applied by the SetVisible leaf (hook)
    CHECK_EQ(ch.actions, nullptr);    // unlinked

    // callCount != 0 -> no-op (no SetVisible call, node not unlinked).
    g_setVisibleCalls = 0;
    static ActionNode node2;
    node2 = ActionNode{};
    node2.owner = &ch;
    node2.args[1] = 1;
    node2.callCount = 1;             // result[3] != 0
    ch.actions = &node2;
    FinishSetVisible(&node2);
    CHECK_EQ(g_setVisibleCalls, 0);
    CHECK_EQ(ch.actions, &node2);

    SetCharActionHooks(nullptr);
}

// --- RunActionOrFree: idle / done paths ------------------------------------
TEST(ActionQueueBoundary, RunActionFirstCallNoMotionPersists) {
    auto hooks = MakeHooks();
    SetCharActionHooks(&hooks);

    Character ch{};
    static ActionNode node;
    node = ActionNode{};
    node.owner = &ch;
    node.type = 7;                     // non -1 -> persist
    node.callCount = 0;
    ch.motion = nullptr;
    ch.actions = &node;
    RunActionOrFree(&node);
    CHECK(ch.motion != nullptr);       // attached a motion handle
    CHECK_EQ(ch.actions, &node);       // node persists

    SetCharActionHooks(nullptr);
}

TEST(ActionQueueBoundary, RunActionAnimDoneFrees) {
    auto hooks = MakeHooks();
    SetCharActionHooks(&hooks);
    g_animDone = 1;                    // anim reports finished

    Character ch{};
    static ActionNode node;
    node = ActionNode{};
    node.owner = &ch;
    node.callCount = 1;
    ch.motion = &node;                // has a motion handle
    ch.actions = &node;
    RunActionOrFree(&node);
    CHECK_EQ(ch.motion, nullptr);     // cleared
    CHECK_EQ(ch.actions, nullptr);    // freed

    g_animDone = 0;
    SetCharActionHooks(nullptr);
}

// --- Node-pool lifecycle: capacity boundary + dequeue-empty ----------------
TEST(ActionQueueBoundary, PoolNotReadyReturnsNull) {
    // Before RegisterHandlers the pool is not ready -> GetFreeEntry yields null.
    QueueShutdown();                  // ensure not-ready
    CHECK_EQ(GetFreeEntry(), static_cast<ActionNode*>(nullptr));
}

TEST(ActionQueueBoundary, UnlinkNullIsNoop) {
    CHECK_EQ(UnlinkEntry(nullptr), 0);   // dequeue empty / null -> 0, no crash
}

TEST(ActionQueueBoundary, ClearAllOnEmptyQueue) {
    Character ch{};
    ch.actions = nullptr;
    ClearAll(&ch);                       // must not crash on an empty queue
    CHECK_EQ(ch.actions, nullptr);
}

TEST(ActionQueueBoundary, PoolCapacityExhaustion) {
    auto hooks = MakeHooks();
    SetCharActionHooks(&hooks);
    CHECK_EQ(RegisterHandlers(), 1);     // arms the 1280-node pool

    Character ch{};
    int allocated = 0;
    // Drain the entire pool through the real allocator.
    for (int i = 0; i < kActionNodeCapacity + 8; ++i) {
        ActionNode* n = QueueInsertEntry(&ch);
        if (!n) break;
        ++allocated;
    }
    CHECK_EQ(allocated, kActionNodeCapacity);   // exactly the pool size, no more
    // One more must fail cleanly (exhaustion -> null, no OOB).
    CHECK_EQ(QueueInsertEntry(&ch), static_cast<ActionNode*>(nullptr));

    // Drain back via the head and confirm we can re-allocate (free list works).
    ClearAll(&ch);
    CHECK_EQ(ch.actions, nullptr);
    ActionNode* reuse = QueueInsertEntry(&ch);
    CHECK(reuse != nullptr);

    QueueShutdown();
    SetCharActionHooks(nullptr);
}

// --- InsertActionVararg: oversized argc is clamped (no node-arg overflow) ---
TEST(ActionQueueBoundary, InsertActionVarargClampsArgc) {
    auto hooks = MakeHooks();
    SetCharActionHooks(&hooks);
    RegisterHandlers();

    Character ch{};
    // 200 args >> the 64-slot node arg array; FillActionNode clamps to <=63.
    i32 args[200];
    for (int i = 0; i < 200; ++i) args[i] = i + 1;
    ActionNode* n = InsertActionVararg(&ch, /*type*/0, args, 200);
    CHECK(n != nullptr);                 // built without overflowing args[64]

    QueueShutdown();
    SetCharActionHooks(nullptr);
}

// --- RegisterHandlers catalog golden (gilde.exe 0x40be30) -------------------
// Pins the (type -> ready, argCount) tuples transcribed 1:1 from
// VIBE_CharAction_RegisterHandlers. The argCounts were corrected against the
// decompile: type 51 -> 3 (was 1), type 52 -> 5 (was 0); types 23 and 56 were
// added; type 56 now binds RotateInterpolate (was a placeholder).
TEST(ActionQueueBoundary, RegisterHandlersCatalogGolden) {
    auto hooks = MakeHooks();
    SetCharActionHooks(&hooks);
    CHECK_EQ(RegisterHandlers(), 1);

    // (type, argCount) pairs straight from the binary's DeclareAction calls.
    struct { int type; int argc; } expect[] = {
        {7, 2}, {0, 0}, {23, 0}, {45, 3}, {46, 1}, {47, 0}, {48, 0},
        {49, 0}, {50, 0}, {51, 3}, {53, 1}, {54, 0}, {55, 1}, {56, 1},
        {59, 1}, {52, 5},
    };
    for (auto& e : expect) {
        const ActionTypeDef& def = ActionType(e.type);
        CHECK(def.step != nullptr);          // declared
        CHECK_EQ(static_cast<int>(def.ready), 1);
        CHECK_EQ(def.argCount, e.argc);
    }
    // Type 7 carries the turn animation name; type 45 (walk gait) "bewegung/gehen".
    // 0x40be6a: type 7's animName arg (ecx) is the literal loaded for the preceding
    // SetGrayColorThunk (which preserves ecx), i.e. "bewegung/dreh_90_rechts" — NOT
    // "bewegung/dreh". Golden corrected against the disasm (see harden/sim_02).
    CHECK_EQ(std::string(ActionType(7).animName), std::string("bewegung/dreh_90_rechts"));
    CHECK_EQ(std::string(ActionType(45).animName), std::string("bewegung/gehen"));

    QueueShutdown();
    SetCharActionHooks(nullptr);
}
