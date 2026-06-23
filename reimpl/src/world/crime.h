#pragma once
// Crime (Straftat) table + Evidence (Beweis) pairing. Faithful 1:1 port of the
// VIBE_Straftat_* / VIBE_Beweis_* record-ops core from gilde.exe.
//
// The originals address parallel named globals at fixed byte offsets of a 45-byte
// crime record and two interleaved 4096-dword evidence arrays. We model the crime
// table as a real CrimeRecord[512] and the evidence as the two interleaved dword
// arrays (owner/crimeId), preserving the exact scan bounds and slot semantics.
//
// Translated functions:
//   VIBE_Straftat_FindFreeSlot        0x4c3390
//   VIBE_Straftat_FindIndexById       0x4c33bc
//   VIBE_Straftat_SetRecordState      0x4c3874
//   VIBE_Straftat_CountActiveByTarget 0x4c3a48
//   VIBE_Straftat_ResolveAndClear     0x4c354c (record/evidence portion)
//   VIBE_Beweis_FindOrAllocSlot       0x4c347c
//   VIBE_Beweis_Add                   0x4c3338
//   VIBE_Beweis_ExistsForPair         0x4c3518
//   VIBE_Beweis_CollectByOwner        0x4c32ec
#include "guild/common/types.h"
#include "world/law_types.h"

namespace guild::world {

// Crime table (dword_11BC760 et al). provenState (+37) == 0 means a free slot.
extern CrimeRecord g_crimeTable[kCrimeCount];

// Evidence interleaved arrays (dword_11C2160 owner / dword_11C2164 crime-id).
// Stride-2 indexing: pair k -> [2*k]. A free pair is (owner==-1 && crimeId==-1).
extern i32 g_evidenceOwner[kEvidenceDwords];
extern i32 g_evidenceCrimeId[kEvidenceDwords];

// Resets both tables to the empty state (crime slots free, evidence pairs -1).
void CrimeAndEvidenceReset();

// ===========================================================================
// Crime / Straftat record ops.
// ===========================================================================

// gilde.exe 0x4c3390 — VIBE_Straftat_FindFreeSlot. Returns the first index whose
// provenState (+37) == 0, or -1 if the table is full. (Scans all 512 slots; the
// original keeps the FIRST free index, matching this loop.)
int StraftatFindFreeSlot();

// gilde.exe 0x4c33bc — VIBE_Straftat_FindIndexById  (eax=id). Returns the index
// of the crime whose id (+0) == id, or -1 if not found. Index 0 is matched
// specially (the original compares the base dword before the loop).
int StraftatFindIndexById(i32 id);

// gilde.exe 0x4c3874 — VIBE_Straftat_SetRecordState  (eax=state, edx=index).
// Sets provenState of record `index` to `state`. Returns index on success,
// -2 if index >= 512, -3 if state < 2.
int StraftatSetRecordState(u32 state, u32 index);

// gilde.exe 0x4c3a48 — VIBE_Straftat_CountActiveByTarget  (eax=personId).
// Counts records whose perpetrator (+22) == personId AND provenState == 1.
int StraftatCountActiveByTarget(i32 personId);

// gilde.exe 0x4c354c — VIBE_Straftat_ResolveAndClear  (eax=crimeId, edx=force).
// Looks up the crime by id, decrements its wanted counter, and (depending on
// force and proven-state) clears the record + all linked evidence pairs.
// Return codes mirror the original:
//   3  : crime id not found
//   2  : wanted counter still > 0 and !force (not cleared)
//   1  : not proven (provenState != 1) and (!force || provenState==0) (not cleared)
//   0  : record + linked evidence cleared
// The original also calls VIBE_City_RemoveCrimeFromGrid and a History notify on
// the cleared path; those side effects are exposed via hooks (default no-op).
int StraftatResolveAndClear(i32 crimeId, int force);

// Hook for the History "rival event" notify the cleared path fires when the
// evidence owner matches the local player (default no-op). Args: (crimeIndex,
// perpetratorId).
using ResolveNotifyFn = void (*)(int crimeIndex, i32 perpetratorId);
void StraftatSetResolveNotifyFn(ResolveNotifyFn fn);

// Hook for VIBE_City_RemoveCrimeFromGrid(target@+33, location@+28) — the cleared
// path calls this UNCONDITIONALLY (0x4c3628), once, before walking the evidence
// pairs. Default no-op (the City grid is a cross-cluster leaf). Args:
// (target == crime +33, location == crime +28).
using ResolveRemoveGridFn = void (*)(i32 target, u8 location);
void StraftatSetResolveRemoveGridFn(ResolveRemoveGridFn fn);

// ===========================================================================
// Evidence / Beweis pairing.
// ===========================================================================

// gilde.exe 0x4c347c — VIBE_Beweis_FindOrAllocSlot  (eax=owner, edx=crimeId).
// Scans the 2048 pairs: returns -2 if the (owner,crimeId) pair already exists;
// otherwise the first free pair index, or -1 if the table is full.
int BeweisFindOrAllocSlot(i32 owner, i32 crimeId);

// gilde.exe 0x4c3338 — VIBE_Beweis_Add  (eax=crimeId, edx=owner, ecx=extra).
// Allocates a slot for (owner,crimeId) and writes the pair. Returns 1 on success,
// 0 if the pair already existed or the table was full.
//   NB: the original's arg order is (crimeId@eax, owner@edx); it stores
//   owner@[2*slot], crimeId@[2*slot]. The wrapper keeps that exact mapping.
int BeweisAdd(i32 crimeId, i32 owner, int extra);

// gilde.exe 0x4c3518 — VIBE_Beweis_ExistsForPair  (eax=owner, edx=crimeId).
// Returns 1 if a pair (owner,crimeId) exists, else 0.
int BeweisExistsForPair(i32 owner, i32 crimeId);

// gilde.exe 0x4c32ec — VIBE_Beweis_CollectByOwner  (eax=key, edx=outBuf).
// Despite the name, scans the 512 CRIME records and appends the index of each
// record whose provenState (+37) == key into out[], stopping at 512 records or
// 32 collected (the original's 128-byte / 4-byte-each output bound). Returns the
// count written.
int BeweisCollectByOwner(i32 key, i32* out, int outCapacity);

} // namespace guild::world
