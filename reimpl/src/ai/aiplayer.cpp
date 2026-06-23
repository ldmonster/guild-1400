#include "ai/aiplayer.h"

#include "sim/ai_meister.h"   // guild::sim::DispatchMeisterCalc (wave-19 Calc bodies)
#include "util/math_random.h"
#include "util/math_rng_float.h"

// Deferred (deeply coupled to the entity/query/office/He clusters — translated
// only as the rule cores above; full bodies LISTed in the module report):
//   VIBE_AiPlayer_TrySingleAttack 0x47d0e8 — full target search
//     (ObjectSearch_FindMatchingColors + CharAction_FindActionByActor).
//   VIBE_AiPlayer_TryGroupAttack  0x47d1d4, _TryRangedAttack 0x47d364.
//   VIBE_AiPlayer_PickBestTarget  0x47d7f8 — full candidate gather
//     (Office_GetEntryByCity + Person_FindRecordById + CanPromoteRank +
//      Ai_ComputePersonFavorability).
//   VIBE_AiPlayer_FindRivalToConfront 0x47d970, _FindOpponentBuilding 0x47dac4,
//   _QueueSpyMission 0x47da2c, _BuyDarkCorner 0x47dd04, _EvaluateApproachDirection
//   0x47d6a0, the Exec/Eval intrigue emitters (0x471ae0/0x47d4cc..0x47d620).
//   VIBE_Ai_EvaluateMeister 0x4533a8 — budget logging + currency snapshot + the
//   per-routine calls (CalcMeister*); only the dispatch core is ported here.

namespace guild::ai {

namespace {
constexpr double kAttackProbScale = 0.25;     // flt_61AE3C
constexpr float  kPickSentinel    = -10.0f;   // v20 init (best score floor)
} // namespace

// gilde.exe 0x4533a8 (core) — Ai_EvaluateMeister dispatch classifier.
MeisterRoutine ClassifyMeisterRoutine(int category, u8 aiClass) {
    // category 1 or 4 -> production family; class 8/14 -> craft, else generic.
    if (category == 1 || category == 4) {
        if (aiClass == kAiClassCraftA || aiClass == kAiClassCraftB)
            return MeisterRoutine::kCraftProduction;
        return MeisterRoutine::kProduction;
    }
    if (category == 2)
        return MeisterRoutine::kFarming;

    // otherwise switch on the AI-class byte directly.
    switch (aiClass) {
    case kAiClassWache:       return MeisterRoutine::kWache;
    case kAiClassDiebe:       return MeisterRoutine::kDiebe;
    case kAiClassAmbush:      return MeisterRoutine::kAmbush;
    case kAiClassBank:        return MeisterRoutine::kBank;
    case kAiClassProduction9: return MeisterRoutine::kPlanProduction;
    default:                  return MeisterRoutine::kNone;
    }
}

// gilde.exe 0x4533a8 — VIBE_Ai_EvaluateMeister dispatch tail. Connects the two
// reconstructed pieces along the original's real call edge: classify by
// category+class, then dispatch to the matching CalcMeister* (Farming/Wache/Diebe/
// Ambush) reconstructed in src/sim/ai_meister*. The decompile (0x4533a8) routes:
//   MapTypeToCategory == 1/4 -> CraftProduction(class 8/14) / Production
//   MapTypeToCategory == 2   -> CalcMeisterFarming
//   else switch(class): 19->Wache 4->Diebe 16->Ambush 5->Bank 9->PlanProduction
// ClassifyMeisterRoutine encodes exactly that mapping; DispatchMeisterCalc runs the
// 4 reconstructed routines and no-ops Production/Bank/PlanProduction (their own
// planner module owns them — deferred there). meisterRec is the original's v2.
MeisterRoutine EvaluateMeister(int category, u8 aiClass, void* meisterRec,
                               int attackBudget) {
    MeisterRoutine routine = ClassifyMeisterRoutine(category, aiClass);
    guild::sim::DispatchMeisterCalc(static_cast<int>(routine),
                                    static_cast<u8*>(meisterRec), attackBudget);
    return routine;
}

// gilde.exe 0x47d0e8 (core) — single-attack acceptance probability rule.
bool TrySingleAttackAccept(int actionDepth) {
    if (actionDepth > 3)
        return false;
    double gate = static_cast<double>(actionDepth) * kAttackProbScale;
    return guild::util::RandomFloatScaled() >= gate;
}

// gilde.exe 0x47d7f8 (core) — best-target selection with RNG tie-break.
i32 PickBestTarget(const TargetCandidate candidates[4]) {
    int bestIdx = -1;
    bool tied = false;
    float best = kPickSentinel;

    for (int j = 0; j < 4; ++j) {
        if (candidates[j].valid && candidates[j].score > best) {
            tied = false;
            bestIdx = j;
            best = candidates[j].score;
        } else if (candidates[j].valid && candidates[j].score == best) {
            tied = true;
        }
    }
    if (bestIdx < 0)
        return -1;
    if (!tied)
        return candidates[bestIdx].targetId;

    // Tie: scan from a random start over the 4 slots for one at the max score.
    int idx = static_cast<int>(guild::util::RandomModulo(4));
    int guard = 4;
    int chosen = -1;
    while (true) {
        if (candidates[idx].valid && candidates[idx].score == best) {
            chosen = idx;
            break;
        }
        idx = (idx + 1) % 4;
        if (--guard == 0)
            break;
    }
    if (chosen < 0)
        return -1;
    return candidates[chosen].targetId;
}

} // namespace guild::ai
