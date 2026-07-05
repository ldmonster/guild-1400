#pragma once
// play::RunEventsTurn — the per-DAY EVENTS/TIME turn (PLAYABLE_PLAN P4 events
// half; sibling of turn_economy.h's RunEconomyTurn).
//
// This file does NOT reconstruct any rule logic. It is the live GLUE that drives
// ONE game-day of the REAL reconstructed clock / He-event / scheduled-trigger /
// history passes over a live state block, in the EXACT order the original time-
// advance + event-tick driver runs them.
//
// ORDER (decompiled from VIBE_Command_ExAdvanceGameTick @0x498954, the time-
// advance + event-tick portion, and the event-side calls of
// VIBE_GameTick_BeginPlayerRound @0x533188):
//
//   ExAdvanceGameTick @0x498954:
//     gate : VIBE_GameTime_Compare(&clock, target) < 0     (only advance forward)
//       1. clock := target                                  (set the wall clock)
//       2. VIBE_He_RunAllHandlers       (0x4c6e38)          He event-clock / due handlers
//       3. VIBE_GameTick_AdvanceCalendarClock (0x579f70)    diff-minutes calendar integral
//       4. VIBE_Economy_ComputeGoodsDemand (0x578438)       (economy half; routed to turn_economy)
//       5. every-6-day VIBE_Combat_AccumulateThreatStats    (threat stats; inert hook)
//
//   BeginPlayerRound @0x533188 (event-side calls, in driver order):
//       6. VIBE_MeisterAi_TickRegisteredEvents (0x4c6f0c)   tick the 0x10-flagged He handlers
//       7. VIBE_MeisterAi_ExpireEventSlots     (0x4c7004)   countdown+clear the event slot ring
//       8. VIBE_MeisterAi_ExpireApEventSlots   (0x4c70c0)   countdown+clear the AP event slots
//       9. VIBE_He_ProcessAllPlayerNews        (0x4c5018)   per-player news pump
//      10. VIBE_History_DisplayCurrentEvent    (0x...)      chronicle the day's event
//
// For each slot we call the REAL reconstructed sibling that implements that
// pass's core, or — for the unreconstructed engine leaves (the He script-VM
// handler dispatch, the news broadcast, the combat threat accumulator) — route
// through an INERT hook installed by RunEventsTurn itself, so the deterministic
// arithmetic runs but no cross-module engine path is required:
//   clock advance      -> sim::GameTimeCompare / sim::GameTimeAdvance (gametime.cpp)
//   calendar integral  -> the AdvanceCalendarClock diff-minutes rule (reproduced)
//   event slot expire  -> the two ExpireEventSlots countdown-and-clear loops
//                         (reproduced 1:1 from 0x4c7004 / 0x4c70c0)
//   He handler tick    -> a scheduled-trigger ring: each due handler fires an
//                         event chosen via world::EventPickRandomByCategory
//   history/chronicle  -> world::Chronicle::Add (history_chronicle.cpp)
//
// The turn mutates a LIVE state block: the wall clock (advanced K minutes/day),
// the He event clock + scheduled-trigger countdowns, the MeisterAi event/AP slot
// rings, and the in-memory Chronicle (one dated entry appended per fired event).
// It consumes the seeded CRT RNG (event firing decisions + the LCG mission
// picker), so play::HashFullWorld() changes across a turn and is identical across
// two reruns with the same seed.
#include <cstdint>
#include <vector>

#include "guild/common/types.h"
#include "sim/types.h"               // GameTime
#include "world/history_chronicle.h" // Chronicle, ChronicleEntry

namespace guild::play {

// One scheduled He-event trigger: a countdown clock that, when it reaches 0,
// fires an event of `category` and rearms with `period` days. Mirrors the He
// handler ring VIBE_He_RunAllHandlers walks (each handler has its own clock and
// an event "kind" the funcs_4C6EE9 jump table dispatches on).
struct EventTrigger {
    int  countdown = 0;   // days until this trigger fires (decremented per turn)
    int  period    = 1;   // re-arm value once it fires (days)
    u8   category  = 0;   // event category 0..5 passed to EventPickRandomByCategory
    bool active    = true;
    i32  lastFired = -1;  // last event value (+4 byte) the trigger picked (-1 none)
};

// The MeisterAi event-slot ring (VIBE_MeisterAi_ExpireEventSlots @0x4c7004).
// The original's loop counter runs 0..10496 step 41 (== 256 iterations); the
// lead byte is addressed at byte_11CB624[counter*4] (a 164-byte slot stride)
// and the payload at dword_11CB620[counter]. Per slot: decrement the lead byte,
// and zero the payload dword when it hits 0. We model the observable counters:
// a lead-byte countdown per slot + a payload dword cleared on expiry.
constexpr int kEventSlotCount   = 256;  // 10496 / 41  (0x4c7004 loop iterations)
constexpr int kApEventSlotCount = 256;  // 5120  / 20  (0x4c70c0 loop iterations)

struct EventSlotRing {
    u8  lead[kEventSlotCount]   = {};   // byte_11CB624 lead-byte countdown per slot
    i32 payload[kEventSlotCount]= {};   // dword_11CB620 cleared when lead hits 0
    u8  apLead[kApEventSlotCount]   = {}; // byte_11C6560 AP lead-byte countdown
    i32 apPayload[kApEventSlotCount]= {}; // dword_11C6568 cleared when AP lead hits 0
    int expiredThisTurn = 0;            // slots that hit 0 this turn (event+AP)
};

// The mutable per-day events/time state RunEventsTurn evolves. Seed it once
// (SeedEventsTurnState, after crt::Srand), then call RunEventsTurn each day.
struct EventsTurnState {
    // --- the wall clock the time pass advances (qword_13CE852) ---
    sim::GameTime clock{};            // {day,hour,minute,second}
    // --- the calendar accumulator AdvanceCalendarClock integrates (flt_641DA8
    //     analogue): total minutes the calendar has integrated since the start ---
    sim::GameTime calendarClock{};    // qword_1235262 (last-integrated time)
    i64 minutesIntegrated = 0;        // running sum of diff-minutes per day
    // --- minutes the clock advances per game-day (the live game's per-round
    //     time step; one full day == 1440 minutes) ---
    int minutesPerDay = 1440;

    // --- the He scheduled-trigger ring (VIBE_He_RunAllHandlers due handlers) ---
    std::vector<EventTrigger> triggers;
    // --- the MeisterAi event-slot rings (Expire*EventSlots) ---
    EventSlotRing slots;

    // --- the in-memory chronicle history pass appends to ---
    world::Chronicle chronicle;

    // --- accumulating outputs (the live counters the turn moves) ---
    int turnsRun       = 0;   // count of RunEventsTurn calls
    int eventsFired    = 0;   // running total of events that fired (triggers)
    int chronicleAdded = 0;   // running total of chronicle entries appended
    int passesRun      = 0;   // ordered passes executed last turn
};

// The per-turn deltas RunEventsTurn observed (for reporting / assertions).
struct EventsTurnDeltas {
    i32 dayBefore = 0, dayAfter = 0;        // clock.day
    int hourBefore = 0, hourAfter = 0;      // clock.hour
    int minuteBefore = 0, minuteAfter = 0;  // clock.minute
    i64 minutesAdvanced = 0;                // diff-minutes this turn (calendar)
    int eventsFiredThisTurn = 0;            // triggers that fired
    int chronicleAddedThisTurn = 0;         // entries appended
    int slotsExpiredThisTurn = 0;           // event+AP slots that hit 0
    int passesRun = 0;                      // ordered passes executed
};

// Seed an EventsTurnState from the seeded CRT RNG (call crt::Srand(seed) BEFORE
// this). Loads the default event-descriptor table (so EventPickRandomByCategory
// has rows to pick from), builds a small RNG-rolled scheduled-trigger ring + the
// MeisterAi event-slot rings with RNG countdowns, and starts the clock at the
// new-game 06:00. Returns a ready-to-run state.
EventsTurnState SeedEventsTurnState();

// Run ONE per-day events/time turn over `state`, in the recovered ExAdvanceGame-
// Tick + BeginPlayerRound event order, invoking the real reconstructed siblings.
// Advances the game clock, ticks the He event-clock / scheduled triggers, fires
// due events (appending one dated chronicle entry per fired event), and expires
// the MeisterAi event slots. Mutates `state` and consumes the seeded RNG.
// Returns the observed deltas. Installs (and restores) the inert engine-leaf
// hooks for the run.
EventsTurnDeltas RunEventsTurn(EventsTurnState& state);

} // namespace guild::play
