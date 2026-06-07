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
//   header sweep: do { Render(id); Render("$A"); } while (id != 2123)  -> 2096..2123.
//   slot 0: "$C"; Render(byte_641DA0 + 2095);
//           sprintf(byte_6257BC, flt_641DAC); Render("%s // ", buf);
//           sprintf(byte_6257E4, flt_641DA8); Render("%s // ", buf);
//           sprintf("Geb Ratio: %3.3f", flt_641FD4/flt_641FD8); Render("%s$A", buf);
//           sprintf("Residential: %3.3f", flt_641FD4); Render("%s", buf);
//           sprintf(" // Industry: %3.3f", flt_641FD8); Render("%s", buf);
//   slot 2: "$C"; do { sprintf("%3.3f", flt_1234758[v6]); Render("%s$A", buf); } while v6!=448;
//   slot 3: "$C"; do { sprintf("%3.3f", flt_123475C[v6]); Render("%s", buf)+Render("$A"); } v6!=448;
StatGeneralPlan Statistics_BuildGeneralPlan(const StatGeneralSummary& summary,
                                            const std::vector<float>& left,
                                            const std::vector<float>& right) {
    StatGeneralPlan plan{};

    // The 2096..2123 header-id sweep (each id is followed by a "$A" separator render).
    for (int id = kStatGenHeaderFirst; ; ++id) {
        plan.headerIds.push_back(id);
        if (id == kStatGenHeaderLast) // do/while ( v4 != 2123 )
            break;
    }

    // --- slot 0 summary lines ---------------------------------------------
    plan.modeHeaderId = summary.modeByte + kStatGenModeTextBase; // byte_641DA0 + 2095
    // The two inhabitant-count lines (each wrapped with "%s // ").
    plan.headerLines.push_back(FormatFloat(kStatFmtPossible, summary.possible));
    plan.headerLines.push_back(FormatFloat(kStatFmtActual, summary.actual));
    // Geb Ratio = residential / industry (the original divides flt_641FD4 / flt_641FD8).
    double ratio = (summary.industry != 0.f)
                       ? static_cast<double>(summary.residential) / summary.industry
                       : 0.0;
    plan.headerLines.push_back(FormatFloat(kStatFmtGebRatio, ratio));
    plan.headerLines.push_back(FormatFloat(kStatFmtResid, summary.residential));
    plan.headerLines.push_back(FormatFloat(kStatFmtIndustry, summary.industry));

    // --- slot 2 / slot 3 figure columns (28 "%3.3f" entries each) ----------
    for (int i = 0; i < kStatGenColCount; ++i) {
        float lv = (i < static_cast<int>(left.size())) ? left[i] : 0.f;
        float rv = (i < static_cast<int>(right.size())) ? right[i] : 0.f;
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
