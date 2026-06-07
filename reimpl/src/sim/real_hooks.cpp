// real_hooks — cross-module "real wiring" installer. Glue only: binds the
// per-module Set*Hook / *LeafHooks / Register*Hooks entry points to their real
// reconstructed targets. See real_hooks.h for the full hook -> target table and
// the list of hooks that remain at their inert default (no real target yet).
//
// This file contains NO module logic — only the indirection that connects the
// already-translated modules to each other.
#include "sim/real_hooks.h"

#include "sim/entity.h"          // PersonFindRecordById, BuildingFindById, g_persons/g_personIds
#include "sim/command_codec.h"   // QueueRequest16 (real command builder)
#include "sim/command_apply3.h"  // SetObjectFindHook
#include "sim/npctarget.h"       // SetNpcTargetHooks
#include "sim/npcaction.h"       // SetNpcCityIdResolver
#include "sim/charaction.h"      // RegisterHandlers (registers the real character steps)

#include "ai/cardgame.h"         // SetBetCmdHook

namespace guild::sim {

namespace {

// --- the shared real CommandQueue the wired emit hooks enqueue onto -----------
// In the original these queue globals are file-scope singletons; here we own one
// real CommandQueue instance that the cross-module command-emit hooks target.
CommandQueue* g_realQueue = nullptr;

CommandQueue& EnsureQueue() {
    if (!g_realQueue) {
        g_realQueue = new CommandQueue();
        g_realQueue->Init();
    }
    return *g_realQueue;
}

// =========================================================================
// AI command-emit hook -> real command codec.
// ai::SetBetCmdHook (cardgame.h) is `void(*)(i32,i32,i32,u8)`; that is exactly
// the argument shape of the real VIBE_Command_QueueRequest16 builder, which
// stages an opcode-16 packet and EnqueuePacket()s it onto the queue.
// =========================================================================
void RealBetCmd(i32 a1, i32 a2, i32 a3, u8 a4) {
    QueueRequest16(EnsureQueue(), a1, a2, a3, a4);
}

// =========================================================================
// command-apply entity-query hook -> real entity lookup.
// sim::SetObjectFindHook (command_apply3.h) is `int(*)(i32 id)` returning
// nonzero on hit. The real resolver is VIBE_Building_FindById (entity.cpp);
// the object/building share one record array.
// =========================================================================
int RealObjectFind(i32 id) {
    return BuildingFindById(id) ? 1 : 0;
}

// =========================================================================
// NPC entity-query hooks -> real entity arrays.
// =========================================================================

// npcaction.h SetNpcCityIdResolver: index -> resolved person/city id. The
// original reads the Person id column dword_12CE914[..] (g_personIds), owned by
// entity.cpp. Bounds-guarded; -1 for out-of-range / the 0xFFFF sentinel.
i32 RealCityIdFromIndex(u16 index) {
    if (index == 0xFFFF || index >= kPersonCapacity)
        return -1;
    return g_personIds[index];
}

// npctarget.h NpcTargetHooks.findPersonById -> VIBE_Person_FindRecordById.
PersonHandle RealFindPersonById(i32 id) {
    return static_cast<PersonHandle>(PersonFindRecordById(id));
}

// npctarget.h NpcTargetHooks.personByIndex -> &g_persons[index] (word_12CE910 +
// 268*index in the original; here the typed entity array). Bounds-guarded.
PersonHandle RealPersonByIndex(i32 index) {
    if (index < 0 || index >= kPersonCapacity)
        return nullptr;
    return static_cast<PersonHandle>(&g_persons[index]);
}

// The wired subset of the NpcTarget hook table. Only the unambiguous entity-array
// lookups are bound to real targets; the Office_/Ai_/ObjectSearch scoring fields
// (and the ambiguous raw byte-offset readers) are left null — npctarget.cpp
// guards every field for null and treats a null field as "no data", so a partial
// table is safe (see real_hooks.h stub list).
NpcTargetHooks g_realNpcTargetHooks{};

} // namespace

void InstallRealSimHooks() {
    EnsureQueue();

    // --- AI command emit -> real codec on the real queue ---------------------
    guild::ai::SetBetCmdHook(&RealBetCmd);

    // --- command-apply entity query -> real entity lookup --------------------
    SetObjectFindHook(&RealObjectFind);

    // --- NPC entity queries -> real entity arrays ----------------------------
    SetNpcCityIdResolver(&RealCityIdFromIndex);

    g_realNpcTargetHooks = NpcTargetHooks{};       // start fully inert (all null)
    g_realNpcTargetHooks.findPersonById = &RealFindPersonById;
    g_realNpcTargetHooks.personByIndex  = &RealPersonByIndex;
    SetNpcTargetHooks(&g_realNpcTargetHooks);

    // --- charaction leaves -> real character.cpp step handlers ---------------
    // RegisterHandlers() fills the action-type catalog with the real
    // VIBE_Character_* step handlers (Turn/Take/Drop/LoadAnim/...) and arms the
    // node pool, so enqueue/dispatch run real character logic. Idempotent.
    RegisterHandlers();
}

CommandQueue* RealCommandQueue() {
    return &EnsureQueue();
}

} // namespace guild::sim
