#pragma once
// Recruitment economy + candidate-eligibility rules for the Guild simulation
// (gilde.exe). MODULE: Personnel/Recruit remainder (namespace guild::sim).
//
// This is the *economy* and *filter* core of recruitment, distinct from the
// proximity check in recruit.{h,cpp}:
//   * Recruit_ComputeRecruitmentCost @0x55d674 — the float-table cost formula
//     (turns wealth ratios + favorability + cash + reputation into a 2..25 fee).
//   * Person_EvaluateCandidateEligibility @0x5596f8 — the big bit-flag matcher
//     that the recruit/AI passes use to decide whether a candidate person passes
//     a 24-byte filter descriptor (gender/family/office/profession/favor/...).
//
// The recruitment offer/pick/confirm DIALOGS (RunRecruitmentOfferWindow,
// RunCandidatePickWindow, RunHireConfirmDialog) and RunPayWorkerDialog are
// cutscene/widget-coupled and are LISTED AS DEFERRED (see report).
//
// Translated functions:
//   VIBE_Recruit_ComputeRecruitmentCost        0x55d674
//   VIBE_Person_EvaluateCandidateEligibility   0x5596f8
//   VIBE_Recruit_CollectNearbyRecruitCandidates 0x55d530 (modeled via a hook leaf)
#include "guild/common/types.h"
#include "sim/types.h"
#include "sim/person.h"   // TargetUnderfullFn (shared leaf hook signature)

namespace guild::sim {

// ---------------------------------------------------------------------------
// Leaf hooks. ComputeRecruitmentCost and EvaluateCandidateEligibility both call
// into the economy/AI valuation leaves that live in other modules:
//   VIBE_Person_ComputeTotalWealth   @0x591f7c (building/value cluster)
//   VIBE_Ai_ComputePersonFavorability@0x594330 (AI relation/office cluster)
// We expose them as mockable function-pointer hooks so the rules core is
// testable in isolation. `wealthOwner` is the recruiter record passed as the
// valuation context (the original's esi/a2).
// ---------------------------------------------------------------------------
// VIBE_Person_ComputeTotalWealth(personIdxOrMarker, ownerRec):
//   returns the cash + building/storage worth of slot `marker`, valued against
//   `ownerRec`. -1 if the slot index is out of range / free.
using TotalWealthFn = int (*)(u16 marker, const Person* ownerRec);
void RecruitSetTotalWealthHook(TotalWealthFn fn);

// VIBE_Ai_ComputePersonFavorability(a, b, mode): 0..100 favorability of person
// `a` toward `b`. mode 1 applies the office/law/inventory modifiers.
using FavorabilityFn = double (*)(int a, int b, int mode);
void RecruitSetFavorabilityHook(FavorabilityFn fn);

// VIBE_DebugCmd_DispatchByType(16, recruiterRec, buf): a debug/cheat hook the
// cost formula consults; bit 1 (& 2) halves the fee. Default returns 0.
using CostDebugFn = int (*)(int type, const Person* recruiterRec, void* buf);
void RecruitSetCostDebugHook(CostDebugFn fn);

// gilde.exe 0x55d674 — VIBE_Recruit_ComputeRecruitmentCost
//   (__fastcall-ish __usercall: eax=recruiterId, edx=candidateId).
// Computes the recruitment fee (clamped to [2, 25]). Returns 0 if either id is
// not found. See recruit_cost.cpp for the verbatim float-table formula.
int RecruitComputeRecruitmentCost(i32 recruiterId, i32 candidateId);

// ---------------------------------------------------------------------------
// Candidate eligibility filter (the 24-byte descriptor a3 in the original).
// Built by the recruit/AI/query passes; the matcher reads these fields by byte
// offset. Modeled as a packed struct so the offsets stay byte-faithful.
//   +0x04 dword flags A  (gender/family/profession/office/turn-bit gates)
//   +0x05 byte  flags B  (employer/jail/class gates)
//   +0x06 byte  flags C  (status/turn-bit + class-set membership gates)
//   +0x07 byte  flags D  (bit 8 == office-rank-distance gate)
//   +0x0D byte  flags E  (bit 2 == "exclude id list" active)
//   +0x0E word  excludeIds[4] (skip candidate if its marker matches one)
//   +0x18 dword favorActive (nonzero => apply the favor min/max window)
//   +0x1C float favorMin
//   +0x20 float favorMax
// ---------------------------------------------------------------------------
// NB: flagsA is the dword at +4; flagsB/C/D are the BYTES at +5/+6/+7 — they
// OVERLAP flagsA (the original reads *(DWORD*)(a3+4) and *(BYTE*)(a3+5/6/7) from
// the same memory). The struct therefore lays flagsA's bytes out explicitly and
// exposes the dword/byte views via accessors so the overlap is faithful.
GUILD_PACKED_BEGIN
struct CandidateFilter {
    u8   pad0[4];          // +0x00  (unused by the matcher)
    u8   a4;               // +0x04  flagsA byte 0  (also LOBYTE(flagsA))
    u8   a5;               // +0x05  flagsB / flagsA byte 1
    u8   a6;               // +0x06  flagsC / flagsA byte 2
    u8   a7;               // +0x07  flagsD / flagsA byte 3 (MSB; sign bit gate)
    u8   pad8[5];          // +0x08..+0x0C
    u8   flagsE;           // +0x0D
    u16  excludeIds[4];    // +0x0E..+0x15
    u8   pad16[2];         // +0x16..+0x17
    u32  favorActive;      // +0x18
    float favorMin;        // +0x1C
    float favorMax;        // +0x20

    // +0x04 dword view (flagsA): little-endian {a4,a5,a6,a7}.
    u32 flagsA() const {
        return static_cast<u32>(a4) | (static_cast<u32>(a5) << 8)
             | (static_cast<u32>(a6) << 16) | (static_cast<u32>(a7) << 24);
    }
    u8  flagsB() const { return a5; }   // *(BYTE*)(a3+5)
    u8  flagsC() const { return a6; }   // *(BYTE*)(a3+6)
    u8  flagsD() const { return a7; }   // *(BYTE*)(a3+7)
} GUILD_PACKED;
GUILD_PACKED_END
static_assert(sizeof(CandidateFilter) == 0x24, "CandidateFilter must be 36 bytes");

// Eligibility-matcher leaf hooks (other-module functions consulted by the
// filter cascade), exposed as settable seams for isolated tests:
//   VIBE_Combat_IsTargetUnderfull @0x57e4c8 — "target underfull/unavailable".
//   VIBE_Character_IsActiveType   @0x45263c — candidate has a live actor type.
//   dword_6498E4 — the record currently shown in the recruit window (excluded).
using TargetUnderfullFn = bool (*)(u16 idx);
void RecruitSetTargetUnderfullHook(TargetUnderfullFn fn);
void RecruitSetActiveTypeHook(bool (*fn)(u16 idx));
void RecruitSetFocusRecord(const Person* rec);

// gilde.exe 0x5596f8 — VIBE_Person_EvaluateCandidateEligibility
//   (__usercall: eax=referenceId/index a1, edx=candidateIndex a2, ebx=filter a3).
// `referenceIdx`/`candidateIdx` are 0..767 person slot indices (the original
// addresses word_12CE910[268*idx]). `filter` may be null (=> always eligible).
// Returns 1 if the candidate passes the filter, 0 otherwise.
int PersonEvaluateCandidateEligibility(u16 referenceIdx, u16 candidateIdx,
                                       const CandidateFilter* filter);

}  // namespace guild::sim
