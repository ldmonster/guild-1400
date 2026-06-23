// See wire_he.h. Binds the three He (handler-pool) leaf bridges — HeEntityQueryHooks,
// HeHandlerHooks, GroupInteractHooks — to their real reconstructed cross-cluster
// leaves. Glue only — no module logic.
//
// All free/find leaves operate on the SAME shared real He/HandlerEntry pool that
// real_hooks3 owns (RealHandlerTable()); all command emits stage onto the SAME
// shared real CommandQueue (RealCommandQueue()). he.h's HeRecord and
// handler_entry.h's HandlerRecord are both raw POD blobs over the same pool-slot
// byte layout (kind@+0, id@+4, …); the reinterpret_casts between them are
// byte-faithful, exactly as real_hooks3 already casts HeRecord*<->HandlerRecord*.
#include "sim/wire_he.h"

#include "sim/he_entity_query.h"   // HeEntityQueryHooks / Set*/Get*
#include "sim/he_handlers.h"       // HeHandlerHooks / Set*/Get*
#include "sim/charaction_misc.h"   // GroupInteractHooks / Set*/Get*

#include "sim/real_hooks.h"        // RealCommandQueue()
#include "sim/real_hooks3.h"       // RealHandlerTable()
#include "sim/handler_entry.h"     // HandlerTable / HandlerRecord
#include "sim/entity.h"            // PersonFindRecordById
#include "sim/npcaction.h"         // NpcAction_Dispatch
#include "sim/command_builders.h"  // QueueRequestEntity29
#include "sim/command_builders2.h" // QueueRequestPair35
#include "sim/command_codec.h"     // EnqueueObjectInteraction
#include "sim/command.h"           // CommandQueue

namespace guild::sim {

namespace {

CommandQueue& Q()   { return *RealCommandQueue(); }
HandlerTable& HeT() { return *RealHandlerTable(); }

// =========================================================================
// Shared He pool / command queue leaves.
// =========================================================================

// VIBE_He_FreeHandlerEntry(record). HeHandlerHooks shape: i32 (*)(HeRecord*).
i32 WhFreeHandlerEntryI32(HeRecord* h) {
    return HeT().FreeHandlerEntry(reinterpret_cast<HandlerRecord*>(h));
}

// GroupInteractHooks freeHandlerEntry shape: void (*)(HeRecord*). The original's
// eax return is discarded on these terminal states.
void WhFreeHandlerEntryVoid(HeRecord* h) {
    HeT().FreeHandlerEntry(reinterpret_cast<HandlerRecord*>(h));
}

// VIBE_Command_GetPacketStatusById(handle) — nonzero once the packet acked.
i32 WhPacketStatus(i32 handle) {
    return Q().GetPacketStatusById(static_cast<u32>(handle));
}

// VIBE_Command_QueueRequestEntity29(arg, record).
i32 WhQueueRequestEntity29(int arg, HeRecord* h) {
    return QueueRequestEntity29(Q(), static_cast<i8>(arg), h);
}

// VIBE_NpcAction_Dispatch(record+172) — run the embedded action sub-record's step.
i32 WhNpcActionDispatch(HeRecord* sub) {
    return NpcAction_Dispatch(sub);
}

// =========================================================================
// HeEntityQueryHooks leaves.
// =========================================================================

// VIBE_Person_FindRecordById(id) -> Person* (or null), surfaced as void*.
void* WhPersonFind(i32 id) {
    return PersonFindRecordById(id);
}

// VIBE_Command_QueueRequestPair35(value, 1) — op35 entity-pair request for the
// matched entity key (the original always passes a2 == 1; see 0x4c3ad4).
void WhQueueRequestPair35(i32 value) {
    QueueRequestPair35(Q(), value, 1);
}

// =========================================================================
// GroupInteractHooks leaves.
// =========================================================================

// VIBE_Person_FindRecordById(id) -> person record (or null). Used to count live
// group members and to resolve the partner record.
void* WhGiFindPersonById(i32 id) {
    return PersonFindRecordById(id);
}

// The partner-alive predicate: nonzero when the "ready" byte at *(person+8) is set
// (the original's `*((_BYTE*)RecordById + 8)` test in GroupInteractStep @0x4d19c0).
// Byte-faithful read over the Person record base; null-safe.
int WhGiPersonReady(void* person) {
    if (!person) return 0;
    return *(reinterpret_cast<const u8*>(person) + 8) != 0;
}

// VIBE_Command_EnqueueObjectInteraction(8, fromId, 0, toId, objField, 0, 0, 2):
// the opcode-8 talk/interaction packet (the packet opcode byte is 11; a1==8 is the
// sub-kind written to +0x14). The role-byte ordering of from/to is decided by the
// caller (GroupInteractStep) before this call, so the hook just forwards them.
void WhGiEnqueueObjectInteraction(i32 fromId, i32 toId, i32 objField) {
    EnqueueObjectInteraction(Q(), /*a1=*/8, /*a2=*/fromId, /*a3=*/0,
                             /*a4=*/toId, /*a5=*/objField,
                             /*a6=*/0, /*a7=*/0, /*a8=*/2);
}

// --- process-lifetime wired hook tables (the global hook ptr references these) ---
HeEntityQueryHooks g_heq{};
HeHandlerHooks     g_heh{};
GroupInteractHooks g_gi{};

} // namespace

void InstallRealHeWiring() {
    HeT();   // force the shared real He pool to exist (composes with real_hooks3)
    Q();     // force the shared real command queue to exist

    // --- HeEntityQueryHooks (he_entity_query.h) ------------------------------
    // Seed from the module inert defaults (personFind already chains to the real
    // entity scan), then bind the real op35 emit. notifyRivalEvent stays inert.
    g_heq = GetHeEntityQueryHooks();
    g_heq.personFind         = &WhPersonFind;          // explicit (== default chain)
    g_heq.queueRequestPair35 = &WhQueueRequestPair35;
    // notifyRivalEvent: VIBE_History_NotifyRivalEvent (0x536070) has no reconstructed
    // standalone leaf (history/news cluster) -> inert (no-op), faithful to a build
    // with no rival-event log sink.
    SetHeEntityQueryHooks(&g_heq);

    // --- HeHandlerHooks (he_handlers.h) --------------------------------------
    // Seed from the inert table (a zeroed struct; the module reads each field
    // null-safely) then bind the real pool / dispatch / queue leaves.
    g_heh = GetHeHandlerHooks();
    g_heh.freeHandlerEntry     = &WhFreeHandlerEntryI32;
    g_heh.npcActionDispatch    = &WhNpcActionDispatch;
    g_heh.packetStatus         = &WhPacketStatus;
    g_heh.queueRequestEntity29 = &WhQueueRequestEntity29;
    // charActionTick / eventTick / buildingTick: the three subsystem RetZero stubs
    // (VIBE_CharAction_RetZero / VIBE_Event_RetZero / VIBE_Building_HandlerStub) all
    // return 0 in the shipping build -> inert default (null => treated as 0) is the
    // faithful behavior; binding them would change nothing.
    SetHeHandlerHooks(&g_heh);

    // --- GroupInteractHooks (charaction_misc.h, VIBE_CharAction_GroupInteractStep)
    // GroupInteractStep invokes these fields WITHOUT a null-check, so seed from the
    // module's NON-null inert defaults (GetGroupInteractHooks() returns g_giDefault)
    // and override only the wireable fields.
    g_gi = GetGroupInteractHooks();
    g_gi.findPersonById           = &WhGiFindPersonById;
    g_gi.personReady              = &WhGiPersonReady;
    g_gi.freeHandlerEntry         = &WhFreeHandlerEntryVoid;
    g_gi.enqueueObjectInteraction = &WhGiEnqueueObjectInteraction;
    // queueRequest39: VIBE_Command_QueueRequest39 (0x494c30) builds an op39 delta
    // packet (with a GameTime-advanced deadline); no reconstructed op39 builder
    // exists -> inert (no-op). DrinkInit-style remaining-stub, documented across the
    // sim bridges (real_hooks3.h queueRequest39 list).
    SetGroupInteractHooks(&g_gi);
}

} // namespace guild::sim
