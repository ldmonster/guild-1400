// End-to-end flow for the misc statistics windows: build both the general-figures
// plan and the tax-history plan from sim inputs, asserting the full recovered
// schedule (header sweep + summary lines + columns + 17 tax rows).
//
//   * Headless path: full plan build from synthetic sim data (always runs).
//   * Real-asset path (GUARDED on GUILD_GAME_DIR): would load the shipped
//     misc\statistics(.taxes) forms; skip-passes cleanly when unset.
#include "gui/statistics_window.h"
#include "world/statistics.h"
#include "tests/framework/test.h"

#include <cstdlib>
#include <string>
#include <vector>

using namespace guild::gui;

TEST(GuiStatisticsE2E, GeneralAndTaxFlow) {
    // --- general window --------------------------------------------------
    StatGeneralSummary s{};
    s.modeByte = 2;
    s.possible = 5000.0f;
    s.actual = 4200.5f;
    s.residential = 0.75f;
    s.industry = 0.25f;     // Geb Ratio = 3.000

    std::vector<float> left(28), right(28);
    for (int i = 0; i < 28; ++i) { left[i] = (float)i * 0.5f; right[i] = (float)i; }

    StatGeneralPlan gen = Statistics_BuildGeneralPlan(s, left, right);
    CHECK(std::string(gen.form) == kFormStatGeneral);
    CHECK_EQ((int)gen.headerIds.size(), 27);          // 2096..2122 (2123 = exit sentinel)
    CHECK_EQ(gen.modeHeaderId, 2 + kStatGenModeTextBase); // 2097
    CHECK_EQ((int)gen.headerLines.size(), 5);
    CHECK(gen.headerLines[2] == "Geb Ratio: 3.000");
    // 27 entries per column; the byte loop starts at offset 16 so entry n reads col[n+1].
    CHECK_EQ((int)gen.colLeft.size(), 27);
    CHECK_EQ((int)gen.colRight.size(), 27);
    CHECK(gen.colRight[10] == "11.000"); // == right[11]
    CHECK_EQ(gen.loopForm, kStatLoopForm);            // RunFrameLoop 423879

    // --- tax window ------------------------------------------------------
    std::vector<int> amounts(guild::world::kStatTaxRounds);
    for (int i = 0; i < (int)amounts.size(); ++i) amounts[i] = 1000 + i * 7;

    int currentRound = 42;
    StatTaxPlan tax = Statistics_BuildTaxPlan(currentRound, amounts);
    CHECK(std::string(tax.form) == kFormStatTaxes);
    CHECK_EQ((int)tax.rows.size(), 17);
    // First/last rows: round = currentRound - 16 + i, agreeing with the world core.
    CHECK_EQ(tax.rows[0].round, guild::world::StatisticsTaxRowRound(currentRound, 0));
    CHECK_EQ(tax.rows[16].round, currentRound);
    CHECK(tax.rows[0].text == "Runde 26:");
    CHECK_EQ(tax.rows[16].amount, 1000 + 16 * 7);
    CHECK_EQ(tax.loopForm, kStatLoopForm);
}

TEST(GuiStatisticsE2E, RealAssetGuarded) {
    const char* dir = std::getenv("GUILD_GAME_DIR");
    if (!dir) {
        CHECK(true); // clean skip-pass without a real install
        return;
    }
    // With a real install the shipped misc\statistics form would be loaded; here we
    // only assert the recovered form names the loader would request.
    CHECK(std::string(kFormStatGeneral) == "misc\\statistics");
    CHECK(std::string(kFormStatTaxes) == "misc\\statistics_taxes");
}
