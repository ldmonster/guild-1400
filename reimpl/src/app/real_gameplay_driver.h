#pragma once
// gilde.exe — REAL game-asset GAMEPLAY-TICK driver (guild::app).
//
// INTEGRATION GLUE, not a translation. Closes the loop from a real "Die Gilde"
// install to a RUNNING economy/turn simulation: it loads the shipped city seed
// (AUGSBURG.cty) into the live world/entity arrays and then advances the world
// N real simulation turns, snapshotting the evolving state after each turn so a
// caller can assert the world STATE EVOLVES (prices drift, the city totals move,
// production accumulates, the turn-driver runs its ordered pass cascade) and that
// the run is DETERMINISTIC across re-runs from the same seed.
//
// The per-turn work is driven entirely through the reconstructed simulation cores
// (no re-implementation here):
//   * world::EconomyTickPriceLevel        — the per-round price-level EMA tick
//     (refreshes g_cityTotalMoney / g_cityTotalGoods via EconomyComputeGoodsDemand,
//      blends the smoothed price level flt_641DAC, runs EconomyComputePriceDeltas).
//   * world::ProductionComputeOutputOverTime — the production-output integral over
//     the per-turn work window (the market-price input).
//   * world::CityTickStatsAndBroadcast    — the city day-step sibling EMA + the
//     44-byte broadcast command body.
//   * world::EconomyComputePopulationTrend — the population/law growth score the AI
//     consumes.
//   * sim::BeginPlayerRound (turn_driver) — the ordered per-turn ORCHESTRATION
//     cascade (wealth grid -> npc sweep -> plant growth -> production recalc ->
//     city stats -> ...). Every pass routes through a recording pass-hook table so
//     no unreconstructed leaf is required; the orchestration order itself is real.
//
// The city seed comes in over the bound VFS (transparent gunzip of the .cty) and
// the reconstructed INI city loader (world::IniParse + world::CityLoadFromIni); the
// building/object type tables come from world::WorldLoadBuildingAndObjectData over
// the shipped data/A_Geb.dat + A_Obj.dat. The only OS boundary is shim::IFileSystem.
// Nothing here edits the shared spine (wiring.cpp).
#include "app/real_boot.h"
#include "shim/IFileSystem.h"

#include "guild/common/types.h"

#include <array>
#include <cstddef>
#include <string>
#include <vector>

namespace guild::app {

// ---------------------------------------------------------------------------
// One per-turn snapshot of the evolving simulation state. Every field is read
// straight out of the reconstructed economy/turn cores after a turn completes.
// ---------------------------------------------------------------------------
struct GameplayTickSnapshot {
    int   turn = 0;                 // 0-based turn index

    // --- economy aggregates (world/city.cpp + economy_tick.cpp globals) ---
    float smoothedPriceLevel = 0;   // flt_641DAC — the EMA price level
    float capDivisor = 0;           // flt_641DA8 — cap/equilibrium divisor
    float cityTotalMoney = 0;       // flt_641FD4 — money-side demand total
    float cityTotalGoods = 0;       // flt_641FD8 — goods-side demand total
    float broadcastSpread = 0;      // flt_1235234 — broadcast price-spread word
    int   priceLevelRounded = 0;    // EconomyTickPriceLevel return (trunc price)

    // --- production / population ---
    int    productionWorkMinutes = 0; // ProductionComputeOutputOverTime integral
    double populationTrend = 0;       // EconomyComputePopulationTrend score

    // --- turn-driver orchestration outcomes (sim::BeginPlayerRound) ---
    int  turnPassCount = 0;         // passes the round cascade actually invoked
    int  plantsAdvanced = 0;        // plant nodes the growth pass advanced
    bool droveHeavyPasses = false;  // the heavy AI/Amt cascade ran this turn

    // --- city day-step broadcast (CityTickStatsAndBroadcast) ---
    int cityBroadcastBytes = 0;     // 44 when the day-step ran
};

// ---------------------------------------------------------------------------
// Run configuration: how many turns, the deterministic seed, and the synthetic
// population the demand recompute consumes (the live person array belongs to the
// sim agent; the economy tick reads the per-good views, mirroring economy_tick).
// ---------------------------------------------------------------------------
struct GameplayRunConfig {
    int  turns = 8;          // number of simulation turns to run
    u32  seed = 0x6D617267;  // deterministic seed (drives the synthetic demand)
    bool heavyPasses = true; // run the host/heavy turn cascade (Amt/AI passes)
};

// ---------------------------------------------------------------------------
// Result of a gameplay run: the load metadata + the per-turn snapshots + a small
// derived delta summary so the caller can assert the state evolved.
// ---------------------------------------------------------------------------
struct RealGameplayResult {
    // --- load metadata (only the real-asset path fills the asset fields) ---
    bool        assetsPresent = false;   // real game dir + AUGSBURG.cty resolved
    bool        cityLoaded = false;      // the .cty INI parsed into g_cities[0]
    std::string cityName;                // parsed city name (e.g. "Augsburg")
    int         buildingTypeCount = 0;   // A_Geb.dat records (72 on real assets)
    int         sceneTypeCount = 0;      // A_Obj.dat records (731 on real assets)

    // --- the simulation timeline ---
    std::vector<GameplayTickSnapshot> ticks;

    // --- derived deltas (first vs last tick) ---
    float  priceDelta = 0;           // last.smoothedPriceLevel - first
    float  moneyDelta = 0;           // last.cityTotalMoney - first
    int    totalPassesRun = 0;       // sum of turnPassCount over all turns
    int    totalPlantsAdvanced = 0;  // sum of plantsAdvanced over all turns
    bool   stateEvolved = false;     // true if any tracked counter moved across ticks

    // Convenience: did the price level change at all across the run?
    bool priceMoved() const { return priceDelta != 0.0f; }
};

// ---------------------------------------------------------------------------
// Load a city seed from an in-memory INI text (the gunzipped .cty body) into the
// live world arrays + seed the economy parameter table + reset the entity arrays,
// exactly as the spine's worldLoadBuildingAndObjectData does for the real path.
// `cityIndex` selects the g_cities slot (0 == the start city). Returns the parsed
// city name (g_cities[cityIndex].name shape via the INI [Allgemein] Name key), or
// "" if the text could not be parsed. SYNTHETIC-safe: needs no real assets.
std::string LoadCityFromIniText(const std::string& iniText, int cityIndex = 0);

// Run `cfg.turns` simulation turns over the CURRENTLY-LOADED world, snapshotting
// the evolving economy/turn state after each turn. Resets the price-level EMA seed
// (g_capDivisor = 0, smoothed price = 0) at the start so the first tick takes the
// deterministic first-tick branch, then lets the EMA blend evolve. Deterministic
// for a given `cfg.seed`. Fills `out.ticks` and the derived delta summary.
void RunGameplayTurns(const GameplayRunConfig& cfg, RealGameplayResult& out);

// Full real-asset driver: MountRealGameAssets(fs, gameDir) -> load the configured
// <Stadt>.cty (gunzip + INI city loader) + the A_Geb/A_Obj type tables into the
// live world arrays -> RunGameplayTurns(cfg). GUARD on asset presence: if the game
// dir / key assets are absent, `assetsPresent` is false and no ticks run (caller
// should skip). The caller owns io::VfsShutdown() afterward (process-global VFS),
// which this driver calls before returning to leave the VFS unbound.
RealGameplayResult DriveRealGameplay(shim::IFileSystem* fs,
                                     const std::string& gameDir,
                                     const GameplayRunConfig& cfg);

} // namespace guild::app
