#pragma once
// Secondary "criminal-record / mission-tracking" table (gilde.exe byte_122FEC0).
// This is a SEPARATE table from the 512-entry Straftat crime table (crime.h):
// it is a 128-entry x 36-byte record table, keyed by a 32-bit *source* value at
// field +4 (the parallel global dword_122FEC4 == byte_122FEC0 + 4). Each record
// caches a copy of some crime/event source plus a per-type progress counter that
// the mission system bumps. Faithful 1:1 port.
//
// Layout recovered from the accessors' raw pointer maths (v2 += 9 dwords == 36 B
// stride; v2 >= 1152 == 128 records; byte_122FEC0[v2*4] == record byte +0):
//   +0x00 (byte)  occupied / type marker — 0 == free slot, else the crime type
//   +0x04 (dword) source key (dword_122FEC4) — the lookup key (== *(src+4))
//   +0x08 (qword) staging copy  (FindAndInit: = qword_13CE852)
//   +0x10 (dword) staging copy  (FindAndInit: = unk_13CE85A)
//   +0x14 (word)  staging copy  (FindAndInit: = unk_13CE85E)
//   +0x18 (dword) zeroed by FindAndInit
//   +0x1C (dword) progress counter — zeroed by FindAndInit, bumped by
//                 VIBE_Mission_TrackCrimeProgress for matching crime types
//   +0x20 (byte)  zeroed by FindAndInit
//
// Translated functions:
//   VIBE_StraftatTable_FindBySource   0x53846c
//   VIBE_StraftatTable_FindAndInit    0x5384a0
//   VIBE_StraftatTable_ContainsSource 0x538524
//   VIBE_Mission_TrackCrimeProgress   0x539054
#include "guild/common/types.h"

namespace guild::world {

constexpr int kStraftatTableCount  = 128;  // 1152 dwords / 9 dwords
constexpr int kStraftatTableStride = 36;   // 9 dwords

GUILD_PACKED_BEGIN
struct StraftatTableRecord {
    u8  type;        // +0x00  occupied marker / crime type (0 == free slot)
    u8  pad1[3];     // +0x01  alignment to the source-key dword
    i32 source;      // +0x04  source key (dword_122FEC4)
    u8  staging8[8]; // +0x08  qword staging copy (qword_13CE852)
    i32 staging16;   // +0x10  dword staging copy (unk_13CE85A)
    u16 staging20;   // +0x14  word staging copy (unk_13CE85E)
    u8  pad22[2];    // +0x16  alignment to the +0x18 dword
    i32 field18;     // +0x18  zeroed by FindAndInit
    i32 progress;    // +0x1C  per-type progress counter (TrackCrimeProgress bumps)
    u8  field20;     // +0x20  zeroed by FindAndInit
    u8  pad33[3];    // +0x21  trailing bytes to the 36-byte stride
} GUILD_PACKED;
GUILD_PACKED_END
static_assert(sizeof(StraftatTableRecord) == kStraftatTableStride,
              "StraftatTableRecord must be 36 bytes");

// The table itself (byte_122FEC0). 128 records, BSS-zeroed (all slots free).
extern StraftatTableRecord g_crimeTrackTable[kStraftatTableCount];

// The FindAndInit staging globals (qword_13CE852 / unk_13CE85A / unk_13CE85E),
// the contiguous source the init copies into a found record's +0x08..+0x15 span.
extern u8  g_crimeTrackStaging8[8]; // qword_13CE852
extern i32 g_crimeTrackStaging16;   // unk_13CE85A
extern u16 g_crimeTrackStaging20;   // unk_13CE85E

// Resets the whole table to free (all bytes zero).
void StraftatTableReset();

// gilde.exe 0x53846c — VIBE_StraftatTable_FindBySource  (__usercall, eax=src).
// Scans the 128 records for the first occupied slot (type byte != 0) whose source
// key (+4) equals *(src+4). Returns the record index, or -1 if none. (The original
// returns a record pointer or null; we return the index, -1 == null.)
//   `src` is a pointer to a source struct; only its dword at +4 is read.
int StraftatTableFindBySource(i32 sourceKey);

// gilde.exe 0x5384a0 — VIBE_StraftatTable_FindAndInit  (__usercall, eax=src).
// FindBySource, then (on hit) overwrites the record's staging fields from the
// g_crimeTrackStaging* globals and zeroes the +0x18 / +0x1C / +0x20 fields. Returns
// the record index, or -1 if not found.
int StraftatTableFindAndInit(i32 sourceKey);

// gilde.exe 0x538524 — VIBE_StraftatTable_ContainsSource  (__usercall, eax=key).
// Returns 1 if some occupied record's source key (+4) == `key`, else 0.
int StraftatTableContainsSource(i32 sourceKey);

// gilde.exe 0x539054 — VIBE_Mission_TrackCrimeProgress
//   (__usercall, eax=src, dl=crimeType).
// Looks up the record for `sourceKey`. If found and `crimeType` is one of the
// tracked codes {0x0B, 0x13, 0x17, 0x1C, 0x28}, and the record's type byte (+0)
// equals `crimeType`, increments its progress counter (+0x1C). Returns 1 when a
// record was found (regardless of the type test), 0 when none / untracked type.
int MissionTrackCrimeProgress(i32 sourceKey, u8 crimeType);

} // namespace guild::world
