#pragma once
// play::RunEconomyTurn — the per-DAY ECONOMY turn (PLAYABLE_PLAN P4 economy half).
//
// This file does NOT reconstruct any rule logic. It is the live GLUE that drives
// ONE game-day of the REAL reconstructed city-economy passes over the live
// entity/economy arrays, in the EXACT order the original per-round driver runs
// them.
//
// ORDER (decompiled from VIBE_GameTick_BeginPlayerRound @0x533188, the host
// "(byte_63CC28 & 8) != 0 || dword_764CE0 == -1" economy cascade):
//   preamble : VIBE_City_ComputeWealthGrid (0x577e74)        -> inert hook leaf
//   ...the AI/player scan + VIBE_Building_RecalcAllProduction (0x583c3c) leaf...
//   then, gated through VIBE_Amt_RefreshGuildState (0x4becdc) command pumps:
//     1. VIBE_Amt_RunProductionPass        (0x57d448) production distribution
//     2. VIBE_Amt_UpdateOfficeProsperity   (0x57b718) prosperity math
//     3. VIBE_Amt_RunBuildingTaxPass(3)    (0x57b9ac) treasury / tax
//     4. VIBE_Amt_ProcessLoanRepayments    (0x57b304) loan interest / foreclose
//     5. VIBE_Amt_ProcessAllOfficeWages    (0x57b6bc) office wage cycle
//     6. VIBE_Amt_UpdateOffices                       (office commit) leaf
//   finally:
//     7. VIBE_City_TickStatsAndBroadcast   (0x57919c) city/price EMA + broadcast
//        (whose sibling VIBE_Economy_TickPriceLevel @0x579098 carries the price-
//        level EMA + g_goods[].priceDelta recompute that the live game runs from
//        VIBE_Command_ExAdvanceGameTick @0x498954).
//
// The pass ORDER itself is replayed through world::AmtRunTurnCycle (the recovered
// order table from the same 0x533188 driver). For each slot we call the REAL
// reconstructed sibling that implements that pass's rules core:
//   Production  -> world::ProductionComputeOutputOverTime / DailyHourOutput
//   Prosperity  -> world::ProsperityUpdateBuilding / ProsperityCompute
//   BuildingTax -> world::TaxCollectOfficeAllTaxes
//   LoanRepay   -> world::AmtEvaluateLoan
//   OfficeWages -> world::AmtComputeOfficeWages
//   (price tick)-> world::EconomyTickPriceLevel + CityTickStatsAndBroadcast
// Every UNRECONSTRUCTED leaf (the command-queue commit of a tax/wage transfer,
// the prosperity field write, the wealth-grid/recalc preambles) is routed through
// an INERT hook installed by RunEconomyTurn itself (world::AmtSetTransferHook /
// world::ProsperitySetCommitHook), so the deterministic arithmetic runs but no
// cross-module command path is required.
//
// The turn mutates the LIVE economy state: g_cityTotalMoney, g_goods[].priceDelta,
// the smoothed price level (flt_641DAC), a player-treasury accumulator (credited
// production work-minutes + collected taxes, debited wages + loan interest), the
// per-building prosperity scores, AND it consumes the seeded CRT RNG — so
// play::HashWorldState() (which folds the live entity arrays + RNG state) changes
// across a turn and is identical across two reruns with the same seed.
#include <cstdint>
#include <vector>

#include "guild/common/types.h"
#include "world/amt.h"            // AmtPass
#include "world/economy.h"        // PersonEcoView

namespace guild::play {

// The mutable per-day economy state RunEconomyTurn evolves. Seed it once, then
// call RunEconomyTurn each day; the fields accumulate the real passes' outputs.
struct EconomyTurnState {
    // --- calendar (the day the passes integrate production over) ---
    int day  = 0;
    int hour = 6;        // window-relative start hour (the new-game 06:00)

    // --- the seeded demand population the price tick consumes (per good 1..27).
    // RunEconomyTurn re-rolls each person's `need` from the seeded RNG each day
    // (the live game's population needs drift per round), so the economy keeps
    // evolving while staying fully reproducible. Built by SeedEconomyTurnState.
    std::vector<std::vector<world::PersonEcoView>> demand;

    // --- a synthetic active business building the prosperity/tax passes act on.
    // (The live passes walk the building table; in isolation one representative
    // building lets the real arithmetic run and mutate observable counters.)
    i32   ownerWealth   = 5000;   // ProsperityInput.ownerWealth
    i32   room[3]       = {1200, 800, 0}; // production-room values
    i32   cityMaxWealth = 10000;  // ProsperityInput.cityMax
    float prosperity    = 0.5f;   // building field +480 (carried turn to turn)
    float aiMethod      = 100.0f; // building field +180 (decayed *0.95/turn)

    // --- tax / wage / loan inputs (the office account the passes bill) ---
    world::OfficeTaxInput tax{};  // rates + account (defaults below in seeder)
    int   officeRankA   = 2;      // wage seat ranks
    int   officeRankB   = 1;
    float wageLawRate   = 0.10f;  // Gesetz(14) field
    int   loanLawSlot   = 2;      // 4 - slot + 10 interest base
    // A building in a small debt within the overdraft limit, so the loan pass
    // CHARGES per-turn interest each day (rather than foreclosing immediately).
    // With the identity rate hook the per-turn base is 4-slot+10 == 12 and the
    // overdraft limit is 2*12 == 24, so a -20 balance stays solvent and accrues.
    i32   heldCurrency  = -20;

    // --- accumulating outputs (the live counters the turn moves) ---
    i64   treasury      = 100000; // player money: + work-minutes + taxes - wages - interest
    int   workMinutes   = 0;      // production integral this day
    i64   taxCollected  = 0;      // running total taxes collected
    i64   wagesPaid     = 0;      // running total office wages paid
    i64   interestPaid  = 0;      // running total loan interest charged
    int   priceLevel    = 0;      // EconomyTickPriceLevel return (truncated EMA)
    int   passesRun     = 0;      // count of Amt passes executed this turn
};

// The per-turn deltas RunEconomyTurn observed (for reporting / assertions).
struct EconomyTurnDeltas {
    float priceBefore = 0.f, priceAfter = 0.f;     // smoothed price (flt_641DAC)
    float cityMoneyBefore = 0.f, cityMoneyAfter = 0.f; // g_cityTotalMoney
    double priceDeltaSum = 0.0;                     // sum g_goods[3..27].priceDelta
    i64   treasuryBefore = 0, treasuryAfter = 0;    // player treasury
    int   workMinutes = 0;                          // production this turn
    i64   taxThisTurn = 0, wagesThisTurn = 0, interestThisTurn = 0;
    int   passesRun = 0;                            // Amt passes invoked
    int   priceLevel = 0;                           // EconomyTickPriceLevel return
};

// Seed an EconomyTurnState's demand population + the 28-good economy parameter
// table from the seeded CRT RNG (call crt::Srand(seed) BEFORE this). Mirrors the
// turn_driver seeding: CityInitParameterTable + capDivisor=0 (forces the first
// price tick onto the seed branch) + a small RNG-built per-good demand list, plus
// the default office tax rates. Returns a ready-to-run state.
EconomyTurnState SeedEconomyTurnState();

// Run ONE per-day economy turn over `state`, in the recovered BeginPlayerRound
// order, invoking the real reconstructed passes. Mutates `state` (treasury, work-
// minutes, prosperity, price level) and the live economy globals (g_cityTotalMoney,
// g_goods[].priceDelta, the smoothed price level) and consumes the seeded RNG.
// Returns the observed deltas. Installs (and restores) the inert command-commit
// hooks for the run.
EconomyTurnDeltas RunEconomyTurn(EconomyTurnState& state);

// Convenience: the full BeginPlayerRound economy pass order (for tests/reporting).
// Returns the count written into `out` (>= world::AmtPass::Count slots).
int EconomyTurnPassOrder(world::AmtPass* out, int cap);

} // namespace guild::play
