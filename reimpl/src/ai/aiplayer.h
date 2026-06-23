#pragma once
// AiPlayer / per-building-type AI descriptor + intrigue action selection for the
// Guild simulation (gilde.exe).
//
// The "AiPlayer" record array (base dword_13CE294, stride 589) is indexed by the
// building/person KIND byte (the record-type at Person+0 / ObjectRec type), NOT
// by faction. Each 589-byte entry is the behavior descriptor for that building
// type: which Guild-Master AI routine drives it (the +0 "AI class" byte), its
// production/income multiplier (+583), and packed guard-target data (+544). The
// turn director (MeisterAi_ProcessPlayerTurn) and the per-meister evaluators
// (Ai_EvaluateMeister) index it as `dword_13CE294 + 589*type`.
//
//   recovered offsets (from Ai_EvaluateMeister @0x4533a8, PlanProduction
//   @0x4596e4, ProcessPlayerTurn @0x5321ec):
//     +0    u8   AI-class id: dispatch key. 4=Diebe(thieves), 5=Bank,
//                8/14=craft-production, 9=production, 16=Ambush, 19=Wache(guard).
//     +544  i32  packed guard-target (HIBYTE used: >>24).
//     +583  u8   production/income multiplier ("Vermehrungsfaktor"): 1/2/3 tiers
//                scale output (PlanProduction) and tax (ProcessPlayerTurn:
//                16000 * mult). Other values => no production scaling.
//
// This file also ports the self-contained DATA/RULES cores of the AI-player
// intrigue selectors (the deeply-coupled entity-query halves are deferred — see
// the .cpp banner). What is faithful here:
//   * AiPlayer_TrySingleAttack (0x47d0e8) — the attack-acceptance probability
//     rule once a target + its in-progress action depth are known.
//   * AiPlayer_PickBestTarget (0x47d7f8) — favorability+rank scoring with the
//     RNG tie-break over up to 4 office candidates.
#include "guild/common/types.h"

namespace guild::ai {

// --- AiPlayer / type-descriptor record ---------------------------------------
constexpr int kAiPlayerStride = 589;

// AI-class dispatch ids (the +0 byte; the switch in Ai_EvaluateMeister).
enum AiClass : u8 {
    kAiClassDiebe       = 4,   // thieves' guild meister
    kAiClassBank        = 5,   // banker meister
    kAiClassCraftA      = 8,   // craft production (CalcMeisterCraftProduction)
    kAiClassProduction9 = 9,   // generic production (MeisterAi_PlanProduction)
    kAiClassCraftB      = 14,  // craft production
    kAiClassAmbush      = 16,  // ambush meister
    kAiClassWache       = 19,  // guard meister
};

// Per-type behavior descriptor accessor over an AiPlayer table (the original is
// the global at dword_13CE294; we accept a base pointer so it is testable).
struct AiPlayerTable {
    u8* base = nullptr;       // dword_13CE294
    bool loaded = false;      // mirrors the "array base != 0" guard

    u8* record(int type) const { return base + kAiPlayerStride * type; }

    // +0: AI-class dispatch byte for `type`.
    u8 ai_class(int type) const { return base[kAiPlayerStride * type + 0]; }
    // +583: production/income multiplier tier.
    u8 multiplier(int type) const { return base[kAiPlayerStride * type + 583]; }
    // +544: packed guard target (the HIBYTE is the meaningful field).
    i32 guard_target_packed(int type) const {
        u8* r = base + kAiPlayerStride * type + 544;
        return static_cast<i32>(r[0] | (r[1] << 8) | (r[2] << 16) | (static_cast<u32>(r[3]) << 24));
    }
    u8 guard_target(int type) const {
        return static_cast<u8>(guard_target_packed(type) >> 24);
    }
};

// AI-class dispatch outcome (which meister routine Ai_EvaluateMeister selects).
enum class MeisterRoutine {
    kNone,
    kCraftProduction,  // category 1/4 + class 8/14
    kProduction,       // category 1/4 + other class
    kFarming,          // category 2
    kWache,            // class 19
    kDiebe,            // class 4
    kAmbush,           // class 16
    kBank,             // class 5
    kPlanProduction,   // class 9
};

// gilde.exe 0x4533a8 (core) — Ai_EvaluateMeister dispatch classifier. Given the
// building category (Building_MapTypeToCategory: 1/2/4 etc.) and the AiPlayer
// class byte for the building type, returns which per-meister routine runs. This
// is the pure decision core of EvaluateMeister (the surrounding budget logging,
// currency snapshot, and the routine calls themselves are deferred).
MeisterRoutine ClassifyMeisterRoutine(int category, u8 aiClass);

// gilde.exe 0x4533a8 — VIBE_Ai_EvaluateMeister dispatch tail (rule 13 bind). After
// its budget logging + currency snapshot + danger-grid stamps, the original
// classifies the meister by VIBE_Building_MapTypeToCategory(typeByte) + the
// AiPlayer +0 class byte and dispatches to the matching VIBE_Ai_CalcMeister*
// routine. This entry point performs exactly that real call edge:
// ClassifyMeisterRoutine then guild::sim::DispatchMeisterCalc (the wave-19
// reconstructed Calc bodies for Farming/Wache/Diebe/Ambush). Production/Bank/
// PlanProduction stay with their own planner module (DispatchMeisterCalc no-ops
// them). `meisterRec` is the live 536-byte person record (the original's v2);
// `attackBudget` is the meister's currency snapshot (v2[110]). The dispatched Calc
// runs against guild::sim::g_meisterLeaves / g_meisterCmdSink / g_meisterGameTime
// (the engine bridge sets these; null leaves take the original's null/zero path).
// Returns the classified routine.
MeisterRoutine EvaluateMeister(int category, u8 aiClass, void* meisterRec,
                               int attackBudget);

// --- intrigue selection rules (self-contained cores) -------------------------

// gilde.exe 0x47d0e8 (core) — AiPlayer_TrySingleAttack acceptance rule. Once a
// candidate target has been found and its in-progress action depth `actionDepth`
// is known (number of queued actions on that actor), decide whether to commit a
// single attack. Accept iff actionDepth <= 3 AND RandomFloatScaled() >=
// actionDepth * 0.25. On accept, the caller writes action code 7 + the target id.
// Returns true if the attack should be committed.
bool TrySingleAttackAccept(int actionDepth);

// PickBestTarget scoring: per-candidate score = favorability + rankBonus (or a
// "rejected" sentinel). Inputs are precomputed per the original's
// Ai_ComputePersonFavorability + Office_CanPromoteRank calls.
struct TargetCandidate {
    bool valid = false;     // Person_FindRecordById succeeded & CanPromoteRank ok
    float score = 0.0f;     // favorability + rankBonus
    i32 targetId = 0;       // the office char id returned on selection
};

// gilde.exe 0x47d7f8 (core) — AiPlayer_PickBestTarget selection. Picks the
// highest-scoring valid candidate; on a tie (>1 at the max score) it picks one
// uniformly at random among the tied maxima (RandomModulo(4) start, scan). Up to
// 4 candidates. Returns the chosen candidate's targetId, or -1 if none valid.
i32 PickBestTarget(const TargetCandidate candidates[4]);

} // namespace guild::ai
