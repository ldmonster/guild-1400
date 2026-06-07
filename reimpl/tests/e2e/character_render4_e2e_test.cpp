// e2e: a sit-down -> stand-up flow plus a take/drop/sound sequence, exercised across
// the character_render4 builders + Cmd handlers on the REAL action-queue pool
// (RegisterHandlers / QueueInsertEntry / UnlinkEntry). The OWNED builders' hooks are
// forwarded into the genuine pool, so the whole flow threads one real intrusive queue.
#include "test.h"

#include "sim/character_render4.h"
#include "sim/charaction.h"
#include "sim/character.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {
ActionNode* realInsert(Character* ch) { return guild::sim::QueueInsertEntry(ch); }
int         realUnlink(ActionNode* n) { return guild::sim::UnlinkEntry(n); }

void wireReal() {
    static CharRender4Hooks h;
    h.queueInsertEntry = realInsert;
    h.unlinkEntry      = realUnlink;
    SetCharRender4Hooks(&h);
}
int queueLen(Character* ch) {
    int n = 0;
    for (ActionNode* p = ch->actions; p; p = p->next_link) ++n;
    return n;
}
}  // namespace

TEST(CharRender4E2E, SitThenReentryThenGetUp) {
    CHECK(RegisterHandlers() != 0);
    wireReal();
    Character ch{};
    ch.flagsA = kSittingFlag;            // seated for the get-up path

    // Turn 1: SitDown on a valid actor, chain-latch armed -> enqueues a type-47 node.
    CmdScriptCtx sit{};
    sit.actor = &ch; sit.sampleName = "sit_s"; sit.chainLatchByte = 1;
    bool latched = false, created = false;
    CHECK_EQ(CmdSitDown(sit, &latched, &created), 0);
    CHECK(created);
    CHECK(latched);
    CHECK_EQ(queueLen(&ch), 1);
    if (ch.actions) {
        CHECK_EQ((int)ch.actions->type, (int)47);
        CHECK_EQ(std::strcmp(ch.actions->animBuf, "sit_s"), 0);
    }

    // Turn 2: same command re-enters with a pending action -> re-latch, no new node.
    CmdScriptCtx reentry{};
    reentry.hasExecCmd = true; reentry.execStepIsSelf = true; reentry.actionHead = true;
    reentry.actor = &ch;
    bool relatched = false, recreated = true;
    CHECK_EQ(CmdSitDown(reentry, &relatched, &recreated), 0);
    CHECK(relatched);
    CHECK(!recreated);
    CHECK_EQ(queueLen(&ch), 1);          // no new node

    // Turn 3: GetUp builds the stand-up sample-loop action (type 48).
    CmdScriptCtx getup{};
    getup.actor = &ch; getup.sampleName = "stand_s"; getup.chainLatchByte = 0;
    bool gl = true, gc = false;
    CHECK_EQ(CmdGetUp(getup, &gl, &gc), 0);
    CHECK(gc);
    CHECK(!gl);
    CHECK_EQ(queueLen(&ch), 2);
    QueueShutdown();
}

// A take-then-drop object flow plus a sound-ex action, asserting the full sequence on
// the real queue (and that the no-model take unlinks correctly).
TEST(CharRender4E2E, TakeDropSoundSequence) {
    CHECK(RegisterHandlers() != 0);
    wireReal();
    Character ch{};

    CHECK_EQ(CreateTakeObjectAction(&ch, "take", "apple", 2), 1);
    CHECK_EQ(CreateDropObjectAction(&ch, 5, "drop", "apple", 1), 1);
    ActionNode* snd = CreateSoundActionEx(&ch, "fanfare", 3, 0);

    CHECK(snd != nullptr);
    CHECK_EQ(queueLen(&ch), 3);
    // A failed (no-model) take must NOT grow the queue (real UnlinkEntry detaches it).
    CHECK_EQ(CreateTakeObjectAction(&ch, "bad", nullptr, 2), 0);
    CHECK_EQ(queueLen(&ch), 3);

    // Walk the queue and confirm the recovered type sequence 49,50,46.
    int types[3] = {0, 0, 0};
    int i = 0;
    for (ActionNode* p = ch.actions; p && i < 3; p = p->next_link) types[i++] = (int)p->type;
    CHECK_EQ(types[0], (int)kTypeTakeObject);
    CHECK_EQ(types[1], (int)kTypeDropObject);
    CHECK_EQ(types[2], (int)kTypeSound);
    QueueShutdown();
}

// The active-scene actor count over a populated table.
TEST(CharRender4E2E, CharacterCountOverTable) {
    int active = 0;
    Character chars[6];
    Character* table[7];
    int expect = 0;
    for (int i = 0; i < 6; ++i) {
        chars[i] = Character{};
        chars[i].universe = (i % 2 == 0) ? (void*)&active : nullptr;
        if (chars[i].universe == &active) ++expect;
        table[i] = &chars[i];
    }
    table[6] = nullptr;
    CHECK_EQ(CmdCharacterCount(table, 7, &active), expect);
}
