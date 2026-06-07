#pragma once
// Office (Amt) promotion-list builder + remaining self-contained office RULE
// leaves deferred by the Wave-12 law-apply agent. Faithful 1:1 port from
// gilde.exe. These complement world/office.{h,cpp} (data model + candidacy /
// promotion-rank rules) and world/law_apply.{h,cpp} (collection / succession
// eligibility).
//
// The headline is the in-place insertion sort that builds the ranked promotion
// candidate list (VIBE_Office_BuildPromotionList / ...Filtered). Each list slot
// is a 12-byte record:
//   +0  u8    office type (byte_B59850 value of the source holder)
//   +4  float promotion cost (the v27/v28 scalar from CanPromoteRank)
//   +8  u8    category / bookCat (byte_62EC92[12*type], == OfficeDefBookCat(type))
// The list is kept sorted ascending by (category, cost): a new entry is inserted
// before the first slot whose (cat, cost) is strictly greater. Empty slots are
// pre-seeded with cat=0, cost=-100.0 ((float)0xC2C80000 == -1027080192 bit
// pattern) so they never win the comparison and sort to the front-as-empty.
//
// Shared state reused (NOT re-defined here; extern from world/office.cpp):
//   g_officeHolders[37]  (gilde.exe byte_B59848, 24 B stride: byte_B59850=type@+8,
//                         dword_B59854=rank@+12, byte_B59858=state@+16)
//   OfficeCanPromoteRank / OfficeDefBookCat / OfficeGetDefinition (world/office.h)
//
// Translated functions:
//   VIBE_Office_BuildPromotionList         0x47f1cc
//   VIBE_Office_BuildPromotionListFiltered 0x47f410
//   VIBE_Office_GetHolderEntryByCity       0x47dfec
//   VIBE_Office_EvalApplyForCandidacy      0x46b8a8
//   VIBE_Office_TryPromoteCharacter        0x47ebd4
#include "guild/common/types.h"
#include "world/law_types.h"
#include "world/office.h"

namespace guild::world {

// ---------------------------------------------------------------------------
// Promotion-list record (12 bytes; matches the original's stride of 12).
// ---------------------------------------------------------------------------
GUILD_PACKED_BEGIN
struct PromotionEntry {
    u8    type;     // +0x00  office type (byte_B59850 of source holder)
    u8    pad1[3];  // +0x01  alignment to the float
    float cost;     // +0x04  promotion cost (CanPromoteRank scalar)
    u8    category; // +0x08  bookCat of `type` (byte_62EC92[12*type])
    u8    pad9[3];  // +0x09  alignment to the 12-byte stride
} GUILD_PACKED;
GUILD_PACKED_END
static_assert(sizeof(PromotionEntry) == 12, "PromotionEntry must be 12 bytes");

// gilde.exe 0x47f1cc — VIBE_Office_BuildPromotionList  (eax=person, edx=max, ebx=out).
// Builds, into `out` (an array of `maxCount` PromotionEntry slots the function
// zero/seed-initializes), the ranked list of office types `person` may be
// promoted into. A holder slot contributes its type when:
//   state(+16)==3, rank(+12)<4, and CanPromoteRank(person, type, &cost) succeeds.
// Duplicates (same type already in the list) are skipped. Returns the number of
// entries inserted, capped at `maxCount`.
int OfficeBuildPromotionList(const OfficePerson& person, int maxCount,
                             PromotionEntry* out);

// gilde.exe 0x47f410 — VIBE_Office_BuildPromotionListFiltered
//                      (eax=person, edx=max, ecx=outRankBlocked, ebx=out).
// Like BuildPromotionList, but the rank<4 gate moves *inside*: a holder whose
// state==3 and CanPromoteRank passes but whose rank(+12) >= 4 sets the
// "rank-blocked" out-flag (*outRankBlocked = 1) instead of being inserted. The
// flag (nullable) is written once after the scan. Returns the inserted count.
int OfficeBuildPromotionListFiltered(const OfficePerson& person, int maxCount,
                                     int* outRankBlocked, PromotionEntry* out);

// gilde.exe 0x47dfec — VIBE_Office_GetHolderEntryByCity  (eax=person, edx=defOut, ebx=holderOut).
// Finds the holder-table entry whose city/owner field (+4) matches `person`'s
// ownerId (+4), among 30 slots (dword_B5984C stepping by 6 dwords == 24 bytes).
// On a hit copies the 24-byte holder record into *holderOut and the decoded
// OfficeDef (3 dwords at def-base+2+12*type) into *defOut, returns 1. Returns 0
// if the person is invalid (marker 0xFFFF / null) or holds no office (+358 == 0)
// or no slot matches.
int OfficeGetHolderEntryByCity(const OfficePerson& person, OfficeDef* defOut,
                               OfficeHolder* holderOut);

// gilde.exe 0x46b8a8 — VIBE_Office_EvalApplyForCandidacy  (eax=person, edx=msg).
// Tiny interaction dispatcher: if msg->opcode (*a2) == 10 and
// OfficeApplyForCandidacy(person, msg->officeType (a2[4])) succeeds, returns 10;
// otherwise returns the reject-stub value (0). `applyOk` is the result of the
// candidacy attempt; `opcode`/0x0A is the message tag. Modeled as a pure
// predicate so the dispatch is golden-testable.
u8 OfficeEvalApplyForCandidacy(u8 opcode, bool applyOk);

// gilde.exe 0x47ebd4 — VIBE_Office_TryPromoteCharacter  (eax=p, edx=fromCity, ebx=toCity).
// Validates a promotion-transfer request between two holder slots and, on
// success, emits a build-op-92 command (the network glue, routed through the
// command hook below). Gate (all must hold, else returns -1):
//   p,fromCity,toCity all non-zero; GetHolderEntryByCity resolves both slots;
//   the two resolved defs share the same bookCat byte (v9==v11, the +8 of the
//   decoded def block) and the same def high byte (v8>>24 == v10>>24).
// The emitted command carries: dword = p.ownerId (+4 of the person), the two
// holder CHARACTER-ID bytes (v13/v14 == holder byte +0 of each slot), and the
// literal 6. Returns the command result (0) on emit, -1 on gate failure.
struct OfficePromoteCommand {
    i32 personId;  // *(p+4)
    u8  fromType;  // holder char-id (+0) of the `fromCity` slot (v13)
    u8  toType;    // holder char-id (+0) of the `toCity`  slot (v14)
    int tag;       // 6 (the literal v15)
};
using OfficePromoteCommandHook = int (*)(const OfficePromoteCommand& cmd, void* ctx);
void OfficeSetPromoteCommandHook(OfficePromoteCommandHook hook, void* ctx);
void OfficePromoteCommandLogReset();
const OfficePromoteCommand* OfficePromoteCommandLog(int* outCount);

// The person view TryPromoteCharacter resolves both slots against. `fromCity` /
// `toCity` select the slot whose +4 owner equals these ids; we model the lookup
// by carrying explicit OfficePerson views for each slot (the caller fills them
// from the live records, as GetHolderEntryByCity does on the live table).
int OfficeTryPromoteCharacter(const OfficePerson& p,
                              const OfficePerson& fromCity,
                              const OfficePerson& toCity);

} // namespace guild::world
