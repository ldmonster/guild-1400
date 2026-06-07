#pragma once
// play::RunGameDay — the FULL GAME DAY (PLAYABLE_PLAN P4 composition).
//
// turn_events.h / turn_economy.h / turn_ai.h each drive ONE HALF of the per-day
// cascade in isolation (the clock/event half, the city-economy half, the AI/NPC
// half). This module COMPOSES them into one whole game-day, invoking each sub-turn
// at its correct position in the ORIGINAL per-day driver
// VIBE_GameTick_BeginPlayerRound @0x533188 (decompiled for the exact ordered
// sequence). It does NOT re-implement any pass and does NOT edit the turn_*.cpp
// files — it is pure glue that replays the recovered driver order.
//
// RECOVERED DAY ORDER (decompiled 1:1 from BeginPlayerRound @0x533188, plus the
// clock advance the live game runs from VIBE_Command_ExAdvanceGameTick @0x498954
// immediately before the player round). Each step is tagged with the sub-turn that
// owns it:
//
//   --- time advance (ExAdvanceGameTick @0x498954, runs before the round) ---
//   [ 0] AdvanceClock              -> RunEventsTurn  (clock := target; gate fwd)
//   [ 1] HeRunAllHandlers          -> RunEventsTurn  (0x4c6e38 due-handler tick)
//   [ 2] AdvanceCalendarClock      -> RunEventsTurn  (0x579f70 diff-min integral)
//   [ 3] ComputeGoodsDemand        -> RunEconomyTurn (0x578438 demand re-roll)
//   [ 4] AccumulateThreatStats     -> RunEventsTurn  (every-6-day; inert leaf)
//
//   --- player round (BeginPlayerRound @0x533188), in driver order ---
//   [ 5] ComputeWealthGrid         -> RunEconomyTurn (0x577e74 preamble leaf)
//   [ 6] NpcTurnFlagSweep          -> RunAiTurn      (0x5331a2 +0x1C8 sweep)
//   [ 7] TickRegisteredEvents      -> RunAiTurn      (0x5331e6 He 0x10 handlers)
//   [ 8] ExpireEventSlots          -> RunEventsTurn  (0x53326f slot ring)
//   [ 9] ExpireApEventSlots        -> RunEventsTurn  (0x533274 AP slot ring)
//   [10] RecalcAllProduction       -> RunEconomyTurn (0x5333be preamble leaf)
//   [11] MeisterProcessPlayers     -> RunAiTurn      (0x533404 mood/relation core)
//   [12] AmtRunProductionPass      -> RunEconomyTurn (0x533426 production)
//   [13] AmtUpdateOfficeProsperity -> RunEconomyTurn (0x533439 prosperity)
//   [14] AmtRunBuildingTaxPass     -> RunEconomyTurn (0x533451 treasury/tax)
//   [15] AmtProcessLoanRepayments  -> RunEconomyTurn (0x533456 loan interest)
//   [16] AmtProcessAllOfficeWages  -> RunEconomyTurn (0x533469 office wages)
//   [17] AmtUpdateOffices          -> RunEconomyTurn (0x53346e office commit)
//   [18] MeisterRunBuildingTasks   -> RunAiTurn      (0x533481 building tasks)
//   [19] HeProcessAllPlayerNews    -> RunEventsTurn  (0x533499 news pump)
//   [20] AiMethodBroadcastGroup    -> RunAiTurn      (0x5334ac group-state masks)
//   [21] CityTickStatsAndBroadcast -> RunEconomyTurn (0x5334bf price EMA + delta)
//   [22] MeisterProcessBuildingNeeds-> RunAiTurn     (0x533510 building needs)
//   [23] TurnEndCoord27Broadcast   -> RunAiTurn      (0x533523 coord-27 ring)
//   [24] HistoryDisplayCurrentEvent-> RunEventsTurn  (0x5336b3 chronicle the day)
//   [25] CharacterSyncAllTurnStates-> RunAiTurn      (0x5336e1 turn-state sync)
//
// The composition collapses to THREE sub-turn invocations driven at their correct
// positions in the day (each sub-turn internally replays its own slice of the
// cascade in the recovered order):
//   * RunEventsTurn  — owns steps 0,1,2,4,8,9,19,24 (clock + events + history).
//                      It MUST run first: it advances the wall clock the day
//                      integrates over and fires the day's events.
//   * RunEconomyTurn — owns steps 3,5,10,12..17,21 (the Amt/City economy cascade).
//                      It runs after the clock has advanced (it integrates the
//                      day's production over the new calendar day).
//   * RunAiTurn      — owns steps 6,7,11,18,20,22,23,25 (the NPC/AI director).
//                      It runs last over the post-economy live world (the original
//                      runs the AI passes interleaved with the Amt passes inside
//                      the same gated block; AI reads the economy's outputs).
//
// The whole day mutates the live world (clock, economy globals, the live entity
// arrays + their AI fields) and consumes the seeded CRT RNG, so play::HashFullWorld
// changes across a day and is byte-identical across two reruns with the same seed.
#include <cstdint>
#include <vector>

#include "guild/common/types.h"
#include "play/turn_ai.h"        // AiTurnEffects
#include "play/turn_economy.h"   // EconomyTurnState, EconomyTurnDeltas
#include "play/turn_events.h"    // EventsTurnState, EventsTurnDeltas

namespace guild::play {

// One named step of the recovered BeginPlayerRound day order. `addr` is the
// original call site (or the ExAdvanceGameTick site for the pre-round clock
// advance); `owner` names the sub-turn that executes it.
enum class DayTurn { Events, Economy, Ai };

struct DayStep {
    const char* name;     // recovered call name (no VIBE_ prefix)
    std::uint32_t addr;   // original address
    DayTurn owner;        // which sub-turn drives it
};

// The composed, ORDERED day pass-list (the golden list the unit test asserts the
// composition replays). It is the exact order recovered from BeginPlayerRound
// @0x533188 (with the pre-round ExAdvanceGameTick @0x498954 clock advance). Returns
// the number of steps written into `out` (>= GameDayStepCount() slots needed).
int GameDayPassOrder(DayStep* out, int cap);

// Number of steps in the composed day order (the size of the golden list).
int GameDayStepCount();

// The mutable full-day state: one EventsTurnState (clock/events/history) + one
// EconomyTurnState (the Amt/City economy cascade). The AI half is stateless across
// days (it runs over the live entity arrays each day), so it has no carried struct;
// only its per-day seed varies. SeedGameDay builds both halves from the seeded RNG.
struct GameDayState {
    EventsTurnState  events;
    EconomyTurnState economy;
    int day = 0;        // the calendar day this state is on (advances each day)
    std::uint32_t aiSeed = 0;  // root seed for the AI sub-turn (per-day re-seeded)
};

// The per-day deltas RunGameDay observed across all three sub-turns (for reporting
// and assertions).
struct GameDayDeltas {
    EventsTurnDeltas  events;
    EconomyTurnDeltas economy;
    AiTurnEffects     ai;

    // Composition-level witnesses.
    int           dayBefore = 0, dayAfter = 0;   // events clock day
    std::uint64_t hashBefore = 0, hashAfter = 0; // HashFullWorld across the day
    int           stepsRun = 0;                  // composed steps replayed
};

// Seed a full GameDayState from the seeded CRT RNG (call crt::Srand(seed) BEFORE
// this — RunGameDay re-roots it itself, but the seeding draws here too). Builds the
// events + economy sub-states (SeedEventsTurnState + SeedEconomyTurnState) and
// stashes `seed` as the AI root seed.
GameDayState SeedGameDay(std::uint32_t seed);

// Run ONE full game day over `state`, invoking the three sub-turns at their correct
// positions in the recovered BeginPlayerRound order:
//   1. RunEventsTurn  (advance the clock + fire the day's events + chronicle)
//   2. RunEconomyTurn (integrate the day's production + run the Amt/City cascade)
//   3. RunAiTurn      (drive the NPC/AI director over the post-economy live world)
//
// `seed` re-roots the shared CRT RNG (crt::Srand) so the day is fully reproducible.
// The day re-seeds with (seed + state.day) internally so each day's RNG stream is
// distinct (the world keeps EVOLVING day to day) yet reproducible. Mutates `state`,
// the live economy globals AND the live entity arrays, and consumes the RNG. After
// the call play::HashFullWorld() has advanced iff any pass mutated the live world.
// Returns the per-day deltas (events + economy + ai + composition witnesses).
GameDayDeltas RunGameDay(std::uint32_t seed, GameDayState& state);

// Run `days` consecutive game days from one root seed. Each day re-seeds with
// (seed + dayIndex) so the per-day RNG stream is distinct but reproducible, keeping
// the world genuinely evolving rather than converging. `state` carries the clock /
// economy forward across days. Returns the per-day delta list (size == days).
std::vector<GameDayDeltas> RunGameDays(std::uint32_t seed, int days,
                                       GameDayState& state);

// Convenience: run `days` days from a fresh SeedGameDay(seed), resetting the live
// entity arrays first (the determinism anchor). Returns the per-day deltas. The
// final live-world state is left in place for the caller to hash.
std::vector<GameDayDeltas> RunGameDays(std::uint32_t seed, int days);

} // namespace guild::play
