#include "world/location_residence.h"

namespace guild::world {

// gilde.exe 0x5150f4 — VIBE_Location_ResidenceMistressVisit eligibility core.
// Per-frame the "court" button enable state is:
//   if ( Person_FindRecordById(ownerId)
//     && He_FindFirstHandlerByFilter(1, 1, ownerType)
//     && (person+188) < qword_13CE852 )            // affair cooldown elapsed
//       enable(courtButton);
//   else disable(courtButton);
// On click: ShowMessageBox(confirm); if confirmed, stamp person+188 = now and
// QueueRequestEntity29(1, person). The promote path is gated by
// Dialog_CheckSkillRequirement(player, 2).
MistressDecision ResidenceComputeMistress(bool ownerExists, bool handlerPresent,
                                          i64 lastAffairTime, i64 nowTime,
                                          bool skillOk, bool confirmAffair) {
    MistressDecision d{};
    d.ownerExists     = ownerExists;
    d.handlerPresent  = handlerPresent;
    // 0x5152df..0x5152f2: mov ebx, dword ptr qword_13CE852 (LOW 32 bits only);
    //   mov eax, [a3+188] (32-bit); cmp eax, ebx; jge.  So the cooldown compare is a
    //   32-bit SIGNED compare of the low dwords, not a full 64-bit compare.
    d.cooldownElapsed = (i32)lastAffairTime < (i32)nowTime;
    d.affairEnabled   = ownerExists && handlerPresent && d.cooldownElapsed;
    d.promoteAllowed  = skillOk;
    if (d.affairEnabled && confirmAffair) {
        d.emit = {ResidenceCommand::StartAffair, 1};
    }
    return d;
}

// gilde.exe 0x5159fc — VIBE_Location_ResidenceMasterExam eligibility + outcome.
//   ComputeRankWithinGroup(...) -> v7 (rank);
//   gauge = Building_DrawProductionGauge(player);
//   pending = He_FindFirstHandlerByFilter(2,2,player);
//   // top-level branch: eligible == !pending && gauge >= 1.0 && rank <= 6
//   if ( pending || gauge < 1.0 || rank > 6 ) { ...ineligible messages... }
//   else { ...show exam, slider ceiling = gauge*flt_621B90... }
//   // on "take exam" (dword_75BF38 == 1210) with gauge >= 1.0:
//   if ( journeymen >= 4 ) { BuildOp90(-4); GameTime_Advance(+2 days); promote; }
//   else                   { "not enough staff" message; }
MasterExamDecision ResidenceComputeMasterExam(bool pendingLetter, float gauge,
                                              int rankInGroup, int journeymen,
                                              bool takeExam) {
    MasterExamDecision d{};
    d.hasPendingLetter = pendingLetter;
    d.gaugeOk = gauge >= residence::kExamMinGauge;
    d.rankOk  = rankInGroup <= residence::kExamMaxRank;
    d.eligible = !pendingLetter && d.gaugeOk && d.rankOk;
    d.gaugeMax = (int)(gauge * residence::kExamGaugeScale);
    d.staffOk = journeymen >= 4;
    // The promote command only fires when the exam is actually taken with a
    // gauge >= 1.0 and enough journeymen; gauge>=1.0 is re-checked in the loop.
    if (takeExam && d.gaugeOk && d.staffOk) {
        d.emit = {ResidenceCommand::PromoteMaster, residence::kExamDaysAdvance};
    }
    return d;
}

} // namespace guild::world
