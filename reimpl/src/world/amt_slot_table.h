#pragma once
// Amt (office) — the office-placement SLOT-TABLE search primitives and the
// object-type record finder. These are the small, self-contained table-walk
// leaves the Amt office/overview windows (VIBE_Amt_RunOfficeGridWindow,
// VIBE_Amt_AssignSlotData, ...) call to locate a placement slot by coordinate,
// object id or hit-test point, and to resolve an object-type definition record.
//
// Recovered byte-for-byte from gilde.exe (all are fixed-stride linear scans):
//   * VIBE_Amt_GetOfficeType        0x47ff14 — office-def book/category lookup.
//   * VIBE_Amt_FindSlotByCoord      0x56e894 — match (+8,+9) == (x,y), occupied.
//   * VIBE_Amt_FindSlotByObjectId   0x56e8c8 — match (+20) == id, occupied.
//   * VIBE_Amt_FindRecordByKey      0x56e8ec — match (+0) == key, occupied.
//   * VIBE_Amt_FindSlotAtPoint      0x56e910 — AABB hit-test around (+8,+9), half
//     extent (+12)>>1.
//   * VIBE_Amt_FindFreePlacement    0x56e974 — scan a free slot + reject duplicate
//     (x,y), then search a (size x size) box for a free hit-test point.
//
// The slot table is a 64-entry array of 24-byte records (SLOT count fixed at 64,
// stride 24). The "occupied" test the originals use is `*(int*)(rec+10) >> 24 !=
// -1`, i.e. the high byte at offset +13 is not 0xFF (0xFF == free slot).
#include "guild/common/types.h"

namespace guild::world {

// ---------------------------------------------------------------------------
// Office-def book/category lookup (gilde.exe 0x47ff14).
// ---------------------------------------------------------------------------
// VIBE_Amt_GetOfficeType reads the same dword_62EC8E table office.cpp owns:
//   if (type < 0x25) return record byte +4 (the bookCat 1..9);  else return 0.
// Record byte +4 == OfficeDefBookCat(type) (the +2-skewed record's byte +2). We
// reuse office.h's accessor so the table stays single-owned.
u8 AmtGetOfficeType(u8 type);

// ---------------------------------------------------------------------------
// Slot record (24 bytes). Only the fields the search primitives read are named.
// ---------------------------------------------------------------------------
struct AmtSlot {
    u8  pad0[8];     // +0x00  (+0 dword is the "key" FindRecordByKey matches; see
                     //         AmtSlotKey() which reads it as a dword)
    u8  x;           // +0x08  grid X
    u8  y;           // +0x09  grid Y
    u8  pad10[2];    // +0x0A  low two bytes of the +10 dword
    u8  size;        // +0x0C  extent (hit-test half-extent is size>>1)
    u8  marker;      // +0x0D  HIBYTE of the +10 dword (0xFF == free slot)
    u8  pad14[6];    // +0x0E
    i32 objectId;    // +0x14  object id FindSlotByObjectId matches
};
static_assert(sizeof(AmtSlot) == 24, "AmtSlot must be 24 bytes (stride)");

constexpr int kAmtSlotCount  = 64;   // the originals cap every scan at 64 entries
constexpr u8  kAmtSlotFreeHi = 0xFF; // marker byte (+13) value for a free slot

// rec is occupied iff *(int*)(rec+10) >> 24 != -1, i.e. marker (+13) != 0xFF.
inline bool AmtSlotOccupied(const AmtSlot& s) { return s.marker != kAmtSlotFreeHi; }

// The +0 dword FindRecordByKey compares (read through the first 4 pad bytes).
i32 AmtSlotKey(const AmtSlot& s);

// gilde.exe 0x56e894 — VIBE_Amt_FindSlotByCoord.
// First OCCUPIED slot whose (x,y) == (`x`,`y`). Returns its index or -1.
int AmtFindSlotByCoord(const AmtSlot* slots, int x, int y);

// gilde.exe 0x56e8c8 — VIBE_Amt_FindSlotByObjectId.
// First OCCUPIED slot whose objectId (+20) == `id`. Returns its index or -1.
int AmtFindSlotByObjectId(const AmtSlot* slots, i32 id);

// gilde.exe 0x56e8ec — VIBE_Amt_FindRecordByKey.
// First OCCUPIED slot whose key (+0 dword) == `key`. Returns its index or -1.
int AmtFindRecordByKey(const AmtSlot* slots, i32 key);

// gilde.exe 0x56e910 — VIBE_Amt_FindSlotAtPoint.
// First OCCUPIED slot whose axis-aligned box [x-h, x+h] x [y-h, y+h] (h = size>>1)
// contains the point (`px`,`py`). Returns its index or -1.
int AmtFindSlotAtPoint(const AmtSlot* slots, int px, int py);

// gilde.exe 0x56e974 — VIBE_Amt_FindFreePlacement.
// Two-stage placement probe at anchor (`x`,`y`) with footprint `size`:
//   1. Scan all 64 slots: note whether ANY slot is free; if an OCCUPIED slot
//      already sits exactly at (x,y) return 0 (placement blocked).
//   2. If a free slot exists and size > 0, sweep the (size x size) box centred on
//      (x,y) (offset by -size/2): if ANY swept point is covered by an occupied
//      slot's hit-test box (AmtFindSlotAtPoint hit) the footprint is blocked -> 0.
//      Return 1 only when the whole footprint is clear. If size <= 0 return 1 (a
//      free slot with no footprint check).
// Returns 1 (placement possible) or 0 (blocked / no room), matching the original.
int AmtFindFreePlacement(const AmtSlot* slots, int x, int y, int size);

// ---------------------------------------------------------------------------
// Object-type definition finder (gilde.exe 0x56e850).
// ---------------------------------------------------------------------------
// VIBE_Amt_FindOfficeTypeRecord scans a record table (word_63D738, 70-byte stride)
// for the record whose type field (the 16-bit word at record offset +2, read by the
// original as *(int*)(&word_63D738[v2]) >> 16) equals `typeId`. The scan stops at the
// first record whose terminator word (the first word of the NEXT record,
// word_63D77E[v2] == word_63D738[v2+35]) is 0.
// Returns the matching record index or -1. Modeled over a caller-supplied array of
// records; each record exposes its type field and a "is the next record present"
// terminator (non-zero == continue).
struct AmtTypeRecord {
    i16 typeField;   // the value compared against typeId (record's +2 HIWORD)
    i16 nextPresent; // word_63D77E[v2]: 0 terminates the scan (no more records)
};
// `count` is the table capacity; the original relies on the terminator, but we cap
// the walk at `count` defensively.
int AmtFindOfficeTypeRecord(const AmtTypeRecord* records, int count, i16 typeId);

} // namespace guild::world
