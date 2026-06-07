#pragma once
// Wave 28 PLAY P7 seed — a multi-day SCRIPTED PLAYTHROUGH harness (namespace
// guild::play).
//
// Wave 26/27 proved single-day determinism (game_day.cpp / RunGameDay) and per-slice
// click->command->day loops (playable_slice + the six Wave-27 P5 slices). This module
// is the next rung toward M4/M7: a SCRIPTED MULTI-DAY playthrough that chains SEVERAL
// real P5 slices across N game-days and proves the whole run is deterministic and
// reproducible — the closest system-level self-consistency check available without
// the Wine oracle.
//
// A "script" is a list of TIMESTAMPED actions. Each action is one real P5 slice
// command applied on a given game-day. The harness:
//   1. loads a world (real city via io::LoadWorld, or a synthetic live world),
//   2. ZeroWorldGlobals + seeds a deterministic baseline (the play-layer memory
//      gotchas: zero the folded tables RunGameDay dirties, Srand(seed) right before
//      each HashFullWorld compare),
//   3. for each day 0..N-1: applies the actions scheduled for that day by CALLING the
//      already-reconstructed PUBLIC slice apply entry points (it does NOT reimplement
//      gameplay), then advances ONE game-day via play::RunGameDay, then records a
//      per-day WITNESS (HashFullWorld + a few real observable values).
//
// PROPERTIES PROVEN (by the three test tiers):
//   (a) the world EVOLVES — the per-day hashes differ day to day,
//   (b) the run is REPRODUCIBLE — the same (script, seed) in a fresh world yields a
//       byte-identical witness table,
//   (c) the run DIVERGES with a different seed.
//
// The chained slices' apply primitives this harness drives (all already
// reconstructed; this file only orchestrates and seeds the live records they need):
//   * MARKET   (slice_market):    ClassifyMarketInteraction + InstallMarketCommandHandler
//                                 + the REAL CommandQueue opcode-17 codec -> treasury/stock.
//   * COUNCIL  (slice_council):   BuildCouncilPacket + ApplyCouncilPacket -> the REAL
//                                 world::OfficeAssignToCandidate (g_officeHolders rank).
//   * CHURCH   (slice_church):    ClassifyChurchInteraction + InstallChurchCommandHandler
//                                 + the REAL opcode-15 ExRemapObjectPair apply -> money.
//   * COMBAT   (slice_combat):    BuildCombatPacket + ApplyCombatPacket (op-80 attack pad),
//                                 OR ApplyCrimeCommand -> g_crimeTable + g_relationMatrix.
//
// ADDITIVE: new files only. It edits no existing .cpp; it calls the slices' public
// apply functions and the public play::RunGameDay / play::HashFullWorld. The only new
// state is the witness table it returns; the one inert-default it owns (the council
// applicant resolver) is DEFINED in playthrough.cpp (the build model).
#include <cstdint>
#include <string>
#include <vector>

#include "guild/common/types.h"
#include "shim/IFileSystem.h"

namespace guild::play {

// ===========================================================================
// PlaythroughActionKind — which real P5 slice a scripted action drives.
// ===========================================================================
enum class PlaythroughActionKind : int {
    kNone    = 0,
    kMarket  = 1,   // a buy/sell trade (slice_market)
    kCouncil = 2,   // an apply-for-candidacy (slice_council)
    kChurch  = 3,   // a church donation (slice_church)
    kCombat  = 4,   // an attack order (slice_combat, op-80)
    kCrime   = 5,   // a crime/intrigue (slice_combat, ExAddStraftat)
};

// ===========================================================================
// PlaythroughAction — one timestamped scripted action. `onDay` is the game-day
// index (0-based) the action fires on, BEFORE that day's RunGameDay advance. The
// per-kind fields name the slice command the action issues.
// ===========================================================================
struct PlaythroughAction {
    int                   onDay = 0;            // game-day this action fires on
    PlaythroughActionKind kind  = PlaythroughActionKind::kNone;

    // --- shared entity ids the harness seats / resolves in the live world ---
    i32 actorId  = 0;     // the acting/buyer/donor/applicant/attacker/perp id
    i32 targetId = 0;     // the market building / church / attack target / victim id

    // --- market (kMarket) ---
    i16 ware = 0;         // good prototype to trade
    i32 qty  = 0;         // amount (>0 sell, <0 buy by convention -> side derived)
    bool buy = false;     // false == sell TO market, true == buy FROM market

    // --- council (kCouncil) ---
    u8  officeType = 0;   // the office type the seat is for
    u8  holderKey  = 0;   // the targeted holder-slot key

    // --- church (kChurch) ---
    i32 amount = 0;       // donation amount

    // --- combat / crime (kCombat / kCrime) ---
    u8  crimeType = 1;    // crime subtype byte (kCrime)
};

// A scripted playthrough: the ordered action list + the number of game-days the
// run spans. Actions may target any day in [0, days).
struct PlaythroughScript {
    int days = 0;
    std::vector<PlaythroughAction> actions;
};

// ===========================================================================
// DayWitness — the per-day observable snapshot the harness records AFTER that
// day's actions + RunGameDay advance. The fold-stable HashFullWorld is the prime
// determinism oracle; the named fields are real observable values (so the report
// is meaningful and a divergence is localizable).
// ===========================================================================
struct DayWitness {
    int           day    = 0;     // the game-day index
    std::uint64_t hash   = 0;     // HashFullWorld() after the day (Srand(seed)-anchored)
    int           actionsApplied = 0;  // scripted actions that fired this day

    // --- real observable values (the brief's witness fields) ---
    i64   treasury   = 0;   // the acting building's money field (object+77), if seated
    int   price      = 0;   // a tracked ware's Building_ComputeMarketPrice(ware,100)
    i32   officeRank = 0;   // g_officeHolders[0].rank (the council seat's rank)
    int   crimeCount = 0;   // active crime records (g_crimeTable provenState != 0)
    float cityMoney  = 0.0f;// g_cityTotalMoney (the economy aggregate)
};

// ===========================================================================
// PlaythroughResult — the whole run.
// ===========================================================================
struct PlaythroughResult {
    bool loaded = false;          // the world loaded (real path) / seeded (synthetic)
    std::uint32_t personCount = 0;
    std::uint32_t objectCount = 0;
    int  daysRun = 0;             // RunGameDay invocations
    int  actionsApplied = 0;      // total scripted actions that fired

    std::vector<DayWitness> witnesses;   // one per game-day, in day order

    std::uint64_t hashAfterLoad = 0;     // HashFullWorld right after load/seed (day -1)

    // The world evolved iff any two consecutive day hashes differ (the run is not
    // a fixed point). True when the witness table has at least one day-to-day change.
    bool worldEvolved() const {
        std::uint64_t prev = hashAfterLoad;
        for (const auto& w : witnesses) {
            if (w.hash != prev) return true;
            prev = w.hash;
        }
        return false;
    }
    // A compact 64-bit signature folding every day's hash + observable fields, so two
    // runs can be compared with one equality (and a divergence is one number).
    std::uint64_t runSignature() const;
};

// ===========================================================================
// RunScriptedPlaythrough — the multi-day scripted playthrough over a REAL city.
//
//   1. mount `gameDir` + io::LoadWorld `cityName` into the live arrays,
//   2. ZeroWorldGlobals (blank every folded table) + a deterministic baseline so the
//      run is a pure function of (city + script + seed),
//   3. seat the live records the scripted actions need (market building, church
//      object, council seat, attack target) keyed off the script's ids,
//   4. HashFullWorld -> hashAfterLoad,
//   5. for day = 0..script.days-1:
//         apply every action scheduled for `day` via the slices' public apply
//         entry points (real command packets / real applies on the live world),
//         then RunGameDay(seed + day) (the real per-day cascade),
//         then Srand(seed) + HashFullWorld + read the observable witness fields,
//   6. return the per-day witness table.
//
// Headless + GUARDED on the city asset (caller supplies fs). Byte-identical on every
// call with the same (script, seed) in a fresh process/world.
// ===========================================================================
PlaythroughResult RunScriptedPlaythrough(shim::IFileSystem* fs,
                                         const std::string& gameDir,
                                         const std::string& cityName,
                                         const PlaythroughScript& script,
                                         std::uint32_t seed);

// ===========================================================================
// RunScriptedPlaythroughSynthetic — the SAME multi-day scripted sequence over a
// SYNTHETIC live world (no assets). Seeds `persons` people + the action-target
// objects directly into the live sim arrays, then runs the identical day loop.
// Deterministic in (seed, persons, script). Used by the unit test.
// ===========================================================================
PlaythroughResult RunScriptedPlaythroughSynthetic(std::uint32_t seed, int persons,
                                                  const PlaythroughScript& script);

} // namespace guild::play
