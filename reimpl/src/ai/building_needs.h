#pragma once
// MeisterAi building-needs / construction planner (gilde.exe 0x4c7774
// ProcessBuildingNeeds + 0x4c930c RunBuildingTasks).
//
// ProcessBuildingNeeds is the city-level "what should the AI BUILD next" pass. Run
// once per turn, it:
//   1. Computes a global build-probability `p = N/(diffScale[difficulty]+N)` where
//      N = (gameTick/4)+1 and diffScale = {4,3,2.5,1.5,1.0} by difficulty (0..4).
//   2. Counts the player's buildings by type into per-category tallies, plus the
//      city-wide totals (employment grids).
//   3. Per turn-phase (tick % 4 selects one of four candidate sets), builds a
//      candidate bitmask: a category is a candidate when its count < 3 OR its
//      "ratio" (owned/total) >= 0.5; a "force" bit is set when the count is 0.
//   4. Picks a category: if any force bit is set, build that (zero-count categories
//      have absolute priority); otherwise roll RandomFloatScaled() and bail if it
//      exceeds `p`, then pick a set candidate bit (rotating from RandomModulo(k)).
//   5. Constructs the chosen building type (EnqueueObjectInteraction) and funds it.
//
// The huge entity-array sweeps (the 256-slot building tally, the per-category
// dword_13CE298 scan to find a sample building, the network packet-status flush)
// are DEFERRED — pure plumbing. The DECISION RULES (probability, candidate mask,
// category pick) are translated 1:1 below and injected with seeded RNG so they are
// deterministic and testable.
//
// Recovered constants (byte-exact):
//   flt_4C5140[5] = {4.0, 3.0, 2.5, 1.5, 1.0}   difficulty build-scale table
//   flt_61E85C    = 0.5                          ratio threshold
#include <vector>

#include "guild/common/types.h"

namespace guild::ai {

// difficulty build-scale table flt_4C5140 (difficulty clamped to 0..4).
extern const float kBuildDiffScale[5];
constexpr float kBuildRatioThreshold = 0.5f; // flt_61E85C

// gilde.exe 0x4c7774 (probability step) — the global build probability.
//   N = (gameTick / 4) + 1;  p = N / (diffScale[clamp(difficulty,0,4)] + N).
// `gameTick` is the low dword of qword_13CE852; the original uses a signed /4 via
// arithmetic shift (we reproduce gameTick>>2, sign-propagating).
double BuildProbability(int gameTick, int difficulty);

// One category tally the candidate-mask rule reads.
struct BuildCategory {
    int owned = 0; // # of this type the player owns
    int total = 0; // city-wide total of this type (ratio denominator)
};

// gilde.exe 0x4c7774 (candidate-mask step) — build the candidate + force masks for
// a phase's category set. For category bit k:
//   candidate bit set when owned < 3 OR (owned/total) >= 0.5  (total<=0 -> ratio 0)
//   force bit set when owned == 0
// Returns the candidate mask in *candidateMask and the force mask in *forceMask.
// `cats` lists the phase's categories in bit order (index 0 = bit 1, etc.). NOTE:
// the original's lowest bit (1) is the "anchor" category that is always evaluated
// last via the fall-through; we treat all entries uniformly.
void BuildCandidateMask(const std::vector<BuildCategory>& cats,
                        int* candidateMask, int* forceMask);

// gilde.exe 0x4c7774 (pick step) — choose which category bit to build.
//   if forceMask != 0  -> return forceMask (zero-count categories win outright).
//   else: if roll_float > p -> return 0 (no build this turn).
//         else pick a set bit of candidateMask, scanning from RandomModulo(k):
//           start = roll_mod_k;  rotate +1 (mod k) up to k times until a set bit;
//           return (1 << foundBit), or 0 if none set.
// `k` is the number of categories in this phase (the modulus of the start roll).
// `roll_float` is one RandomFloatScaled() draw in [0,1); `roll_mod_k` one
// RandomModulo(k) draw. Returns the single-bit mask to build (0 = none).
int BuildPickCategory(int candidateMask, int forceMask, double p,
                      double roll_float, int roll_mod_k, int k);

// ---------------------------------------------------------------------------
// gilde.exe 0x4c930c — RunBuildingTasks. A fixed dispatch of six per-turn building
// sub-tasks, in order. The leaf tasks are owned elsewhere / deferred; this records
// the exact call ORDER so the turn pass can be wired faithfully.
// ---------------------------------------------------------------------------
enum class BuildingTask {
    RequestBuildingCmd43, // VIBE_MeisterAi_RequestBuildingCmd43
    RequestCmd134,        // VIBE_MeisterAi_RequestCmd134
    AssignWorkersToBuilding,
    ClearDarkCorner,
    SuperviseStammtisch,
    UpdateBuildingHealthState,
    // (NullTick — the no-op tail — is omitted)
};
// Returns the dispatch order RunBuildingTasks invokes its sub-tasks in.
std::vector<BuildingTask> RunBuildingTasksOrder();

} // namespace guild::ai
