#pragma once
// play::TurnDriver — the PLAYABLE turn loop (PLAYABLE_PLAN P4 "the heart").
//
// This file does NOT reconstruct any rule logic. It is the live GLUE that:
//   1. INSTALLS the four real sim-hook waves + the apply-3 jump table into the
//      live path (exactly what RealSubsystems::commandQueueInitAndSync does):
//        sim::InstallRealSimHooks{,2,3,4}() and
//        sim::RegisterApplyHandlers3(sim::RealCommandQueue()).
//      Before any of these are invoked the cross-module sim dispatch runs
//      against INERT default hooks; installing them de-inerts the runtime so a
//      command applied to the queue mutates the REAL shared entity arrays.
//   2. SEEDS a small deterministic world: crt::Srand(seed), CityInitParameter-
//      Table (the 28-good economy table), ResetEntityArrays, RNG-built per-good
//      PersonEcoView demand lists, and a FireEvent over one building.
//   3. RUNS N simulation turns, each advancing real reconstructed cores:
//        - world::EconomyTickPriceLevel  (economy_tick.cpp) — demand recompute +
//          smoothed price-level EMA + price deltas (mutates g_cityTotalMoney,
//          flt_641DAC, g_goods[].priceDelta).
//        - world::ProductionComputeOutputOverTime (production.cpp) — the work-
//          window production integral (drives treasury + the fire damage input).
//        - world::FireEventBurnTick (event_fire.cpp) — a real event handler that
//          subtracts production-derived fire damage from a building's value and
//          advances the event clock.
//        - sim::GameTimeAdvance (gametime.cpp) — advances the wall clock.
//      Each turn the player treasury is credited the work-minutes and debited the
//      fire damage; a WorldSnapshot of the evolving state is recorded.
//
// SELF-CONSISTENCY ORACLE (no original binary): assert the state EVOLVES across
// turns AND is REPRODUCIBLE (same seed -> identical snapshots; different seed ->
// different snapshots). Determinism is rooted at crt::Srand.
#include <cstdint>
#include <vector>

#include "guild/common/types.h"
#include "sim/types.h"

namespace guild::play {

// A snapshot of the world's evolving counters at the end of one turn. The
// reproducibility/evolution asserts compare these element-wise.
struct WorldSnapshot {
    // sim clock (sim::GameTimeAdvance output)
    i32   day      = 0;
    int   hour     = 0;
    int   minute   = 0;
    // economy (world::EconomyTickPriceLevel side effects)
    int   priceLevel        = 0;   // truncated smoothed price (return value)
    float smoothedPrice     = 0.f; // flt_641DAC
    float cityTotalMoney    = 0.f; // flt_641FD4
    double priceDeltaSum    = 0.0; // sum g_goods[3..27].priceDelta
    // production + treasury + event
    int   workMinutes       = 0;   // production integral this turn
    i64   treasury          = 0;   // player money (credited prod, debited fire)
    i32   fireValue         = 0;   // remaining building value (fire event)

    bool operator==(const WorldSnapshot& o) const;
    bool operator!=(const WorldSnapshot& o) const { return !(*this == o); }
};

// The driver. Construct, then run(seed, turns) to get the per-turn snapshots.
class TurnDriver {
public:
    TurnDriver();

    // Installs the four real sim-hook waves + the apply-3 jump table onto the
    // shared real CommandQueue (sim::RealCommandQueue()). Idempotent; the four
    // installers are themselves idempotent. Returns true once installed.
    // This is what flips the inert hooks to real in the live path.
    bool installRealHooks();

    // Were the real hooks installed (and thus the runtime de-inerted)?
    bool hooksInstalled() const { return hooksInstalled_; }

    // Seed + run `turns` simulation turns from RNG seed `seed`. Returns the
    // snapshot recorded after each turn (size == turns). installRealHooks() is
    // called once on the first run if not already done.
    std::vector<WorldSnapshot> run(u32 seed, int turns);

    // The initial (turn 0 / pre-run) snapshot of the last run(), for delta tests.
    const WorldSnapshot& initialSnapshot() const { return initial_; }

    // Number of real packets the shared queue has emitted (proof the wired
    // command path is reachable; 0 here since the economy turn enqueues none,
    // but the apply jump table is registered and observable).
    u32 queueSendCount() const;

private:
    bool          hooksInstalled_ = false;
    WorldSnapshot initial_{};
};

} // namespace guild::play
