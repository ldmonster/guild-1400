#pragma once
#include "guild/common/types.h"

// command_recon_syncrange — the two lockstep "sync-range barrier" markers of the
// Command codec (namespace guild::sim).
//
//   VIBE_Command_MarkSyncRangeStart  @0x493a1c
//   VIBE_Command_MarkSyncRangeEnd    @0x493a28
//
// These bracket a batch of enqueued commands so a caller can later block until
// the whole batch has been ACKed (via the already-reconstructed pure scan
// VIBE_Command_CheckSyncRangeAcked @0x493a34, defined in command_apply10).
//
// In the original both functions are three-instruction leaves that read the
// monotonic send counter (dword_11AA494) and stamp a sync-range boundary global:
//
//   int MarkSyncRangeStart() { dword_11AA484 = dword_11AA494 + 1; return ...; }  // 0x493a1c
//   int MarkSyncRangeEnd()   { dword_11AA47C = dword_11AA494 + 1; return ...; }  // 0x493a28
//
// dword_11AA484 is the range *start* boundary, dword_11AA47C the range *end*
// boundary; CheckSyncRangeAcked(start=dword_11AA484, end=dword_11AA47C, ackTable)
// then walks ack entries [start, end) looking for any still-pending slot.
//
// ODR: these two functions are NOT defined anywhere else in src/** (verified by
// address — UPPERCASE 0x493A1C / 0x493a1c and split forms — and by bare name,
// case-insensitively, across command_apply..command_apply12, command_codec,
// command_builders*, command_pending, command_inherit, command_receive,
// command_unit_orders, session_init, cheat_recon: every prior hit is a comment
// or a string, never a definition). The send counter (dword_11AA494) and the
// ack table live inside CommandQueue (command.h); to keep this unit testable in
// isolation without taking a dependency on the full queue, the two boundary
// globals are modeled as an explicit by-value state struct that the caller owns
// (one instance per session mirrors the original single global pair). The
// caller passes the live send-count; the markers compute (sendCount + 1) exactly
// as the binary does (32-bit wraparound preserved by using u32).

namespace guild::sim {

// Models the original's two file globals dword_11AA484 (start) / dword_11AA47C
// (end). Both are seeded to (sendCount + 1) by the markers below; the unsigned
// 32-bit arithmetic reproduces the binary's wraparound exactly.
struct SyncRangeState {
    u32 start = 0;   // dword_11AA484 — range start boundary (inclusive)
    u32 end   = 0;   // dword_11AA47C — range end   boundary (exclusive)
};

// gilde.exe 0x493a1c — VIBE_Command_MarkSyncRangeStart.
// Sets the range start boundary to sendCount + 1 and returns the same value
// (the original returns the stored dword in eax).
u32 MarkSyncRangeStart(SyncRangeState& s, u32 sendCount);

// gilde.exe 0x493a28 — VIBE_Command_MarkSyncRangeEnd.
// Sets the range end boundary to sendCount + 1 and returns the same value.
u32 MarkSyncRangeEnd(SyncRangeState& s, u32 sendCount);

} // namespace guild::sim
