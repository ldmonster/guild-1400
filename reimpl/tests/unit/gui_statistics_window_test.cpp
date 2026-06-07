// Unit tests for the misc statistics windows.
//   gilde.exe 0x57a61c VIBE_Statistics_ShowGeneralWindow (general figures)
//   gilde.exe 0x57a900 VIBE_Statistics_ShowTaxWindow      (per-round tax history)
// Golden vectors: the header-id sweep, the float-format summary lines + columns, and
// the 17-row tax schedule (round = currentRound - 16 + i).
#include "gui/statistics_window.h"
#include "tests/framework/test.h"

#include <string>
#include <vector>

using namespace guild::gui;

// --- general window (0x57a61c) ---------------------------------------------
TEST(GuiStatistics, GeneralHeaderSweep) {
    StatGeneralSummary s{};
    StatGeneralPlan plan = Statistics_BuildGeneralPlan(s, {}, {});
    // The header sweep renders 2096..2123 inclusive (28 ids).
    CHECK_EQ((int)plan.headerIds.size(), 28);
    CHECK_EQ(plan.headerIds.front(), kStatGenHeaderFirst); // 2096
    CHECK_EQ(plan.headerIds.back(), kStatGenHeaderLast);   // 2123
    CHECK_EQ(plan.headerIds[1], 2097);
}

TEST(GuiStatistics, GeneralModeHeaderId) {
    StatGeneralSummary s{};
    s.modeByte = 3;
    StatGeneralPlan plan = Statistics_BuildGeneralPlan(s, {}, {});
    CHECK_EQ(plan.modeHeaderId, 3 + kStatGenModeTextBase); // 3 + 2095 == 2098
}

TEST(GuiStatistics, GeneralSummaryLines) {
    StatGeneralSummary s{};
    s.possible = 123.45f;
    s.actual = 67.8f;
    s.residential = 5.0f;
    s.industry = 2.0f;        // Geb Ratio = 5/2 = 2.500
    StatGeneralPlan plan = Statistics_BuildGeneralPlan(s, {}, {});
    CHECK_EQ((int)plan.headerLines.size(), 5);
    // German extended bytes preserved (\xf6 / \xe4), %3.1f / %3.3f formats.
    // 123.45f rounds to 123.4 under %3.1f (the float is ~123.449997 — faithful libc).
    CHECK(plan.headerLines[0] == std::string("$AM\xf6" "gliche Einwohnerzahl: 123.4"));
    CHECK(plan.headerLines[1] == std::string("Tats\xe4" "chliche Einwohnerzahl: 67.8"));
    CHECK(plan.headerLines[2] == "Geb Ratio: 2.500");
    CHECK(plan.headerLines[3] == "Residential: 5.000");
    CHECK(plan.headerLines[4] == " // Industry: 2.000");
}

TEST(GuiStatistics, GeneralRatioZeroIndustry) {
    StatGeneralSummary s{};
    s.residential = 5.0f;
    s.industry = 0.0f;        // division guard -> ratio 0
    StatGeneralPlan plan = Statistics_BuildGeneralPlan(s, {}, {});
    CHECK(plan.headerLines[2] == "Geb Ratio: 0.000");
}

TEST(GuiStatistics, GeneralColumns) {
    StatGeneralSummary s{};
    std::vector<float> left = {1.5f, 2.25f};
    std::vector<float> right = {9.0f};
    StatGeneralPlan plan = Statistics_BuildGeneralPlan(s, left, right);
    // Always 28 entries per column (the do/while v6!=448 loop); missing -> 0.000.
    CHECK_EQ((int)plan.colLeft.size(), 28);
    CHECK_EQ((int)plan.colRight.size(), 28);
    CHECK(plan.colLeft[0] == "1.500");
    CHECK(plan.colLeft[1] == "2.250");
    CHECK(plan.colLeft[2] == "0.000");
    CHECK(plan.colRight[0] == "9.000");
    CHECK(plan.colRight[1] == "0.000");
    CHECK(plan.colLeft[27] == "0.000");
}

// --- tax window (0x57a900) -------------------------------------------------
TEST(GuiStatistics, TaxRows) {
    std::vector<int> amounts;
    for (int i = 0; i < 17; ++i) amounts.push_back(100 + i);
    StatTaxPlan plan = Statistics_BuildTaxPlan(/*currentRound=*/50, amounts);
    CHECK_EQ((int)plan.rows.size(), 17);
    // round = currentRound - 16 + i.
    CHECK_EQ(plan.rows[0].round, 50 - 16 + 0);  // 34
    CHECK_EQ(plan.rows[16].round, 50 - 16 + 16); // 50
    CHECK_EQ(plan.rows[0].amount, 100);
    CHECK_EQ(plan.rows[16].amount, 116);
    CHECK(plan.rows[0].text == "Runde 34:");
    CHECK(plan.rows[16].text == "Runde 50:");
    CHECK(std::string(plan.form) == kFormStatTaxes);
    CHECK_EQ(plan.loopForm, kStatLoopForm);
}

TEST(GuiStatistics, TaxRowsShortAmounts) {
    // fewer than 17 amounts -> missing rows default to amount 0.
    StatTaxPlan plan = Statistics_BuildTaxPlan(20, {7, 8});
    CHECK_EQ((int)plan.rows.size(), 17);
    CHECK_EQ(plan.rows[0].amount, 7);
    CHECK_EQ(plan.rows[2].amount, 0);
}
