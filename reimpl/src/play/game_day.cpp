#include "play/game_day.h"

#include "crt/rand.h"            // Srand
#include "play/world_digest.h"   // HashFullWorld
#include "sim/entity.h"          // ResetEntityArrays

namespace guild::play {

namespace {

// The composed day order, recovered 1:1 from VIBE_GameTick_BeginPlayerRound
// @0x533188 (with the pre-round ExAdvanceGameTick @0x498954 clock advance). This
// IS the golden list: the sub-turns below are invoked at the positions these steps
// occupy. (Steps with the same owner are executed by that owner's single sub-turn
// call, which internally replays its slice of this order — see the per-call notes.)
const DayStep kDayOrder[] = {
    // --- ExAdvanceGameTick @0x498954 : time advance (before the round) ---------
    {"AdvanceClock",               0x498954, DayTurn::Events},
    {"He_RunAllHandlers",          0x4c6e38, DayTurn::Events},
    {"GameTick_AdvanceCalendarClock", 0x579f70, DayTurn::Events},
    {"Economy_ComputeGoodsDemand", 0x578438, DayTurn::Economy},
    {"Combat_AccumulateThreatStats", 0x57eb64, DayTurn::Events},

    // --- BeginPlayerRound @0x533188 : player round (driver order) --------------
    {"City_ComputeWealthGrid",     0x577e74, DayTurn::Economy},
    {"NpcTurnFlagSweep",           0x5331a2, DayTurn::Ai},
    {"MeisterAi_TickRegisteredEvents", 0x5331e6, DayTurn::Ai},
    {"MeisterAi_ExpireEventSlots", 0x53326f, DayTurn::Events},
    {"MeisterAi_ExpireApEventSlots", 0x533274, DayTurn::Events},
    {"Building_RecalcAllProduction", 0x5333be, DayTurn::Economy},
    {"MeisterAi_ProcessPlayerTurn", 0x533404, DayTurn::Ai},
    {"Amt_RunProductionPass",      0x533426, DayTurn::Economy},
    {"Amt_UpdateOfficeProsperity", 0x533439, DayTurn::Economy},
    {"Amt_RunBuildingTaxPass",     0x533451, DayTurn::Economy},
    {"Amt_ProcessLoanRepayments",  0x533456, DayTurn::Economy},
    {"Amt_ProcessAllOfficeWages",  0x533469, DayTurn::Economy},
    {"Amt_UpdateOffices",          0x53346e, DayTurn::Economy},
    {"MeisterAi_RunBuildingTasks", 0x533481, DayTurn::Ai},
    {"He_ProcessAllPlayerNews",    0x533499, DayTurn::Events},
    {"AiMethod_BroadcastGroupState", 0x5334ac, DayTurn::Ai},
    {"City_TickStatsAndBroadcast", 0x5334bf, DayTurn::Economy},
    {"MeisterAi_ProcessBuildingNeeds", 0x533510, DayTurn::Ai},
    {"TurnEndCoord27Broadcast",    0x533523, DayTurn::Ai},
    {"History_DisplayCurrentEvent", 0x5336b3, DayTurn::Events},
    {"Character_SyncAllTurnStates", 0x5320f0, DayTurn::Ai},
};

constexpr int kDayStepCount =
    static_cast<int>(sizeof(kDayOrder) / sizeof(kDayOrder[0]));

} // namespace

int GameDayStepCount() { return kDayStepCount; }

int GameDayPassOrder(DayStep* out, int cap) {
    int n = kDayStepCount;
    if (n > cap) n = cap;
    for (int i = 0; i < n; ++i) out[i] = kDayOrder[i];
    return n;
}

GameDayState SeedGameDay(std::uint32_t seed) {
    GameDayState st;
    // Both sub-states draw from the same seeded RNG stream, in the fixed order
    // events-then-economy (mirroring the day order: the events half precedes the
    // economy half). The caller is expected to have called crt::Srand(seed); we
    // seed here in that established stream so SeedGameDay is reproducible.
    st.events  = SeedEventsTurnState();
    st.economy = SeedEconomyTurnState();
    st.day     = 0;
    st.aiSeed  = seed;
    return st;
}

GameDayDeltas RunGameDay(std::uint32_t seed, GameDayState& state) {
    GameDayDeltas d{};

    // Witness the world state at entry BEFORE re-seeding (the RNG state is folded
    // into HashFullWorld, so this must precede crt::Srand to reflect entry state).
    d.dayBefore  = state.events.clock.day;
    d.hashBefore = HashFullWorld();

    // Re-root the shared CRT RNG for this day so the whole day (events + economy +
    // ai) is reproducible. A distinct per-day seed keeps the world evolving.
    const std::uint32_t daySeed = seed + static_cast<std::uint32_t>(state.day);
    crt::Srand(daySeed);

    // ----- 1. EVENTS sub-turn (steps 0,1,2,4,8,9,19,24) ----------------------
    // Runs FIRST: it advances the wall clock the day integrates over, fires the
    // day's He-events, expires the MeisterAi slot rings, pumps news, and appends
    // the day's chronicle entry. (RunEventsTurn internally replays its slice of the
    // recovered order; see turn_events.cpp.)
    d.events = RunEventsTurn(state.events);

    // ----- 2. ECONOMY sub-turn (steps 3,5,10,12..17,21) ----------------------
    // Runs after the clock advanced: it integrates the day's production over the new
    // calendar day, runs the full Amt cascade (production -> prosperity -> tax ->
    // loan -> wages -> office), and the final City price-EMA tick. The economy
    // state's `day` tracks the events clock so production integrates over the right
    // calendar day.
    state.economy.day = state.events.clock.day;
    d.economy = RunEconomyTurn(state.economy);

    // ----- 3. AI sub-turn (steps 6,7,11,18,20,22,23,25) ----------------------
    // Runs last over the post-economy live world: NPC turn-flag sweep, the per-
    // faction MeisterAi mood/relation/confront/decay director, building tasks, the
    // group-state broadcast, building needs, the coord-27 ring, and the turn-state
    // sync. RunAiTurn re-roots crt::Srand(aiSeed) itself, so it gets its own
    // reproducible stream rooted at this day's seed (kept distinct per day).
    const std::uint32_t aiDaySeed = state.aiSeed + static_cast<std::uint32_t>(state.day);
    d.ai = RunAiTurn(aiDaySeed);

    d.dayAfter  = state.events.clock.day;
    d.hashAfter = HashFullWorld();
    d.stepsRun  = kDayStepCount;

    ++state.day;
    return d;
}

std::vector<GameDayDeltas> RunGameDays(std::uint32_t seed, int days,
                                       GameDayState& state) {
    std::vector<GameDayDeltas> out;
    if (days < 0) days = 0;
    out.reserve(static_cast<std::size_t>(days));
    for (int i = 0; i < days; ++i)
        out.push_back(RunGameDay(seed, state));
    return out;
}

std::vector<GameDayDeltas> RunGameDays(std::uint32_t seed, int days) {
    // Reset the live entity arrays (the determinism anchor) and build a fresh day
    // state from the seeded RNG, then run `days` consecutive days. The final live
    // world is left in place for the caller to hash.
    sim::ResetEntityArrays();
    crt::Srand(seed);
    GameDayState state = SeedGameDay(seed);
    return RunGameDays(seed, days, state);
}

} // namespace guild::play
