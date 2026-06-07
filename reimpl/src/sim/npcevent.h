#pragma once
// NpcEvent — the timed-appointment setup/reset helpers for the Guild NPC AI
// (gilde.exe, VIBE_NpcEvent_* family). Each helper stamps the global game clock
// into a He/handler record's appointment slot (+82) and/or deadline slot (+176),
// optionally advances it by a fixed/random delta, sets the phase/state, and (for
// the "queue" variants) emits a cmd29 entity-request packet so the host will
// re-dispatch the NPC at the appointment time. The "reset" variants restore the
// saved timestamp (+68) and clear state.
//
// These are the scheduling primitives the big NpcAction state machines call to
// arm their next wake-up. All addresses are absolute (imagebase 0x400000).
//
// Leaf calls that cross into the command/inventory/law clusters are routed through
// NpcLeafHooks (see npcaction.h) so the module is testable in isolation.
#include "guild/common/types.h"
#include "sim/he.h"

namespace guild::sim {

// gilde.exe 0x4d577c — VIBE_NpcEvent_RestorePoseReset.
//   Copies the saved timestamp (+68) into the appointment slot (+82); clears the
//   state dword (+112) and the wait counter (+180). No clock read, no packet.
HeRecord* NpcEvent_RestorePoseReset(HeRecord* h);

// gilde.exe 0x4d63bc — VIBE_NpcEvent_RestorePoseSetRandom.
//   Restores the saved timestamp into +82, then sets the wait counter (+180) to
//   RandomModulo(6) (a random pose index 0..5).
int NpcEvent_RestorePoseSetRandom(HeRecord* h);

// gilde.exe 0x4dada0 — VIBE_NpcEvent_SetupAnim2Reset.
//   Stamps the clock into +82, advances +2 days, clears state (+112).
int NpcEvent_SetupAnim2Reset(HeRecord* h);

// gilde.exe 0x4d75e8 — VIBE_NpcEvent_SetupDuration10Reset.
//   Stamps the clock into +82 with hour:=10, zeroes minute/second; clears the
//   +172 counter, +176 deadline-day and +180 wait counter.
HeRecord* NpcEvent_SetupDuration10Reset(HeRecord* h);

// gilde.exe 0x4d665c — VIBE_NpcEvent_InitRandomDurationEntity.
//   If not already spawned (flag 0x04): seeds the +172 counter from the law
//   base-text id, sets +180:=-1, stamps the clock into +82, and queues a cmd29
//   entity request (storing its handle into +132).
HeRecord* NpcEvent_InitRandomDurationEntity(HeRecord* h);

// gilde.exe 0x4d52b4 — VIBE_NpcEvent_QueueState9Entity.
//   If not already spawned: stamps clock into +82, advances +24h, sets hour:=9 /
//   minute:=0, queues a cmd29 entity request (handle -> +132).
HeRecord* NpcEvent_QueueState9Entity(HeRecord* h);

// gilde.exe 0x4d493c — VIBE_NpcEvent_ResetAndQueueEntity.
//   If not already spawned: resets +212:=-1, +220:=0, copies the clock into both
//   the saved (+68) and appointment (+82) slots, advances +82 by +1 minute, and
//   queues a cmd29 entity request (handle -> +132).
HeRecord* NpcEvent_ResetAndQueueEntity(HeRecord* h);

// gilde.exe 0x4daf28 — VIBE_NpcEvent_SetupRandomDurationReset.
//   Clears state (+112), stamps the clock into +82, sets hour:=RandomModulo(3)+9,
//   minute:=15*RandomModulo(4), clears the +176 deadline-day. Returns the minute.
int NpcEvent_SetupRandomDurationReset(HeRecord* h);

// gilde.exe 0x4d8a94 — VIBE_NpcEvent_SetupTargetTimestamp.
//   Stamps the clock into +82. If inventory item 376 is missing (or its count is
//   0), frees the handler entry. Otherwise copies the clock into the deadline
//   (+176) and advances it by `count` minutes.
int NpcEvent_SetupTargetTimestamp(HeRecord* h);

// gilde.exe 0x4da920 — VIBE_NpcEvent_InitTargetSlotsState12.
//   Shuffles 6 dwords into the +176 region, writes (slot+1) into the parallel
//   +172 region, stamps the clock into +82 with hour:=9, sets state:=12.
HeRecord* NpcEvent_InitTargetSlotsState12(HeRecord* h);

} // namespace guild::sim
