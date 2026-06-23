#include "gui/statistics_window.h"

#include "world/statistics.h" // StatisticsTaxRowRound, kStatTaxRounds (REUSED data core)

#include <cstdio>

namespace guild::gui {

namespace {

// Reproduce VIBE_Crt_Sprintf_0(buf, fmt, floatArg) for the single-float "%3.Nf"
// chronicle/statistics format strings. The originals print into a 256-byte stack
// buffer; std::snprintf with the same format is byte-identical for these specifiers.
std::string FormatFloat(const char* fmt, double value) {
    char buf[256];
    std::snprintf(buf, sizeof(buf), fmt, value);
    return std::string(buf);
}

} // namespace

// gilde.exe 0x57a61c.
//   header sweep: edx=2096; { Render(edx); inc edx; Render("$A"); } while(edx!=2123)
//     -> renders 2096..2122 (27 ids); 2123 is the exit sentinel, never rendered.
//   slot 0: "$C"; Render(byte_641DA0 + 2095);
//           sprintf(byte_6257BC, flt_641DAC); Render("%s // ", buf);
//           sprintf(byte_6257E4, flt_641DA8); Render("%s // ", buf);
//           sprintf("Geb Ratio: %3.3f", flt_641FD4/flt_641FD8); Render("%s$A", buf);
//           sprintf("Residential: %3.3f", flt_641FD4); Render("%s", buf);
//           sprintf(" // Industry: %3.3f", flt_641FD8); Render("%s", buf);
//   slot 2: "$C"; v6=16; do { sprintf("%3.3f", flt_1234758[v6]); Render("%s$A", buf);
//           v6+=16; } while v6!=448;  -> 27 entries at byte 16..432 (byte-0 skipped).
//   slot 3: "$C"; v6=16; do { sprintf("%3.3f", flt_123475C[v6]); Render("%s", buf)+Render("$A");
//           v6+=16; } while v6!=448;  -> 27 entries.
StatGeneralPlan Statistics_BuildGeneralPlan(const StatGeneralSummary& summary,
                                            const std::vector<float>& left,
                                            const std::vector<float>& right) {
    StatGeneralPlan plan{};

    // The header-id sweep. DISASM (0x57a667): edx=0x830; loop top renders edx, then
    // `inc edx`, then cmp edx,0x84B; jnz. The increment is BEFORE the loop test, so the
    // id rendered each iteration is 2096..2122 — id 2123 (kStatGenHeaderLast, the exit
    // value) is NEVER rendered. -> 27 ids.
    for (int id = kStatGenHeaderFirst; id != kStatGenHeaderLast; ++id)
        plan.headerIds.push_back(id);

    // --- slot 0 summary lines ---------------------------------------------
    plan.modeHeaderId = summary.modeByte + kStatGenModeTextBase; // byte_641DA0 + 2095
    // The two inhabitant-count lines (each wrapped with "%s // ").
    plan.headerLines.push_back(FormatFloat(kStatFmtPossible, summary.possible));
    plan.headerLines.push_back(FormatFloat(kStatFmtActual, summary.actual));
    // Geb Ratio = residential / industry. 0x57a733: fld flt_641FD4; fdiv flt_641FD8;
    // fstp <double>. The original divides UNCONDITIONALLY (no zero guard) — when industry
    // is 0 the x87 fdiv yields +/-inf or nan and sprintf("%3.3f") prints "inf"/"nan".
    double ratio = static_cast<double>(summary.residential) /
                   static_cast<double>(summary.industry);
    plan.headerLines.push_back(FormatFloat(kStatFmtGebRatio, ratio));
    plan.headerLines.push_back(FormatFloat(kStatFmtResid, summary.residential));
    plan.headerLines.push_back(FormatFloat(kStatFmtIndustry, summary.industry));

    // --- slot 2 / slot 3 figure columns (27 "%3.3f" entries each) ----------
    // DISASM: the byte offset v6 starts at 16 and the byte-0 element is skipped, so the
    // n-th pass (n=0..26) reads the stride-element at byte 16*(n+1), i.e. left[n+1].
    for (int i = 0; i < kStatGenColCount; ++i) {
        int k = i + 1;  // byte offset 16*(i+1) -> stride-element (i+1)
        float lv = (k < static_cast<int>(left.size())) ? left[k] : 0.f;
        float rv = (k < static_cast<int>(right.size())) ? right[k] : 0.f;
        plan.colLeft.push_back(FormatFloat(kStatFmtFigure, lv));
        plan.colRight.push_back(FormatFloat(kStatFmtFigure, rv));
    }

    return plan;
}

// gilde.exe 0x57a900.
//   v5 = 0; do { Render("Runde %i:   %T$N", currentRound - 16 + v5, dword_12350E0[v5]); }
//   while (v5 < 17);   -> 17 rows, round = currentRound - 16 + i, amount = ring[i].
StatTaxPlan Statistics_BuildTaxPlan(int currentRound, const std::vector<int>& amounts) {
    StatTaxPlan plan{};
    for (int i = 0; i < kStatTaxRows; ++i) {
        StatTaxRow row{};
        // REUSE the world data core for the round number (== currentRound - 16 + i).
        row.round = world::StatisticsTaxRowRound(currentRound, i);
        row.amount = (i < static_cast<int>(amounts.size())) ? amounts[i] : 0;
        // The "%i" round expansion (the "%T" amount + "$N" are engine-rendered).
        char buf[64];
        std::snprintf(buf, sizeof(buf), "Runde %i:", row.round);
        row.text = buf;
        plan.rows.push_back(row);
    }
    return plan;
}

} // namespace guild::gui
