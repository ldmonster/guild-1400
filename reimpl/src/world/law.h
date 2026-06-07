#pragma once
// Law (Gesetz) subsystem: the law table, the violation evaluator, and the
// max-wanted-level roll. Faithful 1:1 port of the VIBE_Gesetz_* data/rules core
// from gilde.exe. UI (law-book sections, person selection, description
// formatting) and the network-command enactment glue are documented as deferred
// in the module report.
//
// Translated functions:
//   VIBE_Gesetz_GetRecord             0x4c244c
//   VIBE_Gesetz_ComputeMaxWantedLevel 0x4c2ba0 (workstation-sum portion abstracted)
//   VIBE_Gesetz_EvaluateViolation     0x4c2c5c
#include "guild/common/types.h"
#include "world/law_types.h"

namespace guild::world {

// The live law table (gilde.exe unk_631E98 @0x631E98, 26 x 36 B). Exposed so the
// game can populate it from the save state; ships pre-loaded with the static
// default table baked into the binary (recovered via get_bytes).
extern LawRecord g_lawTable[kLawCount];

// Resets the table to the binary's static default contents.
void LawTableResetDefaults();

// gilde.exe 0x4c244c — VIBE_Gesetz_GetRecord  (__usercall, eax = (id@al), edx=out@edx).
// Copies the 36-byte record for `id` into *out. Returns 1 on success, 0 if
// id >= 26 (out untouched on failure).
int GesetzGetRecord(u8 id, LawRecord* out);

// Result of a violation check (return value of EvaluateViolation), so callers /
// tests can branch without inspecting the network-command stub.
//   kViolationBadId   : id out of range (orig returns -1)
//   kViolationNoMatch  : the operator/threshold test did NOT trip (orig -1)
//   kViolationEscaped : tripped but the wanted-level roll let the perp escape (-1)
//   kViolationQueued  : tripped and the crime command was queued (orig: command id)
enum ViolationOutcome {
    kViolationBadId   = -3,
    kViolationNoMatch = -2,
    kViolationEscaped = -1,
    kViolationQueued  =  0,
};

// gilde.exe 0x4c2ba0 — VIBE_Gesetz_ComputeMaxWantedLevel  (__usercall, st0).
// In the original this scans the 256-slot building array for guard stations of
// the perpetrator's owner, sums workstation category-10 capacity, and returns
// min(sum)*0.01 with a special cap of 0.75 when the per-building sum == 75.
// The building scan belongs to the sim agent; we expose the pure scalar formula
// so the roll is testable and bit-exact:
//   level(sum) = (sum == 75) ? 0.75 : (double)sum * 0.01
// (flt_61E588 == 0.01f; the 0.75 short-circuit matches the original exactly.)
double GesetzComputeMaxWantedLevelFromSum(int guardStationSum);

// Hook for the perpetrator's guard-station strength used by EvaluateViolation.
// Defaults to 0 (no guards -> max-wanted-level 0 -> every violation rolls as
// "caught"). Tests/callers override to model guard presence. Original: the
// building scan inside VIBE_Gesetz_ComputeMaxWantedLevel.
using GuardStationSumFn = int (*)(i32 perpetratorId);
void GesetzSetGuardStationSumFn(GuardStationSumFn fn);

// RNG hook: the original calls VIBE_Math_RandomFloatScaled (=> RandNext()*flt,
// flt == 1/32768) and compares >= maxWanted. We default to that exact CRT-LCG
// path (guild::crt::RandNext) so a seeded RNG yields deterministic outcomes;
// tests may override for hand-computed references.
using RandomFloatFn = double (*)();
void GesetzSetRandomFloatFn(RandomFloatFn fn);

// The scale constant flt_62675C (== 0x38000100): RandNext()*kRandFloatScale.
extern const double kRandFloatScale;

// gilde.exe 0x4c2c5c — VIBE_Gesetz_EvaluateViolation
//   (__userpurge: al=lawId, edx=value, ecx=victimId, ebx=perpetratorId, +stack a5)
// Evaluates `value` against law `lawId`'s operator/threshold. On a violation it
// rolls RandomFloat() >= ComputeMaxWantedLevel(perp); if the roll says "caught"
// it queues a crime-creation command and returns kViolationQueued, otherwise
// kViolationEscaped. Returns kViolationNoMatch if the law is satisfied, or
// kViolationBadId for an out-of-range id.
//
// `out_*` mirror the crime record the original packs into the queued command
// (perpetrator=perpetratorId, victim=victimId, lawType=record.id,
// penalty=record.penalty, extra=a5); they are written only when kViolationQueued.
struct PendingCrime {
    i32 perpetrator;  // ebx (a4)  -> crime perpetrator
    i32 victim;       // ecx (a3)  -> crime victim / target
    u8  lawType;      // record.id (v21[0])
    i16 penalty;      // record.penalty (v21[4])
    i32 extra;        // a5 (stack arg) -> mission / context tag
    int value;        // edx (a2)  -> the tested value
};

int GesetzEvaluateViolation(u8 lawId, int value, i32 victimId, i32 perpetratorId,
                            int extra, PendingCrime* out /* may be null */);

} // namespace guild::world
