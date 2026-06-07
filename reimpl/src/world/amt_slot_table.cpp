#include "world/amt_slot_table.h"

#include <cstring>

#include "world/office.h"   // OfficeDefBookCat (dword_62EC8E owner)

// Faithful 1:1 port of the Amt office placement-slot table search primitives
// (gilde.exe 0x47ff14, 0x56e850, 0x56e894..0x56e974). Every search is a fixed-
// stride linear scan with the same "occupied" test the originals use
// (*(int*)(rec+10) >> 24 != -1). The slot/object tables are live game state, so
// these take a caller-supplied array; the only baked table (the office-def book/
// category) is reused from office.cpp via OfficeDefBookCat.

namespace guild::world {

// gilde.exe 0x47ff14 — VIBE_Amt_GetOfficeType.
u8 AmtGetOfficeType(u8 type) {
    // if ( a1 < 0x25 ) return record byte +4 (bookCat); else return byte_62EC92[0].
    // byte_62EC92 is dword_62EC8E+4 == record 0 byte +4 == bookCat(0) == 0.
    if (type < 0x25)
        return OfficeDefBookCat(type);
    return 0;
}

i32 AmtSlotKey(const AmtSlot& s) {
    i32 key;
    std::memcpy(&key, &s, sizeof(key)); // the +0 dword (read over pad0[0..3])
    return key;
}

// gilde.exe 0x56e894 — VIBE_Amt_FindSlotByCoord.
int AmtFindSlotByCoord(const AmtSlot* slots, int x, int y) {
    for (int i = 0; i < kAmtSlotCount; ++i) {
        const AmtSlot& s = slots[i];
        // v5 == a2 && byte+9 == a3 && occupied
        if (s.x == x && s.y == y && AmtSlotOccupied(s))
            return i;
    }
    return -1; // the original returns 0 (a null pointer); -1 is our index form.
}

// gilde.exe 0x56e8c8 — VIBE_Amt_FindSlotByObjectId.
int AmtFindSlotByObjectId(const AmtSlot* slots, i32 id) {
    for (int i = 0; i < kAmtSlotCount; ++i) {
        const AmtSlot& s = slots[i];
        // while ( !occupied || a2 != objectId ) advance;
        if (AmtSlotOccupied(s) && s.objectId == id)
            return i;
    }
    return -1;
}

// gilde.exe 0x56e8ec — VIBE_Amt_FindRecordByKey.
int AmtFindRecordByKey(const AmtSlot* slots, i32 key) {
    for (int i = 0; i < kAmtSlotCount; ++i) {
        const AmtSlot& s = slots[i];
        // while ( !occupied || a2 != *result ) advance;
        if (AmtSlotOccupied(s) && AmtSlotKey(s) == key)
            return i;
    }
    return -1;
}

// gilde.exe 0x56e910 — VIBE_Amt_FindSlotAtPoint.
int AmtFindSlotAtPoint(const AmtSlot* slots, int px, int py) {
    for (int i = 0; i < kAmtSlotCount; ++i) {
        const AmtSlot& s = slots[i];
        if (!AmtSlotOccupied(s)) // skip free slots (the +10>>24 == -1 guard)
            continue;
        int h = (int)s.size >> 1; // v7 = (int)byte+12 >> 1
        if (px >= (int)s.x - h && px <= (int)s.x + h &&
            py >= (int)s.y - h && py <= (int)s.y + h)
            return i;
    }
    return -1;
}

// gilde.exe 0x56e974 — VIBE_Amt_FindFreePlacement.
int AmtFindFreePlacement(const AmtSlot* slots, int x, int y, int size) {
    // Pass 1: note any free slot; reject if an occupied slot already sits at (x,y).
    bool anyFree = false; // v5
    for (int i = 0; i < kAmtSlotCount; ++i) {
        const AmtSlot& s = slots[i];
        if (!AmtSlotOccupied(s)) {        // *(int*)(rec+10) >> 24 == -1
            anyFree = true;               // v5 = 1
        } else if (s.x == x && s.y == y) { // occupied AND exactly at (x,y)
            return 0;                     // placement blocked
        }
    }
    if (!anyFree)
        return 0;
    if (size <= 0)                        // if ( a3 <= 0 ) return 1;
        return 1;

    // Pass 2: sweep the (size x size) box centred on (x,y), offset by -size/2. The
    // original's `while ( !FindSlotAtPoint(v7,v13) ) advance;` loop CONTINUES while a
    // swept point is uncovered and BREAKS (return 0) the moment a point IS covered by
    // some occupied slot's hit-test box. It returns 1 only after sweeping the whole
    // box with no covered point. (i.e. 1 == the footprint is entirely clear.)
    int half = size / 2;                  // v10 = a3 / 2
    int rowMax = x + size - half;         // a3 - v10 + a2  (exclusive upper X)
    int colMax = y + size - half;         // a3 + a3/-2 + a4 (exclusive upper Y)
    for (int py = y - half; py < colMax; ++py) {       // v13 from a4 - a3/2
        for (int px = x - half; px < rowMax; ++px) {   // v7 from a2 - v10
            if (AmtFindSlotAtPoint(slots, px, py) >= 0) // a covered point -> blocked
                return 0;
        }
    }
    return 1; // swept the whole footprint, nothing covered it.
}

// gilde.exe 0x56e850 — VIBE_Amt_FindOfficeTypeRecord.
int AmtFindOfficeTypeRecord(const AmtTypeRecord* records, int count, i16 typeId) {
    // if ( !word_63D738[0] ) return 0;  -- empty table guard. With our split record
    // representation the faithful equivalent is "no records present".
    if (count <= 0)
        return -1;
    for (int i = 0; i < count; ++i) {
        // type field == *(int*)(rec+2) >> 16 == record word at +2.
        if (records[i].typeField == typeId)
            return i;
        // v3 = word_63D77E[v2] (next record's first word); if 0 after advancing,
        // stop. (The terminator is read BEFORE the advance, checked AFTER.)
        if (records[i].nextPresent == 0)
            return -1;
    }
    return -1;
}

} // namespace guild::world
