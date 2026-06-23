#pragma once
// gilde.exe — VIBE_He_UpdateHandlerWorldPos @0x4c6cdc (the "He" / handler-entry
// family). namespace guild::sim.
//
// CONTEXT
//   This is the per-handler "refresh" pass the run/tick passes call once per live
//   handler record: VIBE_He_RunAllHandlers @0x4c6e38, VIBE_He_RefreshEntityHandlers
//   @0x4c6e0c, VIBE_He_RunMessageBoxHandlers @0x4c6eb4 and
//   VIBE_MeisterAi_TickRegisteredEvents @0x4c6f0c all call it on each record before
//   dispatching the per-type run callback. In handler_entry.{h,cpp} those passes
//   currently invoke an *inert* `worldPos_` seam (`WorldPosFn`); this file lands the
//   REAL body behind that seam.
//
//   Despite the IDA name ("WorldPos"), the routine is NOT a renderer: it is a pure
//   integer state machine. It checks whether the handler's owning person/entity row
//   is still valid (matching id, not the "dead/garbage" marker 0x0F, and not
//   suppressed by the +8 flag byte). If the row went stale, OR if the handler's kind
//   byte is one of a fixed set of "time-anchored" action kinds, it RESETS the
//   handler: forces the +112 state to -2 (0xFFFFFFFE) and re-stamps the +82
//   appointment clock from the global game clock. Otherwise it leaves the record
//   untouched. No GPU/render/Win32 calls; the function is faithfully recoverable 1:1.
//
// THE 536-STRIDE PERSON/ENTITY TABLE  (word_12CE910 family)
//   The handler record's +8 word is a row index into the live person/entity table
//   (the same `g_persons` 536-byte-stride table the NpcAction step machines read).
//   Three columns are consulted, each addressed as base[536*row]:
//     byte_12CE912[536*row]  : entity "kind/marker" byte (0x0F == dead/garbage).
//     dword_12CE914[536*row] : entity id (matched against handler record +12).
//     byte_12CE918[536*row]  : "suppress refresh" flag (nonzero => leave untouched).
//   (12CE912 = base+2, 12CE914 = base+4, 12CE918 = base+8; the original indexes each
//   column with the *byte* offset 536*row, recovered from the `(((row<<4)+row)<<2
//   - row)<<3 == 536*row` lea/shl chain at 0x4c6cef.) The columns are live engine
//   globals; this slice takes them through an injected, inert-by-default view so the
//   logic is exercised headlessly (rule: no OS/global coupling in src/).
#include "guild/common/types.h"
#include "sim/he.h"

namespace guild::sim {

// One row of the person/entity table, as seen by UpdateHandlerWorldPos.
struct HeWorldPosRow {
    u8  marker;     // byte_12CE912[536*row]  (0x0F => dead)
    i32 id;         // dword_12CE914[536*row]
    u8  suppress;   // byte_12CE918[536*row]  (nonzero => skip refresh)
};

// Injected view of the live 536-stride person/entity table. `lookup` returns the
// row for a given index, or reports absence (the original always has the row when
// the record's +8 index is valid; the inert default reports a row that never
// matches, exercising the stale-row reset path).
struct HeWorldPosTable {
    // row index -> row contents. Returns true if a row exists. Inert default below.
    bool (*lookup)(u16 rowIndex, HeWorldPosRow* out) = nullptr;
    // The 14-byte global game clock (qword_13CE852..unk_13CE85E) re-stamped into the
    // record's +82 appointment slot on reset. Defaults to NpcClock() when null.
    const GameTime* clock = nullptr;
};

// gilde.exe 0x4c6cdc — VIBE_He_UpdateHandlerWorldPos(record@<eax>).
//
// Returns the original's `al` (low byte of eax). The original's eax is, on each
// exit, either the record base (the early `mov esi,eax` / `pop` returns it),
// `536*row` (when it falls into the table-read path), or the kind byte (`mov al,
// [esi]`) on the switch exits — but only the low byte `al` is the declared return
// (`char`). The single observable side effect is the record mutation on reset, so
// we return the faithful `al` value for each exit and let the mutation stand.
//
// Behaviour (byte-for-byte):
//   * record+8 == 0xFFFF                      -> return (no row); record untouched.
//   * row id mismatch (record+12 != row.id)   -> RESET.
//   * row.marker == 0x0F (dead)               -> RESET.
//   * else if row.suppress != 0               -> return; record untouched.
//   * else switch on the kind byte record[0]:
//       kind in {0x16,0x18,0x19,0x1A,0x2D,0x2E,0x2F,0x35,0x36,0x37,0x38,0x41,
//                0x45,0x46,0x47,0x4A,0x4B,0x5E,0x5F,0x60,0x69,0x6A,0x6F,0x7E}
//                                             -> RESET.
//       any other kind                        -> return; record untouched.
//   RESET = (record+112 = -2) and (record+82 = 14-byte global clock image).
u8 He_UpdateHandlerWorldPos(HeRecord* record, const HeWorldPosTable& table);

// True iff `kind` is one of the time-anchored action kinds that force a reset.
// (The fixed switch set recovered from 0x4c6d39..0x4c6e08.) Exposed for tests.
bool He_WorldPosKindResets(u8 kind);

} // namespace guild::sim
