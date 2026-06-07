#pragma once
// he_handlers — the per-type "He" handler STEP functions of the Guild simulation
// (gilde.exe, VIBE_He_* family) that the handler-pool scheduler invokes once per
// tick. These are distinct from the table-ops in handler_entry.{h,cpp} (alloc /
// free / find / tick-pass): they are the *step callbacks* registered into the
// per-type run table (funcs_4C6EE9), each operating on one He/handler record
// (see he.h) — the bridge between the handler pool and NpcAction dispatch.
//
// Translated functions (addresses absolute, imagebase 0x400000):
//   0x4cdd10 VIBE_He_NpcActionHandler        — run NpcAction_Dispatch then free
//   0x4d0ac4 VIBE_He_CounterWaitHandler      — decrement the +172 byte countdown
//   0x4d0a10 VIBE_He_Entity29RequestHandler  — gate on packet ack; re-arm cmd29
//   0x4c57e0 VIBE_He_FindEntityHandlerOrdinal— ordinal of a Person among prof-6
//   0x4c5370 VIBE_He_UpdateSubsystems        — tick handlers + subsystem stubs
//
// Cross-cluster leaves (NpcAction_Dispatch / He_FreeHandlerEntry / packet status /
// cmd29 queue / the subsystem RetZero stubs) are routed through HeHandlerHooks so
// the step control flow is exercisable in isolation; the real game installs the
// cluster bridge.
#include "guild/common/types.h"
#include "sim/he.h"

namespace guild::sim {

// ===========================================================================
// Leaf hooks — side effects that cross into clusters this module does not own.
// Passing nullptr installs an inert default (frees return the record, queries
// return "absent/done", queue returns 0, subsystem stubs return 0).
// ===========================================================================
struct HeHandlerHooks {
    // VIBE_He_FreeHandlerEntry(record) — release the handler entry; returns the
    // original's eax (the inert default returns the record base as an int).
    i32 (*freeHandlerEntry)(HeRecord* h);
    // VIBE_NpcAction_Dispatch(record+172) — run the NpcAction step on the embedded
    // action sub-record. The return value is ignored by the caller.
    i32 (*npcActionDispatch)(HeRecord* subRecord);
    // VIBE_Command_GetPacketStatusById(handle) — nonzero once the packet has been
    // applied/acked; 0 == still pending.
    i32 (*packetStatus)(i32 handle);
    // VIBE_Command_QueueRequestEntity29(arg, record) — queue a cmd29 entity request;
    // returns the new packet handle (stored into He+132).
    i32 (*queueRequestEntity29)(int arg, HeRecord* h);
    // VIBE_CharAction_RetZero / VIBE_Event_RetZero / VIBE_Building_HandlerStub — the
    // three subsystem-tick stubs UpdateSubsystems short-circuits on. Each returns
    // nonzero to claim the tick. (In the shipping build all three return 0.)
    i32 (*charActionTick)();
    i32 (*eventTick)();
    i32 (*buildingTick)();
};

void SetHeHandlerHooks(const HeHandlerHooks* hooks);
const HeHandlerHooks& GetHeHandlerHooks();

// Installs the Person profession-byte column used by FindEntityHandlerOrdinal.
// The original scans byte_12CE912 (Person record +2, stride 536, 768 slots). The
// host supplies `base` (a 768-element profession byte array indexed by slot) so
// the scan is exercisable in isolation; when unset the scan finds no prof-6 rows.
void SetHeProfessionColumn(const u8* base, int slotCount);

// ===========================================================================
// Per-type handler step functions.
// ===========================================================================

// gilde.exe 0x4cdd10 — VIBE_He_NpcActionHandler(h@eax).
//   If the state (+112) is the abort sentinel -2, free the entry. Otherwise run
//   NpcAction_Dispatch on the embedded sub-record (h+172) and free the entry.
//   Returns the free result.
i32 He_NpcActionHandler(HeRecord* h);

// gilde.exe 0x4d0ac4 — VIBE_He_CounterWaitHandler(h@eax).
//   Reads the state dword (+112). state < -2 -> passthrough; state == -2 -> free;
//   state > 0 -> passthrough; state == 0 -> decrement the +172 byte countdown
//   (free when it was already 0). Returns the record (or the free result).
i32 He_CounterWaitHandler(HeRecord* h);

// gilde.exe 0x4d0a10 — VIBE_He_Entity29RequestHandler(h@eax).
//   Gate on the pending cmd29 packet (+132): if it is set and not yet acked,
//   passthrough. Once acked (or none pending) clear +132 and branch on state
//   (+112): < -2 passthrough; == -2 free; <= -1 free; == 0 with the needs-cmd29
//   flag (0x02) set, stamp the clock into +82, advance +2 minutes, and re-arm a
//   cmd29 request (handle -> +132). Returns the resulting handle / state / free.
i32 He_Entity29RequestHandler(HeRecord* h);

// gilde.exe 0x4c57e0 — VIBE_He_FindEntityHandlerOrdinal(slot@eax).
//   Walks the Person profession column (stride 536, 768 slots); counts how many
//   profession==6 Persons precede the Person at `slot`, returning that ordinal.
//   Returns -1 if the Person at `slot` is not profession 6 (loop falls off end).
int He_FindEntityHandlerOrdinal(int slot);

// gilde.exe 0x4c5370 — VIBE_He_UpdateSubsystems().
//   Frees every live handler (via TickActiveHandlers, modeled by the hook), then
//   runs the CharAction / Event / Building subsystem ticks, returning 1 as soon as
//   any reports work, else the last (zero) result. `tickActive` is supplied so the
//   pool tick is driven without owning the table.
i32 He_UpdateSubsystems(i32 (*tickActive)());

} // namespace guild::sim
