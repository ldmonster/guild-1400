#include "world/statistics_full.h"

// Faithful assembly of VIBE_Statistics_BuildEconomyReport (gilde.exe 0x579ad0). The
// per-piece decisions live in world/statistics_report.cpp; here they are combined
// into the single per-tick outcome plus the recipient broadcast walk. Reproduces
// the original's three broadcast branches:
//   if (luxury > 0.55)        -> booming summary (+ weak line if any)
//   else if (luxury < 0.45)   -> slump   summary (+ weak line if any)
//   else if (weakLine)        -> weak line only
// In all three the message is sent to every recipient slot whose kind byte == 6.

namespace guild::world {

int ReportEligibleRecipientCount(const ReportRecipient* table, int count) {
    if (!table)
        return 0;
    int n = 0;
    for (int i = 0; i < count; ++i) {
        if (table[i].kind == kReportRecipientKind)   // byte_12CE912[i*4] == 6
            ++n;
    }
    return n;
}

EconomyReportOutcome EconomyReportBuild(i32 round, const EconomyReportInput& in) {
    EconomyReportOutcome out{};
    out.ran = EconomyReportShouldRun(round);          // round>=8 && round%4==0
    if (!out.ran)
        return out;

    out.trends = EconomyReportTrendIds(in);           // four up/down ids
    out.weak   = EconomyReportWeakest(in);            // running argmin (v12/v24)

    // if ( v24 > 0.20 ) -> weak-sector line fires, with a severity tier.
    out.weakLine = EconomyReportEmitsWeakLine(out.weak);
    if (out.weakLine)
        out.severity = EconomyReportWeakSeverity(out.weak.value);

    // Luxury summary branch keys on the luxury total (v26/v25).
    out.luxury = EconomyReportLuxuryBranch(in.luxury);

    // A message goes out when the luxury branch emits a summary, or (in the neutral
    // luxury case) when the weak line alone fired.
    out.broadcasts = (out.luxury != EconomyReportLuxury::kNeutral) || out.weakLine;
    return out;
}

} // namespace guild::world
