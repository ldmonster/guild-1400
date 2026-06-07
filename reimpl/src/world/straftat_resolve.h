#pragma once
// Crime (Straftat) resolution helpers beyond the record-ops in crime.{h,cpp}:
// the office-holder wanted-flag clear sweep over the Person array, and the
// evidence-driven proven-state update pass. Faithful 1:1 port from gilde.exe.
//
// These reuse the existing crime/evidence tables (crime.h: g_crimeTable,
// g_evidenceOwner/g_evidenceCrimeId, kCrimeCount, kEvidenceDwords) and the live
// Person array (sim/entity.h: g_persons, stride 536). Office holders are Person
// records whose kind byte (+0x02) is 6 or 7; the wanted-flag bitfield lives at
// Person+0x1E4 (dword_12CEAF4).
//
// Translated functions:
//   VIBE_Straftat_ClearWantedFlagOnNpcs   0x4c36ec
//   VIBE_Straftat_ClearWantedFlagOnNpcs2  0x4c3838 (byte-identical twin)
//   VIBE_Straftat_UpdateMatchingRecords   0x4c39a4
//
// DEFERRED: VIBE_Straftat_ResolveAllForType 0x4c3900 — the original re-marks the
// SAME crime slot (the SetRecordState index it feeds back is the previous return,
// which is constant), so a faithful 1:1 port is a non-terminating loop on a single
// slot. It has zero callers in gilde.exe (dead/degenerate code). Translating it
// verbatim would hang; deferred rather than ship an altered (non-1:1) loop.
#include "guild/common/types.h"

namespace guild::world {

// gilde.exe 0x4c36ec — VIBE_Straftat_ClearWantedFlagOnNpcs  (__usercall, eax=mask).
//   (byte-identical twin: 0x4c3838 VIBE_Straftat_ClearWantedFlagOnNpcs2.)
// Sweeps all 768 Person records: for each office holder (kind byte +0x02 == 6 or
// 7) clears the bits of `mask` from the wanted-flag dword at Person+0x1E4
// (dword_12CEAF4[i] &= ~mask). The original returns the scan-byte counter
// (411648); we return the number of holder records whose flag field was touched,
// which is the useful observable. (The clear itself is the load-bearing effect.)
int StraftatClearWantedFlagOnNpcs(i32 mask);

// gilde.exe 0x4c39a4 — VIBE_Straftat_UpdateMatchingRecords
//   (__usercall, eax=ownerKey, edx=perpMatch, ecx=mode, ebx=newState).
// Two modes, mirroring the original:
//   mode != 0 (evidence-driven): scans the 2048 evidence pairs; for each pair
//     whose owner == ownerKey, finds the crime record whose id == that pair's
//     crime-id (linear scan from index 0, stride 45, bound 512). If that crime is
//     proven (provenState == 1) and its perpetrator == perpMatch, sets its
//     provenState to newState and counts it.
//   mode == 0 (state-rewrite): scans all 512 crimes; every record whose
//     provenState == newState is reset to provenState 1 and counted.
// Returns the number of crime records modified.
int StraftatUpdateMatchingRecords(i32 ownerKey, i32 perpMatch, int mode,
                                  i32 newState);

} // namespace guild::world
