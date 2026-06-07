#pragma once
// Office (Amt) data/rules core: the office-definition table, the office-holder
// table, and the candidacy / promotion / succession rules. Faithful 1:1 port of
// the VIBE_Office_* data/rules functions from gilde.exe. The council/trial
// cutscene state machines and the network-command mutation glue are deferred
// (see the module report).
//
// The office-relevant Person fields the originals read live in the sim Person
// record (offsets +358 office-type, +359 rank, +360 candidacy-flag, +404 money).
// Those belong to the sim agent and are not in sim::Person yet, so the rules here
// take an OfficePerson view that the caller fills from the live record.
//
// Translated functions:
//   VIBE_Office_GetDefinition           0x47f008
//   VIBE_Office_GetCategoryByRank       0x47ef94
//   VIBE_Office_GetEntryByCity          0x47efb4
//   VIBE_Office_GetEntryByHolder        0x47ef28
//   VIBE_Office_GetHolderEntryByCity    0x47dfec (holder-table portion)
//   VIBE_Office_CanRunForOffice         0x47e3b8
//   VIBE_Office_CanPromoteRank          0x47e0f8
//   VIBE_Office_IsNextRankInCategory    0x47f6a4
//   VIBE_Office_GetRankRequirements     0x47fb30
//   VIBE_Office_CollectSuccessorCandidates 0x47f858
//   VIBE_Office_AssignToCandidate       0x47e4e0 (rule portion)
#include "guild/common/types.h"
#include "world/law_types.h"

namespace guild::world {

// ---------------------------------------------------------------------------
// Office definition table (dword_62EC8E). 446 raw bytes, 37 records at a +2 skew.
// ---------------------------------------------------------------------------
// A decoded definition (what GetDefinition copies, 3 dwords from base+2+12*rank):
//   word0 layout: id(byte0) | reqCode(byte1, 1..7) | bookCat(byte2, 1..9) | cost(byte3)
//   flag  (dword1): non-zero == "promotable pair head" (used by candidacy)
//   textId(dword2): float/text-id payload
struct OfficeDef {
    u32 word0;   // dword[0] of the record
    i32 flag;    // dword[1]
    u32 textId;  // dword[2]
};

// gilde.exe 0x47f008 — VIBE_Office_GetDefinition  (al=rank, edx=out).
// Fills *out for `rank`. Returns 1 if rank < 37, else 0 (and fills the fallback
// from record 0 as the original does).
int OfficeGetDefinition(u8 rank, OfficeDef* out);

// gilde.exe 0x47ef94 — VIBE_Office_GetCategoryByRank  (al=rank).
// Returns HIBYTE(dword_62EC8E[3*rank]) — the reqCode byte at offset 12*rank+3.
u8 OfficeGetCategoryByRank(u8 rank);

// Office-def table field accessors used by the holder-mutation rules. These read
// the same kOfficeDefTable (dword_62EC8E) the candidacy rules use:
//   OfficeDefBookCat(t): byte_62EC92[12*t] == record t byte +2 (1..9 book/category)
//   OfficeDefReqCode(t): HIBYTE(dword_62EC8E[3*t]) == record t byte +1 (reqCode)
//   OfficeDefFlag(t)   : dword_62EC94[3*t] == record t dword[1] (promotable flag)
u8  OfficeDefBookCat(u8 type);
u8  OfficeDefReqCode(u8 type);
i32 OfficeDefFlag(u8 type);
u8  OfficeDefId(u8 type); // record byte +0 (the def id LOBYTE of dword[0])

// ---------------------------------------------------------------------------
// Office holder table (byte_B59848, 24 B x 30/37). Modeled as OfficeHolder[].
// ---------------------------------------------------------------------------
extern OfficeHolder g_officeHolders[kOfficeDefCount]; // 37-entry storage (table)
void OfficeHolderTableReset();

// gilde.exe 0x47efb4 — VIBE_Office_GetEntryByCity  (al=city, edx=out).
// Linear scan (37 entries) for the first holder whose holder-id (+0) == `city`.
// (Faithful to the original which compares byte_B59848[+0] against the arg.)
// Returns 1 and copies the 24-byte entry on hit, 0 if not found.
int OfficeGetEntryByCity(u8 key, OfficeHolder* out);

// gilde.exe 0x47ef28 — VIBE_Office_GetEntryByHolder  (al=type, edx=out).
// Scans for the first entry whose type (+8) == `type`. Returns 1/0 as above.
int OfficeGetEntryByHolder(u8 type, OfficeHolder* out);

// ---------------------------------------------------------------------------
// Person view for the candidacy/promotion/succession rules.
// ---------------------------------------------------------------------------
// Fields map to the live sim Person record:
//   officeType : +358 (held office type)         -> byte_..[+358]
//   rank       : +359 (held rank-in-category)    -> byte_..[+359]
//   candidacy  : +360 (already a candidate flag)  -> byte_..[+360]
//   ownerId    : +4   (person/owner id)
struct OfficePerson {
    i32 ownerId;   // +4   id used to match holder entries
    u8  officeType;// +358 office type currently held
    u8  rank;      // +359 rank within the office category
    u8  candidacy; // +360 non-zero == already standing for an office
    bool valid;    // models a successful VIBE_Person_FindRecordById
};

// gilde.exe 0x47e3b8 — VIBE_Office_CanRunForOffice.
// True iff `p` is valid, not already a candidate, and there is a holder slot of
// `p`'s office type that is vacant (city==-1, state==3, rank<4) whose type
// matches p.officeType. (The optional second-holder check is modeled too.)
bool OfficeCanRunForOffice(const OfficePerson& p);

// gilde.exe 0x47e0f8 — VIBE_Office_CanPromoteRank  (eax=person, dl=targetRank).
// Validates the rank-progression rule using the def table's reqCode bytes:
//   - person valid, targetRank in 1..29, person.officeType in 0..29,
//     person.rank in 0..29
//   - reqCode(person.rank)+1 >= reqCode(targetRank) and
//     reqCode(person.officeType*3 ...) < reqCode(targetRank)
// On success writes the promotion-cost scalar (dword_62EBCC[...]) into *outCost
// and returns 1; else 0.
int OfficeCanPromoteRank(const OfficePerson& p, u8 targetRank, float* outCost);

// gilde.exe 0x47f6a4 — VIBE_Office_IsNextRankInCategory  (eax=person, dl=rank).
// True iff `rank` is the next rank in `person`'s office book, using the
// reqCode % 3 progression rule recovered from the original.
bool OfficeIsNextRankInCategory(const OfficePerson& p, u8 rank);

// gilde.exe 0x47fb30 — VIBE_Office_GetRankRequirements  (al=rank, edx=out).
// Writes the requirement block (age min/max, money min/max, two byte counts,
// two dword counts = 24 bytes) selected by reqCode(rank). Returns 1/0.
struct RankRequirements {
    i16 ageMin;       // +0
    i16 ageMax;       // +2
    i32 moneyMin;     // +4
    i32 moneyMax;     // +8
    u8  countA;       // +12
    u8  countB;       // +13
    u8  pad14[2];     // +14
    i32 reqOfficesA;  // +16
    i32 reqOfficesB;  // +20
};
int OfficeGetRankRequirements(u8 rank, RankRequirements* out);

// gilde.exe 0x47f858 — VIBE_Office_CollectSuccessorCandidates.
// Given a target office `rank`, collect up to `maxCount` (capped at 6) holder
// person-ids whose held rank is the next rank in that book. The original calls
// VIBE_Office_CollectByCategory then filters via IsNextRankInCategory; here the
// candidate pool is supplied (people array), keeping the IsNextRankInCategory
// filter exact. Writes matching ownerIds into out[], returns the count.
int OfficeCollectSuccessorCandidates(u8 rank, const OfficePerson* people,
                                     int peopleCount, int maxCount,
                                     i32* out, int outCapacity);

} // namespace guild::world
