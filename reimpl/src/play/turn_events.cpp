#include "play/turn_events.h"

#include "crt/rand.h"                // RandNext
#include "sim/gametime.h"            // GameTimeAdvance, GameTimeCompare, GameTimeDiffMinutes
#include "world/event.h"             // EventTableLoadDefault, EventPickRandomByCategory, kEventMaxCategory
#include "world/history.h"           // kChronicleBaseYear
#include "world/history_chronicle.h" // Chronicle, ChronicleEntry

namespace guild::play {

namespace {

// ---------------------------------------------------------------------------
// Inert engine-leaf hooks. The real time/event driver dispatches the He script-
// VM handlers (funcs_4C6EE9[*v]), broadcasts player news (VIBE_He_ProcessPlayer-
// News) and accumulates combat threat stats (VIBE_Combat_AccumulateThreatStats)
// through cross-module engine paths. For a standalone events turn we install
// recording-but-inert defaults so the deterministic clock/event arithmetic runs
// to completion without the live engine. They count invocations (proof the pass
// reached the dispatch point) but mutate nothing outside `state`.
// ---------------------------------------------------------------------------
int g_heHandlerDispatches = 0;
int g_newsPumps           = 0;
int g_threatAccumulates   = 0;

void InertHeHandlerDispatch() { ++g_heHandlerDispatches; }
void InertNewsPump()          { ++g_newsPumps; }
void InertThreatAccumulate()  { ++g_threatAccumulates; }

// ---------------------------------------------------------------------------
// VIBE_MeisterAi_ExpireEventSlots @0x4c7004 (event slot ring) — reproduced 1:1.
// The original walks byte_11CB624 decrementing each slot's lead byte; when a lead
// byte reaches 0 the slot's payload dword (dword_11CB620) is cleared. (The
// original's nested skip-loop is a stride artifact of the flat byte addressing;
// the observable effect per slot is: --lead, and on lead==0 clear payload.)
//
// VIBE_MeisterAi_ExpireApEventSlots @0x4c70c0 — same shape over the AP ring
// (byte_11C6560 lead, dword_11C6568 payload), breaking on lead <= 0.
// Returns the number of slots that hit 0 this turn.
// ---------------------------------------------------------------------------
int ExpireEventSlots(EventSlotRing& r) {
    int expired = 0;
    for (int i = 0; i < kEventSlotCount; ++i) {
        // --lead (the byte_11CB624[result*4] - 1 step), wrapping like the u8 it is.
        r.lead[i] = static_cast<u8>(r.lead[i] - 1);
        if (r.lead[i] == 0) {
            r.payload[i] = 0;   // dword_11CB620[result] = 0
            ++expired;
        }
    }
    for (int i = 0; i < kApEventSlotCount; ++i) {
        // The AP loop tests `v1 <= 0` after the signed decrement, so a 0 lead
        // (-> -1) also clears. We mirror the signed compare on the lead byte.
        i8 v1 = static_cast<i8>(static_cast<i8>(r.apLead[i]) - 1);
        r.apLead[i] = static_cast<u8>(v1);
        if (v1 <= 0) {
            r.apPayload[i] = 0; // dword_11C6568[result] = 0 (+ siblings)
            ++expired;
        }
    }
    return expired;
}

// VIBE_GameTick_AdvanceCalendarClock @0x579f70 core: the recoverable rule is the
// diff-minutes integral — measure the minutes between the last-integrated time
// (qword_1235262) and the new clock (qword_13CE852), accumulate, then snapshot the
// new clock as the last-integrated time. (The flt_641DA8 price-EMA branch is the
// economy half, owned by turn_economy.) Returns the minutes integrated this step.
i64 AdvanceCalendarClock(EventsTurnState& s) {
    int diff = sim::GameTimeDiffMinutes(&s.calendarClock, &s.clock);
    if (diff != 0) {
        s.minutesIntegrated += diff;
        s.calendarClock = s.clock;   // qword_1235262 := qword_13CE852
    }
    return diff;
}

} // namespace

EventsTurnState SeedEventsTurnState() {
    // Load the default event-descriptor table so EventPickRandomByCategory has
    // populated rows (categories 0..5) to pick from when a trigger fires.
    world::EventTableLoadDefault();

    EventsTurnState s;
    // New-game wall clock at 06:00 day 0 (matches turn_economy's 06:00 start).
    s.clock.day = 0; s.clock.hour = 6; s.clock.minute = 0; s.clock.second = 0;
    s.calendarClock = s.clock;       // last-integrated == start
    s.minutesPerDay = 1440;

    // A small ring of scheduled He-event triggers with RNG countdowns/periods so
    // events fire on different days and the schedule genuinely evolves while
    // staying reproducible (RNG rooted at crt::Srand).
    int nTriggers = 3 + (crt::RandNext() % 4);   // 3..6 triggers
    s.triggers.reserve(nTriggers);
    for (int t = 0; t < nTriggers; ++t) {
        EventTrigger tr;
        tr.period   = 1 + (crt::RandNext() % 4);                 // re-arm 1..4 days
        tr.countdown= 1 + (crt::RandNext() % tr.period);          // 1..period
        tr.category = static_cast<u8>(crt::RandNext() %
                                      (world::kEventMaxCategory + 1)); // 0..5
        tr.active   = true;
        s.triggers.push_back(tr);
    }

    // Seed the MeisterAi event-slot rings with RNG lead-byte countdowns + payloads
    // so the Expire passes have live state to count down and clear.
    for (int i = 0; i < kEventSlotCount; ++i) {
        s.slots.lead[i]    = static_cast<u8>(1 + (crt::RandNext() % 5));  // 1..5
        s.slots.payload[i] = static_cast<i32>(crt::RandNext());
    }
    for (int i = 0; i < kApEventSlotCount; ++i) {
        s.slots.apLead[i]    = static_cast<u8>(1 + (crt::RandNext() % 5));
        s.slots.apPayload[i] = static_cast<i32>(crt::RandNext());
    }
    return s;
}

EventsTurnDeltas RunEventsTurn(EventsTurnState& state) {
    EventsTurnDeltas d{};
    d.dayBefore    = state.clock.day;
    d.hourBefore   = state.clock.hour;
    d.minuteBefore = state.clock.minute;
    const int firedBefore   = state.eventsFired;
    const int chronBefore   = state.chronicleAdded;

    // Install the inert engine-leaf hooks for the duration of the turn.
    g_heHandlerDispatches = 0;
    g_newsPumps = 0;
    g_threatAccumulates = 0;

    state.passesRun = 0;

    // === ExAdvanceGameTick @0x498954 : time-advance + event-tick portion ======

    // gate: VIBE_GameTime_Compare(&clock, target) < 0 — only advance forward.
    // The target is the clock + one game-day of minutes. Build it via the real
    // GameTimeAdvance core on a copy, then gate on the compare before committing.
    sim::GameTime target = state.clock;
    sim::GameTimeAdvance(&target, /*addDays=*/0, /*addSeconds=*/0,
                         /*addMinutes=*/state.minutesPerDay);

    if (sim::GameTimeCompare(&state.clock, &target) < 0) {
        // 1. clock := target (the original copies the packed record field-by-field).
        state.clock = target;
        ++state.passesRun;

        // 2. VIBE_He_RunAllHandlers (0x4c6e38): tick the He event-clock / due
        //    handlers. Each scheduled trigger counts down; when due it fires an
        //    event (chosen via the real EventPickRandomByCategory LCG picker) and
        //    re-arms. The real per-handler script-VM body is dispatched through the
        //    inert hook (funcs_4C6EE9). Firing appends a dated chronicle entry.
        for (auto& tr : state.triggers) {
            if (!tr.active) continue;
            InertHeHandlerDispatch();          // funcs_4C6EE9[*v]() leaf
            if (tr.countdown > 0) --tr.countdown;
            if (tr.countdown == 0) {
                // Fire: pick an event of this category (advances the mission LCG).
                int picked = world::EventPickRandomByCategory(tr.category);
                tr.lastFired = picked;
                ++state.eventsFired;
                // 10. (history) append one dated chronicle entry per fired event.
                world::ChronicleEntry e{};
                e.day    = state.clock.day;
                e.month  = 1 + (state.clock.day % 12);   // dated label
                e.year   = world::kChronicleBaseYear + state.clock.day / 365;
                e.textId = 7300 + (picked < 0 ? 0 : picked);
                e.text   = nullptr;                       // engine-formatted text
                if (state.chronicle.Add(e) >= 0)
                    ++state.chronicleAdded;
                tr.countdown = tr.period;                  // re-arm
            }
        }
        ++state.passesRun;

        // 3. VIBE_GameTick_AdvanceCalendarClock (0x579f70): the diff-minutes
        //    calendar integral over the new clock.
        d.minutesAdvanced = AdvanceCalendarClock(state);
        ++state.passesRun;

        // 4. VIBE_Economy_ComputeGoodsDemand: economy half — routed to turn_economy.
        //    (inert here; the economy turn owns the demand recompute.)

        // 5. every-6-day VIBE_Combat_AccumulateThreatStats (0x57eb64): the original
        //    gates on (qword_13CE854 >> 32) % 6 == 0. We gate on day % 6 == 0.
        if (state.clock.day % 6 == 0) {
            InertThreatAccumulate();
            ++state.passesRun;
        }
    }

    // === BeginPlayerRound @0x533188 : event-side calls (driver order) =========

    // 6. VIBE_MeisterAi_TickRegisteredEvents (0x4c6f0c): the 0x10-flagged He
    //    handler tick. Modeled by the same trigger ring above (the registered
    //    events share the He handler table); the dispatch leaf is inert.
    InertHeHandlerDispatch();
    ++state.passesRun;

    // 7+8. VIBE_MeisterAi_ExpireEventSlots (0x4c7004) + ExpireApEventSlots
    //      (0x4c70c0): countdown-and-clear the event / AP slot rings.
    int expired = ExpireEventSlots(state.slots);
    state.slots.expiredThisTurn = expired;
    ++state.passesRun;   // ExpireEventSlots
    ++state.passesRun;   // ExpireApEventSlots

    // 9. VIBE_He_ProcessAllPlayerNews (0x4c5018): per-player news pump (inert leaf).
    InertNewsPump();
    ++state.passesRun;

    // Restore inert defaults (no persistent global hooks to un-install here; the
    // counters reset at the next turn's start).

    ++state.turnsRun;

    d.dayAfter    = state.clock.day;
    d.hourAfter   = state.clock.hour;
    d.minuteAfter = state.clock.minute;
    d.eventsFiredThisTurn    = state.eventsFired   - firedBefore;
    d.chronicleAddedThisTurn = state.chronicleAdded - chronBefore;
    d.slotsExpiredThisTurn   = expired;
    d.passesRun              = state.passesRun;
    return d;
}

} // namespace guild::play
