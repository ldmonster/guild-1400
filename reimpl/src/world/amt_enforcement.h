#pragma once
// Law-violation enforcement sweep — the rules core of
// VIBE_Amt_EnforceLawViolations (gilde.exe 0x57bf20). Once every fourth Amt turn
// the pass reads law #2 (the "Strafgesetz" trigger), then walks every person
// record and, depending on that law's threshold field, runs one of two sweeps:
//
//   * threshold == 2  -> a *reputation-decay* sweep: each qualifying person whose
//     reputation field (+460) is exceeded by a scaled RNG roll has a small random
//     reputation penalty queued and a flag (record+12 == 0 -> set) committed.
//   * otherwise        -> a *law-violation* sweep: each qualifying person is run
//     through VIBE_Gesetz_EvaluateViolation(law 2, value, victim=-1, perp), which
//     (on a caught violation) queues a crime-creation command — i.e. an arrest /
//     penalty. The `value` passed is 0 when the law threshold is 0 (v5=1/v15=0),
//     else 1 (v5=0/v15=1); the per-person guard byte (+9 high byte) must differ
//     from v15 for the person to be swept.
//
// The person walk and the network-command commit are sim/command-owned; this
// module recovers the deterministic per-turn cadence, the qualifying predicate,
// the branch select, and (for the violation sweep) the EvaluateViolation call —
// with mutations routed through hooks. RNG is guild::crt::RandNext (via law.h's
// RandomFloatScaled path) for determinism under a seeded generator.
//
// Translated:
//   VIBE_Amt_EnforceLawViolations   0x57bf20
#include "guild/common/types.h"
#include "world/law.h"

namespace guild::world {

// ===========================================================================
// Recovered single-precision constants (get_bytes; see comments).
// ===========================================================================
// flt_6258F0 @0x6258F0 == 0x3E4CCCCD == 0.2f  (reputation-roll scale: the decay
// sweep fires when RandomFloatScaled()*0.2 > reputation(+460)).
constexpr float kEnforceRepRollScale = 0.2f;
// flt_6258F4 @0x6258F4 == 0x3F000000 == 0.5f  and
// flt_6258F8 @0x6258F8 == 0x3E800000 == 0.25f : the penalty magnitude is
//   RandomFloatScaled()*0.5 + 0.25  queued as command 460.
constexpr float kEnforcePenaltyScale = 0.5f;
constexpr float kEnforcePenaltyBase  = 0.25f;
// flt_3DCCCCCD == 0.1f at +460 comparison in the violation sweep (the float bits
// 1036831949): a reputation above 0.1 has command 460 = -0.1f queued (clamp).
constexpr float kEnforceRepClampLevel = 0.1f;  // 0x3DCCCCCD
constexpr float kEnforceRepClampValue = -0.1f; // 0xBDCCCCCD (-1110651699)

// The reputation-word minimum (v2[5] / v8[5] at byte +10): persons whose value is
// < 12 are skipped (too new / not established).
constexpr u16 kEnforceMinReputationWord = 0x0C;

// Office-type bytes that exempt a person from the sweep (the v3/v9 != 6/7/8 gate).
// (Bytes 6,7,8 are the guard/office-holder classes that cannot be swept.)

// ===========================================================================
// Per-person view filled from the live person record (word_12CE910 stride 536).
// ===========================================================================
struct EnforcePerson {
    bool present = false;     // word @+0 != 0xFFFF (occupied slot)
    u8   profClass = 0;       // byte @+2  (swept only when 0 < profClass < 10)
    u8   officeType = 0;      // byte @+2 word's high byte / +3 (exempt if 6/7/8)
    u16  reputationWord = 0;  // word @+10 (must be >= 12 to be swept)
    float reputation = 0.0f;  // float @+460 (reputation scalar, v2+115)
    u8   flag12 = 0;          // byte @+12 (decay sweep sets the "==0" flag)
    i32  personId = 0;        // dword @+4 (the perpetrator id)
    i32  guardHighByte = 0;   // (int @+9) >> 24 (violation sweep: must != v15)
    i32  buildingLink = 0;    // dword @+92*4 (+368); when set, victim = *(link+1)
    i32  buildingVictim = -1; // resolved victim id from the building link (or -1)
};

// gilde.exe 0x57bf20 — the qualifying predicate shared by both sweeps:
//   present && 0 < profClass < 10 && officeType not in {6,7,8} && reputationWord >= 12
bool EnforceQualifies(const EnforcePerson& p);

// ===========================================================================
// Mutation hooks (command lockstep). Defaults record nothing.
// ===========================================================================
// The reputation/penalty command (VIBE_Command_QueueRequestArgs26(personId, 460,
// floatValue)). For the decay sweep this is the random penalty; for the violation
// sweep it is the -0.1 clamp. Args: (personId, fieldId=460, value).
using EnforceRepCommandHook = void (*)(i32 personId, int fieldId, float value,
                                       void* ctx);
void EnforceSetRepCommandHook(EnforceRepCommandHook hook, void* ctx);

// The decay-sweep flag commit (BeginDeltaPacket + AppendDeltaField(1,1,&flag,+12)
// + QueueRequestState22). Args: (personId, flagValue) where flagValue = (flag12==0).
using EnforceFlagCommandHook = void (*)(i32 personId, u8 flagValue, void* ctx);
void EnforceSetFlagCommandHook(EnforceFlagCommandHook hook, void* ctx);

// ===========================================================================
// The sweep result (counts, for the e2e reference).
// ===========================================================================
struct EnforceResult {
    bool ran = false;          // gameTurn % 4 == 0 (the cadence gate passed)
    int  threshold = 0;        // law 2 threshold (v14): selects the branch
    bool decayBranch = false;  // threshold == 2
    int  swept = 0;            // persons that passed the predicate + branch gate
    int  penalties = 0;        // decay penalties queued / violations evaluated
    int  caught = 0;           // violation sweep: EvaluateViolation != escaped
    int  flagsSet = 0;         // decay sweep: flag commits
    int  repClamps = 0;        // violation sweep: -0.1 reputation clamps
};

// gilde.exe 0x57bf20 — run the enforcement pass over a person table.
//   `gameTurn` gates the pass (only when gameTurn % 4 == 0).
//   `people`/`count` are the swept population (the stride-536 walk).
// The branch is chosen by law 2's threshold field (read via GesetzGetRecord(2)).
// Mutations route through the hooks; the EvaluateViolation call reuses law.cpp
// (so its RNG/guard hooks govern the violation outcome). Returns the tallies.
EnforceResult EnforceRun(int gameTurn, const EnforcePerson* people, int count);

} // namespace guild::world
