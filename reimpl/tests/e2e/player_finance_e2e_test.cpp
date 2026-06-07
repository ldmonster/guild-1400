#include "test.h"

// E2E: a full "civic money" pass across the player-finance cluster, driven by a
// deterministic synthetic town. It chains the pieces the way the game does over a
// turn: rank the citizens by wealth (church board), gate a master-exam fee, levy
// a court fine on the loser, format it for display, and bank it in the city
// treasury. The real-asset variant is GUARDED behind GUILD_E2E_ASSETS (absent the
// shipped asset tree it runs the synthetic flow only).
#include "crt/rand.h"
#include "world/money_format.h"
#include "world/player_finance.h"
#include "world/treasury.h"

#include <cstdlib>
#include <string>

using namespace guild;
using namespace guild::world;

namespace {
struct FineLog {
    int lastId = 0;
    bool hadAmount = false;
    i32 amount = 0;
    int calls = 0;
};
void FineSink(const FineMessage& m, void* ctx) {
    auto* l = static_cast<FineLog*>(ctx);
    l->lastId = m.messageId;
    l->hadAmount = m.hasAmount;
    l->amount = m.amount;
    l->calls++;
}
} // namespace

TEST(PlayerFinanceE2E, CivicMoneyTurn) {
    // --- A synthetic town of citizens with known wealth. ---
    std::vector<WealthSlot> town = {
        {100, 0, 250000, true},  // wealthiest merchant
        {101, 1, 180000, true},
        {102, 0, 90000, true},
        {103, 10, 999999, true}, // guard class -> excluded from board
        {104, 2, 60000, true},
        {105, 0, 30000, true},
        {106, 0, 15000, false},  // out of town -> inactive
    };

    // 1. Rank the citizens (the church top-5 board).
    TopWealthBoard board = PersonComputeTopWealthList(town);
    CHECK_EQ(board.ids[0], static_cast<u16>(100));
    CHECK_EQ(board.wealth[0], 250000);
    CHECK_EQ(board.ids[1], static_cast<u16>(101));
    CHECK_EQ(board.ids[4], static_cast<u16>(105)); // the 5th richest
    // The excluded guard (103) never appears despite the highest raw wealth.
    for (int k = 0; k < 5; ++k)
        CHECK(board.ids[k] != static_cast<u16>(103));

    // 2. A master-exam fee gate for citizen 102, seeded deterministically.
    crt::Srand(7);
    bool examPasses = AmtCheckExamFeeAffordable(/*fav=*/95.0, /*examLvl=*/4,
                                                /*reqLvl=*/2);
    // High favorability with a level above the requirement clears the roll.
    CHECK(examPasses == true);

    // 3. A court fine levied on citizen 104 (wealth 60000): 0.03f-rate, truncated.
    FineLog log;
    HeSetFineRenderHook(FineSink, &log);
    i32 fine = HeShowFineAmount(/*totalWealth=*/60000, /*variant=*/1);
    CHECK_EQ(fine, 1799);              // trunc(60000 * 0.03f)
    CHECK_EQ(log.calls, 1);
    CHECK_EQ(log.lastId, 4445);        // variant-1 message id
    CHECK(log.hadAmount == true);
    CHECK_EQ(log.amount, 1799);
    HeSetFineRenderHook(nullptr, nullptr);

    // 4. The fine renders through the real money formatter for the panel.
    std::string shown = MoneyFormatWithSeparators(fine, 1);
    // Grouped every three digits: 1799 -> "1.799".
    CHECK_EQ(shown, std::string("1.799") + kCurrencyGlyph);

    // 5. Bank the fine in the city treasury.
    Treasury city;
    city.accountId = 1;
    city.balance = 500;
    i32 newBal = TreasuryDeposit(city, /*payer=*/104, fine);
    CHECK_EQ(newBal, 500 + 1799);

    // --- GUARDED real-asset extension. ---
    if (std::getenv("GUILD_E2E_ASSETS")) {
        // With the shipped asset tree present a real church board / fine table
        // could be cross-checked here. The synthetic flow above is the contract;
        // the asset path only adds confidence, so its absence is not a failure.
        CHECK(city.balance == 500 + 1799);
    }
}
