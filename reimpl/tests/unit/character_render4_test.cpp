// Unit tests for character_render4 — the genuinely-untranslated builders
// (CreateSoundActionEx, CreateTakeObjectAction[Alt], CreateDropObjectAction[Alt]) and
// the SitDown/GetUp/SitDownAtOnce/CharacterCount script handlers.
//
// The builders this TU OWNS are driven against a recording mock of the
// queueInsertEntry/unlinkEntry hooks (a stack ActionNode), so the field arithmetic,
// type bytes, step-kind binding and the verbatim stride-2 name copy are asserted
// exactly. The Cmd* handlers delegate to the REAL charaction_misc builders
// (CreatePlaySampleAction / CreateSampleLoopAction), which use the genuine node pool —
// so those tests bring up the real pool via RegisterHandlers().
#include "test.h"

#include "sim/character_render4.h"
#include "sim/charaction.h"     // RegisterHandlers / QueueShutdown / QueueInsertEntry
#include "sim/character.h"

#include <cstring>
#include <vector>
#include <string>

using namespace guild;
using namespace guild::sim;

namespace {
// A recording mock node pool for the OWNED builders.
struct MockPool {
    ActionNode  node{};
    bool        gaveNode = true;
    int         inserts  = 0;
    int         unlinks  = 0;
};
MockPool* g_pool = nullptr;

ActionNode* mockInsert(Character*) {
    if (!g_pool || !g_pool->gaveNode) return nullptr;
    g_pool->inserts++;
    g_pool->node = ActionNode{};
    return &g_pool->node;
}
int mockUnlink(ActionNode*) { if (g_pool) g_pool->unlinks++; return 1; }

void installMock(MockPool& p) {
    g_pool = &p;
    static CharRender4Hooks h;
    h.queueInsertEntry = mockInsert;
    h.unlinkEntry      = mockUnlink;
    SetCharRender4Hooks(&h);
}
void uninstall() { SetCharRender4Hooks(nullptr); g_pool = nullptr; }
}  // namespace

// ---------------------------------------------------------------------------
// CopyNamePairwise — strcpy-equivalent for normal strings; returns length+1.
// ---------------------------------------------------------------------------
TEST(CharRender4Copy, MatchesStrcpyForNormalStrings) {
    const char* cases[] = {"", "a", "ab", "abc", "bewegung/gehen", "Normal_s"};
    for (const char* s : cases) {
        char buf[64];
        std::memset(buf, 0x7f, sizeof(buf));
        int n = CopyNamePairwise(buf, s);
        CHECK_EQ(std::strcmp(buf, s), 0);
        CHECK_EQ(n, (int)std::strlen(s) + 1);
    }
}

// Interior null on an EVEN index stops immediately (even byte is the guard).
TEST(CharRender4Copy, StopsAtEvenIndexNull) {
    char src[8] = {'x', 'y', 0, 'z', 0};   // null at index 2 (even)
    char buf[16];
    std::memset(buf, 0x7f, sizeof(buf));
    CopyNamePairwise(buf, src);
    CHECK_EQ(buf[0], 'x');
    CHECK_EQ(buf[1], 'y');
    CHECK_EQ(buf[2], 0);
}

// ---------------------------------------------------------------------------
// CreateSoundActionEx (type 46) — does NOT clear callCount; writes explicit speed.
// ---------------------------------------------------------------------------
TEST(CharRender4Sound, ExWritesSpeedAndKeepsType) {
    MockPool p; installMock(p);
    Character ch{};
    float speed = 2.5f;
    int bits; std::memcpy(&bits, &speed, sizeof(bits));
    ActionNode* n = CreateSoundActionEx(&ch, "snd", 9, bits);
    CHECK(n == &p.node);
    if (n) {
        CHECK_EQ((int)n->type, (int)kTypeSound);
        CHECK(StepKindOf(n) == ActionStepKind::kSound);
        CHECK_EQ(n->speedScale, 2.5f);
        CHECK_EQ(n->args[1], 9);
        CHECK_EQ(std::strcmp(n->animBuf, "snd"), 0);
    }
    uninstall();
}

TEST(CharRender4Sound, ExExhaustedPoolReturnsNull) {
    MockPool p; p.gaveNode = false; installMock(p);
    Character ch{};
    CHECK(CreateSoundActionEx(&ch, "x", 1, 0) == nullptr);
    CHECK_EQ(p.inserts, 0);
    uninstall();
}

// ---------------------------------------------------------------------------
// Take object (type 49) — success copies a second name; missing model unlinks.
// ---------------------------------------------------------------------------
TEST(CharRender4Take, SuccessSetsHandAndSecondName) {
    MockPool p; installMock(p);
    Character ch{};
    int r = CreateTakeObjectAction(&ch, "take", "apple", 2);
    CHECK_EQ(r, 1);
    CHECK_EQ((int)p.node.type, (int)kTypeTakeObject);
    CHECK(StepKindOf(&p.node) == ActionStepKind::kTakeObject);
    CHECK_EQ(p.node.args[3], 2);         // hand (normal)
    CHECK_EQ(p.node.args[2], 0);         // node+52
    CHECK(p.node.owner == &ch);
    CHECK_EQ(std::strcmp(p.node.animBuf, "take"), 0);
    CHECK_EQ(p.unlinks, 0);
    uninstall();
}

TEST(CharRender4Take, AltUsesLeftHand) {
    MockPool p; installMock(p);
    Character ch{};
    CHECK_EQ(CreateTakeObjectAction(&ch, "take", "apple", 1), 1);   // Alt == hand 1
    CHECK_EQ(p.node.args[3], 1);
    uninstall();
}

TEST(CharRender4Take, NoModelUnlinksAndFails) {
    MockPool p; installMock(p);
    Character ch{};
    int r = CreateTakeObjectAction(&ch, "take", nullptr, 1);  // null srcObjName
    CHECK_EQ(r, 0);
    CHECK_EQ(p.unlinks, 1);
    CHECK_EQ(p.node.args[3], 1);         // hand set before the unlink decision
    uninstall();
}

// ---------------------------------------------------------------------------
// Drop object (type 50) — null names write a single terminator; no unlink path.
// ---------------------------------------------------------------------------
TEST(CharRender4Drop, NullNamesWriteEmpty) {
    MockPool p; installMock(p);
    Character ch{};
    int r = CreateDropObjectAction(&ch, 0x77, nullptr, nullptr, 2);
    CHECK_EQ(r, 1);
    CHECK_EQ((int)p.node.type, (int)kTypeDropObject);
    CHECK(StepKindOf(&p.node) == ActionStepKind::kDropObject);
    CHECK_EQ(p.node.args[1], 0x77);      // node+48 param
    CHECK_EQ(p.node.args[3], 2);         // hand
    CHECK_EQ(p.node.animBuf[0], 0);      // node+240 = 0
    CHECK_EQ(p.unlinks, 0);
    uninstall();
}

TEST(CharRender4Drop, BothNamesCopied) {
    MockPool p; installMock(p);
    Character ch{};
    CreateDropObjectAction(&ch, 1, "drop", "coin", 1);
    CHECK_EQ(std::strcmp(p.node.animBuf, "drop"), 0);
    CHECK_EQ(p.node.args[3], 1);         // Alt == hand 1
    uninstall();
}

// ---------------------------------------------------------------------------
// CmdSitDown / CmdGetUp / CmdSitDownAtOnce — delegate to the REAL pool builders.
// ---------------------------------------------------------------------------
namespace { std::vector<std::string>* g_errs = nullptr;
void recErr(const char* m) { if (g_errs) g_errs->emplace_back(m ? m : ""); } }

TEST(CharRender4Cmd, InvalidActorReportsAndReturnsOne) {
    std::vector<std::string> errs; g_errs = &errs;
    CmdScriptCtx c{};
    c.actor = nullptr; c.reportError = recErr;
    bool latched = true, created = true;
    int r = CmdSitDown(c, &latched, &created);
    CHECK_EQ(r, 1);
    CHECK(!latched);
    CHECK(!created);
    CHECK_EQ((int)errs.size(), 1);
    if (!errs.empty()) CHECK_EQ(std::strcmp(errs[0].c_str(), "SitDown(): Invalid character"), 0);
    g_errs = nullptr;
}

TEST(CharRender4Cmd, ReentryWithPendingActionRelatches) {
    Character ch{};
    CmdScriptCtx c{};
    c.hasExecCmd = true; c.execStepIsSelf = true; c.actionHead = true; c.actor = &ch;
    bool latched = false, created = true;
    int r = CmdSitDown(c, &latched, &created);
    CHECK_EQ(r, 0);
    CHECK(latched);              // re-latched
    CHECK(!created);             // did NOT create a new action
}

TEST(CharRender4Cmd, ValidActorCreatesAndLatchesOnByte) {
    CHECK(RegisterHandlers() != 0);     // bring up the real node pool
    Character ch{};
    CmdScriptCtx c{};
    c.actor = &ch; c.sampleName = "sit_s"; c.chainLatchByte = 1;
    bool latched = false, created = false;
    int r = CmdSitDown(c, &latched, &created);
    CHECK_EQ(r, 0);
    CHECK(created);             // a real type-47 node was enqueued
    CHECK(latched);            // chainLatchByte == 1
    CHECK(ch.actions != nullptr);
    if (ch.actions) CHECK_EQ((int)ch.actions->type, (int)47);
    QueueShutdown();
}

TEST(CharRender4Cmd, GetUpUsesSampleLoopAndNeedsSitting) {
    CHECK(RegisterHandlers() != 0);
    Character ch{};
    ch.flagsA = kSittingFlag;            // CreateSampleLoopAction only fires when sitting
    CmdScriptCtx c{};
    c.actor = &ch; c.sampleName = "stand_s"; c.chainLatchByte = 0;
    bool latched = true, created = false;
    int r = CmdGetUp(c, &latched, &created);
    CHECK_EQ(r, 0);
    CHECK(created);             // sample-loop node enqueued (type 48)
    CHECK(!latched);            // byte != 1 and not a re-entry
    if (ch.actions) CHECK_EQ((int)ch.actions->type, (int)48);
    QueueShutdown();
}

TEST(CharRender4Cmd, GetUpNotSittingCreatesNothing) {
    CHECK(RegisterHandlers() != 0);
    Character ch{};
    ch.flagsA = 0;                       // not sitting -> CreateSampleLoopAction no-ops
    CmdScriptCtx c{};
    c.actor = &ch; c.sampleName = "stand_s"; c.chainLatchByte = 0;
    bool latched = false, created = true;
    int r = CmdGetUp(c, &latched, &created);
    CHECK_EQ(r, 0);
    CHECK(!created);            // sample-loop builder returned null (not sitting)
    CHECK(ch.actions == nullptr);
    QueueShutdown();
}

TEST(CharRender4Cmd, SitDownAtOnceSetsSeekFlag) {
    CHECK(RegisterHandlers() != 0);
    Character ch{};
    CmdScriptCtx c{};
    c.actor = &ch; c.sampleName = "sit_s"; c.chainLatchByte = 0;
    bool latched = false, created = false, seek = false;
    int r = CmdSitDownAtOnce(c, &latched, &created, &seek);
    CHECK_EQ(r, 0);
    CHECK(created);
    CHECK(seek);
    if (ch.actions) CHECK_EQ((int)(ch.actions->seekFlag & 1), 1);   // node+396 |= 1
    QueueShutdown();
}

// ---------------------------------------------------------------------------
// CmdCharacterCount — counts table slots whose universe matches the active tag.
// ---------------------------------------------------------------------------
TEST(CharRender4Count, CountsMatchingUniverse) {
    int active = 0;            // the active-scene tag (off_649D64)
    int other  = 0;
    Character a{}, b{}, c{};
    a.universe = &active; b.universe = &other; c.universe = &active;
    Character* table[5] = {&a, nullptr, &b, &c, nullptr};
    CHECK_EQ(CmdCharacterCount(table, 5, &active), 2);
    CHECK_EQ(CmdCharacterCount(table, 5, &other), 1);
    CHECK_EQ(CmdCharacterCount(table, 0, &active), 0);
}
