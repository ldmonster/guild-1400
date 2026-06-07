#include "audio/ambient.h"

namespace guild::audio {

bool WildlifeShouldTrigger(WildlifeCategory& cat, int season, int now,
                           int modifier, int suppressAbove) {
    // Delay jitter is drawn unconditionally (matches the original: the RandNext
    // for `% 2000` happens before the cooldown compare).
    int jitter = guild::crt::RandNext() % 2000;
    int delay = jitter + cat.baseDelay[season];

    if (now - cat.lastTrigger[season] > delay && modifier < suppressAbove) {
        cat.lastTrigger[season] = now;
        // Probability gate: RandNext()*(1/32767) + threshold > 1.0.
        float gate = (float)((double)guild::crt::RandNext() * (double)kAmbRandNorm
                             + (double)cat.threshold[season]);
        return gate > 1.0f;
    }
    return false;
}

void WildlifeResetTimers(WildlifeCategory& cat, int t) {
    for (int s = 0; s < 4; ++s)
        cat.lastTrigger[s] = t;
}

bool MarketStartLoop(MarketLoop& m, int newHandle) {
    if (m.handle)
        return false;
    if (!newHandle)
        return false;
    m.handle = newHandle;
    return true;
}

bool MarketStopLoop(MarketLoop& m) {
    if (!m.handle)
        return false;
    m.handle = 0;
    return true;
}

int AmbientPopulationVolume(float pop) {
    // Original: if (64.0 >= pop) use pop else 64.0  == min(pop, 64.0).
    float v = (kAmbVolumeClamp >= pop) ? pop : kAmbVolumeClamp;
    return (int)v;
}

} // namespace guild::audio
