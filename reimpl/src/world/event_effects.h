#pragma once
// Scripted-event effect bodies — the recoverable data-rules cores of the He-update
// callbacks beyond VIBE_Event_FireRaidRun (which lives in world/event_fire.h).
//
// Translated functions:
//   VIBE_Event_FireRaidComputeDuration   0x4ee804  (distance->burn-duration)
//   VIBE_Event_PriceStateMachine         0x4ef408  (price-spike phase machine)
//   VIBE_Event_BuildingProductionTrigger 0x4f16b8  (production-gauge phase machine)
//
// Each original is a phased state machine that walks a He (handler-entry) record,
// drives the .esc script VM / 3D sound / scene slot and commits through the command
// queue. The recoverable cores here are the arithmetic (burn duration), the phase
// transitions, and the per-phase effect select — the engine plumbing is mocked
// through small hooks. RNG is VIBE_Math_RandomModulo == crt::RandNext()%n.
#include "guild/common/types.h"

namespace guild::world {

// ===========================================================================
// VIBE_Event_FireRaidComputeDuration 0x4ee804 — burn-duration from distance.
// ===========================================================================
// On a fire/raid ignition the original finds the nearest same-type building to the
// burning one, measures the world-space distance between their root bones, and
// scales it into a burn-duration multiplier:
//   dist   = |p_fire - p_neighbour|
//   raw    = dist * dbl_61FC60                  (0.001 metres->ticks)
//   if no neighbour:           raw = 1.0
//   factor = (raw <= dbl_61FC68) ? raw : 1.5    (clamp at 1.5)
//   duration = trunc( factor * baseValue )      (baseValue = He +176)
// Recovered constants (get_bytes):
//   dbl_61FC60 == 0x3F50624DD2F1A9FC == 0.001
//   dbl_61FC68 == 0x3FF8000000000000 == 1.5
constexpr double kFireDistScale = 0.001;  // dbl_61FC60
constexpr double kFireDurClamp  = 1.5;    // dbl_61FC68

// Computes the clamped distance->duration factor. `distance` is the world-space
// gap to the nearest same-type building; `hasNeighbour` is false when none was
// found (the original substitutes raw = 1.0 in that case).
double FireRaidDurationFactor(double distance, bool hasNeighbour);

// gilde.exe 0x4ee804 — the full duration: trunc(factor * baseValue).
i32 FireRaidComputeDuration(double distance, bool hasNeighbour, i32 baseValue);

// ===========================================================================
// VIBE_Event_PriceStateMachine 0x4ef408 — price-spike event phase machine.
// ===========================================================================
// A market-disruption event: phase 0/1 -> free the handler; phase 2 -> set a band
// of price slots to a spiked value (1000) and demand class 3, then advance to the
// final phase; phase 3 -> when authoritative and armed, dock the mood of every
// gathered target by 5..9 then free the handler. We recover the phase select and
// the spike write + the mood penalty range. The He record's phase counter lives at
// +112 (the original computes `*(int*)(a1+112) + 2` and switches on it).
enum class PriceEventPhase {
    kFreeHandler,   // result 0/1 (counter -2/-1) -> teardown
    kSpike,         // result 2 (counter 0) -> write the price band, advance
    kPenalise,      // result 3 (counter 1) -> mood penalty sweep, teardown
    kIdle,          // default -> nothing
};

// gilde.exe 0x4ef41a — maps the He phase counter (a1+112) to the phase. The switch
// keys on (counter + 2): 0/1 -> free, 2 -> spike, 3 -> penalise, else idle.
PriceEventPhase PriceEventClassify(int phaseCounter);

// The spike value written into each demand slot (constant 1000), and the demand
// class (constant 3) the spike band sets.
constexpr int kPriceSpikeValue = 1000;
constexpr int kPriceSpikeClass = 3;

// gilde.exe 0x4ef4e2 — the per-target mood penalty: -(RandomModulo(5)+5), i.e. a
// loss in [5,9]. `rand5` is RandomModulo(5).
i32 PriceEventMoodPenalty(int rand5);

// ===========================================================================
// VIBE_Event_BuildingProductionTrigger 0x4f16b8 — production-gauge phase machine.
// ===========================================================================
// A "go produce" trigger on a building: phase -2 / <-1 / <=-1 tear down; phase 0
// inspects the building's production gauge and reschedules itself:
//   gauge < 0          -> not ready: re-arm in 4 hours
//   gauge in [0,1)     -> warming up: re-arm in 20 seconds
//   gauge >= 1         -> fire production (queue slot-reset, set busy flag), re-arm
//                          in 4 hours
// We recover the phase decode + the reschedule selection (the gauge read and the
// command commit are sim/command-owned). Returns which action the phase-0 body took.
enum class ProductionTriggerAction {
    kTeardown,       // phase < 0
    kWaitHours,      // gauge < 0  -> advance 4h, idle
    kWaitSeconds,    // gauge < 1  -> advance 20s, idle
    kProduce,        // gauge >= 1 -> fire production, advance 4h
    kIdle,           // phase > 0  (no body)
};

// gilde.exe 0x4f16b8 — phase decode + phase-0 gauge branch.
//   phaseCounter == -2 or < -1 (with !=-2 returning idle) -> teardown
//   phaseCounter < 0  (i.e. -1) -> teardown
//   phaseCounter == 0 -> gauge branch (gaugeBelowZero / gauge < 1 / else produce)
//   phaseCounter > 0  -> idle
// `gauge` is VIBE_Building_DrawProductionGauge(); `gaugeBelowZero` mirrors the
// dword_12CEAD8[...] < 0 "not yet allowed" short-circuit checked before the gauge.
ProductionTriggerAction ProductionTriggerStep(int phaseCounter, bool gaugeBelowZero,
                                              float gauge);

// The reschedule intervals (recovered from the GameTime_Advance calls):
//   wait-hours  : advance 4 hours
//   wait-seconds: advance 20 seconds
//   produce     : advance 4 hours
constexpr int kProdWaitHours    = 4;   // VIBE_GameTime_Advance(...,4,0,0)
constexpr int kProdWaitSeconds  = 20;  // VIBE_GameTime_Advance(...,0,0,20)
constexpr int kProdProduceHours = 4;

} // namespace guild::world
