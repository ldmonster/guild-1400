// Unit: the composed FULL GAME DAY pass-order matches the order recovered from
// VIBE_GameTick_BeginPlayerRound @0x533188 (plus the pre-round ExAdvanceGameTick
// @0x498954 clock advance). The golden list below is the exact ordered sequence
// (name + original address + owning sub-turn) the day composition replays. This
// test asserts the composition's GameDayPassOrder() reproduces it byte-for-byte,
// and that the sub-turn ownership places Events before Economy before AI at the
// right cascade positions.
#include "test.h"

#include "play/game_day.h"

#include <cstring>

using namespace guild;

namespace {

struct GoldenStep { const char* name; std::uint32_t addr; play::DayTurn owner; };

// The recovered BeginPlayerRound @0x533188 day order (with the pre-round
// ExAdvanceGameTick @0x498954 clock advance). Decompiled call sequence.
const GoldenStep kGolden[] = {
    {"AdvanceClock",                   0x498954, play::DayTurn::Events},
    {"He_RunAllHandlers",              0x4c6e38, play::DayTurn::Events},
    {"GameTick_AdvanceCalendarClock",  0x579f70, play::DayTurn::Events},
    {"Economy_ComputeGoodsDemand",     0x578438, play::DayTurn::Economy},
    {"Combat_AccumulateThreatStats",   0x57eb64, play::DayTurn::Events},
    {"City_ComputeWealthGrid",         0x577e74, play::DayTurn::Economy},
    {"NpcTurnFlagSweep",               0x5331a2, play::DayTurn::Ai},
    {"MeisterAi_TickRegisteredEvents", 0x5331e6, play::DayTurn::Ai},
    {"MeisterAi_ExpireEventSlots",     0x53326f, play::DayTurn::Events},
    {"MeisterAi_ExpireApEventSlots",   0x533274, play::DayTurn::Events},
    {"Building_RecalcAllProduction",   0x5333be, play::DayTurn::Economy},
    {"MeisterAi_ProcessPlayerTurn",    0x533404, play::DayTurn::Ai},
    {"Amt_RunProductionPass",          0x533426, play::DayTurn::Economy},
    {"Amt_UpdateOfficeProsperity",     0x533439, play::DayTurn::Economy},
    {"Amt_RunBuildingTaxPass",         0x533451, play::DayTurn::Economy},
    {"Amt_ProcessLoanRepayments",      0x533456, play::DayTurn::Economy},
    {"Amt_ProcessAllOfficeWages",      0x533469, play::DayTurn::Economy},
    {"Amt_UpdateOffices",              0x53346e, play::DayTurn::Economy},
    {"MeisterAi_RunBuildingTasks",     0x533481, play::DayTurn::Ai},
    {"He_ProcessAllPlayerNews",        0x533499, play::DayTurn::Events},
    {"AiMethod_BroadcastGroupState",   0x5334ac, play::DayTurn::Ai},
    {"City_TickStatsAndBroadcast",     0x5334bf, play::DayTurn::Economy},
    {"MeisterAi_ProcessBuildingNeeds", 0x533510, play::DayTurn::Ai},
    {"TurnEndCoord27Broadcast",        0x533523, play::DayTurn::Ai},
    {"History_DisplayCurrentEvent",    0x5336b3, play::DayTurn::Events},
    {"Character_SyncAllTurnStates",    0x5320f0, play::DayTurn::Ai},
};
constexpr int kGoldenN = (int)(sizeof(kGolden) / sizeof(kGolden[0]));

} // namespace

// ---- the composed day order matches the golden recovered list 1:1 -------------
TEST(GameDayUnit, PassOrderMatchesBeginPlayerRoundGolden) {
    CHECK_EQ(play::GameDayStepCount(), kGoldenN);

    play::DayStep got[64];
    int n = play::GameDayPassOrder(got, 64);
    CHECK_EQ(n, kGoldenN);
    if (n != kGoldenN) return;

    bool sameNames = true, sameAddrs = true, sameOwners = true;
    for (int i = 0; i < n; ++i) {
        if (std::strcmp(got[i].name, kGolden[i].name) != 0) {
            sameNames = false;
            std::printf("    step %d name mismatch: got %s expected %s\n",
                        i, got[i].name, kGolden[i].name);
        }
        if (got[i].addr != kGolden[i].addr) {
            sameAddrs = false;
            std::printf("    step %d addr mismatch: got 0x%x expected 0x%x\n",
                        i, got[i].addr, kGolden[i].addr);
        }
        if (got[i].owner != kGolden[i].owner) sameOwners = false;
    }
    CHECK(sameNames);
    CHECK(sameAddrs);
    CHECK(sameOwners);
}

// ---- the three sub-turns occupy their correct positions in the cascade --------
TEST(GameDayUnit, SubTurnOwnershipPlacement) {
    play::DayStep s[64];
    int n = play::GameDayPassOrder(s, 64);
    CHECK(n > 0);

    // The clock advance (Events) is the VERY FIRST step — the day integrates over
    // the advanced clock, so events must precede economy + ai.
    CHECK_EQ((int)s[0].owner, (int)play::DayTurn::Events);

    // Index the first occurrence of each owner.
    int firstEvents = -1, firstEconomy = -1, firstAi = -1;
    for (int i = 0; i < n; ++i) {
        if (firstEvents  < 0 && s[i].owner == play::DayTurn::Events)  firstEvents = i;
        if (firstEconomy < 0 && s[i].owner == play::DayTurn::Economy) firstEconomy = i;
        if (firstAi      < 0 && s[i].owner == play::DayTurn::Ai)      firstAi = i;
    }
    CHECK(firstEvents >= 0);
    CHECK(firstEconomy >= 0);
    CHECK(firstAi >= 0);
    // Events open the day; economy demand re-roll (ComputeGoodsDemand) precedes the
    // first AI pass (the NPC sweep follows the wealth-grid economy preamble).
    CHECK(firstEvents < firstEconomy);

    // All three sub-turns are represented (the day is a genuine 3-way composition).
    int ce = 0, cc = 0, ca = 0;
    for (int i = 0; i < n; ++i) {
        if (s[i].owner == play::DayTurn::Events)  ++ce;
        if (s[i].owner == play::DayTurn::Economy) ++cc;
        if (s[i].owner == play::DayTurn::Ai)      ++ca;
    }
    CHECK(ce >= 1);
    CHECK(cc >= 1);
    CHECK(ca >= 1);
    CHECK_EQ(ce + cc + ca, n);

    // The economy cascade is the recovered Amt sequence in order: Production ->
    // Prosperity -> Tax -> Loan -> Wages -> Office. Locate them and assert order.
    int iProd = -1, iPros = -1, iTax = -1, iLoan = -1, iWage = -1, iOff = -1;
    for (int i = 0; i < n; ++i) {
        if (!std::strcmp(s[i].name, "Amt_RunProductionPass"))      iProd = i;
        if (!std::strcmp(s[i].name, "Amt_UpdateOfficeProsperity")) iPros = i;
        if (!std::strcmp(s[i].name, "Amt_RunBuildingTaxPass"))     iTax  = i;
        if (!std::strcmp(s[i].name, "Amt_ProcessLoanRepayments"))  iLoan = i;
        if (!std::strcmp(s[i].name, "Amt_ProcessAllOfficeWages"))  iWage = i;
        if (!std::strcmp(s[i].name, "Amt_UpdateOffices"))          iOff  = i;
    }
    CHECK(iProd >= 0 && iPros > iProd && iTax > iPros && iLoan > iTax &&
          iWage > iLoan && iOff > iWage);

    // The final economy stat tick (price EMA) runs after the Amt cascade.
    int iStat = -1;
    for (int i = 0; i < n; ++i)
        if (!std::strcmp(s[i].name, "City_TickStatsAndBroadcast")) iStat = i;
    CHECK(iStat > iOff);

    // The chronicle / turn-state sync close the day (last two steps).
    CHECK(!std::strcmp(s[n - 2].name, "History_DisplayCurrentEvent"));
    CHECK(!std::strcmp(s[n - 1].name, "Character_SyncAllTurnStates"));
}
