#pragma once
// Residence location rule-cores — the ELIGIBILITY / OUTCOME math behind the
// "take a mistress" and "master examination" residence actions, lifted out of
// the GUI dialog bodies in gilde.exe.
//
//   VIBE_Location_ResidenceMistressVisit  0x5150f4  (affair eligibility + cooldown)
//   VIBE_Location_ResidenceMasterExam     0x5159fc  (rank gate + exam pass + time)
//
// Mutations (start an affair, promote rank) route through a mockable command
// hook so the rule cores stay unit-testable without the engine.
#include "guild/common/types.h"

namespace guild::world {

namespace residence {
// Master-exam production-gauge scale (0x5159fc): the building's production gauge
// (0..1) is shown scaled to 0..100 via flt_621B90.
constexpr float kExamGaugeScale = 100.0f;   // flt_621B90  0x00 00 c8 42
// Exam thresholds (0x5159fc): rank-within-group must be <= 6 to take the exam,
// and the building's production gauge must be >= 1.0 to be eligible at all.
constexpr int   kExamMaxRank    = 6;
constexpr float kExamMinGauge   = 1.0f;
// Master-exam grants this many days of "study" time on success (GameTime_Advance
// with day delta 2).
constexpr int   kExamDaysAdvance = 2;
// Mistress promotion skill requirement (Dialog_CheckSkillRequirement(..., 2)).
constexpr int   kMistressSkillReq = 2;
} // namespace residence

enum class ResidenceCommand {
    None = 0,
    StartAffair,    // Command_QueueRequestEntity29(1 / 3, person) + cooldown stamp
    PromoteMaster,  // Command_RequestBuildOp90_Thunk + slot reset + time advance
};
struct ResidenceEmit { ResidenceCommand cmd = ResidenceCommand::None; int arg = 0; };

// ===========================================================================
// Mistress visit  (gilde.exe 0x5150f4 — VIBE_Location_ResidenceMistressVisit).
// ===========================================================================
// The "court the mistress" button is ENABLED only when all hold:
//   * the mistress's owner person still exists (Person_FindRecordById)
//   * a handler of type `ownerType` exists (He_FindFirstHandlerByFilter(1,1,..))
//   * the affair cooldown has elapsed: lastAffair (person+188) < now (qword_13CE852)
// On confirm an affair command is queued (Entity29) and the cooldown stamp is
// reset to `now`. A second path (the "make her the master's wife" button) is
// gated by a skill check (CheckSkillRequirement(player, 2)).
struct MistressDecision {
    bool ownerExists;     // Person_FindRecordById(ownerId)
    bool handlerPresent;  // He_FindFirstHandlerByFilter
    bool cooldownElapsed; // lastAffairTime < nowTime
    bool affairEnabled;   // all three above
    bool promoteAllowed;  // skill requirement met (only checked on the promote path)
    ResidenceEmit emit;   // StartAffair when the player confirms an enabled affair
};

// gilde.exe 0x5150f4 — mistress eligibility + cooldown core.
//   ownerExists     : Person_FindRecordById(person+? owner) != 0
//   handlerPresent  : He_FindFirstHandlerByFilter(1,1,ownerType) != 0
//   lastAffairTime  : person+188 (last affair timestamp)
//   nowTime         : qword_13CE852 (current packed game time)
//   skillOk         : Dialog_CheckSkillRequirement(player, 2) (promote path)
//   confirmAffair   : player clicked the "court" button
MistressDecision ResidenceComputeMistress(bool ownerExists, bool handlerPresent,
                                          i64 lastAffairTime, i64 nowTime,
                                          bool skillOk, bool confirmAffair);

// ===========================================================================
// Master examination  (gilde.exe 0x5159fc — VIBE_Location_ResidenceMasterExam).
// ===========================================================================
// To take the master's exam the visitor must:
//   * have no pending master-letter handler (He_FindFirstHandlerByFilter(2,2,..) == 0)
//   * have a production gauge >= 1.0  (Building_DrawProductionGauge)
//   * have a rank-within-group <= 6   (BuildingType_ComputeRankWithinGroup)
// When eligible the exam offers a slider 0..(gauge*100). On "take exam"
// (button 1210) the journeyman count (person+404, dwords +101) must be >= 4,
// else a "not enough staff" message. On pass the player is promoted (BuildOp90)
// and game-time advances `kExamDaysAdvance` days.
struct MasterExamDecision {
    bool hasPendingLetter; // a master-letter handler already exists
    bool gaugeOk;          // production gauge >= 1.0
    bool rankOk;           // rank-within-group <= 6
    bool eligible;         // !hasPendingLetter && gaugeOk && rankOk
    int  gaugeMax;         // (int)(gauge * 100) — exam slider ceiling
    bool staffOk;          // journeyman count >= 4 (checked on "take exam")
    ResidenceEmit emit;    // PromoteMaster on a passing exam, None otherwise
};

// gilde.exe 0x5159fc — master-exam eligibility + outcome core.
//   pendingLetter : He_FindFirstHandlerByFilter(2,2,player) != 0
//   gauge         : Building_DrawProductionGauge(player) (0..1+)
//   rankInGroup   : BuildingType_ComputeRankWithinGroup(...) (the v7 register)
//   journeymen    : person dwords[101] (staff count)
//   takeExam      : player clicked the "take exam" button (1210)
MasterExamDecision ResidenceComputeMasterExam(bool pendingLetter, float gauge,
                                              int rankInGroup, int journeymen,
                                              bool takeExam);

} // namespace guild::world
