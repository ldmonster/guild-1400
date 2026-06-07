// Unit tests for the third-wave RealSimHooks installer (src/sim/real_hooks3.cpp).
// After InstallRealSimHooks3(), the NPC/AI/interaction leaf hooks that earlier
// waves left inert must invoke the REAL reconstructed targets: an NPC action that
// previously hit a mock now emits a REAL command packet via the real builder onto
// the real CommandQueue, and a freeHandlerEntry call hits the real He pool.
#include "sim/real_hooks3.h"

#include "sim/real_hooks.h"        // RealCommandQueue() — the shared real queue
#include "sim/real_hooks2.h"       // InstallRealSimHooks2()
#include "sim/command.h"
#include "sim/handler_entry.h"     // HandlerTable / HandlerRecord
#include "sim/he.h"                // HeRecord (full 514B view) + accessors
#include "sim/npcaction.h"         // NpcLeafHooks / step fns
#include "sim/npcaction2.h"        // NpcAction2Hooks
#include "sim/npcaction3.h"        // NpcAction3Hooks
#include "sim/npcaction4.h"        // NpcAction4Hooks
#include "sim/combat.h"            // ICombatCommandSink / CombatCommandSink

#include "test.h"

#include <cstring>

using namespace guild;
using namespace guild::sim;

namespace {

// A full-size handler/He record buffer the step functions can write all their
// far fields into (he.h's HeRecord is larger than the pool stride).
struct HeBuf { HeRecord rec; };

// Register a no-op handler type into the real He pool and alloc one entry for it.
// Returns the pool record (a real HandlerRecord slot), or null on failure.
HandlerRecord* AllocRealHandler(HandlerTable& he, u8 kind) {
    he.RegisterHandlerByType(kind, [](HandlerRecord*) {}, [](HandlerRecord*) -> i32 { return 0; });
    // Descriptor: he.h HeRecord, +4 kind byte, +8 personId (-1 == no person).
    HeBuf d{};
    std::memset(&d, 0, sizeof(d));
    u8* a1 = reinterpret_cast<u8*>(&d.rec);
    a1[4] = kind;
    *reinterpret_cast<i32*>(a1 + 8) = -1;   // personId = -1 (no person resolve)
    return he.AllocHandlerEntry(&d.rec);
}

} // namespace

// --- NpcLeafHooks now drive the real entity-29 builder + real He pool ---------
TEST(SimRealHooks3, IdleWaitStateEmitsRealEntity29Packet) {
    InstallRealSimHooks();
    InstallRealSimHooks2();
    InstallRealSimHooks3();

    CommandQueue* q = RealCommandQueue();
    CHECK(q != nullptr);
    const NpcLeafHooks& h = GetNpcLeafHooks();
    // The command-builder / He-pool / packet-status fields are now REAL (non-null).
    CHECK(h.queueRequestEntity29 != nullptr);
    CHECK(h.freeHandlerEntry != nullptr);
    CHECK(h.packetStatus != nullptr);
    CHECK(h.requestBuildOp93 != nullptr);
    // The no-real-target fields stay inert (null).
    CHECK(h.lawBaseTextId == nullptr);
    CHECK(h.findInventorySlot == nullptr);
    CHECK(h.shuffleDwords == nullptr);

    u32 before = q->send_count();

    // BeginIdleWaitState (flag 0x04 clear) -> queueRequestEntity29(0,h) -> the REAL
    // QueueRequestEntity29 builder -> EnqueuePacket on the REAL CommandQueue.
    HeBuf b{};
    std::memset(&b, 0, sizeof(b));
    He_Flags(&b.rec) = 0;            // not already-spawned
    He_Id(&b.rec) = 4242;           // entity id snapshot into the packet
    i32 handle = NpcAction_BeginIdleWaitState(&b.rec);

    // A real opcode-29 packet was enqueued (send_count advanced, pending node set).
    CHECK_EQ(q->send_count(), before + 1);
    CHECK(q->pending_head() != nullptr);
    CommandPacket& slot = q->ring_slot((before + 1) & 0x7FFF);
    CHECK_EQ((int)slot.opcode(), 29);
    // The builder snapshotted the He id (+4) into the packet payload at +0x10.
    CHECK_EQ((int)slot.get32(0x10), 4242);
    // The returned handle was stored back into +132 and gates via the real ACK
    // table (status 0 == pending, freshly enqueued).
    CHECK_EQ(He_ReqHandle(&b.rec), handle);
    CHECK_EQ(h.packetStatus(handle), 0);
}

// --- freeHandlerEntry now hits the real He pool -------------------------------
TEST(SimRealHooks3, FreeHandlerEntryHitsRealHePool) {
    InstallRealSimHooks3();
    HandlerTable* he = RealHandlerTable();
    CHECK(he != nullptr);
    he->Init();   // start from a clean pool

    HandlerRecord* r = AllocRealHandler(*he, /*kind=*/3);
    CHECK(r != nullptr);
    CHECK_EQ(he->live_count(), 1);
    CHECK(HrKind(r) != 0);   // slot is alive

    // The wired NpcLeafHooks.freeHandlerEntry -> HandlerTable::FreeHandlerEntry.
    const NpcLeafHooks& h = GetNpcLeafHooks();
    h.freeHandlerEntry(reinterpret_cast<HeRecord*>(r));

    // The REAL pool freed the slot: live count back to 0, kind byte cleared.
    CHECK_EQ(he->live_count(), 0);
    CHECK_EQ((int)HrKind(r), 0);
}

// --- plague step (npcaction2) builders are now real ---------------------------
TEST(SimRealHooks3, NpcAction2BuildersAreReal) {
    InstallRealSimHooks3();
    const NpcAction2Hooks& h = GetNpcAction2Hooks();
    CHECK(h.queueRequestEntity29 != nullptr);
    CHECK(h.queueSingle49 != nullptr);
    CHECK(h.queueNamedObject53 != nullptr);
    CHECK(h.queuePair33 != nullptr);
    CHECK(h.requestBuildOp73Str != nullptr);
    CHECK(h.packetStatus != nullptr);
    CHECK(h.packetSeq != nullptr);
    CHECK(h.freeHandlerEntry != nullptr);
    // Unreconstructed leaves stay inert.
    CHECK(h.objectKindAt == nullptr);
    CHECK(h.broadcastOutbreak == nullptr);
    CHECK(h.plagueInfectNearby == nullptr);

    CommandQueue* q = RealCommandQueue();
    u32 before = q->send_count();
    // queueSingle49 -> real opcode-49 builder; named53 -> real opcode-53 builder.
    h.queueSingle49(700);
    CHECK_EQ(q->send_count(), before + 1);
    CHECK_EQ((int)q->ring_slot((before + 1) & 0x7FFF).opcode(), 49);
    h.queueNamedObject53(700, 50, 0, -1);
    CHECK_EQ(q->send_count(), before + 2);
    CHECK_EQ((int)q->ring_slot((before + 2) & 0x7FFF).opcode(), 53);
}

// --- npcaction3 / npcaction4 command builders are real ------------------------
TEST(SimRealHooks3, NpcAction3And4BuildersAreReal) {
    InstallRealSimHooks3();
    const NpcAction3Hooks& h3 = GetNpcAction3Hooks();
    CHECK(h3.queueRequestEntity29 != nullptr);
    CHECK(h3.queueSingle49 != nullptr);
    CHECK(h3.queueNamedObject53 != nullptr);
    CHECK(h3.queueRequest16 != nullptr);
    CHECK(h3.requestBuildOp91 != nullptr);
    CHECK(h3.enqueueArgs25 != nullptr);
    CHECK(h3.freeHandlerEntry != nullptr);
    // No-target leaves stay inert.
    CHECK(h3.requestChrMove == nullptr);
    CHECK(h3.queueFlag55 == nullptr);
    CHECK(h3.evaluateViolation == nullptr);

    const NpcAction4Hooks& h4 = GetNpcAction4Hooks();
    CHECK(h4.queueRequestEntity29 != nullptr);
    CHECK(h4.queueSingle49 != nullptr);
    CHECK(h4.queuePair33 != nullptr);
    CHECK(h4.queueRequest16 != nullptr);
    CHECK(h4.freeHandlerEntry != nullptr);
    CHECK(h4.enqueueCmd15 == nullptr);
    CHECK(h4.queueRequest39 == nullptr);
    CHECK(h4.cityIdFromIndex == nullptr);

    CommandQueue* q = RealCommandQueue();
    u32 before = q->send_count();
    h3.requestBuildOp91(700, 2);   // real opcode-91 builder
    CHECK_EQ(q->send_count(), before + 1);
    CHECK_EQ((int)q->ring_slot((before + 1) & 0x7FFF).opcode(), 91);
    h3.enqueueArgs25(700, 1, 2, 3, 4);  // real opcode-25 builder
    CHECK_EQ((int)q->ring_slot((before + 2) & 0x7FFF).opcode(), 25);
}

// --- combat command sink now emits real lockstep packets ----------------------
TEST(SimRealHooks3, CombatSinkEmitsRealPackets) {
    InstallRealSimHooks3();
    ICombatCommandSink* sink = CombatCommandSink();
    CHECK(sink != nullptr);

    CommandQueue* q = RealCommandQueue();
    u32 before = q->send_count();
    // OnUnitDamage -> real QueueRequestPair33 (opcode 33); OnUnitDeath -> real
    // QueueRequestSingle49 (opcode 49).
    sink->OnUnitDamage(/*unitId=*/12, /*newHp=*/34);
    CHECK_EQ(q->send_count(), before + 1);
    CommandPacket& dmg = q->ring_slot((before + 1) & 0x7FFF);
    CHECK_EQ((int)dmg.opcode(), 33);
    CHECK_EQ((int)dmg.get32(0x10), 12);
    CHECK_EQ((int)dmg.get32(0x14), 34);
    sink->OnUnitDeath(/*unitId=*/12);
    CHECK_EQ(q->send_count(), before + 2);
    CHECK_EQ((int)q->ring_slot((before + 2) & 0x7FFF).opcode(), 49);
}
