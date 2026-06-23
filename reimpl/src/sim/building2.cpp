#include "sim/building2.h"

#include <cstring>

#include "sim/building_type.h"   // BuildingType_GroupFromCode (reconstructed)

namespace guild::sim {

// ===========================================================================
// Recovered constant tables (byte-for-byte from gilde.exe).
// ===========================================================================
// gilde.exe byte_649910 — 76 six-byte records (LookupTypeRecordA). Record 0 is
// all-zero and doubles as the out-of-range fallback.
const std::uint8_t kTypeRecordTableA[kTypeRecordA_Count * kTypeRecordStride] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x69, 0x69, 0xbd, 0x93, 0x69, 0x04,
    0x3f, 0x3f, 0x93, 0x69, 0x3f, 0x03, 0x3f, 0x3f, 0x69, 0x54, 0x2a, 0x02,
    0x3f, 0x2a, 0x3f, 0x3f, 0x15, 0x02, 0x3f, 0x15, 0x3f, 0x3f, 0x15, 0x01,
    0x2a, 0x15, 0x3f, 0x3f, 0x15, 0x01, 0x69, 0x69, 0x93, 0x69, 0xbd, 0x04,
    0x3f, 0x3f, 0x69, 0x3f, 0x93, 0x03, 0x3f, 0x2a, 0x54, 0x3f, 0x69, 0x02,
    0x3f, 0x15, 0x3f, 0x2a, 0x3f, 0x02, 0x3f, 0x15, 0x3f, 0x15, 0x3f, 0x01,
    0x2a, 0x15, 0x3f, 0x15, 0x3f, 0x01, 0x69, 0x69, 0x93, 0x69, 0xbd, 0x04,
    0x3f, 0x3f, 0x69, 0x3f, 0x93, 0x03, 0x3f, 0x2a, 0x54, 0x3f, 0x69, 0x02,
    0x3f, 0x2a, 0x3f, 0x15, 0x3f, 0x02, 0x2a, 0x2a, 0x3f, 0x15, 0x3f, 0x01,
    0x15, 0x2a, 0x3f, 0x15, 0x3f, 0x01, 0xbd, 0x93, 0x69, 0x69, 0x69, 0x04,
    0x93, 0x69, 0x3f, 0x3f, 0x3f, 0x03, 0x69, 0x54, 0x3f, 0x2a, 0x3f, 0x02,
    0x3f, 0x3f, 0x3f, 0x15, 0x2a, 0x02, 0x3f, 0x3f, 0x3f, 0x15, 0x15, 0x01,
    0x3f, 0x3f, 0x2a, 0x15, 0x15, 0x01, 0xbd, 0x69, 0x69, 0x69, 0x93, 0x04,
    0x93, 0x3f, 0x3f, 0x3f, 0x69, 0x03, 0x69, 0x3f, 0x3f, 0x2a, 0x54, 0x02,
    0x54, 0x15, 0x2a, 0x2a, 0x3f, 0x02, 0x54, 0x15, 0x2a, 0x2a, 0x2a, 0x01,
    0x3f, 0x15, 0x2a, 0x2a, 0x2a, 0x01, 0x54, 0x69, 0x3f, 0x54, 0x2a, 0x02,
    0x54, 0x69, 0x3f, 0x54, 0x2a, 0x02, 0x54, 0x69, 0x3f, 0x54, 0x2a, 0x02,
    0x93, 0xbd, 0x69, 0x69, 0x69, 0x04, 0x69, 0x93, 0x3f, 0x3f, 0x3f, 0x03,
    0x54, 0x69, 0x3f, 0x3f, 0x2a, 0x02, 0x3f, 0x69, 0x2a, 0x15, 0x15, 0x02,
    0x3f, 0x69, 0x15, 0x15, 0x15, 0x01, 0x3f, 0x54, 0x15, 0x15, 0x15, 0x01,
    0x69, 0x69, 0x93, 0xbd, 0x69, 0x04, 0x3f, 0x3f, 0x69, 0x93, 0x3f, 0x03,
    0x3f, 0x3f, 0x54, 0x69, 0x2a, 0x02, 0x2a, 0x15, 0x3f, 0x69, 0x15, 0x02,
    0x2a, 0x15, 0x3f, 0x54, 0x15, 0x01, 0x2a, 0x15, 0x3f, 0x3f, 0x15, 0x01,
    0x93, 0xbd, 0x69, 0x69, 0x69, 0x04, 0x69, 0x93, 0x3f, 0x3f, 0x3f, 0x03,
    0x54, 0x69, 0x3f, 0x3f, 0x2a, 0x02, 0x3f, 0x69, 0x2a, 0x15, 0x15, 0x02,
    0x3f, 0x69, 0x15, 0x15, 0x15, 0x01, 0x3f, 0x54, 0x15, 0x15, 0x15, 0x01,
    0x69, 0x69, 0x93, 0xbd, 0x69, 0x04, 0x3f, 0x3f, 0x69, 0x93, 0x3f, 0x03,
    0x3f, 0x3f, 0x54, 0x69, 0x2a, 0x02, 0x3f, 0x15, 0x54, 0x3f, 0x15, 0x02,
    0x2a, 0x15, 0x54, 0x3f, 0x15, 0x01, 0x2a, 0x15, 0x3f, 0x3f, 0x15, 0x01,
    0x93, 0xbd, 0x69, 0x69, 0x69, 0x04, 0x69, 0x93, 0x3f, 0x3f, 0x3f, 0x03,
    0x54, 0x69, 0x3f, 0x3f, 0x2a, 0x02, 0x3f, 0x69, 0x2a, 0x15, 0x15, 0x02,
    0x3f, 0x69, 0x15, 0x15, 0x15, 0x01, 0x3f, 0x54, 0x15, 0x15, 0x15, 0x01,
    0x93, 0xbd, 0x69, 0x69, 0x69, 0x04, 0x69, 0x93, 0x3f, 0x3f, 0x3f, 0x03,
    0x54, 0x69, 0x3f, 0x3f, 0x2a, 0x02, 0x3f, 0x69, 0x2a, 0x15, 0x15, 0x02,
    0x3f, 0x69, 0x15, 0x15, 0x15, 0x01, 0x3f, 0x54, 0x15, 0x15, 0x15, 0x01,
    0xbd, 0x93, 0x69, 0x69, 0x69, 0x04, 0x93, 0x69, 0x3f, 0x3f, 0x3f, 0x03,
    0x69, 0x54, 0x3f, 0x2a, 0x3f, 0x02, 0x54, 0x54, 0x2a, 0x15, 0x15, 0x02,
    0x54, 0x3f, 0x2a, 0x15, 0x15, 0x01, 0x54, 0x3f, 0x15, 0x15, 0x15, 0x01,
};

// gilde.exe byte_649AD8 — 27 six-byte records (LookupTypeRecordB).
const std::uint8_t kTypeRecordTableB[kTypeRecordB_Count * kTypeRecordStride] = {
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x2a, 0x00, 0x7e, 0x7e, 0x15, 0x00,
    0x15, 0x00, 0x54, 0x54, 0x00, 0x00, 0x54, 0x54, 0x00, 0x00, 0x54, 0x00,
    0x15, 0x2a, 0x00, 0x00, 0x2a, 0x00, 0x54, 0x54, 0x15, 0x15, 0x2a, 0x00,
    0x2a, 0x2a, 0x00, 0x00, 0x15, 0x00, 0x54, 0x54, 0x15, 0x15, 0x2a, 0x00,
    0x2a, 0x2a, 0x00, 0x00, 0x15, 0x00, 0x54, 0x54, 0x15, 0x15, 0x2a, 0x00,
    0x2a, 0x2a, 0x00, 0x00, 0x15, 0x00, 0x2a, 0x2a, 0x00, 0x00, 0x15, 0x00,
    0x2a, 0x2a, 0x00, 0x00, 0x15, 0x00, 0x54, 0x54, 0x15, 0x15, 0x2a, 0x00,
    0x2a, 0x2a, 0x00, 0x00, 0x15, 0x00, 0x2a, 0x00, 0x7e, 0x7e, 0x15, 0x00,
    0x15, 0x00, 0x54, 0x54, 0x00, 0x00, 0x54, 0x54, 0x15, 0x15, 0x2a, 0x00,
    0x2a, 0x2a, 0x00, 0x00, 0x15, 0x00, 0x2a, 0x00, 0x7e, 0x7e, 0x15, 0x00,
    0x15, 0x00, 0x54, 0x54, 0x00, 0x00, 0x54, 0x54, 0x15, 0x15, 0x2a, 0x00,
    0x2a, 0x2a, 0x00, 0x00, 0x15, 0x00, 0x54, 0x54, 0x15, 0x15, 0x2a, 0x00,
    0x2a, 0x2a, 0x00, 0x00, 0x15, 0x00, 0x54, 0x54, 0x15, 0x15, 0x2a, 0x00,
    0x2a, 0x2a, 0x00, 0x00, 0x15, 0x00,
};

// ===========================================================================
// Module state: game-array bindings + cross-module query hooks.
// ===========================================================================
static BuildingArrayBindings g_arrays;
void SetBuildingArrayBindings(const BuildingArrayBindings& b) { g_arrays = b; }
const BuildingArrayBindings& BuildingArrays() { return g_arrays; }

static IBuilding2QueryHooks  g_defaultQueryHooks;
static IBuilding2QueryHooks* g_queryHooks = &g_defaultQueryHooks;
void SetBuilding2QueryHooks(IBuilding2QueryHooks* hooks) {
    g_queryHooks = hooks ? hooks : &g_defaultQueryHooks;
}
IBuilding2QueryHooks* Building2QueryHooks() { return g_queryHooks; }

// ---------------------------------------------------------------------------
// Internal helpers: address into the three byte-base arrays at the originals'
// strides. Return nullptr when the base is unbound (the null-table bail).
// ---------------------------------------------------------------------------
static const std::uint8_t* TypeRec(std::uint32_t index) {     // dword_13CE294, 589
    if (!g_arrays.buildingTypeBase) return nullptr;
    return g_arrays.buildingTypeBase + 589u * index;
}
static const std::uint8_t* PersonRec(std::uint32_t index) {   // word_12CE910, 536
    if (!g_arrays.personFamilyBase) return nullptr;
    return g_arrays.personFamilyBase + 536u * index;
}
static int ObjectCategory(std::uint16_t objId) {              // dword_13CE27C, 65
    if (!g_arrays.objectTypeBase) return -1;
    return g_arrays.objectTypeBase[65u * objId];
}

// ===========================================================================
// Type-record table lookups
// ===========================================================================
// gilde.exe 0x589778 — VIBE_Building_LookupTypeRecordA
TypeRecord* Building_LookupTypeRecordA(std::uint8_t code, TypeRecord* out) {
    const std::uint8_t* rec;
    if (code < kTypeRecordA_Count)                       // a1 < 76
        rec = &kTypeRecordTableA[kTypeRecordStride * code];
    else
        rec = &kTypeRecordTableA[0];                     // fallback: record 0
    out->dword0 = static_cast<std::uint32_t>(rec[0]) |
                  (static_cast<std::uint32_t>(rec[1]) << 8) |
                  (static_cast<std::uint32_t>(rec[2]) << 16) |
                  (static_cast<std::uint32_t>(rec[3]) << 24);
    out->word4  = static_cast<std::uint16_t>(rec[4] | (rec[5] << 8));
    return out;
}

// gilde.exe 0x5897c8 — VIBE_Building_LookupTypeRecordB
TypeRecord* Building_LookupTypeRecordB(std::uint8_t code, TypeRecord* out) {
    const std::uint8_t* rec;
    if (code < kTypeRecordB_Count)                       // a1 < 27
        rec = &kTypeRecordTableB[kTypeRecordStride * code];
    else
        rec = &kTypeRecordTableB[0];
    out->dword0 = static_cast<std::uint32_t>(rec[0]) |
                  (static_cast<std::uint32_t>(rec[1]) << 8) |
                  (static_cast<std::uint32_t>(rec[2]) << 16) |
                  (static_cast<std::uint32_t>(rec[3]) << 24);
    out->word4  = static_cast<std::uint16_t>(rec[4] | (rec[5] << 8));
    return out;
}

// ===========================================================================
// Type-record matchers
// ===========================================================================
// gilde.exe 0x589960 — VIBE_Building_MatchTypeCode
int Building_MatchTypeCode(std::uint16_t typeIndex, int count,
                           const std::uint8_t* codes) {
    // gilde.exe 589998: the record is copied from word_12CE910[268*a1] (the
    // person/family array, byte stride 536) — NOT the 589-stride building-type
    // table. The matched field is *(int*)&rec[353] >> 24 == rec[356].
    const std::uint8_t* rec = PersonRec(typeIndex);
    if (count <= -1)                                     // a2 <= -1
        return 0;
    if (count <= 0)                                      // a2 <= 0 (==0): 0
        return 0;
    if (!rec)                                            // null table => no match
        return 0;
    // Original: signed compare (movsx ecx, bl  vs  sar esi, 18h). The +356 byte
    // is sign-extended before the comparison; match the signed semantics.
    std::int32_t typeByte = static_cast<std::int8_t>(rec[356]);
    for (int i = 0; i < count; ++i) {
        if (typeByte == static_cast<std::int32_t>(static_cast<std::int8_t>(codes[i])))
            return 1;
    }
    return 0;
}

// gilde.exe 0x5898e8 — VIBE_Building_MatchProfessionCode
int Building_MatchProfessionCode(std::uint16_t typeIndex, int count,
                                 const std::uint8_t* codes) {
    // gilde.exe 589906: rec = word_12CE910[268*a1] (person/family array, byte
    // stride 536). Fields +358 and +361 are compared as unsigned bytes against the
    // (unsigned char) code (the original loads them with no sign extension).
    const std::uint8_t* rec = PersonRec(typeIndex);
    int result = 0;
    if (count > -1 && rec) {                             // a2 > -1
        std::uint8_t prof0 = rec[358];
        std::uint8_t prof1 = rec[361];
        for (int i = 0; i < count; ++i) {
            if (result)                                  // early-out latch
                break;
            std::uint8_t code = codes[i];
            if (prof0 == code || prof1 == code)
                result = 1;
        }
    }
    return result;
}

// gilde.exe 0x592a5c — VIBE_Building_MapTypeToState
int Building_MapTypeToState(std::uint8_t typeCode, std::uint8_t* outState) {
    const std::uint8_t* rec = TypeRec(typeCode);
    std::uint8_t v2 = rec ? rec[0] : 0;                  // type record +0
    if (v2 >= 0x10u) {
        if (v2 <= 0x10u) {                               // == 16
            *outState = 7;
            return 0;
        }
        if (v2 >= 0x18u) {                               // >= 24
            if (v2 <= 0x18u) {                           // == 24
                *outState = 3;
                return 1;
            } else if (v2 <= 0x19u) {                    // == 25
                *outState = 4;
                return 1;
            } else {
                if (v2 != 26)
                    return 0;
                *outState = 5;
                return 1;
            }
        } else {                                         // 17..23
            if (v2 != 23)
                return 0;
            *outState = 2;
            return 1;
        }
    } else {                                             // < 16
        if (v2 < 7u) {
            if (v2 != 4)
                return 0;
            *outState = 7;                               // LABEL_4
            return 0;
        }
        if (v2 <= 7u) {                                  // == 7
            *outState = 6;
            return 0;
        } else {                                         // 8..15
            if (v2 != 15)
                return 0;
            *outState = 1;
            return 1;
        }
    }
}

// gilde.exe 0x589d24 — VIBE_Building_GetCategoryForObject
std::uint8_t Building_GetCategoryForObject(std::int8_t recCode, std::int8_t matchCode,
                                           std::uint8_t directCat, int familyCat) {
    std::uint8_t group = BuildingType_GroupFromCode(static_cast<std::uint8_t>(recCode));
    if (static_cast<std::int8_t>(group) == matchCode)    // movsx al cmp ecx
        return directCat;                                // byte_12CEA74[536*idx]
    if (familyCat < 0)                                   // !FamilyRecord => 0
        return 0;
    return static_cast<std::uint8_t>(familyCat);         // family[matchCode + 0x70]
}

// ===========================================================================
// Room / object slot collectors
// ===========================================================================
// gilde.exe 0x58faa8 — VIBE_Building_CollectObjectSlots
int Building_CollectObjectSlots(std::uint8_t typeCode, std::int32_t containerHandle,
                                std::int16_t* out) {
    const std::uint8_t* rec = TypeRec(typeCode);
    int collected = 0;
    if (!rec)
        return 0;
    const std::uint16_t* roomList =
        reinterpret_cast<const std::uint16_t*>(rec + 35);   // +35 word[64]
    int idx = 0;
    if (roomList[0]) {
        do {
            std::uint16_t slot = roomList[idx];
            slot &= 0x7FFFu;                                // HIBYTE &= ~0x80
            std::int16_t hit = Building2QueryHooks()->GameObjectQueryFind(
                containerHandle, 2, 6, 0, slot);
            if (hit) {
                out[collected] = hit;
                ++collected;
            }
            ++idx;
        } while (idx < 64 && roomList[idx]);
    }
    return collected;
}

// gilde.exe 0x58fb30 — VIBE_Building_CollectStorableSlots
int Building_CollectStorableSlots(std::uint8_t typeCode, std::int16_t* out) {
    const std::uint8_t* rec = TypeRec(typeCode);
    if (!rec)
        return 0;
    const std::uint16_t* roomList =
        reinterpret_cast<const std::uint16_t*>(rec + 35);
    std::int16_t* cur = out;                                // ecx = a2 (write cursor)
    int collected = 0;                                      // edi
    int idx = 0;                                            // edx
    if (roomList[0]) {                                      // v5 != 0
        do {
            std::uint16_t v7 = roomList[idx] & 0x7FFFu;
            if (ObjectCategory(v7) != 33) {                 // not a wall
                ++cur;
                ++collected;
            }
            if (out)                                        // a2 != 0
                *cur = static_cast<std::int16_t>(v7);
            ++idx;
        } while (idx < 64 && roomList[idx]);
    }
    return collected;
}

// gilde.exe 0x58fbb4 — VIBE_Building_CollectSlotsAfterObject
int Building_CollectSlotsAfterObject(std::uint8_t typeCode, std::uint8_t slotCount,
                                     std::int16_t afterSlot, std::int16_t* out) {
    const std::uint8_t* rec = TypeRec(typeCode);
    if (!rec)
        return 0;
    const std::uint16_t* roomList =
        reinterpret_cast<const std::uint16_t*>(rec + 35);
    // gilde.exe 58fbfc: *a2 is loaded with movsx (sign-extended int16), the room
    // entry is masked to (& 0x7FFF) then ZERO-extended (and ecx,0FFFFh). So the
    // search key is the SIGNED *a2, NOT a masked copy — a high-bit afterSlot can
    // never match a masked-positive room entry. Do not mask afterSlot.
    std::int32_t target = static_cast<std::int16_t>(afterSlot);
    int i = 0;
    for (; i < slotCount; ++i) {                            // i < *(u8*)(rec+34)
        std::int32_t v8 = roomList[i] & 0x7FFFu;            // HIBYTE &= ~0x80, then zero-ext
        if (v8 == target)
            break;
    }
    if (i >= slotCount)                                     // not found
        return 0;
    int collected = 0;
    int j = i + 1;
    if (j < 64) {
        std::int16_t* cur = out;
        do {
            std::uint16_t v12 = roomList[j];                // raw word
            if (!v12)                                       // terminator
                break;
            std::uint16_t v13 = v12 & 0x7FFFu;
            int cat = ObjectCategory(v13);
            if (cat == 2 || cat == 6)                       // container/workstation
                break;
            if (cat != 33) {                                // skip walls from output
                ++cur;
                ++collected;
            }
            if (out)
                *cur = static_cast<std::int16_t>(v13);
            ++j;
        } while (j < 64);
    }
    return collected;
}

// gilde.exe 0x588044 — VIBE_Building_CollectFlagNodeCallback
bool Building_CollectFlagNodeCallback(std::int32_t nodeHandle,
                                      std::int32_t nodePackedKindSlot,
                                      FlagNodeAccumulator* acc) {
    std::uint16_t slot = static_cast<std::uint16_t>(nodePackedKindSlot); // +72 low word
    int kind = nodePackedKindSlot >> 24;                                 // +72 high byte
    if (kind != 1 || ObjectCategory(slot) != 33)
        return acc->count < 64;
    acc->slots[acc->count]   = static_cast<std::int16_t>(slot);          // +256 + 2*count
    int c = acc->count;
    acc->count = c + 1;                                                  // +384
    acc->handles[c] = nodeHandle;                                        // +4*count
    return acc->count < 64;
}

// gilde.exe 0x594cfc — VIBE_Building_FindOwnedDungeonSlot
int Building_FindOwnedDungeonSlot(bool hasDungeonFlag, std::int32_t dungeonOwnerId) {
    if (!hasDungeonFlag)                                    // *(u8*)(a1+433) == 0
        return 0;
    const std::uint8_t* base = g_arrays.charArrayBase;      // dword_13CE298, 169
    if (!base)
        return 1;                                           // empty scan -> exhausts
    for (int i = 0; i < 256; ++i) {
        const std::uint8_t* slot = base + 169u * i;
        std::uint8_t alive = slot[0];                       // *v2
        if (alive) {
            const std::uint8_t* td = TypeRec(alive);        // buildingType[589*alive]
            if (td && td[0] == 4) {                         // dungeon kind
                std::int32_t owner;
                std::memcpy(&owner, slot + 101, 4);         // *(dword)(v2+101)
                if (owner == dungeonOwnerId)
                    return dungeonOwnerId ^ owner;          // == 0 on hit
            }
        }
    }
    return 1;                                               // v3 >= 256
}

// ===========================================================================
// Office helpers (cross-module person/scene query -> hooks)
// ===========================================================================
// gilde.exe 0x58a280 — VIBE_Building_HasActiveOffice
bool Building_HasActiveOffice(std::uint8_t goodType, int ctx) {
    std::uint8_t kind = 0;
    return Building2QueryHooks()->PersonQueryByGoodType(goodType, ctx, &kind) != -1;
}

// gilde.exe 0x58a354 — VIBE_Building_FindOfficeStorage
std::int16_t Building_FindOfficeStorage(std::uint8_t goodType, int ctx) {
    std::uint8_t kind = 0;
    std::int32_t containerHandle =
        Building2QueryHooks()->PersonQueryByGoodType(goodType, ctx, &kind);
    std::int32_t protoId = (kind == 1) ? 277 : 322;        // v4 == 1 ? 277 : 322
    return Building2QueryHooks()->GameObjectQueryFind(containerHandle, 2, 6, 0, protoId);
}

}  // namespace guild::sim
