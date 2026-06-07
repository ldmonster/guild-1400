#include "test.h"

#include "sim/command_apply9.h"
#include "sim/command.h"
#include "sim/command_codec.h"

#include <cstring>
#include <vector>
#include <memory>

using namespace guild;
using namespace guild::sim;

// E2E: a console-driven command session that exercises the apply9 leaves the
// way the dispatcher does — reset the ring, build a few outbound packets across
// opcodes, classify a flag word, run the Check* guards, and wait for a reply.

namespace {

// --- snapshot + selection + check spies ------------------------------------
i32 e_snap = 0x600DF00D;
i32 e_snap_hook() { return e_snap; }

struct E2ESel {
    bool recOk; i32 entityId; i32 active; u8 flag;
    std::vector<i32> revealIds;
};
E2ESel* g_es = nullptr;
i32 e_parse(const char* s) {
    bool neg = false; if (*s == '+' || *s == '-') { neg = (*s == '-'); ++s; }
    int v = 0; while (*s >= '0' && *s <= '9') v = 10 * v + (*s++ - '0');
    return neg ? -v : v;
}
int e_record(int, int, i32* out) { if (out) *out = g_es->entityId; return g_es->recOk ? 1 : 0; }
i32 e_active() { return g_es->active; }
int e_reveal(int i, i32* out) {
    if (i < 0 || i >= (int)g_es->revealIds.size() || g_es->revealIds[i] < 0) return 0;
    if (out) *out = g_es->revealIds[i];
    return 1;
}
u8 e_flag() { return g_es->flag; }

struct E2ECheck { bool found; u8 flag90; int prereq; };
E2ECheck* g_ec = nullptr;
int e_query(int, int, u8* o) { if (o) *o = g_ec->flag90; return g_ec->found ? 1 : 0; }
int e_canrun(int, int) { return 0; }
int e_prereq(int) { return g_ec->prereq; }

// --- wait spy: a packet arrives after two pumps ----------------------------
struct E2EWait { u32 tick; int pumps; std::vector<CommandPacket>* chain; int arriveAt; };
E2EWait* g_ew = nullptr;
void e_pump() { ++g_ew->pumps; g_ew->tick += 1; }
CommandPacket* e_head() {
    if (g_ew->pumps < g_ew->arriveAt) return nullptr;
    return &(*g_ew->chain)[0];
}
CommandPacket* e_next(CommandPacket* node) {
    auto& c = *g_ew->chain;
    for (size_t i = 0; i + 1 < c.size(); ++i)
        if (&c[i] == node) return &c[i + 1];
    return nullptr;
}
u32 e_tick() { return g_ew->tick; }

} // namespace

TEST(CommandApply9_E2E, ConsoleCommandSession) {
    // 1) Reset the send ring + ACK table.
    auto ringPtr = std::make_unique<RingResetState>(); RingResetState& ring = *ringPtr;
    i32 resetRc = QueueReset(ring);
    CHECK_EQ(resetRc, 327680);
    CHECK_EQ(ring.ack_status(0), (u8)1);
    CHECK_EQ(ring.link_next(0x1FFF), (u32)0);

    // 2) Spin up a live queue and install builder/selection/check hooks.
    CommandQueue q; q.Init();

    BuilderHooks bh{}; bh.op80SnapshotDword = &e_snap_hook; SetBuilderHooks(&bh);

    E2ESel sel{}; sel.recOk = true; sel.entityId = 0xCAFE; sel.active = 9; sel.flag = 0x5C;
    sel.revealIds.assign(768, -1);
    sel.revealIds[10] = 0xA0; sel.revealIds[200] = 0xB0;
    g_es = &sel;
    SelectionHooks sh{};
    sh.parseInt = &e_parse; sh.selectedPersonRecord = &e_record;
    sh.worldActiveCount = &e_active; sh.revealableSlot = &e_reveal; sh.revealFlagByte = &e_flag;
    SetSelectionHooks(&sh);

    E2ECheck chk{}; chk.found = true; chk.flag90 = 0x02; chk.prereq = 1;
    g_ec = &chk;
    CheckHooks ch{};
    ch.personQueryBeginFlag90 = &e_query; ch.officeCanRunFor = &e_canrun; ch.officePrereqMet = &e_prereq;
    SetCheckHooks(&ch);

    // 3) Guard checks before issuing: object flag has bit 2 set -> NOT clear.
    CHECK_EQ(CheckObjectFlagClear(0x10, 0x20), 0);
    // Office prereqs met -> predicate returns 0 (the binary's inverted result).
    CHECK_EQ(CheckOfficePrerequisites(0x4000), 0);

    // 4) Build an Op81 packet with a 44-byte body.
    u8 body[44];
    for (int i = 0; i < 44; ++i) body[i] = static_cast<u8>(i * 3 + 1);
    i32 s81 = RequestBuildOp81(q, 0xDEADBEEF, body);
    CommandPacket& r81 = q.ring_slot(static_cast<u32>(s81));
    CHECK_EQ(r81.opcode(), (u8)81);
    CHECK_EQ(r81.get32(0x10), (u32)0xDEADBEEF);
    CHECK_EQ(r81.get32(0x40), (u32)0x600DF00D);

    // 5) Move a character to the universe by name (opcode 48).
    i32 sMove = RequestChrMoveToUniverse(q, 0x01, 0x02, "Marco", 0x03);
    CommandPacket& rMove = q.ring_slot(static_cast<u32>(sMove));
    CHECK_EQ(rMove.opcode(), (u8)48);
    CHECK_EQ(rMove.bytes[0x1C], (u8)'M');
    CHECK_EQ(rMove.bytes[0x20], (u8)'o');

    // 6) Classify a flag word and emit (opcode 25).
    u32 sendBefore = q.send_count();
    i32 flagRc = EncodeFlagState(q, 0x4242, 0x300 /* -> mask 768 */);
    CHECK_EQ(flagRc, 1);
    CHECK_EQ(q.send_count() - sendBefore, (u32)1);
    CommandPacket& rFlag = q.ring_slot(q.send_count() & kSeqMask);
    CHECK_EQ(rFlag.opcode(), (u8)25);

    // 7) Console "-N" set-selected-flag emits a delta packet.
    DeltaWriter dw;
    i32 selRc = QueueSetSelectedFlag(q, dw, 0x7000, 1, "-3");
    CHECK_EQ(selRc, 1);
    CHECK_EQ(dw.field_count(), (u8)1);

    // 8) Console reveal-all emits one opcode-17 per revealable slot (2 here).
    u32 beforeReveal = q.send_count();
    i32 revRc = QueueRevealAllPersons(q, "-reveal");
    CHECK_EQ(revRc, 1);
    CHECK_EQ(q.send_count() - beforeReveal, (u32)2);

    // 9) Block until an opcode-7 reply arrives after two pumps.
    std::vector<CommandPacket> reply(1);
    reply[0].opcode() = 7;
    E2EWait w{}; w.tick = 1000; w.pumps = 0; w.chain = &reply; w.arriveAt = 2;
    g_ew = &w;
    WaitHooks wh{}; wh.pump = &e_pump; wh.receivedHead = &e_head; wh.nextNode = &e_next; wh.gameTick = &e_tick;
    SetWaitHooks(&wh);
    CommandPacket* got = WaitForPacketType(7, 1000);
    CHECK(got == &reply[0]);
    CHECK(w.pumps >= 2);

    // cleanup
    SetBuilderHooks(nullptr); SetSelectionHooks(nullptr);
    SetCheckHooks(nullptr); SetWaitHooks(nullptr);
}
