#include "ai/meister_buildtasks.h"

#include <array>

// MeisterAi building-needs ORCHESTRATION (gilde.exe 0x4c7774). The 256-slot tally
// sweep and the per-phase candidate/pick selection are translated 1:1 on top of the
// recovered decision cores in building_needs.cpp (BuildProbability /
// BuildCandidateMask / BuildPickCategory). The construct emission (the
// EnqueueObjectInteraction + GetPacketStatusById spin + EnqueueCmd15 funding) is a
// command-hook here. Phases 0 (owned<2 threshold + guild leader/member split) and 1
// (derived tallies) are DEFERRED — phase-specific entanglement (see report).

namespace guild::ai {

namespace {
// the per-phase AiPlayer building-type sets in candidate-bit order (bit 0 first).
// phase 2: {18 anchor, 21, 20, 14}; phase 3: {8 anchor, 22}.
const std::vector<int> kPhase2Types = {18, 21, 20, 14};
const std::vector<int> kPhase3Types = {8, 22};
const std::vector<int> kEmpty;
} // namespace

const std::vector<int>& BuildPhaseTypes(int phase) {
    if (phase == 2)
        return kPhase2Types;
    if (phase == 3)
        return kPhase3Types;
    return kEmpty;
}

// gilde.exe 0x4c7774 (tally sweep).
void BuildTallySweep(const std::vector<BuildSlot>& slots,
                     std::vector<int>& totals, std::vector<int>& owned,
                     int* members, int* leaders) {
    totals.assign(23, 0); // AiPlayer types 0..22 (the *a2 < 0x17 gate)
    owned.assign(23, 0);
    int memberCount = 0;
    int leaderCount = 0;

    for (const BuildSlot& s : slots) {
        if (!s.alive)
            continue;
        int t = s.aiType;
        if (t < 0 || t >= 23) // *a2 < 0x17u
            continue;
        ++totals[static_cast<std::size_t>(t)];
        if (s.ownedOrFree) {
            ++owned[static_cast<std::size_t>(t)];
        } else if (t == 7) {
            // type 7 (guild): split non-owned guild buildings into leaders/members.
            if (s.guildLeader)
                ++leaderCount;
            else
                ++memberCount;
        }
    }
    if (members)
        *members = memberCount;
    if (leaders)
        *leaders = leaderCount;
}

// gilde.exe 0x4c7774 (selection orchestration, phases 2 & 3).
int SelectBuildToConstruct(int phase, int gameTick, int difficulty,
                           const std::vector<int>& totals,
                           const std::vector<int>& owned,
                           double roll_float, int roll_modk,
                           const BuildEnv& env, std::vector<BuildCommand>& out) {
    const std::vector<int>& types = BuildPhaseTypes(phase);
    if (types.empty())
        return -1; // deferred phase

    // assemble the per-category tallies in bit order.
    std::vector<BuildCategory> cats;
    cats.reserve(types.size());
    for (int t : types) {
        BuildCategory c;
        c.owned = owned[static_cast<std::size_t>(t)];
        c.total = totals[static_cast<std::size_t>(t)];
        cats.push_back(c);
    }

    int candMask = 0, forceMask = 0;
    BuildCandidateMask(cats, &candMask, &forceMask);

    double p = BuildProbability(gameTick, difficulty);
    int k = static_cast<int>(types.size());
    int pick = BuildPickCategory(candMask, forceMask, p, roll_float, roll_modk, k);
    if (pick == 0)
        return -1;

    // map the chosen single-bit mask back to its category index / AiPlayer type.
    int bit = 0;
    while (((pick >> bit) & 1) == 0)
        ++bit;
    int aiType = types[static_cast<std::size_t>(bit)];
    int ownedN = owned[static_cast<std::size_t>(aiType)];

    BuildCommand cmd;
    cmd.aiType = aiType;
    cmd.fundWorth = env.worth_of ? env.worth_of(aiType, ownedN) : 0;
    cmd.variant = env.variant_of ? env.variant_of(aiType, ownedN) : 0;
    out.push_back(cmd);
    return aiType;
}

} // namespace guild::ai
