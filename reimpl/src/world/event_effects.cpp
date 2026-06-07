#include "world/event_effects.h"

#include <cmath>

// Faithful port of three scripted-event effect bodies (gilde.exe). The phase
// decodes, the distance->duration arithmetic, the price-spike write band, and the
// production-gauge reschedule branches are reproduced 1:1; the .esc VM, 3D sound,
// scene-slot and command commits are the engine's. Truncation matches the
// VIBE_Coord_ConvertX (round-toward-zero) idiom in the originals.

namespace guild::world {

// ===========================================================================
// FireRaidComputeDuration.
// ===========================================================================
//   v20 = neighbour ? sqrt(dx^2+dy^2+dz^2) * 0.001 : 1.0;
//   v18 = (1.5 >= v20) ? v20 : 1.5;
//   duration = (int)(v18 * (double)baseValue);
double FireRaidDurationFactor(double distance, bool hasNeighbour) {
    double raw = hasNeighbour ? distance * kFireDistScale : 1.0;
    // if ( dbl_61FC68 >= v20 ) v18 = v20; else v18 = 1.5;
    return (kFireDurClamp >= raw) ? raw : kFireDurClamp;
}

i32 FireRaidComputeDuration(double distance, bool hasNeighbour, i32 baseValue) {
    double factor = FireRaidDurationFactor(distance, hasNeighbour);
    double v10 = factor * static_cast<double>(baseValue);
    return static_cast<i32>(v10);   // VIBE_Coord_ConvertX -> trunc toward zero
}

// ===========================================================================
// PriceStateMachine.
// ===========================================================================
// result = *(int*)(a1+112) + 2;  switch(result) { 0,1: free; 2: spike; 3: penalise }
PriceEventPhase PriceEventClassify(int phaseCounter) {
    int result = phaseCounter + 2;
    switch (result) {
        case 0:
        case 1:
            return PriceEventPhase::kFreeHandler;
        case 2:
            return PriceEventPhase::kSpike;
        case 3:
            return PriceEventPhase::kPenalise;
        default:
            return PriceEventPhase::kIdle;
    }
}

// v11 = VIBE_Math_RandomModulo(5u); AdjustMood(target, -(v11 + 5));
i32 PriceEventMoodPenalty(int rand5) {
    return -(rand5 + 5);
}

// ===========================================================================
// BuildingProductionTrigger.
// ===========================================================================
ProductionTriggerAction ProductionTriggerStep(int phaseCounter, bool gaugeBelowZero,
                                              float gauge) {
    // if ( result < -1 ) { if ( result != -2 ) return; free; }
    if (phaseCounter < -1) {
        // -2 frees; anything more negative is "idle" (the original returns result).
        return (phaseCounter == -2) ? ProductionTriggerAction::kTeardown
                                    : ProductionTriggerAction::kIdle;
    }
    // if ( result <= -1 ) free;   (i.e. -1)
    if (phaseCounter <= -1)
        return ProductionTriggerAction::kTeardown;
    // if ( !result ) { gauge branch }
    if (phaseCounter == 0) {
        if (gaugeBelowZero)                 // dword_12CEAD8[..] < 0
            return ProductionTriggerAction::kWaitHours;     // advance 4h
        if (gauge < 1.0f)                   // DrawProductionGauge() < 1.0
            return ProductionTriggerAction::kWaitSeconds;   // advance 20s
        return ProductionTriggerAction::kProduce;           // fire + advance 4h
    }
    return ProductionTriggerAction::kIdle;  // result > 0
}

} // namespace guild::world
