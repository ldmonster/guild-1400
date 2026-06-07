// Integration test: the character_render4 builders wired against the REAL reconstructed
// action-queue sibling (guild::sim::QueueInsertEntry @0x40c15c / UnlinkEntry @0x404370 /
// RegisterHandlers @0x40be30 in src/sim/charaction.cpp) — NOT a mock. We forward the
// module's queueInsertEntry / unlinkEntry hooks straight into the genuine pool allocator
// and unlinker, exactly as the live game wires them, then assert the cross-module flow:
//   * each builder grabs a real free node from the shared 1280-node pool;
//   * QueueInsertEntry threads the node into the actor's REAL intrusive queue
//     (ch->actions head, owner, next_link tail-append);
//   * the take-object no-model path calls the REAL UnlinkEntry, which zeroes the slot
//     and detaches it from the actor's queue.
#include "test.h"

#include "sim/character_render4.h"
#include "sim/charaction.h"      // REAL QueueInsertEntry / UnlinkEntry / RegisterHandlers
#include "sim/character.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {
// The hook forwarders — exactly the live wiring: delegate to the genuine sibling.
ActionNode* realInsert(Character* ch) { return guild::sim::QueueInsertEntry(ch); }
int         realUnlink(ActionNode* n) { return guild::sim::UnlinkEntry(n); }

void wireReal() {
    static CharRender4Hooks h;
    h.queueInsertEntry  = realInsert;   // REAL pool allocator
    h.unlinkEntry       = realUnlink;   // REAL unlinker
    SetCharRender4Hooks(&h);
}

// Count the nodes in an actor's real intrusive queue (head at +296, +40 next_link).
int queueLen(Character* ch) {
    int n = 0;
    for (ActionNode* p = ch->actions; p; p = p->next_link) ++n;
    return n;
}
}  // namespace

TEST(CharRender4Itest, OwnedBuildersThreadRealQueue) {
    CHECK(guild::sim::RegisterHandlers() != 0);   // allocate the real node pool
    wireReal();
    Character ch{};

    // Build three actions through THIS module's OWNED builders; each must land on the
    // REAL intrusive queue via the genuine QueueInsertEntry the hook forwards into.
    ActionNode* snd  = CreateSoundActionEx(&ch, "fanfare", 0x10, 0);
    int         take = CreateTakeObjectAction(&ch, "take", "apple", 2);
    int         drop = CreateDropObjectAction(&ch, 5, "drop", "apple", 1);

    CHECK(snd != nullptr);
    CHECK_EQ(take, 1);
    CHECK_EQ(drop, 1);
    CHECK_EQ(queueLen(&ch), 3);          // genuine intrusive queue formed by the sibling

    // The first node is the sound one; the sibling set owner + the module the type/name.
    if (snd) {
        CHECK(snd->owner == &ch);        // QueueInsertEntry reserved the slot for ch
        CHECK_EQ((int)snd->type, (int)kTypeSound);
        CHECK_EQ(std::strcmp(snd->animBuf, "fanfare"), 0);
        CHECK(StepKindOf(snd) == ActionStepKind::kSound);
    }

    guild::sim::QueueShutdown();      // release the pool (idempotent for the suite)
}

TEST(CharRender4Itest, TakeNoModelUnlinksFromRealQueue) {
    CHECK(guild::sim::RegisterHandlers() != 0);
    wireReal();
    Character ch{};

    // A take with a present model lands on the queue.
    CHECK_EQ(CreateTakeObjectAction(&ch, "take", "apple", 2), 1);
    CHECK_EQ(queueLen(&ch), 1);

    // A take with NO model must route through the REAL UnlinkEntry and detach.
    int r = CreateTakeObjectAction(&ch, "take2", /*srcObjName=*/nullptr, 1);
    CHECK_EQ(r, 0);
    CHECK_EQ(queueLen(&ch), 1);          // the failed node was unlinked by the sibling

    guild::sim::QueueShutdown();
}
