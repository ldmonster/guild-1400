#pragma once
// ===========================================================================
// person_lifecycle.h — person/object lifecycle & type-table query leaves
// ===========================================================================
// MODULE: sim entity lifecycle (namespace guild::sim).
//
// Faithful 1:1 ports of a cluster of person/building-type lifecycle helpers from
// gilde.exe (imagebase 0x400000). These are the small, self-contained leaves the
// birth/aging/family and building-type-catalog code calls:
//
//   VIBE_Person_FindByObjectRef           0x586a40  (object back-reference lookup)
//   VIBE_Person_CountActiveSlots          0x587b60  (type name -> type id)
//   VIBE_Person_CollectByType             0x587b9c  (collect type ids by kind byte)
//   VIBE_Person_ComputeBirthDate          0x58be24  (derive a birth date)
//   VIBE_Person_ComputeBirthDateFromRecord 0x58bd84 (derive a birth date, dated)
//   VIBE_BuildingType_GetGuildRankPair    0x589b40  (type -> guild rank pair)
//
// The three "Person_*" name/kind queries actually walk the 589-byte building-TYPE
// descriptor table (gilde.exe dword_13CE294, sim::g_buildingTypes) — see
// person_record.h for the proof that the "Person_*" iterators iterate the TYPE/
// OBJECT arrays, not the 536-byte person array. We keep the original names.
#include "guild/common/types.h"
#include "sim/types.h"
#include "sim/building_types.h"

namespace guild::sim {

// ---------------------------------------------------------------------------
// Object back-reference lookup.
// ---------------------------------------------------------------------------
// gilde.exe 0x586a40 — VIBE_Person_FindByObjectRef (__usercall, eax=ref@a1,
// esi=token@a2). Walks the object/building array via the shared Person query
// iterator with filter {alive/type byte == 71}, returning the first object whose
// back-reference dword at +101 (0x65) equals `ref`. Returns nullptr on miss.
//
// Original:
//   result = QueryBegin(token, 1, 0, 71);          // one filter: op 0, value 71
//   while (ref != *(DWORD*)(result + 101)) {
//       result = IterNext(); if (!result) return 0;
//   }
ObjectRec* PersonFindByObjectRef(i32 ref, int token);

// ---------------------------------------------------------------------------
// Building-type catalog queries (walk g_buildingTypes, stride 589).
// ---------------------------------------------------------------------------
// The catalog scan bound is 72 entries (589 * 72 == 42408, the original's loop
// bound). Each type record stores its kind byte at +0 and a NUL-terminated type
// NAME string at +1 (within BuildingTypeDef::pad1[34]).
constexpr int kBuildingTypeScanCount = 72;        // 0x48
constexpr int kBuildingTypeScanBound = 589 * 72;  // 42408
constexpr int kBuildingTypeNameOff   = 1;         // name string @ type+1

// gilde.exe 0x587b60 — VIBE_Person_CountActiveSlots (__usercall, eax=name@a1).
// Despite the name, this is a TYPE-NAME -> TYPE-INDEX lookup: it scans the type
// table comparing `name` (case-insensitively, VIBE_Util_StrCmpNoCase) against the
// name string at each type record's +1, returning the 0-based index of the first
// match. Returns 0 if no match within the 72-entry bound (== the original's
// `return 0` on the >=42408 exit, which collides with index 0 — faithful).
u8 PersonCountActiveSlots(const char* name);

// gilde.exe 0x587b9c — VIBE_Person_CollectByType (__usercall, al=kind@a1,
// edx=out@a2). Scans the first 72 type records; for each whose kind byte (+0)
// equals `kind`, appends that record's index (as a byte) to `out`. Returns the
// number appended. `out` must hold at least 72 bytes.
int PersonCollectByType(u8 kind, u8* out);

// ---------------------------------------------------------------------------
// Building-type -> guild rank pair.
// ---------------------------------------------------------------------------
// gilde.exe 0x589b40 — VIBE_BuildingType_GetGuildRankPair (__usercall, al=type,
// edx=outA, ebx=outB). Maps a building-type code to a pair of guild rank bytes
// via a verbatim switch; always returns 1 (no failure path).
int BuildingTypeGetGuildRankPair(u8 typeCode, u8* outA, u8* outB);

// ---------------------------------------------------------------------------
// Birth-date derivation.
// ---------------------------------------------------------------------------
// The 12-byte date record produced by the birth-date functions. Byte layout
// mirrors the packed game-time record the originals build then overwrite:
//   +0 day (1..28), +1 month (1..12), +2 year word, +4/+5 hour/minute carried
//   from the pack, +8 a cursor dword (carried from the pack input).
GUILD_PACKED_BEGIN
struct PersonBirthDate {
    u8  day;        // +0  (1 + (seed>>5) % 28)
    u8  month;      // +1  (1 + (... ) % 12)
    u16 year;       // +2  pack year tag (in.dateLow + 1400)
    u8  hour;       // +4  carried from pack input +4
    u8  minute;     // +5  carried from pack input +6
    u8  pad6[2];    // +6  (pack out +6/+7 — unused)
    i32 cursor;     // +8  carried from pack input +10
} GUILD_PACKED;
GUILD_PACKED_END
static_assert(sizeof(PersonBirthDate) == 12, "PersonBirthDate must be 12 bytes");

// gilde.exe 0x58be24 — VIBE_Person_ComputeBirthDate (__usercall, eax=person@a1,
// edx=out@a2). Returns 0 (no date written) if the record is a live actor
// (person+8 != 0); otherwise derives a deterministic birth date from the record's
// spawn-date field (person+38 >> 16) and seed field (person+48), writes it to
// `out`, and returns 1.
//   month = ((spawnDate % 19) + (seed>>3) % 7) % 12 + 1
//   day   = (seed>>5) % 28 + 1     where seed = (packYear ^ person[48])
// NOTE: the pack input's hour/minute/cursor fields are left uninitialized by the
// original (it reads stack garbage); only day/month/year are deterministic. We
// expose the input's carried fields via `packCarry` so a caller/test can pin them.
int PersonComputeBirthDate(const Person* person, PersonBirthDate* out);

// gilde.exe 0x58bd84 — VIBE_Person_ComputeBirthDateFromRecord (__usercall,
// eax=person@a1, edx=out@a2). Like ComputeBirthDate but seeds the pack input from
// the live game-date snapshot (g_gameDateSnapshot, gilde.exe qword_13CE852..)
// minus the record's age word (person+10), and uses different moduli:
//   month = ((dateLow % 11) + (seed>>3) % 13) % 12 + 1
//   day   = (seed>>5) % 28 + 1
// Returns (seed>>5) / 28 (the original's return value — a "years" quotient).
u32 PersonComputeBirthDateFromRecord(const Person* person, PersonBirthDate* out);

// gilde.exe qword_13CE852 / unk_13CE85A / unk_13CE85E — the live 14-byte game-date
// snapshot the FromRecord variant seeds from. Modeled as a settable global (runtime
// BSS in the cold IDB). Layout: dword[0] date, +4 byte, +6 byte, +8 dword, +12 word
// — only +0/+4/+6/+10 are read by the pack (see PackToRecord). Tests seed it.
struct GameDateSnapshot {
    i32 dateLow;   // +0  (the date counter)
    u8  byte4;     // +4  (-> pack hour)
    u8  pad5;      // +5
    u8  byte6;     // +6  (-> pack minute)
    u8  pad7;      // +7
    u8  byte8[2];  // +8  (var_24 low — part of +10 read)
    u8  byte10[2]; // +10 (-> pack cursor low word) ; +12 word (var_20)
    u8  byte12[2]; // +12
};
extern GameDateSnapshot g_gameDateSnapshot;

void ResetPersonLifecycle();  // test/setup helper (not in orig)

} // namespace guild::sim
