#pragma once
// Wave 29 PLAY — MULTI-CITY ROBUSTNESS (PLAYABLE_PLAN generality check).
//
// Everything in the play layer so far has been AUGSBURG-only. The shipped game
// ships FIVE cities under Resources/gamedata/Cities/ (AUGSBURG.cty, BERLIN.cty,
// DRESDEN.cty, HANNOVER.cty, KOELN.CTY). This module proves the engine LOADS,
// RENDERS and SIMULATES every one of them — a strong robustness check that flushes
// out AUGSBURG-specific assumptions and, critically, proves loading multiple real
// cities in ONE process leaves NO leftover global state between loads (the play
// layer documents heavy global state — sim::g_objects/g_persons, the folded world
// tables, the CRT RNG, the kind-30 plant heap pointers — that MUST be fully reset
// between loads or city N+1 inherits city N).
//
// This module is the INTEGRATOR for that proof. It is ADDITIVE (a new file) and
// CALLS only already-reconstructed siblings (never redefines them):
//   app::MountRealGameAssets                       (app/real_boot.h)
//   io::LoadWorld / io::VfsShutdown                (io/save_world_load.h, io/vfs.h)
//   play::WorldRenderer                            (world_render.h)
//   play::RunGameDay / SeedGameDay                 (game_day.h)
//   play::HashFullWorld                            (world_digest.h)
//   sim::g_objects / g_persons / ResetEntityArrays (sim/entity.h)
//   crt::Srand                                     (crt/rand.h)
//
// The per-load determinism gotchas (documented in playable_slice.cpp and reused
// here verbatim in shape) are: ZeroWorldGlobals() the WHOLE folded world before
// each io::LoadWorld; crt::Srand(seed) right before every HashFullWorld() compared
// across reruns; normalize the kind-30 plant heap-pointer column. Without the full
// pre-load zero, city 2's hash inherits city 1's dirtied tables (cross-talk).
#include <cstdint>
#include <string>
#include <vector>

#include "guild/common/types.h"
#include "shim/IFileSystem.h"

namespace guild::shim { class IGraphicsDevice; }

namespace guild::play {

// ===========================================================================
// CityWitness — the deterministic per-city result of LoadAndExerciseCity. The
// whole struct is a pure function of (city bytes + seed): byte-identical on rerun.
// ===========================================================================
struct CityWitness {
    std::string   cityPath;              // the .cty path exercised
    bool          loaded       = false;  // io::LoadWorld succeeded
    std::uint32_t objectCount  = 0;      // world.objectCount
    std::uint32_t personCount  = 0;      // world.cityRecCount
    std::uint32_t nodeCount    = 0;      // world.sceneTileCount (scene-node count)

    // --- render (one frame of the loaded world) ---
    bool          rendered     = false;  // the device present() succeeded
    int           sceneObjects = 0;      // scene objects drawn in the frame
    int           nonClearPx   = 0;      // non-background pixels painted

    // --- one game-day of the REAL day cascade ---
    bool          dayRan       = false;  // RunGameDay executed
    int           dayStepsRun  = 0;      // composed day steps replayed

    // --- determinism oracle (the full-world digest before/after the day) ---
    std::uint64_t hashBefore   = 0;      // HashFullWorld() after load+render
    std::uint64_t hashAfter    = 0;      // HashFullWorld() after the game-day

    // The day evolved the world (the central sim proof).
    bool simEvolved() const { return hashBefore != hashAfter; }

    // A loaded city that rendered and whose sim evolved deterministically.
    bool ok() const { return loaded && rendered && simEvolved(); }
};

// ===========================================================================
// LoadAndExerciseCity — mount + load + render + sim ONE city.
//
//   1. mount `fs`/`gameDir` (app::MountRealGameAssets),
//   2. FULLY reset the live world (ZeroWorldGlobals + ResetEntityArrays) so this
//      city cannot inherit a prior city's globals,
//   3. io::LoadWorld(ctyPath) into the live sim arrays; report object/person/node
//      counts,
//   4. normalize the kind-30 plant heap-pointer column (reproducible digest) and
//      seed the deterministic economy baseline,
//   5. RENDER one frame of the live world through the REAL WorldRenderer into
//      `dev` (if non-null); report scene-objects + non-clear pixels,
//   6. crt::Srand(seed); HashFullWorld() -> hashBefore,
//   7. advance ONE game-day via play::RunGameDay (seeded by `seed`),
//   8. crt::Srand(seed); HashFullWorld() -> hashAfter,
//   9. shut the VFS down (the live arrays are left populated with the post-day
//      world) and return the witness.
//
// `gameDir` is the mounted real game directory; `ctyPath` is the relative VFS path
// to the city seed (the caller resolves the on-disk extension casing, e.g.
// KOELN.CTY vs .cty). `dev` must be init()'d to `fbW`x`fbH`x16bpp or be null.
//
// The SAME (city bytes, seed) reproduce a byte-identical witness on every call,
// even across a sequence of OTHER cities exercised in the same process (the
// pre-load full reset is what guarantees this independence).
CityWitness LoadAndExerciseCity(shim::IFileSystem* fs, const std::string& gameDir,
                                const std::string& ctyPath, std::uint32_t seed,
                                int fbW, int fbH, shim::IGraphicsDevice* dev);

// ===========================================================================
// ExerciseLiveWorld — the render+sim+witness half over the ALREADY-populated live
// world (no mount/load). Exposed so the unit tier can drive the SAME exercise
// pipeline over a SYNTHETIC live world (seeded directly into sim::g_objects) and
// assert the witness behaves identically — including the 0-object case, which must
// be handled gracefully (render produces a clean clear frame, no crash). It does
// NOT mount, load, or reset the live arrays (the caller owns those); it only
// renders, runs one day, and folds the before/after digest.
//
// `objectCount`/`personCount`/`nodeCount` are recorded into the witness verbatim
// (the synthetic caller knows them; the real loader fills them from WorldState).
CityWitness ExerciseLiveWorld(std::uint32_t seed,
                              std::uint32_t objectCount, std::uint32_t personCount,
                              std::uint32_t nodeCount,
                              int fbW, int fbH, shim::IGraphicsDevice* dev);

// Fully reset the live world to a blank, reproducible slate (zero every table
// HashFullWorld folds + ResetEntityArrays + the deterministic economy baseline).
// Exposed so the synthetic unit tier can reset between its pseudo-cities exactly as
// the real path resets between real cities (proving zero cross-talk in BOTH tiers).
void ResetLiveWorldForCity(std::uint32_t seed);

} // namespace guild::play
