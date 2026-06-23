#pragma once
// Person attribute / skill / bio accessors for the Guild simulation (gilde.exe).
//
// These are the per-person scalar queries the AI / economy / recruitment code
// calls against the 536-byte Person record (see types.h, word_12CE910 @0x12CE910,
// 768 slots). The originals address the record by raw byte offset (folded into
// per-field global symbols); we keep the same offsets via the PersonField enum so
// the reads/writes are byte-faithful and unaligned exactly where the binary is.
//
// All "index" arguments are the 0..767 person slot index (the originals scale it
// by 536/134/268/536*a1 depending on the symbol's element width — the same slot).
//
// Translated functions:
//   VIBE_Person_IsValidActiveRecord    0x4f8e60
//   VIBE_Person_GetCashAmount          0x58bc9c
//   VIBE_Person_ComputePriceMultiplier 0x58f71c
//   VIBE_Person_ComputeWealthRank      0x592ae8
//   VIBE_Person_ComputeOfficeRank      0x58bccc
//   VIBE_Person_IsFamilyMemberEligible 0x58c2f8
#include "guild/common/types.h"
#include "sim/types.h"

namespace guild::sim {

// Raw field accessors on a Person record (read/write by byte offset, matching
// the binary's unaligned access). `rec` is the record base; `off` a PersonField.
u8  PersonGetByte(const Person* rec, int off);
void PersonSetByte(Person* rec, int off, u8 v);
i16 PersonGetWord(const Person* rec, int off);
void PersonSetWord(Person* rec, int off, i16 v);
i32 PersonGetDword(const Person* rec, int off);
void PersonSetDword(Person* rec, int off, i32 v);

// gilde.exe 0x4f8e60 — VIBE_Person_IsValidActiveRecord (__usercall, eax=(idx@ax)).
// True iff slot `idx` is alive (marker != -1), is a live actor (+8 != 0), and is
// a "real" person (kind byte +2 < 10).
bool PersonIsValidActiveRecord(u16 idx);

// gilde.exe 0x58bc9c — VIBE_Person_GetCashAmount (__usercall, st0=(idx@ax)).
// Returns the cash-on-hand word (+0x0A) of slot `idx` as a double.
double PersonGetCashAmount(u16 idx);

// gilde.exe 0x58f71c — VIBE_Person_ComputePriceMultiplier (__usercall, st0=(idx@eax)).
// Reputation-based price multiplier for slot `idx`. If reputation (+0x80 byte) is
// <= 42.0 returns 1.0, else 1.0 + rep * 0.25 * (1/252) (computed in float).
// (dbl_62698C @0x62698C == 0.003968253968253968 == 1/252, not 1/256.)
double PersonComputePriceMultiplier(int idx);

// gilde.exe 0x592ae8 — VIBE_Person_ComputeWealthRank (__usercall, eax=(idx@eax),
// edx=(bonus@edx)). 1-based rank of slot `idx` by wealth score (+0x1AC) with an
// additive bonus, counting how many other valid persons have >= score. Returns 0
// if idx out of range.
int PersonComputeWealthRank(u32 idx, int bonus);

// Office definition lookup (VIBE_Office_GetDefinition @0x47f008) — owned by the
// Amt/office module; forward-declared. Fills a 24-byte office descriptor whose
// BYTE2 (out[2] byte) is the office's rank level. Returns nonzero on success.
// person.cpp calls it via a mockable hook (see PersonSetOfficeDefinitionHook).
using OfficeDefinitionFn = int (*)(u8 officeId, u8 out[24]);
void PersonSetOfficeDefinitionHook(OfficeDefinitionFn fn);

// gilde.exe 0x58bccc — VIBE_Person_ComputeOfficeRank (__usercall, eax=(idx@ax),
// edx=(stopAtSelf@edx)). Resolves the office rank of slot `idx`:
//   0 if the slot is free (id == -1) or not a live actor (+8 == 0);
//   1 if the slot holds no resolvable office;
//   else (officeLevel + 1), recursing up the office-superior chain (+0x60) and
//   taking the max, unless `stopAtSelf` or the office level is the cap (10).
int PersonComputeOfficeRank(u16 idx, int stopAtSelf);

// "Target underfull / unavailable" predicate (VIBE_Combat_IsTargetUnderfull
// @0x57e4c8) — owned by the combat module; forward-declared via a mockable hook.
using TargetUnderfullFn = bool (*)(u16 idx);
void PersonSetTargetUnderfullHook(TargetUnderfullFn fn);

// gilde.exe 0x58c2f8 — VIBE_Person_IsFamilyMemberEligible (__usercall,
// eax=(candidate@eax record ptr), edx=(reference@edx record ptr)). Decides if the
// `candidate` Person is an eligible family member relative to `reference`,
// walking the relation array (+0x5C..) and the family/age/household fields.
// Operates on record pointers (as the original does). Returns 1 if eligible.
bool PersonIsFamilyMemberEligible(const Person* candidate, const Person* reference);

} // namespace guild::sim
