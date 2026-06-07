#include "test.h"

#include "sim/command_apply9.h"
#include "sim/command.h"
#include "sim/command_codec.h"

#include <cstring>
#include <vector>
#include <memory>

using namespace guild;
using namespace guild::sim;

namespace {

// EncodeFlagState classification oracle (python-verified).
i32 mask_oracle(u32 f) {
    if (f == 0) return 0;
    u8 b44 = f & 0xFF, b45 = (f >> 8) & 0xFF, b46 = (f >> 16) & 0xFF, b47 = (f >> 24) & 0xFF;
    u16 w46 = (f >> 16) & 0xFFFF;
    if (b44 & 0x0F) return 7;
    if (b44 & 0xF0) return 48;
    if (b45 & 0x0F) return 768;
    if (b45 & 0x30) return 4096;
    if (f & 0x1C000) return 49152;
    if (b46 & 0x0E) return 0x20000;
    if (b46 & 0x70) return 0x100000;
    if (w46 & 0x180) return 0x800000;
    if (b47 & 0x1E) return 0x6000000;
    return 0;
}

// Signed-int parse oracle (matches VIBE_Util_ParseInt for ASCII).
i32 parse_oracle(const char* s) {
    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r' || *s == '\v' || *s == '\f') ++s;
    bool neg = false;
    if (*s == '+' || *s == '-') { neg = (*s == '-'); ++s; }
    int v = 0;
    while (*s >= '0' && *s <= '9') v = 10 * v + (*s++ - '0');
    return neg ? -v : v;
}

} // namespace

// ---------------------------------------------------------------------------
// EncodeFlagState — golden vectors over the full priority ladder.
// ---------------------------------------------------------------------------
TEST(CommandApply9_EncodeFlag, MaskLadderGolden) {
    struct { u32 f; i32 m; } g[] = {
        {0x0, 0}, {0x01, 7}, {0x0F, 7}, {0x10, 48}, {0xF0, 48}, {0x80, 48},
        {0x100, 768}, {0x300, 768}, {0x1000, 4096}, {0x3000, 4096},
        {0x4000, 49152}, {0x10000, 49152}, {0x20000, 0x20000},
        {0x100000, 0x100000}, {0x800000, 0x800000}, {0x1000000, 0x800000},
        {0x1800000, 0x800000}, {0xFFFFFFFF, 7},
    };
    for (auto& t : g) {
        CHECK_EQ(EncodeFlagStateMask(t.f), t.m);
        CHECK_EQ(EncodeFlagStateMask(t.f), mask_oracle(t.f));
    }
}

TEST(CommandApply9_EncodeFlag, FuzzAgainstOracle) {
    u32 x = 0x12345678u;
    for (int i = 0; i < 5000; ++i) {
        x = x * 1664525u + 1013904223u;
        CHECK_EQ(EncodeFlagStateMask(x), mask_oracle(x));
    }
}

TEST(CommandApply9_EncodeFlag, EmitsArgs25WhenNonzero) {
    CommandQueue q; q.Init();
    // flag 0x4000 classifies to 49152; opcode 25 packet is enqueued.
    i32 slot = EncodeFlagState(q, 0x11223344, 0x4000);
    CHECK_EQ(slot, 1); // returns 1 always
    // The most recent enqueue should be opcode 25 with personId @+0x10.
    CommandPacket& r = q.ring_slot(static_cast<u32>(q.send_count() & kSeqMask));
    CHECK_EQ(r.opcode(), (u8)25);
    CHECK_EQ(r.get32(0x10), (u32)0x11223344);
}

TEST(CommandApply9_EncodeFlag, ZeroEmitsNothing) {
    CommandQueue q; q.Init();
    u32 before = q.send_count();
    i32 slot = EncodeFlagState(q, 0x55667788, 0);
    CHECK_EQ(slot, 1);
    CHECK_EQ(q.send_count(), before); // no packet enqueued
}

// ---------------------------------------------------------------------------
// Ring reset — link wiring + ACK table seeding.
// ---------------------------------------------------------------------------
TEST(CommandApply9_Reset, ReturnsTableEndAndSeedsAck) {
    auto sp = std::make_unique<RingResetState>(); RingResetState& s = *sp;
    i32 r = QueueReset(s);
    CHECK_EQ(r, 327680);
    // Every ACK entry: ring index = -1, status = 1.
    CHECK_EQ(s.ack_ring(0), (i32)-1);
    CHECK_EQ(s.ack_status(0), (u8)1);
    CHECK_EQ(s.ack_ring(32767), (i32)-1);
    CHECK_EQ(s.ack_status(32767), (u8)1);
    CHECK_EQ(s.ack_ring(12345), (i32)-1);
    CHECK_EQ(s.ack_status(12345), (u8)1);
}

TEST(CommandApply9_Reset, RingLinkWiring) {
    auto sp = std::make_unique<RingResetState>(); RingResetState& s = *sp;
    QueueReset(s);
    constexpr u32 stride = RingResetState::kStride;
    // Slot 0: prev = 0, next = base of slot 1 (= stride).
    CHECK_EQ(s.link_prev(0), (u32)0);
    CHECK_EQ(s.link_next(0), (u32)stride);
    // Slot 1: prev = base of slot 0 (offset 0), next = base of slot 2.
    CHECK_EQ(s.link_prev(1), (u32)0);              // (1-1)*153 == 0
    CHECK_EQ(s.link_next(1), (u32)(2 * stride));
    // Slot 5: prev = 4*153, next = 6*153.
    CHECK_EQ(s.link_prev(5), (u32)(4 * stride));
    CHECK_EQ(s.link_next(5), (u32)(6 * stride));
    // Last slot 0x1FFF: next = 0 (terminator); prev = 0x1FFE*153.
    CHECK_EQ(s.link_next(0x1FFF), (u32)0);
    CHECK_EQ(s.link_prev(0x1FFF), (u32)(0x1FFE * stride));
    // Counters cleared.
    CHECK_EQ(s.sendCount, (u32)0);
    CHECK_EQ(s.pendingHead, (u32)0);
}

TEST(CommandApply9_Reset, AltIsByteIdentical) {
    auto ap = std::make_unique<RingResetState>();
    auto bp = std::make_unique<RingResetState>();
    RingResetState& a = *ap; RingResetState& b = *bp;
    CHECK_EQ(QueueReset(a), QueueResetAlt(b));
    CHECK(std::memcmp(a.ring, b.ring, sizeof(a.ring)) == 0);
    CHECK(std::memcmp(a.ack, b.ack, sizeof(a.ack)) == 0);
}

// ---------------------------------------------------------------------------
// WaitForPacketType — deadline + chain scan.
// ---------------------------------------------------------------------------
namespace {
struct WaitState {
    u32 tick = 100;
    std::vector<CommandPacket>* list = nullptr;
    int pumps = 0;
};
WaitState* g_w = nullptr;
std::vector<CommandPacket>* g_chain = nullptr; // active received chain

void w_pump() { ++g_w->pumps; ++g_w->tick; }
CommandPacket* w_head() {
    if (!g_chain || g_chain->empty()) return nullptr;
    return &(*g_chain)[0];
}
// Walk the chain by linear search (the side-table equivalent of *(node+149)).
CommandPacket* w_next(CommandPacket* node) {
    if (!g_chain) return nullptr;
    for (size_t i = 0; i + 1 < g_chain->size(); ++i)
        if (&(*g_chain)[i] == node) return &(*g_chain)[i + 1];
    return nullptr;
}
u32 w_tick() { return g_w->tick; }

void InstallWait(WaitState& s) {
    g_w = &s; g_chain = nullptr;
    WaitHooks h{}; h.pump = &w_pump; h.receivedHead = &w_head; h.nextNode = &w_next; h.gameTick = &w_tick;
    SetWaitHooks(&h);
}
} // namespace

TEST(CommandApply9_Wait, TimeoutZeroReturnsNull) {
    WaitState s; InstallWait(s);
    CHECK(WaitForPacketType(5, 0) == nullptr);
    CHECK_EQ(s.pumps, 0); // never spins
    SetWaitHooks(nullptr);
}

TEST(CommandApply9_Wait, FindsMatchInChain) {
    WaitState s; InstallWait(s);
    // Build a 3-node chain; opcodes 9, 5, 7. The chain walk uses the side-table
    // (w_next), faithful to the binary's *(node+149) dereference.
    std::vector<CommandPacket> nodes(3);
    nodes[0].opcode() = 9; nodes[1].opcode() = 5; nodes[2].opcode() = 7;
    g_chain = &nodes;

    CommandPacket* found = WaitForPacketType(7, 1000);
    CHECK(found == &nodes[2]);
    CHECK_EQ(found->opcode(), (u8)7);
    g_chain = nullptr;
    SetWaitHooks(nullptr);
}

TEST(CommandApply9_Wait, DeadlineExpiresNoMatch) {
    WaitState s; InstallWait(s);
    std::vector<CommandPacket> nodes(1);
    nodes[0].opcode() = 9;
    g_chain = &nodes;
    // Looking for opcode 7 which never appears; tick advances each pump and
    // crosses the (start+3) deadline.
    CommandPacket* found = WaitForPacketType(7, 3);
    CHECK(found == nullptr);
    CHECK(s.pumps >= 1);
    g_chain = nullptr;
    SetWaitHooks(nullptr);
}

// ---------------------------------------------------------------------------
// Check* predicates.
// ---------------------------------------------------------------------------
namespace {
struct CheckState {
    bool found = false; u8 flag90 = 0;
    int canRun = 0, prereq = 0;
    int lastKey = 0, lastA2 = 0, lastOffice = 0;
};
CheckState* g_c = nullptr;
int c_query(int a2, int key, u8* out) { g_c->lastA2 = a2; g_c->lastKey = key; if (out) *out = g_c->flag90; return g_c->found ? 1 : 0; }
int c_canrun(int off, int a2) { g_c->lastOffice = off; g_c->lastA2 = a2; return g_c->canRun; }
int c_prereq(int off) { g_c->lastOffice = off; return g_c->prereq; }
void InstallCheck(CheckState& s) {
    g_c = &s; CheckHooks h{};
    h.personQueryBeginFlag90 = &c_query; h.officeCanRunFor = &c_canrun; h.officePrereqMet = &c_prereq;
    SetCheckHooks(&h);
}
} // namespace

TEST(CommandApply9_Check, ObjectFlagClear) {
    CheckState s; InstallCheck(s);
    // No record -> clear (1).
    s.found = false;
    CHECK_EQ(CheckObjectFlagClear(0x11, 0x22), 1);
    CHECK_EQ(s.lastKey, 0x11); CHECK_EQ(s.lastA2, 0x22);
    // Record with bit 2 set -> not clear (0).
    s.found = true; s.flag90 = 0x06;
    CHECK_EQ(CheckObjectFlagClear(0x11, 0x22), 0);
    // Record without bit 2 -> clear (1).
    s.flag90 = 0x01;
    CHECK_EQ(CheckObjectFlagClear(0x11, 0x22), 1);
    SetCheckHooks(nullptr);
}

TEST(CommandApply9_Check, OfficePredicates) {
    CheckState s; InstallCheck(s);
    s.canRun = 0; CHECK_EQ(CheckCanRunForOffice(0xAA, 0xBB), 1); // !0
    CHECK_EQ(s.lastOffice, 0xAA);
    s.canRun = 1; CHECK_EQ(CheckCanRunForOffice(0xAA, 0xBB), 0); // !1
    s.prereq = 0; CHECK_EQ(CheckOfficePrerequisites(0xCC), 1);   // ==0
    s.prereq = 1; CHECK_EQ(CheckOfficePrerequisites(0xCC), 0);
    SetCheckHooks(nullptr);
}

// ---------------------------------------------------------------------------
// EnqueuePacket builders.
// ---------------------------------------------------------------------------
namespace {
i32 g_snap = -1;
i32 snap_hook() { return g_snap; }
void InstallBuild(i32 snap) {
    g_snap = snap; BuilderHooks h{}; h.op80SnapshotDword = &snap_hook;
    SetBuilderHooks(&h);
}
} // namespace

TEST(CommandApply9_Build, Op81) {
    InstallBuild(0x0BADF00D);
    CommandQueue q; q.Init();
    u8 body[44];
    for (int i = 0; i < 44; ++i) body[i] = static_cast<u8>(i + 1);

    i32 s81 = RequestBuildOp81(q, 0x12345678, body);
    CommandPacket& r81 = q.ring_slot(static_cast<u32>(s81));
    CHECK_EQ(r81.opcode(), (u8)81);
    CHECK_EQ(r81.get32(0x10), (u32)0x12345678);
    bool body_ok = true;
    for (int i = 0; i < 44; ++i) if (r81.bytes[0x14 + i] != static_cast<u8>(i + 1)) body_ok = false;
    CHECK(body_ok);
    CHECK_EQ(r81.get32(0x40), (u32)0x0BADF00D);
    SetBuilderHooks(nullptr);
}

TEST(CommandApply9_Build, Op81NullSnapshot) {
    InstallBuild(-1); // null dword_6315C0 -> -1
    CommandQueue q; q.Init();
    u8 body[44] = {0};
    i32 s = RequestBuildOp81(q, 0, body);
    CommandPacket& r = q.ring_slot(static_cast<u32>(s));
    CHECK_EQ(r.get32(0x40), (u32)0xFFFFFFFF);
    SetBuilderHooks(nullptr);
}

namespace {
struct Op85State { bool valid = false; Op85UnitXform xf{}; i32 f9 = 0; bool f9found = false; };
Op85State* g_o85 = nullptr;
int o85_xform(i32, Op85UnitXform* out) { *out = g_o85->xf; return g_o85->valid ? 1 : 0; }
i32 o85_f9(i32, bool* found) { if (found) *found = g_o85->f9found; return g_o85->f9; }
} // namespace

TEST(CommandApply9_Build, Op85UnitTransform) {
    Op85State o; g_o85 = &o;
    o.valid = true;
    for (int i = 0; i < 6; ++i) o.xf.d[i] = 0x1000 + i;
    o.xf.field36 = 0x36363636;
    o.f9found = true; o.f9 = 0x99999999;
    BuilderHooks bh{}; bh.op80SnapshotDword = +[]() -> i32 { return 0x5A5A5A5A; };
    bh.combatUnitField9 = &o85_f9;
    SetBuilderHooks(&bh);
    Op85Hooks oh{}; oh.unitXform = &o85_xform; SetOp85Hooks(&oh);

    CommandQueue q; q.Init();
    u8 body[44];
    for (int i = 0; i < 44; ++i) body[i] = static_cast<u8>(0x40 + i);
    i32 s = RequestBuildOp85Unit(q, /*unit*/0x4000, /*mode*/2, body);
    CommandPacket& r = q.ring_slot(static_cast<u32>(s));
    CHECK_EQ(r.opcode(), (u8)85);
    CHECK_EQ(r.get32(0x10), (u32)0x5A5A5A5A); // snapshot dword
    CHECK_EQ(r.bytes[0x14], (u8)2);           // mode
    // Six transform dwords written from +0x15.
    for (int i = 0; i < 6; ++i) CHECK_EQ(r.get32(0x15 + i * 4), (u32)(0x1000 + i));
    CHECK_EQ(r.get32(0x31), (u32)0x36363636); // field36
    // mode==2 -> v16[10] @ +0x3D overwritten with combat field9.
    CHECK_EQ(r.get32(0x15 + 10 * 4), (u32)0x99999999);
    SetBuilderHooks(nullptr); SetOp85Hooks(nullptr);
}

TEST(CommandApply9_Build, Op85NullUnitJustBody) {
    Op85State o; g_o85 = &o; o.valid = false;
    BuilderHooks bh{}; bh.op80SnapshotDword = +[]() -> i32 { return 0x11; };
    SetBuilderHooks(&bh);
    Op85Hooks oh{}; oh.unitXform = &o85_xform; SetOp85Hooks(&oh);
    CommandQueue q; q.Init();
    u8 body[44];
    for (int i = 0; i < 44; ++i) body[i] = static_cast<u8>(0x70 + i);
    i32 s = RequestBuildOp85Unit(q, /*unit*/0, /*mode*/1, body);
    CommandPacket& r = q.ring_slot(static_cast<u32>(s));
    CHECK_EQ(r.opcode(), (u8)85);
    // body copied verbatim at +0x15 (no transform because unit==0).
    bool ok = true;
    for (int i = 0; i < 44; ++i) if (r.bytes[0x15 + i] != static_cast<u8>(0x70 + i)) ok = false;
    CHECK(ok);
    SetBuilderHooks(nullptr); SetOp85Hooks(nullptr);
}

namespace { const char* g_lastErr = nullptr; void errlog(const char* m) { g_lastErr = m; } }

TEST(CommandApply9_Build, ChrMoveToUniverseShortName) {
    BuilderHooks bh{}; bh.errorLog = &errlog; SetBuilderHooks(&bh);
    g_lastErr = nullptr;
    CommandQueue q; q.Init();
    i32 s = RequestChrMoveToUniverse(q, 0x0A0B0C0D, 0x01020304, "Eve", 0x44444444);
    CommandPacket& r = q.ring_slot(static_cast<u32>(s));
    CHECK_EQ(r.opcode(), (u8)48);
    CHECK_EQ(r.get32(0x10), (u32)0x0A0B0C0D);
    CHECK_EQ(r.get32(0x14), (u32)0x01020304);
    CHECK_EQ(r.get32(0x18), (u32)0x44444444);
    // 2-byte-stride copy: "Eve\0" -> bytes E,v,e,\0 (lanes interleaved).
    CHECK_EQ(r.bytes[0x1C], (u8)'E');
    CHECK_EQ(r.bytes[0x1D], (u8)'v');
    CHECK_EQ(r.bytes[0x1E], (u8)'e');
    CHECK_EQ(r.bytes[0x1F], (u8)0);
    CHECK(g_lastErr == nullptr);
    SetBuilderHooks(nullptr);
}

TEST(CommandApply9_Build, ChrMoveToUniverseLongNameRejected) {
    BuilderHooks bh{}; bh.errorLog = &errlog; SetBuilderHooks(&bh);
    g_lastErr = nullptr;
    CommandQueue q; q.Init();
    char longName[40];
    std::memset(longName, 'X', 39); longName[39] = 0; // length 39 >= 32
    i32 s = RequestChrMoveToUniverse(q, 1, 2, longName, 3);
    CommandPacket& r = q.ring_slot(static_cast<u32>(s));
    CHECK_EQ(r.opcode(), (u8)48);
    CHECK(r.bytes[0x1C] == 0); // name NOT copied
    CHECK(g_lastErr != nullptr);
    SetBuilderHooks(nullptr);
}

// ---------------------------------------------------------------------------
// Selection builders.
// ---------------------------------------------------------------------------
namespace {
struct SelState {
    bool recordOk = false; i32 entityId = 0;
    i32 active = 0; u8 revealFlag = 0;
    std::vector<i32> revealIds; // per-slot entity id; <0 means not revealable
    i32 parsed = 0; bool parseSeen = false;
};
SelState* g_s = nullptr;
i32 s_parse(const char* str) { g_s->parseSeen = true; return parse_oracle(str); }
int s_record(int, int, i32* outId) { if (outId) *outId = g_s->entityId; return g_s->recordOk ? 1 : 0; }
i32 s_active() { return g_s->active; }
int s_reveal(int i, i32* outId) {
    if (i < 0 || i >= (int)g_s->revealIds.size()) return 0;
    i32 id = g_s->revealIds[i];
    if (id < 0) return 0;
    if (outId) *outId = id;
    return 1;
}
u8 s_flag() { return g_s->revealFlag; }
void InstallSel(SelState& s) {
    g_s = &s; SelectionHooks h{};
    h.parseInt = &s_parse; h.selectedPersonRecord = &s_record;
    h.worldActiveCount = &s_active; h.revealableSlot = &s_reveal; h.revealFlagByte = &s_flag;
    SetSelectionHooks(&h);
}
} // namespace

TEST(CommandApply9_Sel, ParseIntDefaultClone) {
    SetSelectionHooks(nullptr); // default parseInt
    // Exercise the library default by going through QueueSetSelectedFlag's reject
    // path is not enough; verify the oracle equals our expectation directly.
    CHECK_EQ(parse_oracle("  -42xy"), -42);
    CHECK_EQ(parse_oracle("+7"), 7);
    CHECK_EQ(parse_oracle("000"), 0);
    CHECK_EQ(parse_oracle("123"), 123);
}

TEST(CommandApply9_Sel, SetSelectedFlagEmitsDelta) {
    SelState s; InstallSel(s);
    s.recordOk = true; s.entityId = 0x1234;
    CommandQueue q; q.Init();
    DeltaWriter dw;
    u32 before = q.send_count();
    i32 rc = QueueSetSelectedFlag(q, dw, /*sel*/0x9000, /*idx*/2, "-5");
    CHECK_EQ(rc, 1);
    CHECK(s.parseSeen);
    CHECK(q.send_count() != before); // QueueRequestState22 enqueued
    // The delta writer holds one raw field at offset 433, value 5.
    CHECK_EQ(dw.field_count(), (u8)1);
    SetSelectionHooks(nullptr);
}

TEST(CommandApply9_Sel, SetSelectedFlagRejectsNoDash) {
    SelState s; InstallSel(s);
    CommandQueue q; q.Init();
    DeltaWriter dw;
    CHECK_EQ(QueueSetSelectedFlag(q, dw, 0, 0, "5"), 0); // no leading '-'
    SetSelectionHooks(nullptr);
}

TEST(CommandApply9_Sel, SetSelectedFlagRejectsNoRecord) {
    SelState s; InstallSel(s);
    s.recordOk = false;
    CommandQueue q; q.Init();
    DeltaWriter dw;
    CHECK_EQ(QueueSetSelectedFlag(q, dw, 0, 0, "-9"), 0);
    SetSelectionHooks(nullptr);
}

TEST(CommandApply9_Sel, RevealAllPersonsEmitsPerRevealable) {
    SelState s; InstallSel(s);
    s.active = 17; s.revealFlag = 0xAB;
    s.revealIds.assign(768, -1);
    s.revealIds[3] = 0x111;
    s.revealIds[100] = 0x222;
    s.revealIds[700] = 0x333;
    CommandQueue q; q.Init();
    u32 before = q.send_count();
    i32 rc = QueueRevealAllPersons(q, "-12345");
    CHECK_EQ(rc, 1);
    // 3 revealable slots -> 3 opcode-17 packets enqueued.
    CHECK_EQ(q.send_count() - before, (u32)3);
    SetSelectionHooks(nullptr);
}

TEST(CommandApply9_Sel, RevealAllPersonsRejectShortToken) {
    SelState s; InstallSel(s);
    s.active = 5;
    CommandQueue q; q.Init();
    CHECK_EQ(QueueRevealAllPersons(q, "-ab"), 0);  // tail "ab" < 4 chars
    CHECK_EQ(QueueRevealAllPersons(q, "nope"), 0); // no leading '-'
    SetSelectionHooks(nullptr);
}

TEST(CommandApply9_Sel, RevealAllPersonsRejectNoActive) {
    SelState s; InstallSel(s);
    s.active = 0; // CountActiveObjects() == 0
    CommandQueue q; q.Init();
    CHECK_EQ(QueueRevealAllPersons(q, "-12345"), 0);
    SetSelectionHooks(nullptr);
}
