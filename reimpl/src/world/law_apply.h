#pragma once
// Office holder-table collection / eligibility rules (the self-contained "rule
// leaves" of the VIBE_Office_* family that scan the office-holder table and the
// office-definition table). Faithful 1:1 port from gilde.exe. These complement
// the candidacy / promotion rules already in world/office.{h,cpp}: this file
// holds the *collection* and *succession-eligibility* logic that the election /
// promotion forms drive.
//
// Shared state reused (NOT re-defined here):
//   g_officeHolders[37]  (world/office.cpp; gilde.exe byte_B59848, 24 B stride)
//   OfficeDefReqCode / OfficeDefBookCat (world/office.h; def table dword_62EC8E)
//   OfficeGetEntryByHolder / OfficeIsNextRankInCategory (world/office.h)
//
// The originals call VIBE_Person_FindRecordById to confirm a holder's character
// still exists and to read its +358 (held office-type) and +433 (busy flag).
// Those live in the sim Person table (another module); we route them through a
// pluggable resolver hook so the rule logic stays self-contained and testable.
//
// (VIBE_Office_CheckPrerequisitesMet 0x47e7d8 is already in world/office_assign.cpp.)
//
// Translated functions:
//   VIBE_Office_CollectByCategory         0x47f03c
//   VIBE_Office_CollectByCategoryResolved 0x47f0b4
//   VIBE_Office_CollectElectiveOffices    0x47f150
//   VIBE_Office_CollectHoldersByCategory  0x47fe8c
//   VIBE_Office_LookupHolderCharacter     0x47f66c
//   VIBE_Office_HasAvailableSuccessor     0x47f79c
//   VIBE_Office_CollectCategoryRankList   0x47f928
//   VIBE_Office_FindHighestVacantRank     0x47fac0
//   VIBE_Office_CopyEntriesByIndex        0x47fa8c
//   VIBE_Office_GetSecondaryHolderEntry   0x47e070
#include "guild/common/types.h"
#include "world/law_types.h"
#include "world/office.h"

namespace guild::world {

// ---------------------------------------------------------------------------
// Person resolver hook (models VIBE_Person_FindRecordById @0x58bc6c).
// ---------------------------------------------------------------------------
// Returns a view of the person fields the office rules read, or {present=false}
// if the id resolves to no record (the original's null return). Field map:
//   present     : VIBE_Person_FindRecordById(id) != null
//   office358   : person record byte +358 (held office type; the value
//                 OfficeIsNextRankInCategory reads as p.officeType)
//   busy433     : person record byte +433 (HasAvailableSuccessor requires == 0)
struct OfficePersonRecord {
    bool present = false;
    u8   office358 = 0;
    u8   busy433   = 0;
};
using OfficePersonResolver = OfficePersonRecord (*)(i32 personId, void* ctx);
void OfficeSetPersonResolver(OfficePersonResolver resolver, void* ctx);

// gilde.exe 0x47f03c — VIBE_Office_CollectByCategory  (al=reqCode, edx=max, ebx=out).
// Scans the 30-entry holder table top-down (offset 696..0, stride 24); for each
// holder whose def-table reqCode == `reqCode`, copies its 24-byte record to the
// next `out` slot. Stops at `max` entries. Returns the count copied.
int OfficeCollectByCategory(u8 reqCode, int maxCount, OfficeHolder* out);

// gilde.exe 0x47f0b4 — VIBE_Office_CollectByCategoryResolved (al=reqCode, edx=max, ebx=out).
// Like CollectByCategory but iterates 30..0 and additionally requires the
// holder's +4 id to resolve to a live person. Returns 0 immediately if
// reqCode==0. Returns the count copied.
int OfficeCollectByCategoryResolved(u8 reqCode, int maxCount, OfficeHolder* out);

// gilde.exe 0x47f150 — VIBE_Office_CollectElectiveOffices (eax=allowVacant, edx=max, ebx=out).
// Scans holder slots 30..37 (offset 720..222 stepping by 24); collects those
// whose def reqCode == 7 (elective) when `allowVacant` is nonzero OR the holder's
// +4 id is not -1. Returns the count copied.
int OfficeCollectElectiveOffices(int allowVacant, int maxCount, OfficeHolder* out);

// gilde.exe 0x47fe8c — VIBE_Office_CollectHoldersByCategory (eax=bookCat, edx=max, ebx=out).
// Scans 28 holder slots (offset 0..672, stride 24); writes the holder TYPE byte
// (+8) of each whose def-table bookCat == `bookCat` into the byte array `out`.
// Returns the count.
int OfficeCollectHoldersByCategory(u8 bookCat, int maxCount, u8* out);

// gilde.exe 0x47f66c — VIBE_Office_LookupHolderCharacter (al=type).
// Finds the first holder slot (0..37, stride 24) whose TYPE byte (+8) == `type`,
// then resolves its +4 id. Returns true (and fills *out, when non-null) if a
// match resolves to a live person; false otherwise.
bool OfficeLookupHolderCharacter(u8 type, OfficePersonRecord* out);

// gilde.exe 0x47fac0 — VIBE_Office_FindHighestVacantRank (al=startRank).
// From startRank (clamped to 27) downward, returns the first rank whose holder
// entry (via OfficeGetEntryByHolder) has state(+16)==3 and rank(+12)<=1; returns
// 0 if startRank==0 or none found before reaching 0.
u8 OfficeFindHighestVacantRank(u8 startRank);

// gilde.exe 0x47fa8c — VIBE_Office_CopyEntriesByIndex (eax=count, edx=buf).
// `buf` is an array of `count` records; the first byte of each record is a holder
// index. Overwrites each record in place with g_officeHolders[index] (24 bytes).
// Returns the one-past-end pointer offset semantics as a count (== count).
int OfficeCopyEntriesByIndex(int count, OfficeHolder* buf);

// gilde.exe 0x47f79c — VIBE_Office_HasAvailableSuccessor (al=rank, ecx=ctxPerson).
// True iff, among up to 6 holders in `rank`'s book category, some holder resolves
// to a live person who is the next rank in category (OfficeIsNextRankInCategory)
// and whose +433 busy flag is 0. `rank` must be < 37.
bool OfficeHasAvailableSuccessor(u8 rank);

// gilde.exe 0x47e070 — VIBE_Office_GetSecondaryHolderEntry.
// Person-record view the original reads: marker (u16 @+0; 0xFFFF == invalid),
// ownerId (@+4), has361 (byte @+361). See the .cpp for the scan.
struct OfficeSecondaryPerson {
    bool present = false; // a1 != null
    u16  marker  = 0;     // *(u16*)a1
    i32  ownerId = -1;    // *(a1+4)
    u8   has361  = 0;     // *(a1+361)
};
int OfficeGetSecondaryHolderEntry(const OfficeSecondaryPerson& person,
                                  OfficeDef* defOut, OfficeHolder* holderOut);

// gilde.exe 0x47f928 — VIBE_Office_CollectCategoryRankList (eax=person, edx=max, ebx=out).
// Given the held office-type (person +358), collects holder TYPE bytes (+8) of the
// next-rank successors in that book into the byte array `out`, using the exact
// bookCat %3 progression of the original. `person` provides office358. Returns
// the count (early-returns the same count the original does on its terminal hit).
int OfficeCollectCategoryRankList(u8 office358, int maxCount, u8* out);

} // namespace guild::world
