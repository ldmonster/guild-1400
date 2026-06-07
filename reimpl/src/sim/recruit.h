#pragma once
// Recruitment rules core for the Guild simulation (gilde.exe).
//
// The recruitment-offer / candidate-pick / hire-confirm DIALOGS
// (VIBE_Recruit_RunRecruitmentOfferWindow, RunCandidatePickWindow,
// RunHireConfirmDialog) are cutscene/widget-coupled and are LISTED AS DEFERRED.
// The recruitment *eligibility* / *proximity* rules core is translated here.
//
// Translated functions:
//   VIBE_Recruit_CheckRecruitProximity  0x55d5c0
#include "guild/common/types.h"
#include "sim/types.h"

namespace guild::sim {

// gilde.exe 0x55d5c0 — VIBE_Recruit_CheckRecruitProximity
//   (__usercall: eax=ret, eax=(recruiterId@eax), edx=(candidateId@edx)).
// Validates whether person `candidateId` may be recruited by `recruiterId`:
//   -1024  either id not found
//   -1025  recruiter is not a live actor (+8 == 0)
//   -1026  candidate is not a live actor (+8 == 0)
//   -1027  candidate already bound to a different employer (relation +92 set
//          to someone other than the recruiter)
//   -1028  recruiter already bound to a different employer than the candidate
//   else   1 if |officeRank(recruiter) - officeRank(candidate)| < 5, 0 otherwise.
// The employer field is relation slot 0 = dword index 23 (+92); -1 == unbound.
int RecruitCheckRecruitProximity(i32 recruiterId, i32 candidateId);

} // namespace guild::sim
