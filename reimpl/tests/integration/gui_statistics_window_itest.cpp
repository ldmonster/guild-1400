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
    // The general window walks 28 floats per column (do/while v6 != 448, stride 16).
    StatGeneralSummary s{};
    std::vector<float> col(28);
    for (int i = 0; i < 28; ++i) col[i] = (float)i;
    StatGeneralPlan plan = Statistics_BuildGeneralPlan(s, col, col);
    CHECK_EQ((int)plan.colLeft.size(), kStatGenColCount);
    CHECK_EQ((int)plan.colRight.size(), kStatGenColCount);
    CHECK(plan.colLeft[5] == "5.000");
    CHECK(plan.colRight[27] == "27.000");
}
