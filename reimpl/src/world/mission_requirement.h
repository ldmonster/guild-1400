#pragma once
// Mission / objective requirement evaluation — the Guild's victory-condition and
// goal-checking core. Faithful 1:1 port of the VIBE_MissionReq_* cluster from
// gilde.exe (the routines that decide whether a mission/objective goal is met).
//
// Translated functions (gilde.exe addresses):
//   VIBE_MissionReq_AccumulateTimer     0x539c44 — per-goal "held for N minutes"
//                                                  timer state machine.
//   VIBE_MissionReq_CheckStatThreshold  0x539160 — count clan members whose stat
//                                                  >= scaled threshold.
//   VIBE_MissionReq_CheckOwnPersonRatio 0x539380 — fraction of citizens owned.
//   VIBE_MissionReq_CountMembersByState 0x539d14 — count guild members in a state
//                                                  and return the running average.
//   VIBE_MissionReq_CountGuildMembers   0x539da0 — "busy member" counter/averager.
//   VIBE_MissionReq_CheckMultiStat      0x5393ec — 3-state ">=1 avg" gate.
//   VIBE_MissionReq_CheckStatCombo      0x53945c — 3-state "count>=3 & avg>=1".
//   VIBE_MissionReq_CheckCumulativeStats 0x539534 — 6-state cumulative gate.
//   VIBE_MissionReq_CheckMemberStats    0x53963c — 3-state gate + timer.
//   VIBE_MissionReq_CheckAverageStat    0x5396dc — guild average vs scaled goal.
//
// The top-level dispatcher VIBE_MissionReq_Evaluate (0x5398c4) is deferred: it
// fans out to ~20 leaf checks that span the Person/family, Building and Economy
// clusters, several of which are not yet translated. It is listed in the report
// rather than left half-translated.
//
// Cross-cluster leaves (GameTime, the Person iterator) are declared extern in
// their owning modules and reused; this module never redefines them.
#include "guild/common/types.h"
#include "sim/types.h"   // guild::sim::GameTime (14-byte packed record)

namespace guild::world {

// ---------------------------------------------------------------------------
// Requirement-table row (gilde.exe byte_63CD4C, stride 24 bytes, dword_5383F0
// entries). Loaded from the mission/objective data file; the dispatcher matches
// an objective's type byte against row[+0] and reads the threshold / timer.
// ---------------------------------------------------------------------------
GUILD_PACKED_BEGIN
struct ReqTableRow {
    u8  type;        // +0x00  objective type id (matched against objective[+0])
    u8  pad1[15];    // +0x01..+0x0F  (loaded fields, unused by the evaluator)
    i32 threshold;   // +0x10  (+4 dwords) comparison threshold
    i32 timerMin;    // +0x14  (+5 dwords) "must hold for" duration, minutes
};
GUILD_PACKED_END
static_assert(sizeof(ReqTableRow) == 24, "ReqTableRow stride must be 24 bytes");

// ---------------------------------------------------------------------------
// Objective / mission-goal record (the `a1` argument to VIBE_MissionReq_Evaluate
// and the leaf checkers). The evaluator only touches a few fields; the embedded
// 14-byte GameTime at +8 is the per-objective hold timer mutated by
// AccumulateTimer.
// ---------------------------------------------------------------------------
GUILD_PACKED_BEGIN
struct ObjectiveRecord {
    u8       type;        // +0x00  objective type byte (switch selector)
    u8       pad1[3];     // +0x01  pad to the dword at +4
    i32      personId;    // +0x04  (+1 dword) target person/clan id
    sim::GameTime timer;  // +0x08  14-byte hold-timer GameTime (+8..+0x15)
    u16      pad22;       // +0x16  gap to the +24 dword
    i32      fieldAt24;   // +0x18  (+6 dwords)
    i32      value;       // +0x1C  (+7 dwords) direct compare value (some cases)
};
GUILD_PACKED_END
static_assert(sizeof(ObjectiveRecord) == 32, "ObjectiveRecord must be 32 bytes");

// Output triple written by the counter routines (matches the 12-byte BYREF block
// the originals fill: [+0]=int sum, [+4]=int count, [+8]=float average).
struct MemberCount {
    i32   sum;       // +0x00  running stat sum
    i32   count;     // +0x04  number of matching members
    float average;   // +0x08  sum/count (0 when count<=0)
};

// =====================  Translated functions  ==============================

// gilde.exe 0x539c44 — VIBE_MissionReq_AccumulateTimer
//   (__usercall: eax=record@a1, edx=conditionMet@a2, ebx=requiredMinutes@a3).
// "Hold the condition for N minutes" tracker. When `conditionMet`:
//   * if the timer is fresh (all four 4/2/4/4-byte time words zero) it stamps it
//     with the current wall clock (g_sysGameTime);
//   * returns (minutes elapsed since stamp) >= requiredMinutes.
// When the condition is not met it clears the timer and returns false.
bool MissionReqAccumulateTimer(ObjectiveRecord* record, bool conditionMet,
                               int requiredMinutes);

// gilde.exe 0x539160 — VIBE_MissionReq_CheckStatThreshold (eax=clan, edx=row).
// Counts the 5 stat bytes at clan[+128..+132] whose value*flt_623D50 >= row+20
// (a float compare); the goal is met when that count >= row.threshold.
bool MissionReqCheckStatThreshold(const u8* clanRecord, const ReqTableRow* row);

// gilde.exe 0x539380 — VIBE_MissionReq_CheckOwnPersonRatio (eax=record, edx=row).
// Walks all person records; among "citizens" (kind<10) counts how many are owned
// by the same owner as `record` (top byte of the dword at +9 == record+12), then
// tests row.threshold <= (owned/citizens)*flt_623D54.
bool MissionReqCheckOwnPersonRatio(const u8* objectiveOwnerRec,
                                   const ReqTableRow* row);

// gilde.exe 0x539d14 — VIBE_MissionReq_CountMembersByState (dl=state, ebx=out,
//   eax=objective). Iterates guild members (state==0 -> "match-any" query, else
// the given state) and, for each, increments `count`; `sum` counts those whose
// owner word (member+0x27) equals objective[+0]. Fills out->{sum,count,average}
// and returns the average's float bit-pattern (callers read out->average).
i32 MissionReqCountMembersByState(u8 state, MemberCount* out,
                                  const ObjectiveRecord* objective);

// gilde.exe 0x539da0 — VIBE_MissionReq_CountGuildMembers (al=state, edx=out).
// Same iteration shape, but `sum` counts members whose owning person's kind byte
// (g_persons[member.ownerWord].kind) is 6 or 7 ("busy"). Fills out and returns
// the average's float bit-pattern.
i32 MissionReqCountGuildMembers(u8 state, MemberCount* out);

// NOTE: VIBE_Mission_FindGuildMemberState (0x539cbc) is already translated in
// world/mission_member.cpp (guild::world::MissionFindGuildMemberState) and is
// reused, not redefined here.

// gilde.exe 0x5393ec — VIBE_MissionReq_CheckMultiStat (this=objective). True when
// the running average of member states 19, 4 and 16 are each >= 1.0 (with the
// 4/16 short-circuits). Uses CountMembersByState (owner-matched against
// objective[+0]).
bool MissionReqCheckMultiStat(const ObjectiveRecord* objective);

// gilde.exe 0x53945c — VIBE_MissionReq_CheckStatCombo (this=objective). True when
// states 21, 20 and 18 each have count>=3 AND average>=1.0.
bool MissionReqCheckStatCombo(const ObjectiveRecord* objective);

// gilde.exe 0x539534 — VIBE_MissionReq_CheckCumulativeStats (edx=row). Five
// gating states (21,20,18,22,8) must not be "count>0 with avg<1", and the final
// (sum+count) over state 9 must reach row.threshold.
bool MissionReqCheckCumulativeStats(const ReqTableRow* row);

// gilde.exe 0x53963c — VIBE_MissionReq_CheckMemberStats (ebx=row). States 11,12,
// 13 must each not be "count>0 with avg<1"; result is gated through the hold
// timer with conditionMet = (row.threshold <= 3).
bool MissionReqCheckMemberStats(ObjectiveRecord* record, const ReqTableRow* row);

// gilde.exe 0x5396dc — VIBE_MissionReq_CheckAverageStat (ecx=row).
// True when row.threshold*flt_623D58 <= (guild average over all states).
bool MissionReqCheckAverageStat(const ReqTableRow* row);

}  // namespace guild::world
