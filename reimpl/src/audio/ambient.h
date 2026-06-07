#pragma once
#include "guild/common/types.h"

// =============================================================================
// guild::audio — ambient / wildlife sound director (gilde.exe, msx ambient).
//
//   0x5800f0  VIBE_Ambient_UpdateWildlifeSounds
//   0x582858  VIBE_Ambient_StartMarketLoop
//   0x5828bc  VIBE_Ambient_StopMarketLoop
//
// The wildlife updater runs once per frame while outdoors. For each wildlife
// sound category it keeps a per-SEASON cooldown and threshold: a sound may
// trigger only when (now - lastTrigger[season]) has exceeded a randomized delay
// AND a RandNext()-driven probability gate passes. The original then plays the
// sample positionally; here the decision (the deterministic, RNG-exact part) is
// separated from playback so it is testable in isolation.
//
// RNG FIDELITY: each category draws RandNext() twice per evaluated tick — once
// for the delay jitter (`% 2000`), once for the probability gate
// (`RandNext()*(1/32767) + threshold > 1.0`). Call order matches the original.
// Per-season tables (base delays, thresholds, volume scales) are config-loaded
// in the original (no static initializer in the binary), so they are supplied
// by the caller.
// =============================================================================
#include "crt/rand.h"

namespace guild::audio {

// Recovered scalar constants (get_bytes; bit-exact).
constexpr float kAmbDelayJitterMax = 2000.0f; // flt_625DC8 / `% 2000`
constexpr float kAmbRandNorm = 3.0518509447574615e-05f; // flt_625DD0 (1/32767)
constexpr float kAmbVolumeScale = 127.0f;     // flt_625DD4
constexpr float kAmbVolumeClamp = 64.0f;      // flt_625DD8
constexpr float kAmbHeightScale = 0.0005000000237487257f; // flt_625DCC

// One wildlife sound category's per-season state + tuning (modelled from the
// dword_642114/642118 (delay/lastTrigger) and flt_642108/64210C (threshold/vol)
// table families, indexed by season 0..3).
struct WildlifeCategory {
    int baseDelay[4] = {0, 0, 0, 0};   // dword_642114[season]: min cooldown
    float threshold[4] = {0, 0, 0, 0}; // flt_642108[season]: prob bias (gate>1.0)
    int lastTrigger[4] = {0, 0, 0, 0}; // dword_642118[season]: last fire time
};

// gilde.exe 0x5800f0 (per-category decision core). Evaluates whether the sound
// should fire this tick for `season` at time `now`. Draws RandNext() for the
// delay jitter and (only if the cooldown elapsed) for the probability gate, in
// that order — matching the original. On a fire it updates lastTrigger[season]
// and returns true. `modifier` is the "numDropsModifier" suppressor: the drops
// category suppresses above its cap (pass the cap, or a large value to disable).
bool WildlifeShouldTrigger(WildlifeCategory& cat, int season, int now,
                           int modifier, int suppressAbove);

// Reset all per-season lastTrigger timers to `t` (the !boden branch resets the
// timers to the current frame so nothing fires the moment we go outdoors).
void WildlifeResetTimers(WildlifeCategory& cat, int t);

// Market-loop state (a single 3D-sound handle, dword_6420F4 in the original).
struct MarketLoop {
    int handle = 0; // 0 = not playing
};

// gilde.exe 0x582858 — start the looping market ambience iff not already
// playing. `newHandle` is the handle returned by the 3D-sound layer (non-zero
// to indicate success). Returns true if it started a new loop.
bool MarketStartLoop(MarketLoop& m, int newHandle);

// gilde.exe 0x5828bc — stop + detach the market loop if playing. Returns true
// if it stopped one.
bool MarketStopLoop(MarketLoop& m);

// Per-tick volume helper used by the ambient track logic: clamp a population-
// derived volume to [.., 127] with the original's 64.0 floor branch
// (vol = scaledPop; if 127.0 >= scaledPop then keep else use 64.0). Matches the
// `if (flt_625DD8 >= v) keep; else 64` ... wait: the original takes min-style.
// volume = (clampMax >= pop) ? pop : floor64.
int AmbientPopulationVolume(float pop);

} // namespace guild::audio
