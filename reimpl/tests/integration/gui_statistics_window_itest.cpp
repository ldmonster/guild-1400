// Integration tests: the statistics window builders against the REAL world data cores
// (world::StatisticsTaxRowRound, world::kStatTaxRounds) rather than re-deriving the
// row math. Verifies the gui tax-plan rows agree with the world statistics core for
// every row across several current-round values.
#include "gui/statistics_window.h"
#include "world/statistics.h"
#include "tests/framework/test.h"

#include <vector>

using namespace guild::gui;

TEST(GuiStatisticsItest, TaxRowRoundsMatchWorldCore) {
    // The gui builder must reproduce the world core's per-row round exactly.
    std::vector<int> rounds = {16, 17, 33, 100, 500};
    for (int cr : rounds) {
        std::vector<int> amounts(guild::world::kStatTaxRounds, 0);
        for (int i = 0; i < (int)amounts.size(); ++i) amounts[i] = cr * 10 + i;

        StatTaxPlan plan = Statistics_BuildTaxPlan(cr, amounts);
        CHECK_EQ((int)plan.rows.size(), guild::world::kStatTaxRounds); // 17
        for (int i = 0; i < (int)plan.rows.size(); ++i) {
            // Cross-check each row's round against the real world data core.
            CHECK_EQ(plan.rows[i].round, guild::world::StatisticsTaxRowRound(cr, i));
            CHECK_EQ(plan.rows[i].amount, cr * 10 + i);
        }
    }
}

TEST(GuiStatisticsItest, GeneralColumnsTrackWorldColumnCount) {
    // The general window walks 27 floats per column starting at byte offset 16
    // (do/while v6 = 16..448, stride 16) — stride-element 0 is skipped, so the n-th
    // emitted entry reads col[n+1].
    StatGeneralSummary s{};
    std::vector<float> col(28);
    for (int i = 0; i < 28; ++i) col[i] = (float)i;
    StatGeneralPlan plan = Statistics_BuildGeneralPlan(s, col, col);
    CHECK_EQ((int)plan.colLeft.size(), kStatGenColCount);  // 27
    CHECK_EQ((int)plan.colRight.size(), kStatGenColCount);
    CHECK(plan.colLeft[5] == "6.000");    // == col[6]
    CHECK(plan.colRight[26] == "27.000"); // == col[27], the last entry
}
