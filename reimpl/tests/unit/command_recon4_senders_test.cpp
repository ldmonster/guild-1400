#include "tests/framework/test.h"
#include "sim/command_recon4_senders.h"

#include <vector>

using namespace guild;
using namespace guild::sim;

// ---------------------------------------------------------------------------
// Recording sender harness: a synthetic resolved record + an emission log so we
// can assert the packet field layout byte-for-byte.
// ---------------------------------------------------------------------------
namespace {

struct Env {
    // resolved record fields
    i16 typeWord = 100;
    i32 entityId = 5555;
    u8  statusKind = 0;
    i32 cmdTo = 4242;
    // table columns keyed by typeWord
    u8  tblKind = 6;
    i32 tblId = 9999;
    // whether resolution succeeds
    bool resolve = true;

    std::vector<Recon4EmitEvent> log;
};

Env* g_e = nullptr;

void* RealResolve() { return g_e->resolve ? (void*)g_e : nullptr; }

void* PersonQueryBegin(i32, i32, i32, i32) { return RealResolve(); }
void* PersonFindRecordById(i32) { return RealResolve(); }
void* MapViewPanel(i32, const void*, const char*) { return RealResolve(); }
void* OfficeOverview(const void*, const char*, i32) { return RealResolve(); }
i16   RecTypeWord(void*) { return g_e->typeWord; }
i32   RecEntityId(void*) { return g_e->entityId; }
u8    RecStatusKind(void*) { return g_e->statusKind; }
u8    TblKind(u16) { return g_e->tblKind; }
i32   TblId(u16) { return g_e->tblId; }
i32   CmdToId(void*) { return g_e->cmdTo; }
void  Emit(const Recon4EmitEvent* ev) { g_e->log.push_back(*ev); }

void install() {
    Recon4SenderHooks h{};
    h.personQueryBegin = &PersonQueryBegin;
    h.personFindRecordById = &PersonFindRecordById;
    h.mapViewPanel = &MapViewPanel;
    h.officeOverview = &OfficeOverview;
    h.recTypeWord = &RecTypeWord;
    h.recEntityId = &RecEntityId;
    h.recStatusKind = &RecStatusKind;
    h.tblKind = &TblKind;
    h.tblId = &TblId;
    h.cmdToId = &CmdToId;
    h.emit = &Emit;
    SetRecon4SenderHooks(&h);
}

bool hasOp(const Env& e, Recon4Emit op) {
    for (auto& ev : e.log) if (ev.op == op) return true;
    return false;
}
const Recon4EmitEvent* find(const Env& e, Recon4Emit op) {
    for (auto& ev : e.log) if (ev.op == op) return &ev;
    return nullptr;
}

} // namespace

TEST(CommandRecon4, RetZeroIsZero) {
    CHECK_EQ(CommandRetZero(), 0);
}

// SendEntityActionA non-6 arm: emits SlotReset28(cmd 118, entId, submsg 375),
// BeginDeltaPacket, AppendDeltaField(4,1,entId,37), State22, and (since tblKind
// 6) a SendEntityMessage(tblId,...1418).
TEST(CommandRecon4, SendAEmitsDiseaseDeltaAndMessage) {
    Env e; g_e = &e; install();
    e.tblKind = 6; e.tblId = 9999; e.entityId = 5555;
    u8 cmd[8] = {0}; cmd[2] = 0; // non-6 person-query arm
    i32 params[4] = {0, 77, 0, 0};
    int rc = SendEntityActionA(cmd, params);
    CHECK_EQ(rc, 1);
    const Recon4EmitEvent* sr = find(e, Recon4Emit::SlotReset28);
    CHECK(sr != nullptr);
    CHECK_EQ(sr->a, 118);          // command byte
    CHECK_EQ(sr->c, 5555);         // entity id
    CHECK_EQ(sr->d, 375);          // submsg
    const Recon4EmitEvent* af = find(e, Recon4Emit::AppendDeltaField);
    CHECK(af != nullptr);
    CHECK_EQ(af->a, 4);            // width
    CHECK_EQ(af->b, 1);            // count
    CHECK_EQ(af->d, 37);          // slot offset
    CHECK(hasOp(e, Recon4Emit::QueueRequestState22));
    const Recon4EmitEvent* sm = find(e, Recon4Emit::SendEntityMessage);
    CHECK(sm != nullptr);
    CHECK_EQ(sm->a, 9999);        // tbl id
    CHECK_EQ(sm->d, 1418);        // message id constant
}

TEST(CommandRecon4, SendANoMessageWhenNotCarried) {
    Env e; g_e = &e; install();
    e.tblKind = 1; // not 6/7 -> no SendEntityMessage
    u8 cmd[8] = {0};
    i32 params[4] = {0, 77, 0, 0};
    CHECK_EQ(SendEntityActionA(cmd, params), 1);
    CHECK(!hasOp(e, Recon4Emit::SendEntityMessage));
}

TEST(CommandRecon4, SendAFailsWhenUnresolved) {
    Env e; g_e = &e; install();
    e.resolve = false;
    u8 cmd[8] = {0};
    i32 params[4] = {0, 77, 0, 0};
    CHECK_EQ(SendEntityActionA(cmd, params), 0);
    CHECK(e.log.empty());
}

// SendEntityActionB emits TWO SlotReset28 (118 then 119) + Args25 + delta.
TEST(CommandRecon4, SendBEmitsTwoBlocksAndArgs25) {
    Env e; g_e = &e; install();
    e.typeWord = 100; e.tblKind = 7; e.tblId = 8888;
    u8 cmd[8] = {0}; cmd[4] = 0;
    i32 params[4] = {0, 55, 0, 0};
    int rc = SendEntityActionB(cmd, params, /*src*/123);
    CHECK_EQ(rc, 1);
    int slotResets = 0, cmd118 = 0, cmd119 = 0;
    for (auto& ev : e.log) if (ev.op == Recon4Emit::SlotReset28) {
        ++slotResets;
        if (ev.a == 118) cmd118 = 1;
        if (ev.a == 119) cmd119 = 1;
    }
    CHECK_EQ(slotResets, 2);
    CHECK_EQ(cmd118, 1);
    CHECK_EQ(cmd119, 1);
    const Recon4EmitEvent* a25 = find(e, Recon4Emit::QueueRequestArgs25);
    CHECK(a25 != nullptr);
    CHECK_EQ(a25->b, 90);
    CHECK_EQ(a25->c, 32);
    CHECK(hasOp(e, Recon4Emit::SendEntityMessage)); // tblKind 7 -> carried msg
}

// SendEntityActionC: SlotReset28 cmd -126 with fields 2/3/2; SendEntityMessage
// only when the command status kind is 6/7.
TEST(CommandRecon4, SendCOfficeSlotResetFields) {
    Env e; g_e = &e; install();
    e.statusKind = 6; e.entityId = 333;
    u8 cmd[8] = {0}; cmd[2] = 6; // office-overview arm
    i32 params[4] = {0, 0, 0, 0};
    e.cmdTo = 333; // office arm uses cmdToId(tgt) for the entity id
    int rc = SendEntityActionC(cmd, params);
    CHECK_EQ(rc, 1);
    const Recon4EmitEvent* sr = find(e, Recon4Emit::SlotReset28);
    CHECK(sr != nullptr);
    CHECK_EQ(sr->a, (int)(signed char)-126);
    CHECK_EQ(sr->b, 2);
    CHECK_EQ(sr->d, 3);
    CHECK_EQ(sr->e, 2);
    CHECK(hasOp(e, Recon4Emit::SendEntityMessage));
}

TEST(CommandRecon4, SendCPersonArmNoPacketJustMaybeMessage) {
    Env e; g_e = &e; install();
    e.statusKind = 1; // not 6/7
    u8 cmd[8] = {0}; cmd[2] = 0; // person FindRecordById arm
    i32 params[4] = {0, 12, 0, 0};
    int rc = SendEntityActionC(cmd, params);
    CHECK_EQ(rc, 1);
    // person arm still emits the SlotReset28 packet (cmd -126).
    CHECK(hasOp(e, Recon4Emit::SlotReset28));
    CHECK(!hasOp(e, Recon4Emit::SendEntityMessage));
}

// SendEntityActionD: office arm emits SlotReset28 (fields 2/0/4); person arm
// emits NO packet, only the carried message.
TEST(CommandRecon4, SendDOfficeArmFields) {
    Env e; g_e = &e; install();
    e.statusKind = 7; e.cmdTo = 444;
    u8 cmd[8] = {0}; cmd[2] = 6;
    i32 params[4] = {0, 0, 0, 0};
    int rc = SendEntityActionD(cmd, params, /*aux*/0);
    CHECK_EQ(rc, 1);
    const Recon4EmitEvent* sr = find(e, Recon4Emit::SlotReset28);
    CHECK(sr != nullptr);
    CHECK_EQ(sr->a, (int)(signed char)-126);
    CHECK_EQ(sr->b, 2);
    CHECK_EQ(sr->d, 0);
    CHECK_EQ(sr->e, 4);
    CHECK(hasOp(e, Recon4Emit::SendEntityMessage));
}

TEST(CommandRecon4, SendDPersonArmNoPacket) {
    Env e; g_e = &e; install();
    e.statusKind = 6; e.entityId = 222;
    u8 cmd[8] = {0}; cmd[2] = 0; // person arm
    i32 params[4] = {0, 9, 0, 0};
    int rc = SendEntityActionD(cmd, params, 0);
    CHECK_EQ(rc, 1);
    CHECK(!hasOp(e, Recon4Emit::SlotReset28)); // person arm emits no packet
    const Recon4EmitEvent* sm = find(e, Recon4Emit::SendEntityMessage);
    CHECK(sm != nullptr);
    CHECK_EQ(sm->a, 222);
}

// SendMapEntityAction: on resolve, emits BeginDeltaPacket + AppendDeltaField
// (width 1, slotOff 453) + State22. value = (col%2 + col>>1).
TEST(CommandRecon4, SendMapDeltaFieldLayout) {
    Env e; g_e = &e; install();
    e.typeWord = 50; e.tblKind = 5 /*col*/; e.tblId = 7000;
    u8 cmd[8] = {0}; cmd[2] = 0; // person query arm
    i32 params[4] = {0, 17, 0, 0};
    int rc = SendMapEntityAction(cmd, params, /*src*/8);
    CHECK_EQ(rc, 1);
    const Recon4EmitEvent* af = find(e, Recon4Emit::AppendDeltaField);
    CHECK(af != nullptr);
    CHECK_EQ(af->a, 1);                 // width 1
    CHECK_EQ(af->d, 453);               // slot offset
    CHECK_EQ(af->c, (5 % 2) + (5 >> 1)); // value = 1 + 2 = 3
    CHECK(hasOp(e, Recon4Emit::QueueRequestState22));
}

TEST(CommandRecon4, SendMapFailsWhenUnresolved) {
    Env e; g_e = &e; install();
    e.resolve = false;
    u8 cmd[8] = {0};
    i32 params[4] = {0, 17, 0, 0};
    CHECK_EQ(SendMapEntityAction(cmd, params, 8), 0);
    CHECK(e.log.empty());
}
