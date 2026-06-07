#include "sim/recruit.h"
#include "sim/person.h"
#include "sim/entity.h"

#include <cstdlib>

namespace guild::sim {

// gilde.exe 0x55d5c0 — VIBE_Recruit_CheckRecruitProximity.
// recruiter = Find(a1), candidate = Find(a2). The employer field is the dword at
// +92 (relation slot 0); -1 means unbound.
int RecruitCheckRecruitProximity(i32 recruiterId, i32 candidateId) {
    Person* recruiter = PersonFindRecordById(recruiterId);
    Person* candidate = PersonFindRecordById(candidateId);
    if (!recruiter || !candidate)
        return -1024;

    if (PersonGetByte(recruiter, kPfIsPlayer) == 0)
        return -1025;
    if (PersonGetByte(candidate, kPfIsPlayer) == 0)
        return -1026;

    // candidate employer (+92): bound to someone other than the recruiter?
    i32 candEmployer = PersonGetDword(candidate, kPfRelationBase);
    if (candEmployer != -1 && recruiterId != candEmployer)
        return -1027;

    // recruiter employer (+92): bound to someone other than the candidate?
    i32 recEmployer = PersonGetDword(recruiter, kPfRelationBase);
    if (recEmployer != -1 && candidateId != recEmployer)
        return -1028;

    int rRank = PersonComputeOfficeRank(static_cast<u16>(recruiter->marker), 0);
    int cRank = PersonComputeOfficeRank(static_cast<u16>(candidate->marker), 0);
    return std::abs(rRank - cRank) < 5 ? 1 : 0;
}

} // namespace guild::sim
