#pragma once
#include "guild/common/types.h"

// gilde.exe — the FAMILY-RECORD table (namespace guild::sim).
//
// The dynasty/family ledger lives in one global array:
//   word_13C3110 @0x13C3110 — 16 records, stride 82 words (164 bytes).
//
// Reconstructed 1:1 from:
//   VIBE_Person_GetFamilyRecord     @0x58c408  (the accessor / named boundary)
//   the family-table reset           @0x5896fc  (VIBE_Building_ResetAllBuildings tail)
//   the inline allocator             @0x58ecf3  (VIBE_Person_CreateAndSpawn family arm)
//   the writer field map             @0x5a45bc  (VIBE_Save_WriteBuildingTable)
//
// HOW A PERSON TIES TO A FAMILY RECORD
// ------------------------------------
// A person record carries a "family word" at byte offset +0x50 (word index 40 in
// the 536-byte person record). For the family kinds (6 = player/head, 7 = relative,
// 5 = ancestor) the allocator stamps it as `(familyCount | 0x8000)` and bumps the
// global family counter (dword_647720). The 0x8000 high bit marks "has a family
// slot"; the low nibble (& 0xF) is the table index 0..15.
//
//   person +0x50 (2 bytes): family word = index | 0x8000
//   person +81   (1 byte) : the HIGH byte of that word -> 0x80 when the bit is set,
//                           so (signed char)+81 < 0  IFF the person has a family slot.
//
// VIBE_Person_GetFamilyRecord(person):
//   if person.kind not in {5,6,7}  -> 0
//   if (signed char)person[+81] >= 0 (no 0x8000 bit) -> 0
//   else index = person[+0x50] & 0x0F; return &word_13C3110[82 * index].
//
// FAMILY-RECORD STRUCT (164 bytes / 82 words). Offsets recovered from the save
// writer @0x5a45bc; only the fields the live tree touches are named here, the
// rest are an opaque image preserved verbatim for save round-trips.
//   +0   (2)  family word (index | 0x8000); -1 == free record (set by reset)
//   +2   (16) family / dynasty name (StrNCopyPad, 15 chars + NUL)
//   +128 (4)  float seed, stamped to -1.0f (0xBF800000) at allocation time
//   ... (the +20..+108 dword block + the +132/+148/+160 tail are save-only here)

namespace guild::sim {

// 16 records of 82 words (164 bytes). word_13C3110.
constexpr int kFamilyStride   = 164; // bytes (== 82 * sizeof(i16))
constexpr int kFamilyWords     = 82;  // words per record
constexpr int kFamilyCapacity = 16;

// Field offsets inside a 164-byte family record (byte offsets).
constexpr int kFamWordOff   = 0;    // +0   family word / -1 free sentinel
constexpr int kFamNameOff   = 2;    // +2   dynasty name (16 bytes)
constexpr int kFamSeedOff   = 128;  // +128 float seed (-1.0f at alloc)

// gilde.exe word_13C3110 — the family ledger. Owned here.
extern u8 g_familyTable[kFamilyCapacity * kFamilyStride];

// gilde.exe dword_647720 @0x647720 — the family-slot allocation counter (next free
// family index). The person factory (person_create.cpp) keeps its own mirror for
// the no-parents create path; this one is the table-module's authority for the
// reconstructed allocator entry point below and is reset together with the table.
extern i32 g_familyCount; // dword_647720

// gilde.exe 0x58c408 — VIBE_Person_GetFamilyRecord.
//   `personRec` points at a 536-byte person record. Returns the 164-byte family
//   record pointer, or nullptr when the person has no family slot (kind not in
//   {5,6,7}, or the 0x8000 bit is clear at +0x50 i.e. (signed char)+81 >= 0).
u8* Person_GetFamilyRecord(const void* personRec);

// gilde.exe 0x5896fc (tail) — reset the family table: each record cleared to 164
// zero bytes with word[0] = -1; dword_647720 reset to 0. (The building/guild
// counters dword_647724 / dword_64771C reset by their owners.)
void FamilyRecord_ResetAll();

// gilde.exe 0x58ecf3 — the inline family allocator from VIBE_Person_CreateAndSpawn.
//   Stamps the person's family word (+0x50 = g_familyCount | 0x8000), looks up the
//   matching family record, and (when the table has a free slot) stamps the record
//   seed float (+128 = -1.0f) and word[0] = the family word, then increments
//   g_familyCount.  Returns:
//     * the family-record pointer on success (g_familyCount < 16);
//     * nullptr when the family table is FULL (g_familyCount >= 16) — in which case
//       the person's +0x50 is NOT written and the caller must fail the create
//       (CreateAndSpawn returns 0xFFFF at 0x58e4ac).
// `kind` is the person kind byte (must be 5/6/7 for the gate to fire).
u8* FamilyRecord_AllocForPerson(void* personRec, u8 kind);

// Stamp a family record's name + seed from the apply layer (ExCreatePersonB,
// 0x4967bf..0x49681c).  `fam` is a family record pointer; copies up to 15 chars
// of `name` into +2 (NUL-padded to 16) and re-stamps word[0]=familyWord and the
// +128 = -1.0f seed. (Idempotent with the allocator; matches the original which
// re-writes both fields in the apply handler.)
void FamilyRecord_StampName(u8* fam, i16 familyWord, const char* name);

} // namespace guild::sim
