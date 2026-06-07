#pragma once
// AI method helpers + representative AiAction target-finder predicates (gilde.exe).
//   Math_RandomModulo            0x58b89c  (the RNG modulo every finder uses)
//   AiMethod_RandomBoolCheck     0x46797c
//   AiMethod_RandomValue         0x467994
//   AiAction_AlwaysAllow         0x47b9c0  (gate: always accept)
//   AiAction_CheckSameFaction    0x47b9c8  (gate: faction/turn match + 1-in-4 roll)
//   AiAction_PrepareGroupMember  0x47be3c  (clamp group size to active crimes)
//
// These are the self-contained AiAction/AiMethod leaf functions: the RNG modulo,
// the two tiny random wrappers the planner uses, and a representative set of the
// target-finder *gate predicates*. The heavyweight Find* finders (FindNearbyPerson
// etc.) are entangled with the render/object-search subsystem and are routed
// through a forward-declared hook; see the deferred list in the report.
#include "guild/common/types.h"

namespace guild::ai {

// gilde.exe 0x58b89c — VIBE_Math_RandomModulo  (__usercall, eax = (n@ax))
//   result = 0; if (n) return (int)RandNext() % n; return 0;
// Consumes one RandNext draw when n != 0 (none when n == 0). Determinism-critical:
// every AiAction finder derives its random offsets through this.
int RandomModulo(u16 n);

// gilde.exe 0x46797c — VIBE_AiMethod_RandomBoolCheck
//   return RandomModulo(n) != 0;   (true with probability (n-1)/n)
bool RandomBoolCheck(u16 n);

// gilde.exe 0x467994 — VIBE_AiMethod_RandomValue
//   return (unsigned __int16)RandomModulo(n);
int RandomValue(u16 n);

// --- AiAction gate predicates ----------------------------------------------

// gilde.exe 0x47b9c0 — VIBE_AiAction_AlwaysAllow.  return 1;
int AlwaysAllow();

// Candidate object the faction-gate inspects: its packed faction/flags word lives
// at +4 in the original (*(target+4)). We pass that word directly.
//
// gilde.exe 0x47b9c8 — VIBE_AiAction_CheckSameFaction
//   return (flags & 3) == (gameTimeLo % 4)
//       && (flags & 7) == (gameTimeHi % 8)
//       && !RandomModulo(4);
// `gameTimeLo` is the low dword of the packed game-time record (qword_13CE852 % 4)
// and `gameTimeHi` is WORD2(qword_13CE852) % 8. The roll consumes one RandNext.
bool CheckSameFaction(u32 targetFlags, u32 gameTimeLo, u32 gameTimeHi);

// gilde.exe 0x47be3c — VIBE_AiAction_PrepareGroupMember
//   if (*(person+404) < 2) return 0;          // not enough capacity
//   active = CountActiveByTarget();           // # active crimes on the target
//   if (active <= 0) return 0;
//   half = *(person+404) / 2;
//   out = (half >= active) ? active : half;   // clamp group size
//   return 1;
// `capacity` is *(person+404); `activeCrimes` is the CountActiveByTarget result
// (forward-declared hook in the binary). On success writes the clamped size to
// *outGroupSize and returns true.
bool PrepareGroupMember(int capacity, int activeCrimes, int* outGroupSize);

} // namespace guild::ai
