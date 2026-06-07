#pragma once
// Statistics — the FULL economy-report orchestration (gilde.exe
// VIBE_Statistics_BuildEconomyReport 0x579ad0), assembling the decision pieces
// already recovered in world/statistics.h (float reduction) and
// world/statistics_report.h (cadence gate, trend ids, weakest argmin, severity,
// luxury branch) into the single decision the original makes per report tick, plus
// the recipient-broadcast person walk.
//
// The GUI text render (VIBE_Text_RenderFormattedMessage) and the He message send
// (VIBE_He_SendEntityMessage) are engine leaves; the recoverable cores are (1) the
// assembled report decision and (2) the recipient iteration rule.
#include "guild/common/types.h"
#include "world/statistics_report.h"   // EconomyReportInput + the decision helpers

namespace guild::world {

// ===========================================================================
// Recipient broadcast walk  (gilde.exe 0x579e3c.. the do/while over the player
// person table). The report scans a fixed table of 768 slots, stride 536 bytes,
// reading a "kind" byte at slot+0 (byte_12CE912) and a person id at slot+2
// (dword_12CE914); only slots whose kind byte == 6 receive the report message.
// ===========================================================================
constexpr int kReportRecipientStride = 134;   // element stride (134 dwords = 536B)
constexpr int kReportRecipientEnd    = 102912; // loop bound (768 slots * 134)
constexpr int kReportRecipientCount  = kReportRecipientEnd / kReportRecipientStride; // 768
constexpr u8  kReportRecipientKind   = 6;      // byte_12CE912[i*4] == 6 gate

// A recipient table slot (the two fields the broadcast loop reads).
struct ReportRecipient {
    u8  kind;        // byte_12CE912[i*4] (6 == eligible player slot)
    i32 personId;    // dword_12CE914[i]  (He entity id to message)
};

// Counts the recipients (kind == 6) the report would broadcast to over a table of
// `count` slots. Mirrors the `if (byte_12CE912[i*4] == 6)` gate in the loop.
int ReportEligibleRecipientCount(const ReportRecipient* table, int count);

// ===========================================================================
// Assembled report outcome.
// ===========================================================================
// The single decision the report makes once the cadence gate passes. Combines:
//   * the four per-category trend ids,
//   * whether the weak-sector line fires + its severity tier,
//   * the luxury summary branch (booming / slump / neutral),
//   * whether ANY message is broadcast this tick.
struct EconomyReportOutcome {
    bool                   ran;          // cadence gate passed (round>=8 && %4==0)
    EconomyReportTrends    trends;       // per-category up/down ids
    EconomyReportWeak      weak;         // weakest category + value
    bool                   weakLine;     // the "<sector> struggling" line fired
    EconomyReportSeverity  severity;     // weak-line severity tier (valid if weakLine)
    EconomyReportLuxury    luxury;       // luxury summary branch
    bool                   broadcasts;   // a He message is sent this tick
};

// gilde.exe 0x579ad0 — the full report decision for `round` over the input totals.
// When the cadence gate fails, returns {ran=false} with the rest defaulted. The
// `broadcasts` flag is true when either the luxury branch emits a summary OR the
// weak line fired (matching the original's three broadcast branches: luxury-high,
// luxury-low, and the weak-line-only fall-through).
EconomyReportOutcome EconomyReportBuild(i32 round, const EconomyReportInput& in);

} // namespace guild::world
