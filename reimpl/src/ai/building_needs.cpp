#include "ai/building_needs.h"

// MeisterAi building-needs construction RULES (gilde.exe 0x4c7774 / 0x4c930c).
// The entity-array tally sweeps and the network packet-status flush are DEFERRED;
// the probability / candidate-mask / category-pick decision logic is translated
// 1:1 and exercised with seeded RNG.

namespace guild::ai {

const float kBuildDiffScale[5] = {4.0f, 3.0f, 2.5f, 1.5f, 1.0f};

// gilde.exe 0x4c7774 (probability step).
double BuildProbability(int gameTick, int difficulty) {
    int d = difficulty;
    if (d <= 0)
        d = 0;
    else if (d > 4)
        d = 4;
    // N = (gameTick / 4) + 1.
    // gilde.exe 0x4c77b5: mov edx,eax / sar edx,1Fh / shl edx,2 / sbb eax,edx /
    // sar eax,2 / inc eax — the MSVC signed-divide-by-4 idiom that rounds TOWARD
    // ZERO, not a bare arithmetic shift. For gameTick>=0 it equals gameTick>>2;
    // for negatives it differs (e.g. -1/4==0 vs -1>>2==-1). C++ integer division
    // truncates toward zero, so it reproduces the binary exactly.
    int n = (gameTick / 4) + 1;
    double scale = static_cast<double>(kBuildDiffScale[d]);
    return static_cast<double>(n) / (scale + static_cast<double>(n));
}

// gilde.exe 0x4c7774 (candidate-mask step).
void BuildCandidateMask(const std::vector<BuildCategory>& cats,
                        int* candidateMask, int* forceMask) {
    int cand = 0;
    int force = 0;
    for (std::size_t k = 0; k < cats.size(); ++k) {
        const BuildCategory& c = cats[k];
        double ratio = (c.total > 0)
                           ? static_cast<double>(c.owned) / static_cast<double>(c.total)
                           : 0.0;
        // candidate when owned < 3 OR ratio >= 0.5.
        if (c.owned < 3 || ratio >= static_cast<double>(kBuildRatioThreshold)) {
            cand |= (1 << k);
            if (c.owned == 0)
                force |= (1 << k);
        }
    }
    if (candidateMask)
        *candidateMask = cand;
    if (forceMask)
        *forceMask = force;
}

// gilde.exe 0x4c7774 (pick step).
int BuildPickCategory(int candidateMask, int forceMask, double p,
                      double roll_float, int roll_mod_k, int k) {
    if (forceMask != 0)
        return forceMask; // zero-count categories take absolute priority

    if (candidateMask == 0)
        return 0;

    // gated by the build probability: bail if the roll exceeds p.
    if (roll_float > p)
        return 0;

    // scan from the random start, rotating up to k times for a set bit.
    int bit = roll_mod_k % (k > 0 ? k : 1);
    int remaining = k;
    while ((candidateMask & (1 << bit)) == 0) {
        --remaining;
        bit = (bit + 1) % (k > 0 ? k : 1);
        if (remaining == 0)
            return 0;
    }
    return 1 << bit;
}

// gilde.exe 0x4c930c — RunBuildingTasks dispatch order.
std::vector<BuildingTask> RunBuildingTasksOrder() {
    return {
        BuildingTask::RequestBuildingCmd43,
        BuildingTask::RequestCmd134,
        BuildingTask::AssignWorkersToBuilding,
        BuildingTask::ClearDarkCorner,
        BuildingTask::SuperviseStammtisch,
        BuildingTask::UpdateBuildingHealthState,
    };
}

} // namespace guild::ai
