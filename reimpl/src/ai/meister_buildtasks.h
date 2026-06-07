#pragma once
// MeisterAi building-needs ORCHESTRATION — the deferred 256-slot construction
// sweep + the per-turn building-task dispatch (gilde.exe 0x4c7774 / 0x4c930c).
//
// ProcessBuildingNeeds (0x4c7774, ~1455 insns) is the city-level "what should the
// AI BUILD next" pass. This module translates its ORCHESTRATION on top of the
// already-recovered decision cores in building_needs.h:
//   1. BalanceCityGoods (the wealth top-up, recovered in meister_trade2).
//   2. The 256-slot building tally sweep over the object array (dword_13CE298,
//      stride 169): for each living building, look up its AiPlayer type byte and
//      bump the city-wide tally `total[type]`; if it is unowned/own-faction bump
//      `owned[type]` too (special handling for type 7 — guild members vs leaders).
//   3. The build-probability p = N/(diffScale+N) (BuildProbability).
//   4. Per turn-phase (gameTick % 4 selects one of four candidate category sets),
//      build the candidate + force masks (BuildCandidateMask), pick a category
//      (BuildPickCategory), map it to a concrete building variant, and emit the
//      construct command (EnqueueObjectInteraction + fund via EnqueueCmd15).
//
// The two clean phases this module covers (candidate threshold owned<3, the same
// shape as BuildCandidateMask) address fixed sets of AiPlayer building types:
//   phase 2 (tick%4==2): {18,21,20,14}     (4 categories; bit0=18 anchor)
//   phase 3 (tick%4==3): {8,22}            (2 categories; bit0=8 anchor)
// Phases 0 (tick%4==0, owned<2 threshold + the type-7 guild leader/member split)
// and 1 (the derived v8/v9/v10 tallies) carry phase-specific entanglement and are
// DEFERRED (LISTed in the report). The per-type ComputeRoomWorth /
// ComputeVariantIndex / construct-command emission (the scene-tree sample-building
// lookup + the network packet-status spin) are deeply-coupled leaf reads injected
// through a BuildEnv hook so the SELECTION orchestration is testable.
#include <vector>
#include <functional>

#include "guild/common/types.h"
#include "ai/building_needs.h"

namespace guild::ai {

// the per-phase AiPlayer building-type sets (in candidate bit order; bit 0 first).
extern const std::vector<int>& BuildPhaseTypes(int phase);

// One building the 256-slot sweep reads (object array slot).
struct BuildSlot {
    bool alive = false;     // *v5 (alive byte)
    int  aiType = 0;        // AiPlayer type byte (*a2), 0..22
    bool ownedOrFree = false; // owner == 0xFFFF (unowned) || owner == current player
    bool guildLeader = false; // for type 7: HIBYTE(dword_12CE919) set => leader
};

// gilde.exe 0x4c7774 (tally sweep). Walk the building slots, producing per-type
// city-wide totals and owned counts. type 7 (guild) splits owned-or-free into
// leaders (guildLeader) vs members; non-7 types bump owned[type] when ownedOrFree.
// `totals`/`owned` are indexed by aiType (0..22). Returns the {member,leader} type-7
// split via *members / *leaders.
void BuildTallySweep(const std::vector<BuildSlot>& slots,
                     std::vector<int>& totals, std::vector<int>& owned,
                     int* members, int* leaders);

// A construct decision the pass emits.
struct BuildCommand {
    int  aiType = 0;     // the chosen AiPlayer building type
    int  variant = 0;    // BuildingType_ComputeVariantIndex result
    int  fundWorth = 0;  // EnqueueCmd15 amount (2 * worth)
};

// Leaf reads for the per-category construct step (injected).
struct BuildEnv {
    // 2 * worth for a category (ComputeRoomWorth of a sample building, or
    // SumFlaggedSlotsWorth when the player owns < 3 of the type).
    int (*worth_of)(int aiType, int owned) = nullptr;
    // BuildingType_ComputeVariantIndex(buildingGroup, qualityArg).
    int (*variant_of)(int aiType, int owned) = nullptr;
    // building group id for an AiPlayer type (the v34/v52 case constant).
    int (*group_of)(int aiType) = nullptr;
};

// gilde.exe 0x4c7774 (selection orchestration) — choose and emit a build for the
// given `phase` (gameTick % 4). Builds the candidate/force masks from the phase's
// per-type tallies, applies the probability gate, picks a category, maps it to a
// variant via `env`, and appends one BuildCommand to `out`. `gameTick`/`difficulty`
// feed BuildProbability. `roll_float` = RandomFloatScaled(); `roll_modk` =
// RandomModulo(k). Returns the chosen AiPlayer type, or -1 for no build.
int SelectBuildToConstruct(int phase, int gameTick, int difficulty,
                           const std::vector<int>& totals,
                           const std::vector<int>& owned,
                           double roll_float, int roll_modk,
                           const BuildEnv& env, std::vector<BuildCommand>& out);

} // namespace guild::ai
