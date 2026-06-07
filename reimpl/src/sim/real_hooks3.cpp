// real_hooks3 — third-wave cross-module "real wiring" installer. Glue only:
// binds the NPC/AI/interaction leaf hooks (npcaction*.h NpcLeafHooks /
// NpcAction2/3/4Hooks command-builder + freeHandlerEntry + packet-status fields,
// the command_apply2 He-find hook, and the combat command sink) to their real
// reconstructed targets — the command builders (command_builders.{h,cpp} /
// command_codec) and the He/HandlerEntry pool (handler_entry.{h,cpp}). See
// real_hooks3.h for the full hook -> target table and the remaining-stub list.
//
// This file contains NO module logic — only the indirection that connects the
// already-translated modules to each other.
//
// NB: command_apply2.h declares its OWN 176-byte `guild::sim::HeRecord`, which
// conflicts with he.h's full HeRecord that handler_entry.h / command_builders.h /
// npcaction*.h use. So this TU (the he.h side) cannot include command_apply2.h.
// The SetHeFindHook wiring therefore lives in a separate TU
// (real_hooks3_apply2.cpp) that talks to the shared He pool through the raw
// `void*`-returning helper RealHeFindHandlerRaw() implemented at the bottom here.
#include "sim/real_hooks3.h"
#include "sim/real_hooks.h"        // RealCommandQueue() — the shared real queue

#include "sim/handler_entry.h"     // HandlerTable, HandlerRecord, FreeHandlerEntry
#include "sim/command_builders.h"  // QueueRequestEntity29 / Single49 / Pair33 / …
#include "sim/command_codec.h"     // QueueRequest16 / QueueRequestArgs25

#include "sim/npcaction.h"         // NpcLeafHooks / SetNpcLeafHooks
#include "sim/npcaction2.h"        // NpcAction2Hooks / SetNpcAction2Hooks
#include "sim/npcaction3.h"        // NpcAction3Hooks / SetNpcAction3Hooks
#include "sim/npcaction4.h"        // NpcAction4Hooks / SetNpcAction4Hooks
#include "sim/combat.h"            // ICombatCommandSink / SetCombatCommandSink

namespace guild::sim {

namespace {

// --- the shared real He/HandlerEntry pool -----------------------------------
// In the original the pool (byte_11D6040) is a process-wide singleton; here we
// own one real HandlerTable the wired freeHandlerEntry / He-find hooks operate
// on. Init()'d on the first install.
HandlerTable* g_realHe = nullptr;

HandlerTable& EnsureHe() {
    if (!g_realHe) {
        g_realHe = new HandlerTable();
        g_realHe->Init();
    }
    return *g_realHe;
}

CommandQueue& Q() { return *RealCommandQueue(); }

// =========================================================================
// He pool leaf -> real HandlerTable.
// The NpcLeafHooks/NpcAction*Hooks freeHandlerEntry field is
//   i32 (*)(HeRecord* h)
// mapping to VIBE_He_FreeHandlerEntry. The real target is
// HandlerTable::FreeHandlerEntry(HandlerRecord*). he.h's HeRecord and
// handler_entry.h's HandlerRecord are both raw POD-byte blobs over the same
// pool slot (kind@+0, ordinal@+4, …); the cast is byte-faithful (the freed
// record must have been AllocHandlerEntry'd into the same pool).
// =========================================================================
i32 RealFreeHandlerEntry(HeRecord* h) {
    return EnsureHe().FreeHandlerEntry(reinterpret_cast<HandlerRecord*>(h));
}

// =========================================================================
// packet status / seq gating -> real CommandQueue ACK table.
// VIBE_Command_GetPacketStatusById / GetPacketSeqById (command.h). The handle
// is the ring id the entity-request builders returned.
// =========================================================================
i32 RealPacketStatus(i32 handle) {
    return Q().GetPacketStatusById(static_cast<u32>(handle));
}
i32 RealPacketSeq(i32 handle) {
    return Q().GetPacketSeqById(static_cast<u32>(handle));
}

// =========================================================================
// command builders -> real command_builders / command_codec on the shared queue.
// Each adapter has the exact pointer-shape of the corresponding hook field and
// folds its args into the real builder's signature (documented per field in the
// npcaction*.h structs). All return the assigned ring slot where the hook does.
// =========================================================================

// queueRequestEntity29(arg, h) -> QueueRequestEntity29(q, (i8)arg, h).
i32 RealQueueRequestEntity29(int arg, HeRecord* h) {
    return QueueRequestEntity29(Q(), static_cast<i8>(arg), h);
}

// queueRequestSingle49(personId) -> QueueRequestSingle49(q, personId).
void RealQueueSingle49(i32 personId) { QueueRequestSingle49(Q(), personId); }

// queuePair33(personId, value) -> QueueRequestPair33(q, personId, value).
void RealQueuePair33(i32 personId, int v) { QueueRequestPair33(Q(), personId, v); }

// requestBuildOp93(id, kind, id2, amount) -> RequestBuildOp93(q, id, kind, id2,
// amount). (a3 register arg does not reach the wire; faithful.)
void RealRequestBuildOp93(i32 id, int kind, i32 id2, u8 amount) {
    RequestBuildOp93(Q(), id, kind, id2, amount);
}

// requestBuildOp91(personId, kind) -> RequestBuildOp91(q, personId, kind, 0, 0).
void RealRequestBuildOp91(i32 personId, int kind) {
    RequestBuildOp91(Q(), personId, kind, 0, 0);
}

// queueRequest16(fromId, toId, amount, market) -> QueueRequest16(q, fromId, toId,
// amount, market). (real a3 slot = 0; the hook only carries four fields.)
void RealQueueRequest16(i32 fromId, i32 toId, i32 amount, u8 market) {
    QueueRequest16(Q(), fromId, toId, amount, market);
}

// enqueueArgs25(personId, a, b, c, d) -> QueueRequestArgs25(q, a1..a5).
void RealEnqueueArgs25(i32 personId, int a, int b, int c, int d) {
    QueueRequestArgs25(Q(), personId, a, b, c, d);
}

// npcaction2 queueNamedObject53(personId, objId, flagA, flagB) -> the opcode-53
// builder. The 2-arg-pair flavour: a1=personId, a2=objId, a4=flagB, a5=flagA,
// no obj/name string (the plague phase passes only the two flags).
void RealQueueNamedObject53_2(i32 personId, i32 objId, int flagA, int flagB) {
    QueueRequestNamedObject53(Q(), personId, objId, /*obj=*/nullptr, /*a4=*/flagB,
                              /*a5=*/static_cast<i8>(flagA), /*name=*/nullptr,
                              /*personStamped=*/true);
}

// npcaction3/4 queueNamedObject53(personId, objId, a, targetId, flag, name) ->
// the full opcode-53 builder: a1=personId, a2=objId, a4=targetId, a5=flag, name.
// `a` is the leading discriminator the original passes but which does not reach
// this packet's wire payload (it gates the entity look-up the builder folds in).
void RealQueueNamedObject53_6(i32 personId, i32 objId, int /*a*/, i32 targetId,
                              int flag, const char* name) {
    QueueRequestNamedObject53(Q(), personId, objId, /*obj=*/nullptr, /*a4=*/targetId,
                              /*a5=*/static_cast<i8>(flag), name,
                              /*personStamped=*/true);
}

// npcaction2 requestBuildOp73Str(objId) -> the "PEST" slot opcode-73 builder. The
// plague setup passes only the object id; it lands in the a2 field, the remaining
// fields are zero / no name (the original stamps them from the object record,
// which is the unported entity look-up — faithful to the resolved-arg form).
i32 RealRequestBuildOp73Str_2(i32 objId) {
    return RequestBuildOp73Str(Q(), /*a1=*/0, /*a2=*/objId, /*a3=*/0, /*a4=*/0,
                               /*a5=*/0, /*a6=*/0, /*name=*/nullptr);
}

// =========================================================================
// combat command sink -> real command builders on the shared queue.
// ICombatCommandSink (combat.h): the original broadcast combat damage/death as
// lockstep network commands. OnUnitDamage(unitId,newHp) -> a 2-int request
// (QueueRequestPair33); OnUnitDeath(unitId) -> a single-int request
// (QueueRequestSingle49). Both stage+enqueue a real packet onto the shared queue.
// =========================================================================
class RealCombatSink final : public ICombatCommandSink {
public:
    void OnUnitDamage(i32 unitId, i32 newHp) override {
        QueueRequestPair33(Q(), unitId, newHp);
    }
    void OnUnitDeath(i32 unitId) override {
        QueueRequestSingle49(Q(), unitId);
    }
};
RealCombatSink g_combatSink;

// --- the wired hook tables (own storage; module setters take a pointer) ------
NpcLeafHooks    g_npc1{};
NpcAction2Hooks g_npc2{};
NpcAction3Hooks g_npc3{};
NpcAction4Hooks g_npc4{};

} // namespace

// Implemented in real_hooks3_apply2.cpp — wires command_apply2's SetHeFindHook to
// the shared real He pool. Forward-declared here (its TU sees the conflicting
// 176-byte HeRecord, so it cannot be defined in this TU) and called from
// InstallRealSimHooks3() so the public installer stays a single entry point.
void InstallRealApply2HeFind();

// Raw helper consumed by real_hooks3_apply2.cpp (the command_apply2 He-find
// hook). Returns the first live handler record whose +16 field matches `id`
// (HandlerTable::FindFirstHandlerByFilter selector 3 = field@+16, the same
// VIBE_He_FindFirstHandlerByFilter call the original He-find leaf made), as an
// opaque void* (the apply2 TU casts it to its own 176-byte HeRecord*; both view
// the same pool slot's leading bytes). Also forces the shared He pool to exist so
// the apply2 wiring and the freeHandlerEntry wiring share ONE table.
void* RealHeFindHandlerRaw(i32 id) {
    HandlerRecord* r = EnsureHe().FindFirstHandlerByFilter(1, /*selector=*/3, id);
    return r;
}

void InstallRealSimHooks3() {
    EnsureHe();   // ensure the shared He pool exists (composes with apply2 wiring)

    // --- NpcLeafHooks (npcaction.h) ------------------------------------------
    g_npc1 = NpcLeafHooks{};                 // start fully inert (all null)
    g_npc1.queueRequestEntity29 = &RealQueueRequestEntity29;
    g_npc1.freeHandlerEntry     = &RealFreeHandlerEntry;
    g_npc1.packetStatus         = &RealPacketStatus;
    g_npc1.requestBuildOp93     = &RealRequestBuildOp93;
    // lawBaseTextId / findInventorySlot / shuffleDwords: no real target (stubs).
    SetNpcLeafHooks(&g_npc1);

    // --- NpcAction2Hooks (npcaction2.h, plague) ------------------------------
    g_npc2 = NpcAction2Hooks{};
    g_npc2.queueRequestEntity29 = &RealQueueRequestEntity29;
    g_npc2.packetStatus         = &RealPacketStatus;
    g_npc2.packetSeq            = &RealPacketSeq;
    g_npc2.freeHandlerEntry     = &RealFreeHandlerEntry;
    g_npc2.queueSingle49        = &RealQueueSingle49;
    g_npc2.queueNamedObject53   = &RealQueueNamedObject53_2;
    g_npc2.queuePair33          = &RealQueuePair33;
    g_npc2.requestBuildOp73Str  = &RealRequestBuildOp73Str_2;
    // object-array probes / broadcast / plagueInfectNearby: no real target.
    SetNpcAction2Hooks(&g_npc2);

    // --- NpcAction3Hooks (npcaction3.h, burglary/recruit/jail) ---------------
    g_npc3 = NpcAction3Hooks{};
    g_npc3.queueRequestEntity29 = &RealQueueRequestEntity29;
    g_npc3.packetStatus         = &RealPacketStatus;
    g_npc3.packetSeq            = &RealPacketSeq;
    g_npc3.freeHandlerEntry     = &RealFreeHandlerEntry;
    g_npc3.queueSingle49        = &RealQueueSingle49;
    g_npc3.queueNamedObject53   = &RealQueueNamedObject53_6;
    g_npc3.queueRequest16       = &RealQueueRequest16;
    g_npc3.requestBuildOp91     = &RealRequestBuildOp91;
    g_npc3.enqueueArgs25        = &RealEnqueueArgs25;
    // requestChrMove / queueFlag55 / queuePair36 / queueQuad43 / setEntityField /
    // queueSlotReset28 / enqueueBuildingAction* / entity queries / physics: stubs.
    SetNpcAction3Hooks(&g_npc3);

    // --- NpcAction4Hooks (npcaction4.h, patrol/sabotage/raid) ----------------
    g_npc4 = NpcAction4Hooks{};
    g_npc4.queueRequestEntity29 = &RealQueueRequestEntity29;
    g_npc4.packetStatus         = &RealPacketStatus;
    g_npc4.packetSeq            = &RealPacketSeq;
    g_npc4.freeHandlerEntry     = &RealFreeHandlerEntry;
    g_npc4.queueSingle49        = &RealQueueSingle49;
    g_npc4.queueNamedObject53   = &RealQueueNamedObject53_6;
    g_npc4.queuePair33          = &RealQueuePair33;
    g_npc4.queueRequest16       = &RealQueueRequest16;
    // queueFlag55 / queuePair36 / queueQuad43 / enqueueCmd15 / queueRequest39 /
    // cityIdFromIndex / family* / entity queries / physics: no real target (stubs).
    SetNpcAction4Hooks(&g_npc4);

    // --- combat command sink -> real command builders ------------------------
    SetCombatCommandSink(&g_combatSink);

    // --- command_apply2 He-find -> real He pool (separate TU; see note) ------
    InstallRealApply2HeFind();
}

HandlerTable* RealHandlerTable() {
    return &EnsureHe();
}

} // namespace guild::sim
