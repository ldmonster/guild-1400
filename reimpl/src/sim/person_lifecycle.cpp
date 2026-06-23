#include "sim/person_lifecycle.h"

#include "sim/entity.h"     // PersonQueryBegin / PersonIterNext / g_objects
#include "sim/building.h"   // g_buildingTypes (589-stride type table)

#include <cstring>

// Faithful 1:1 port of a person/building-type lifecycle leaf cluster from
// gilde.exe. See person_lifecycle.h for the function map and the proof (in
// person_record.h) that the "Person_*" name/kind queries actually walk the
// 589-byte building-TYPE descriptor table, not the 536-byte person array.

namespace guild::sim {

// ---------------------------------------------------------------------------
// Raw unaligned access helpers (the originals do raw x86 word/dword loads at
// arbitrary byte offsets; memcpy keeps that faithful without C++ UB).
// ---------------------------------------------------------------------------
static i32 ReadI32(const void* base, int off) {
    i32 v;
    std::memcpy(&v, reinterpret_cast<const u8*>(base) + off, sizeof(v));
    return v;
}
static u16 ReadU16(const void* base, int off) {
    u16 v;
    std::memcpy(&v, reinterpret_cast<const u8*>(base) + off, sizeof(v));
    return v;
}
static u8 ReadU8(const void* base, int off) {
    return reinterpret_cast<const u8*>(base)[off];
}

// gilde.exe 0x5cb8f0 — VIBE_Util_StrCmpNoCase (__usercall, eax=a1, edx=a2).
// Case-insensitive byte compare (ASCII 'A'..'Z' folded to lower). Returns
// (foldedA - foldedB) at the first mismatch (0 == equal). Kept as a static leaf
// here (not the canonical util owner) — CountActiveSlots is its only caller in
// this module.
static int StrCmpNoCase(const u8* a1, const u8* a2) {
    for (;;) {
        u8 v3 = *a1;
        u8 v4 = *a2;
        if (v3 >= 0x41u && v3 <= 0x5Au) v3 += 32;
        if (v4 >= 0x41u && v4 <= 0x5Au) v4 += 32;
        if (v3 != v4 || !v4)
            return static_cast<int>(v3) - static_cast<int>(v4);
        ++a1;
        ++a2;
    }
}

// ===========================================================================
// Globals.
// ===========================================================================
// qword_13CE852 / unk_13CE85A / unk_13CE85E — live game-date snapshot (runtime
// BSS; zero in the cold IDB).
GameDateSnapshot g_gameDateSnapshot{};

// ---------------------------------------------------------------------------
// gilde.exe 0x586a40 — VIBE_Person_FindByObjectRef
// ---------------------------------------------------------------------------
//   result = QueryBegin(token, /*argc*/1, /*op*/0, /*value*/71);
//   if (!result) return 0;
//   while (ref != *(DWORD*)(result + 101)) {
//       result = IterNext();  if (!result) return 0;
//   }
//   return result;
// The variadic {1, 0, 71} decodes to argc=1, one filter {op 0 (alive/type byte),
// value 71}. The token (esi arg) selects the query context; our PersonQueryBegin
// models it as the static iterator (token is unused state in the reimpl).
ObjectRec* PersonFindByObjectRef(i32 ref, int token) {
    (void)token;
    PersonFilter filter{0, 71};                 // op 0: alive/type byte == 71
    ObjectRec* result = PersonQueryBegin(&filter, 1);
    if (!result)
        return nullptr;
    while (ref != ReadI32(result, 101)) {       // *(DWORD*)(result + 101)
        result = PersonIterNext();
        if (!result)
            return nullptr;
    }
    return result;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x587b60 — VIBE_Person_CountActiveSlots  (type name -> type index)
// ---------------------------------------------------------------------------
//   v2 = 0; v3 = 0;
//   while (StrCmpNoCase(name, (u8*)(v3 + dword_13CE294 + 1))) {
//       v3 += 589; ++v2;
//       if (v3 >= 42408) return 0;          // 589 * 72
//   }
//   return v2;                              // 0-based index of the match
// (the original walks the type table comparing each record's +1 name string).
u8 PersonCountActiveSlots(const char* name) {
    const u8* nm = reinterpret_cast<const u8*>(name);
    int v2 = 0;
    int v3 = 0;
    const u8* base = reinterpret_cast<const u8*>(g_buildingTypes);
    while (StrCmpNoCase(nm, base + v3 + kBuildingTypeNameOff)) {
        v3 += kBuildingTypeStride;
        ++v2;
        if (v3 >= kBuildingTypeScanBound)
            return 0;
    }
    return static_cast<u8>(v2);
}

// ---------------------------------------------------------------------------
// gilde.exe 0x587b9c — VIBE_Person_CollectByType  (collect type ids by kind byte)
// ---------------------------------------------------------------------------
//   v4 = 0; v5 = 0; v6 = 0;
//   do {
//       if (*(u8*)(v6 + base) == kind) out[v4++] = v5;
//       ++v5; v6 += 589;
//   } while (v5 < 72);
//   return v4;
int PersonCollectByType(u8 kind, u8* out) {
    const u8* base = reinterpret_cast<const u8*>(g_buildingTypes);
    // The original takes `char a1@<al>` and compares the table's (zero-extended,
    // `movzx`) kind byte against `a1` SIGN-EXTENDED to 32 bits (sar ebx,18h at
    // 0x587bbf). For kind >= 0x80 the sign-extended compare value is negative and
    // can never equal the 0..255 table byte — so high-bit kinds match nothing.
    const int kindSx = static_cast<int>(static_cast<i8>(kind));   // a1 sign-extended
    int v4 = 0;   // count written
    int v5 = 0;   // type index
    int v6 = 0;   // byte cursor into the table
    do {
        if (static_cast<int>(ReadU8(base, v6)) == kindSx)   // kind byte @ type +0
            out[v4++] = static_cast<u8>(v5);
        ++v5;
        v6 += kBuildingTypeStride;
    } while (v5 < kBuildingTypeScanCount);
    return v4;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x589b40 — VIBE_BuildingType_GetGuildRankPair
// ---------------------------------------------------------------------------
// Maps a building-type code (al) to a pair of guild rank bytes (*a2, *a3).
// Always returns 1 (the original has no failure path). Verbatim switch.
int BuildingTypeGetGuildRankPair(u8 typeCode, u8* outA, u8* outB) {
    switch (typeCode) {
        case 4:            *outA = 1; *outB = 2; break;
        case 6:            *outA = 0; *outB = 1; break;
        case 7:            *outA = 4; *outB = 1; break;
        case 8:            *outA = 0; *outB = 3; break;
        case 9:            *outA = 0; *outB = 4; break;
        case 10: case 12:  *outA = 3; *outB = 2; break;
        case 11:           *outA = 2; *outB = 3; break;
        default:           *outA = 1; *outB = 0; break;
    }
    return 1;
}

// ---------------------------------------------------------------------------
// Birth-date derivation helpers.
// ---------------------------------------------------------------------------
// Inlined effect of VIBE_GameTime_PackToRecord (0x583304) for the birth-date
// pack, producing the 12-byte date record the originals then patch. PackToRecord:
//   out+2 (word) = (u16)inDateLow + 1400          // year tag
//   out+0 = 1                                      // season (overwritten -> day)
//   out+1 = 3 * (inDateLow % 4) + 1                // (overwritten -> month)
//   out+4 = in+4 byte ; out+5 = in+6 byte          // hour / minute
//   out+8 = in+10 dword                            // cursor
// We compute the year tag (the only pre-patch field both callers read back) plus
// the carried hour/minute/cursor, then the caller patches day/month.
struct PackResult {
    u16 yearTag;
    u8  hour;
    u8  minute;
    i32 cursor;
};
static PackResult PackTime(i32 inDateLow, u8 in4, u8 in6, i32 in10) {
    PackResult r;
    r.yearTag = static_cast<u16>(static_cast<u16>(inDateLow) + 1400);
    r.hour    = in4;
    r.minute  = in6;
    r.cursor  = in10;
    return r;
}

// gilde.exe 0x58be24 — VIBE_Person_ComputeBirthDate
//   if (person[8]) return 0;
//   inDateLow = person[38] >> 16;                          // (signed arith. shift)
//   pack = PackToRecord(inDateLow, <garbage 4/6/10>);
//   seed = (u32)((pack.year) ^ person[48]);                // (pack.out[0]>>16)
//   month = ((inDateLow % 19) + (seed>>3) % 7) % 12 + 1;
//   day   = (seed>>5) % 28 + 1;
//   out = pack with day/month patched;  return 1;
// The pack input's +4/+6/+10 bytes are read from stack garbage by the original;
// only day/month/year are deterministic. We zero the carried fields.
int PersonComputeBirthDate(const Person* person, PersonBirthDate* out) {
    if (ReadU8(person, 8))                       // *(BYTE*)(person+8) != 0
        return 0;

    i32 inDateLow = ReadI32(person, 38) >> 16;   // *(int*)(person+38) >> 16 (asr)
    PackResult pack = PackTime(inDateLow, 0, 0, 0);

    u32 seed = static_cast<u32>(pack.yearTag) ^ static_cast<u32>(ReadI32(person, 48));

    // month: signed modulo on inDateLow (idiv esi=19), unsigned on the seed term
    // (div ebx=7). The final reduction `% 12` is an UNSIGNED div in the binary
    // (0x58be88: xor edx,edx; div ebx=0Ch) — so the sum is taken as u32 here even
    // when (inDateLow % 19) is negative, which is the only case where unsigned and
    // signed modulo diverge.
    int monthTerm = (inDateLow % 19) + static_cast<int>((seed >> 3) % 7u);
    u8 month = static_cast<u8>(static_cast<u32>(monthTerm) % 12u + 1);
    u8 day   = static_cast<u8>((seed >> 5) % 28u + 1);

    std::memset(out, 0, sizeof(*out));
    out->day    = day;
    out->month  = month;
    out->year   = pack.yearTag;
    out->hour   = pack.hour;
    out->minute = pack.minute;
    out->cursor = pack.cursor;
    return 1;
}

// gilde.exe 0x58bd84 — VIBE_Person_ComputeBirthDateFromRecord
//   buf = g_gameDateSnapshot (14 bytes);
//   if (!person[8]) buf.dateLow = person[38] >> 16;
//   buf.dateLow -= (u16)person[10];                        // subtract age word
//   pack = PackToRecord(buf.dateLow, buf+4, buf+6, buf+10);
//   seed = (u32)(pack.year ^ person[48]);
//   month = ((buf.dateLow % 11) + (seed>>3) % 13) % 12 + 1;
//   day   = (seed>>5) % 28 + 1;
//   out = pack with day/month patched;  return (seed>>5) / 28;
u32 PersonComputeBirthDateFromRecord(const Person* person, PersonBirthDate* out) {
    const GameDateSnapshot& snap = g_gameDateSnapshot;

    i32 dateLow = snap.dateLow;
    if (!ReadU8(person, 8))                      // if (!*(BYTE*)(person+8))
        dateLow = ReadI32(person, 38) >> 16;     //   dateLow = person[38] >> 16
    dateLow -= ReadU16(person, 10);              // dateLow -= *(u16*)(person+10)

    // PackToRecord reads a full dword at input+10 (snapshot bytes 10..13).
    i32 in10;
    const u8 in10bytes[4] = { snap.byte10[0], snap.byte10[1],
                              snap.byte12[0], snap.byte12[1] };
    std::memcpy(&in10, in10bytes, 4);
    PackResult pack = PackTime(dateLow, snap.byte4, snap.byte6, in10);

    u32 seed = static_cast<u32>(pack.yearTag) ^ static_cast<u32>(ReadI32(person, 48));

    // signed `% 11` (idiv edi=0Bh), unsigned `% 13` (div esi=0Dh), and the final
    // `% 12` is an UNSIGNED div (0x58bdf1: xor edx,edx; div esi=0Ch) — match it.
    int monthTerm = (dateLow % 11) + static_cast<int>((seed >> 3) % 13u);
    u8 month = static_cast<u8>(static_cast<u32>(monthTerm) % 12u + 1);
    u8 day   = static_cast<u8>((seed >> 5) % 28u + 1);

    std::memset(out, 0, sizeof(*out));
    out->day    = day;
    out->month  = month;
    out->year   = pack.yearTag;
    out->hour   = pack.hour;
    out->minute = pack.minute;
    out->cursor = pack.cursor;
    return (seed >> 5) / 28u;
}

void ResetPersonLifecycle() {
    g_gameDateSnapshot = GameDateSnapshot{};
}

} // namespace guild::sim
