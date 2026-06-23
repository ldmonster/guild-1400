#pragma once
// gilde.exe — Game-event / NPC-event state-machine cluster (recon batch 2).
//
// This file reconstructs the *portable, pure* trigger / step-transition /
// outcome-roll cores of the game-event and NPC-event handler family. The
// reconstructed functions are the per-type "step" handlers that the engine
// drives through its handler-entry (He) dispatcher. Each handler is a small
// state machine keyed on the handler-entry's state field (offset +112), and
// each step advances game time, rolls RNG-driven outcomes, and emits engine
// effects (messages, command packets, scene spawns).
//
// The original bodies are deeply coupled to the live entity / person-record /
// building / command-queue / handler-entry clusters and to the text/UI message
// renderer. Those coupled leaves are NOT reproduced here (they belong to the
// entity/world/command layers); instead the genuinely 1:1 numeric/logic heart
// — the trigger conditions, the state-transition arithmetic, the RNG outcome
// gates, and the recovered constant tables — is reconstructed byte-for-byte
// against the Hex-Rays decompile and made deterministic + golden-vector
// testable by taking RNG draws as explicit parameters.
//
// Functions whose logic cores are reconstructed here (provenance per fn):
//   VIBE_NpcEvent_RunSimDiseases          0x4d766c — disease outbreak roll/scan
//   VIBE_NpcEvent_BardCreateScriptStep    0x4d66b4 — bard event step machine
//   VIBE_NpcEvent_BroadcastWinnerPointsStep 0x4d9a60 — winner-points broadcast step
//   VIBE_NpcEvent_OfficeMatchmakingStep   0x4da978 — office matchmaking step machine
//   VIBE_Event_InitBetriebRun             0x4ef900 — "found business" packet step
//   VIBE_Event_AllocProduktion            0x4f2a80 — production-action allocator
//   VIBE_Event_AllocWorkActorAction       0x4f2530 — work-actor allocator
//   VIBE_Event_OpenBuildingDialogRun      0x4f1d48 — building-dialog event step
//   VIBE_Event_OpenHelpEventsForKind      0x4f1a00 — help-ini selection by kind
//   VIBE_Event_AllocSlotResetAction       0x4f41bc — slot-reset action allocator
//   VIBE_Event_AllocGebaeudeBauen         0x4f5110 — building-build allocator
//
// Coupled leaves intentionally omitted (rule 8 — no fake stand-ins) and only
// reported as deferred at the end of event_recon2.cpp:
//   VIBE_Event_OpenHelpEventsFromIni 0x4f1850 — INI parser + MessageBoxA (I/O/UI leaf)
//   VIBE_Event_SpawnBuildEffectByName 0x4f58d4 — scene object-group spawn (Vulkan/scene leaf)
//   VIBE_Event_BroadcastFamilyNews 0x58c92c — pure text-message builder (UI leaf)
//   VIBE_Event_RegisterHandlerTable 0x4f1ed0 — handler-fn pointer registrar (dispatch wiring)
//   VIBE_NpcAction_HandleAccidentRandom / VIBE_He_SendEntityMessage / command
//     emitters / scene loaders — world mutation + UI, deferred.

#include "guild/common/types.h"

namespace guild::sim {

using namespace guild;

// ===========================================================================
// Recovered constant pools (.rdata; via get_bytes).
// ===========================================================================

// VIBE_NpcEvent_RunSimDiseases @0x4d766c float pool (0x61ef3c..0x61ef5b):
constexpr float  kDiseaseSeverityScale100 = 100.0f;  // flt_61EF3C (0x42c80000)
constexpr float  kDiseaseWorkstationScale = 20.0f;   // flt_61EF40 (0x41a00000)
constexpr double kDiseaseBaselineHalf     = 0.5;     // dbl_61EF44 (0x3fe0...)
constexpr float  kDiseaseFarmRiskScale    = 0.02f;   // flt_61EF4C (0x3ca3d70a)
constexpr float  kDiseaseSpecialRiskScale = 0.0125f; // flt_61EF50 (0x3c4ccccd)
constexpr double kDiseaseEmptyBase25      = 25.0;    // dbl_61EF54 (0x4039...)

// dword_4C9768 — 16-entry disease-stride table (the candidate stride/seed pool
// from which the per-run stride at entry+176 is randomly picked, index rand%16).
constexpr u32 kDiseaseStrideTable[16] = {
    0x1, 0x3, 0x5, 0x7, 0xB, 0xD, 0x11, 0x13,
    0x2ED, 0x2EF, 0x2F3, 0x2F5, 0x2F9, 0x2FB, 0x2FD, 0x2FF,
};
constexpr int kDiseasePersonSlotCount = 768;  // % 768 ring (205824/268, 768 slots)
constexpr int kDiseaseStepBudget      = 12;   // per-tick scan budget (v25 = 12)

// VIBE_Event_AllocProduktion @0x4f2a80 favorability pool:
constexpr double kProdFavorBias  = -0.5;   // dbl_620138 (0xbfe0...)
constexpr double kProdFavorScale = 0.25;   // dbl_620140 (0x3fd0...)
// VIBE_Event_AllocWorkActorAction @0x4f2530 favorability pool:
constexpr double kWorkFavorBias  = -0.5;   // dbl_6200D8 (0xbfe0...)
constexpr double kWorkFavorScale = 0.25;   // dbl_6200E0 (0x3fd0...)

// ===========================================================================
// VIBE_NpcEvent_RunSimDiseases @0x4d766c — disease outbreak risk roll.
// ===========================================================================
// For each scanned person slot the engine computes a "resistance" threshold
// (v30) and an RNG severity draw (v27); an outbreak fires when v27 < v30.
//   0x4d77be: v27 = RandomFloatScaled() * 100.0 (flt_61EF3C)
//   building present (slotPtr):
//     0x4d77fc: if building category byte == 2 (special/seuche building):
//                 v26 = (workstationCount * 0.0125) * 20.0          [flt_61EF50,flt_61EF40]
//               else:
//                 v26 = (workstationCount * 0.02)   * 20.0          [flt_61EF4C,flt_61EF40]
//   no building:
//     0x4d792a: v30 = 25.0 - RandomFloatScaled()*20.0*0.5          [dbl_61EF54,flt_61EF40,dbl_61EF44]
//               v26 = 0.0
//   0x4d7840: v30 -= v26
//   0x4d7858: outbreak iff (double)v27 < (double)v30.
// Note: the "building present" branch leaves v30 at its prior/zero value before
// subtracting v26 — reproduced faithfully (the original initialises *v30 only
// in the no-building branch; with a building, the stored v30 retains whatever
// PickRandomDiseaseEvent later writes). We model only the closed-form severity
// (v27) and the workstation penalty (v26) which are the testable numeric core.

// Severity draw: 0x4d77be.
inline float DiseaseSeverity(float randFloatScaled) {
    return randFloatScaled * kDiseaseSeverityScale100;
}

// Workstation penalty v26 for a building, given its category-2 flag.
//   isCategory2 true  -> count * 0.0125 * 20.0
//   isCategory2 false -> count * 0.02   * 20.0
inline float DiseaseWorkstationPenalty(int workstationCount, bool isCategory2) {
    double scaled = static_cast<double>(workstationCount) *
                    (isCategory2 ? static_cast<double>(kDiseaseSpecialRiskScale)
                                 : static_cast<double>(kDiseaseFarmRiskScale));
    return static_cast<float>(scaled * static_cast<double>(kDiseaseWorkstationScale));
}

// No-building resistance threshold v30: 25.0 - rand*20.0*0.5  (0x4d792a).
inline float DiseaseEmptyThreshold(float randFloatScaled) {
    return static_cast<float>(
        kDiseaseEmptyBase25 -
        static_cast<double>(randFloatScaled) *
            static_cast<double>(kDiseaseWorkstationScale) * kDiseaseBaselineHalf);
}

// Outbreak gate: 0x4d7858 — (double)severity < (double)threshold.
inline bool DiseaseOutbreakFires(float severity, float threshold) {
    return static_cast<double>(severity) < static_cast<double>(threshold);
}

// Ring-walk advance: 0x4d7744 — slot = (slot + stride) % 768.
inline int DiseaseNextSlot(int slot, int stride) {
    return (slot + stride) % kDiseasePersonSlotCount;
}

// Per-run stride/start pick (0x4d76cd..0x4d76f5):
//   stride[+176] = kDiseaseStrideTable[rand % 16]
//   start [+172] = rand % 0x300 (=768)
struct DiseaseRunSeed {
    u32 stride;
    u32 start;
};
inline DiseaseRunSeed DiseaseSeedRun(u16 randStrideIdx /*rand%16*/,
                                     u16 randStart /*rand%768*/) {
    DiseaseRunSeed s;
    s.stride = kDiseaseStrideTable[randStrideIdx & 0xF];
    s.start = randStart;
    return s;
}

// Tail bookkeeping (0x4d7768..0x4d779f): after the 12-step scan, if the running
// processed-count (entry+180) reached the full 768 person slots, the pass is
// complete (reset start/stride/count, bump phase day). Otherwise advance time
// by 30 units and, if minute field >= 18, normalize. Returns whether the full
// sweep finished this tick.
inline bool DiseaseSweepComplete(int processedCount) {
    return processedCount >= kDiseasePersonSlotCount;  // 0x4d777a: !(v9 < 768)
}

// ===========================================================================
// VIBE_NpcEvent_BardCreateScriptStep @0x4d66b4 — bard event state machine.
// ===========================================================================
// State at +112 selects the step. -1/-2 -> free. Steps 0..3:
//   case 0 (0x4d66d6): if (flag120 & 4)==0: pick law, schedule (+3 .. +6 days),
//          delay  = rand(4); next time = now + 3 + rand4; minute field = 10.
//   case 1 (0x4d677a): broadcast intro message to player households; +5 min.
//   case 2 (0x4d6820): wait/run the barde_create.esc script; advance +5 min;
//          when minute field >= 20 -> finish script, +5 min, state := 3.
//   case 3 (0x4d6975): notify build/icon cleanup; minute field = 6; day += 1.
// The testable cores are the case-0 schedule arithmetic and the >=20 gate.

// case 0 day offset: now + 3 + rand(4)   (0x4d6735..0x4d6752).
inline int BardSchedDayOffset(u16 rand4 /*rand%4*/) {
    return 3 + static_cast<int>(rand4);
}
// case 0 trigger gate: runs only if (flagByte120 & 4) == 0  (0x4d66e1).
inline bool BardCase0Enabled(u8 flagByte120) { return (flagByte120 & 4) == 0; }
// case 1 trigger gate: same (0x4d677a).
inline bool BardCase1Enabled(u8 flagByte120) { return (flagByte120 & 4) == 0; }
// case 2 finish gate: minuteField >= 0x14 (20)  (0x4d683a).
inline bool BardScriptFinished(u16 minuteField) { return minuteField >= 0x14; }
// case 3 demolition gate: (flagByte120 & 2) != 0  (0x4d6975 / 0x4d6a03).
inline bool BardCase3Demolish(u8 flagByte120) { return (flagByte120 & 2) != 0; }

// ===========================================================================
// VIBE_NpcEvent_BroadcastWinnerPointsStep @0x4d9a60 — winner-points broadcast.
// ===========================================================================
// state 0: build the official-comparison chart, broadcast a header + one packet
//          per ranked official, then advance +1 hour; when minute field >= 0x15
//          (21) -> state := 1.
// state 1 (0x4d9eed): advance +24 hours, set minute field = 7, state := 0.
// Per-official packet "rank kind" (0x4d9c77): 1 if person category byte is
// 6 or 7 (a human player household), else 2.

// state-0 advance gate: minuteField >= 0x15 -> advance state (0x4d9e89).
inline bool WinnerPointsAdvance(u16 minuteField) { return minuteField >= 0x15; }
// per-official packet kind (v49): 6/7 -> 1 else 2  (0x4d9c77).
inline u8 WinnerPointsRankKind(u8 personCategoryByte) {
    return (personCategoryByte == 6 || personCategoryByte == 7) ? 1 : 2;
}

// ===========================================================================
// VIBE_NpcEvent_OfficeMatchmakingStep @0x4da978 — office matchmaking machine.
// ===========================================================================
// state 12 (0x4da991): final broadcast of the election/matchmaking result text
//          (3361 if flag+172, else 3359); state := 0; +1 hour.
// state > 5 (0x4daa8d): free the handler entry.
// state 0..5: collect officials of the indexed category, and for each official:
//   - "promotion" branch (0x4dabab): only when flag+172 == 1 AND the office
//     definition's byte-2 is 9 or 6 AND rand(3) != 0 -> add a table entry,
//     mark v12 (this official was matched/promoted).
//   - vote-tally branch (0x4dabe5): for every other office, when the candidate
//     ids match, accumulate a random penalty:
//         penalty = 3*defByte2*? ... command coord = -(3*v18 + rand(3*defByte2))
//     where v18 low byte = defByte2.  Reproduced as the penalty magnitude core.

// state 12 result text id: 0x4da997 — flag ? 3361 : 3359.
inline int OfficeMatchResultTextId(bool flagSet) { return flagSet ? 3361 : 3359; }
// state classification (0x4da991 / 0x4daa8d): 12 = finalize, >5 = free, else step.
enum class OfficeMatchPhase { Finalize, Free, Step };
inline OfficeMatchPhase OfficeMatchClassify(u32 state) {
    if (state == 12) return OfficeMatchPhase::Finalize;
    if (state > 5)   return OfficeMatchPhase::Free;
    return OfficeMatchPhase::Step;
}
// promotion gate (0x4dabab): flag172==1 && (defByte2==9||defByte2==6) && rand3!=0.
inline bool OfficePromotionFires(int flag172, u8 defByte2, u16 rand3 /*rand%3*/) {
    return (flag172 == 1) && (defByte2 == 9 || defByte2 == 6) && (rand3 != 0);
}
// vote penalty magnitude (0x4dace3..0x4dad02): negated (3*defByte2 + rand(3*defByte2)).
//   v18 low byte == defByte2, the 3*v18 term uses that byte value.
inline i32 OfficeVotePenalty(u8 defByte2, u16 randSpan /*rand%(3*defByte2)*/) {
    return -(3 * static_cast<int>(defByte2) + static_cast<int>(randSpan));
}
// per-official notify text id by promotion/flag state (0x4dac06 family):
//   flag172==0                 -> 3360
//   flag172!=0 && matched(v12) -> 3363
//   flag172!=0 && !matched     -> 3362
inline int OfficeMatchNotifyTextId(int flag172, bool matched) {
    if (flag172 == 0) return 3360;
    return matched ? 3363 : 3362;
}

// ===========================================================================
// VIBE_Event_InitBetriebRun @0x4ef900 — "found business" packet step.
// ===========================================================================
// state validation: < -1 && != -2 -> return as-is; -2 -> free; -1 -> free;
//   0 -> if the request packet is ready, emit founding effects then free.
// The "founding fee" is fixed 3000, scaled by the regional money rate.
//   0x4ef9a1: VIBE_Money_MultiplyByRate(3000, region).
constexpr int kInitBetriebFoundingFee = 3000;  // 0x4ef9a1

enum class InitBetriebOutcome { Pass, Free, Continue, ProcessReady };
inline InitBetriebOutcome InitBetriebClassify(i32 state, bool packetReady) {
    if (state < -1) {                      // 0x4ef912
        if (state != -2) return InitBetriebOutcome::Pass;   // 0x4ef917
        return InitBetriebOutcome::Free;                    // -2 -> free
    }
    if (state <= -1) return InitBetriebOutcome::Free;       // 0x4ef928 (state == -1)
    if (state != 0) return InitBetriebOutcome::Continue;    // other positive
    return packetReady ? InitBetriebOutcome::ProcessReady   // 0x4ef93f
                       : InitBetriebOutcome::Continue;
}

// ===========================================================================
// VIBE_Event_AllocProduktion @0x4f2a80 / VIBE_Event_AllocWorkActorAction @0x4f2530
// — production / work-actor action allocators (favorability dampening core).
// ===========================================================================
// Both compute a favorability multiplier:
//   AllocProduktion    (0x4f2b6d): (avgFavor + (-0.5)) * 0.25      [dbl_620138,dbl_620140]
//   AllocWorkActor     (0x4f2595): (avgFavor + (-0.5)) * 0.25      [dbl_6200D8,dbl_6200E0]
// Production additionally seeds the duration field +180/+184 from a building
// stat (65*buildIndex + table + 34) and sets +212 = 1.0f, +200 = 0.
inline float AllocProductionFavorMul(double avgFavorability) {
    return static_cast<float>((avgFavorability + kProdFavorBias) * kProdFavorScale);
}
inline float AllocWorkActorFavorMul(double avgFavorability) {
    return static_cast<float>((avgFavorability + kWorkFavorBias) * kWorkFavorScale);
}

// ===========================================================================
// VIBE_Event_OpenBuildingDialogRun @0x4f1d48 — building-dialog event machine.
// ===========================================================================
// state -2/-1 -> free. state 0: if global byte set, mark slot state 5, ++state.
// state 1 (0x4f1db0): while day-hour (WORD2 qword_13CE852) < 22:
//     gather persons, pick one (rand(count+1)); if rand(4) and not self ->
//     try work-product object, else storable; open dialog. Then bump minute by
//     rand(2) and set seconds = rand(60).  When hour >= 22 -> ++state.
// state 2 (0x4f1eb5): set slot state 6, free.
// Testable cores: the hour gate and the RNG branch decisions.
constexpr int kBuildingDialogHourGate = 0x16;  // 22 (0x4f1db0)
inline bool BuildingDialogHourOpen(u16 dayHour) { return dayHour < kBuildingDialogHourGate; }
// 0x4f1e1a: take the work-product branch only when rand(4) is non-zero.
inline bool BuildingDialogTakeObject(u16 rand4 /*rand%4*/) { return rand4 != 0; }
// 0x4f1e77: minute += rand(2). 0x4f1e80: seconds = rand(60).
inline u16 BuildingDialogMinuteBump(u16 minuteField, u16 rand2) {
    return static_cast<u16>(minuteField + rand2);
}

// ===========================================================================
// VIBE_Event_OpenHelpEventsForKind @0x4f1a00 — help-INI selection by kind.
// ===========================================================================
// Maps the active building's group code to one of the tutorial INI names, then
// seeds the handler-entry timing from the first parsed help-event row.
// 0x4f1a34 switch on VIBE_BuildingType_GroupFromCode(...):
//   1,2,3 -> "help_events_hw"     4 -> "help_events_wirt"
//   5,6   -> "help_events_alpa"   7 -> "help_events_kirche"
//   11    -> "help_events_dieb"   (others -> none)
enum class HelpEventIni {
    None, Handwerk, Wirt, Alpa, Kirche, Dieb,
};
inline HelpEventIni HelpEventIniForGroup(int groupCode) {
    switch (groupCode) {
        case 1: case 2: case 3: return HelpEventIni::Handwerk;
        case 4:                 return HelpEventIni::Wirt;
        case 5: case 6:         return HelpEventIni::Alpa;
        case 7:                 return HelpEventIni::Kirche;
        case 11:                return HelpEventIni::Dieb;
        default:                return HelpEventIni::None;
    }
}
inline const char* HelpEventIniName(HelpEventIni k) {
    switch (k) {
        case HelpEventIni::Handwerk: return "help_events_hw";
        case HelpEventIni::Wirt:     return "help_events_wirt";
        case HelpEventIni::Alpa:     return "help_events_alpa";
        case HelpEventIni::Kirche:   return "help_events_kirche";
        case HelpEventIni::Dieb:     return "help_events_dieb";
        case HelpEventIni::None:     return nullptr;
    }
    return nullptr;
}

// ===========================================================================
// VIBE_Event_AllocSlotResetAction @0x4f41bc — slot-reset action allocator.
// ===========================================================================
// Dedup gate (0x4f41e0): if another handler of (type2/filter11/3) already owns
// the same target object (field+172) and belongs to a different owner (field+4)
// -> free this one. Otherwise initialise timing, +1s schedule, and (if the
// target object still exists) either advance time (already animating) or enqueue
// a slot-reset packet. The person-record presence sets the +240 marker to 4.
inline bool SlotResetIsDuplicate(bool otherOwnsSameTarget, bool differentOwner) {
    return otherOwnsSameTarget && differentOwner;  // 0x4f4307
}

// ===========================================================================
// VIBE_Event_AllocGebaeudeBauen @0x4f5110 — building-build action allocator.
// ===========================================================================
// Dedup gate (0x4f5138): if a foreign build handler of the same type exists
// whose state (+112) is < 5, free this one. Then look up the type name; if the
// candidate plot scan yields plots, pick the nearest by distance, else fail.
// The dedup predicate is: existing handler is NOT self AND its state < 5.
inline bool BuildDedupConflicts(bool isOther, i32 otherState) {
    return isOther && otherState < 5;  // 0x4f5138 (loop continues while same/>=5)
}

} // namespace guild::sim
