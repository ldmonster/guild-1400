#include "sim/family_record.h"

#include <cstring>

// ============================================================================
// The family-record table (word_13C3110).  See family_record.h for the recovered
// layout and the per-function provenance.  This module reconstructs:
//   * the table itself (16 x 164 bytes),
//   * VIBE_Person_GetFamilyRecord  @0x58c408,
//   * the table reset              @0x5896fc tail,
//   * the inline allocator         @0x58ecf3 (CreateAndSpawn family arm),
//   * the apply-time name/seed stamp @0x4967bf..0x49681c (ExCreatePersonB).
// ============================================================================

namespace guild::sim {

// gilde.exe word_13C3110 / dword_647720.
u8  g_familyTable[kFamilyCapacity * kFamilyStride] = {};
i32 g_familyCount = 0; // dword_647720

namespace {

// -1082130432 == 0xBF800000 == -1.0f. The seed written at +128 by both the
// CreateAndSpawn allocator (0x58ed11) and the ExCreatePersonB stamp (0x49681c).
inline void StoreSeedMinusOne(u8* fam) {
    const u32 bits = 0xBF800000u; // -1.0f
    std::memcpy(fam + kFamSeedOff, &bits, 4);
}

inline void StoreWord(u8* p, i16 v) { std::memcpy(p, &v, 2); }
inline i16  LoadWord(const u8* p)   { i16 v; std::memcpy(&v, p, 2); return v; }

} // namespace

// ----------------------------------------------------------------------------
// gilde.exe 0x58c408 — VIBE_Person_GetFamilyRecord (__usercall eax = a1@<eax>).
//   v1 = *(BYTE*)(a1+2);            // kind
//   if ((v1 != 6 && v1 != 7 && v1 != 5) || *(char*)(a1+81) >= 0) return 0;
//   v2 = *(WORD*)(a1+80);
//   HIBYTE(v2) = 0; LOBYTE(v2) = v2 & 0xF;    // v2 = (word & 0xF)
//   return &word_13C3110[82 * v2];
// ----------------------------------------------------------------------------
u8* Person_GetFamilyRecord(const void* personRec) {
    const u8* p = static_cast<const u8*>(personRec);
    const u8 kind = p[2];
    if ((kind != 6 && kind != 7 && kind != 5) || static_cast<i8>(p[81]) >= 0)
        return nullptr;
    const u16 word = LoadWord(p + 80);
    const u32 index = static_cast<u32>(word & 0x0F);
    return &g_familyTable[kFamilyStride * index];
}

// ----------------------------------------------------------------------------
// gilde.exe 0x5896fc tail — the family-table reset inside ResetAllBuildings.
//   v1 = word_13C3110; v2 = 0;
//   do { Light_SetGrayColorThunk(0, 164, v1);  // memset 164 bytes -> 0
//        v1 += 82; word_13C3110[82*v2] = -1; ++v2; } while (v2 < 16);
//   dword_647724 = 0; dword_64771C = 0; dword_647720 = 0;
// (Only the family-table portion + dword_647720 are owned here; the building/guild
//  counters dword_647724 / dword_64771C are reset by their own modules.)
// ----------------------------------------------------------------------------
void FamilyRecord_ResetAll() {
    for (int i = 0; i < kFamilyCapacity; ++i) {
        u8* rec = &g_familyTable[kFamilyStride * i];
        std::memset(rec, 0, kFamilyStride);
        StoreWord(rec + kFamWordOff, static_cast<i16>(-1));
    }
    g_familyCount = 0;
}

// ----------------------------------------------------------------------------
// gilde.exe 0x58ecf3 — the inline family allocator in VIBE_Person_CreateAndSpawn.
//   if (dword_647720 < 16) {
//       v14[40] = dword_647720 | 0x8000;        // person +0x50 = idx | 0x8000
//       FamilyRecord = VIBE_Person_GetFamilyRecord(v14);
//       if (FamilyRecord) {
//           *((DWORD*)FamilyRecord + 32) = -1082130432; // +128 = -1.0f
//           *FamilyRecord            = v14[40];          // +0   = family word
//       }
//       ... (kind 7/5 wappen dedup, handled by the caller) ...
//       ++dword_647720;
//   } else {
//       return 0xFFFF;   // family table full -> create fails
//   }
// Returns the family record on success, nullptr when full (caller fails create).
// ----------------------------------------------------------------------------
u8* FamilyRecord_AllocForPerson(void* personRec, u8 /*kind*/) {
    if (g_familyCount >= kFamilyCapacity)
        return nullptr; // 0x58e4ac — table full, caller returns 0xFFFF.

    u8* p = static_cast<u8*>(personRec);
    const i16 famWord = static_cast<i16>(g_familyCount | 0x8000);
    StoreWord(p + 0x50, famWord); // person +0x50 = idx | 0x8000

    u8* fam = Person_GetFamilyRecord(p); // re-derives via the +81 sign + low nibble
    if (fam) {
        StoreSeedMinusOne(fam);                                  // +128 = -1.0f
        StoreWord(fam + kFamWordOff, famWord);                   // +0   = family word
    }
    ++g_familyCount; // ++dword_647720
    return fam;
}

// ----------------------------------------------------------------------------
// gilde.exe 0x4967bf..0x49681c — the ExCreatePersonB family stamp.
//   *(WORD*)familyRec = person +0x50 word;            // +0
//   StrNCopyPad(familyRec+2, packet+53, 16);          // +2 dynasty name
//   *((DWORD*)familyRec + 32) = -1082130432;          // +128 = -1.0f
// ----------------------------------------------------------------------------
void FamilyRecord_StampName(u8* fam, i16 familyWord, const char* name) {
    if (!fam) return;
    StoreWord(fam + kFamWordOff, familyWord);
    // VIBE_Util_StrNCopyPad(familyRec+2, packet+53, 16) @0x4967e3: copy up to 16
    // source chars (stop early at the first NUL, WITHOUT emitting it), then
    // zero-pad the remainder of the 16-byte field. A 16-char name fills all 16
    // bytes with no terminator (faithful to the binary's count == 16).
    int i = 0;
    if (name) {
        for (; i < 16 && name[i]; ++i)
            fam[kFamNameOff + i] = static_cast<u8>(name[i]);
    }
    for (; i < 16; ++i)
        fam[kFamNameOff + i] = 0;
    StoreSeedMinusOne(fam);
}

} // namespace guild::sim
