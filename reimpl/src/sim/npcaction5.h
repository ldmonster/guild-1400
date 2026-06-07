#pragma once
// NpcAction5 — the remaining self-contained NpcEvent setup/tick helpers (the
// "mechanical tail" of the VIBE_NpcEvent_* family that were deferred by the
// npcevent.cpp batch). Each operates on a He/handler record (see he.h), stamping
// the global clock into a time slot, advancing it, gating on the +132 packet
// handle, and re-arming a cmd29 entity request — the same scheduling vocabulary as
// npcevent.cpp. They are exercisable in isolation through the shared NpcLeafHooks
// (npcaction.h) plus the small NpcAction5Hooks below for the entity-resolve leaf.
//
// Translated functions (addresses absolute, imagebase 0x400000):
//   0x4db248 VIBE_NpcEvent_CountdownTickEntity   — countdown-and-re-arm tick
//   0x4d877c VIBE_NpcEvent_ResolveTargetAndReset  — resolve target + inventory gate
#include "guild/common/types.h"
#include "sim/he.h"

namespace guild::sim {

// ===========================================================================
// Leaf hook for the entity-resolve leaf ResolveTargetAndReset calls
// (VIBE_GameObject_ResolveEntityById). Returns nonzero (the resolved entity's
// +97 dword, which the original requires to be nonzero) or 0 if the id does not
// resolve / has no +97. Tests install a mock; nullptr installs an inert default
// that reports "unresolved".
// ===========================================================================
struct NpcAction5Hooks {
    // VIBE_GameObject_ResolveEntityById(id) -> the entity's +97 dword (0 == absent
    // or the entity exists but its +97 slot is 0; both make the original free).
    i32 (*resolveEntityField97)(i32 id);
};
void SetNpcAction5Hooks(const NpcAction5Hooks* hooks);
const NpcAction5Hooks& GetNpcAction5Hooks();

// gilde.exe 0x4db248 — VIBE_NpcEvent_CountdownTickEntity(h@eax).
//   if (state(+112) < 0 || counter(+172) <= 0) -> FreeHandlerEntry.
//   if (!(flags(+120) & 4)): --counter(+172); stamp clock into +82; advance +24h;
//      queue a cmd29 entity request (handle -> +132). Returns the packet handle
//      (or the FreeHandlerEntry / record passthrough result).
i32 NpcAction5_CountdownTickEntity(HeRecord* h);

// gilde.exe 0x4d877c — VIBE_NpcEvent_ResolveTargetAndReset(h@eax).
//   Resolve the entity at filter id (+172) via ResolveEntityById; if it (or its
//   +97 field) is absent -> FreeHandlerEntry. Look up inventory slot for the item
//   id HIWORD(+192); if missing or count==0 -> FreeHandlerEntry. Else copy the
//   clock into the +180 GameTime slot, advance it by `count` minutes, clear the
//   +176 dword and the state (+112). Returns the advance result (resulting hour).
i32 NpcAction5_ResolveTargetAndReset(HeRecord* h);

// Registration entry point for this batch (does not claim a dispatch-table type;
// these are CharAction-style He coroutines reached by address). Returns the count.
int RegisterNpcActions5();

// Returns the step fn for `address`, or nullptr if not one of this batch.
i32 (*NpcAction5_TableEntry(int address))(HeRecord*);

} // namespace guild::sim
