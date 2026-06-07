#include "sim/building3.h"

#include <cmath>
#include <cstring>

#include "sim/building_type.h"   // BuildingType_GroupFromCode (reconstructed, 0x58a4c8)

namespace guild::sim {

// ===========================================================================
// Recovered constant tables (byte-for-byte from gilde.exe).
// ===========================================================================
// gilde.exe dword_63C708 (records at 0x63C70D). {typeCode, openHour, closeHour}.
const OpenHoursRecord kOpenHoursTable[kOpenHoursCount] = {
    { 0x06,  5,  9 }, { 0x14,  6,  7 }, { 0x16,  8,  9 }, { 0x14,  9,  9 },
    { 0x14, 14,  9 }, { 0x14, 15,  6 }, { 0x16, 18,  9 }, { 0x14, 19,  9 },
    { 0x14, 20,  9 }, { 0x14, 21,  9 }, { 0x14, 22, 12 }, { 0x17, 23,  7 },
    { 0x16, 24,  7 }, { 0x16, 25,  7 }, { 0x16, 26,  7 }, { 0x16,  0,  0 },
};

// gilde.exe 0x63C4F8 (aBkBrunnen) — 12 rows of 44 bytes (32-byte name + 12 codes).
const std::uint8_t kTypeNameTable[kTypeNameRows * kTypeNameRowStride] = {
    // row 0: "bk_BRUNNEN"
    0x62,0x6b,0x5f,0x42,0x52,0x55,0x4e,0x4e,0x45,0x4e,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x08,0x09,0x0a,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // row 1: "bk_GEFAENGNIS"
    0x62,0x6b,0x5f,0x47,0x45,0x46,0x41,0x45,0x4e,0x47,0x4e,0x49,0x53,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x11,0x12,0x13,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // row 2: "bk_HANDWERK"
    0x62,0x6b,0x5f,0x48,0x41,0x4e,0x44,0x57,0x45,0x52,0x4b,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x0b,0x0c,0x0d,0x0e,0x0f,0x10,0x17,0x18,0x19,0x1a,0x1b,0x1c,
    // row 3: "bk_HANDWERK"
    0x62,0x6b,0x5f,0x48,0x41,0x4e,0x44,0x57,0x45,0x52,0x4b,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x29,0x2a,0x2b,0x2f,0x30,0x31,0x32,0x33,0x34,0x21,0x22,0x23,
    // row 4: "bk_KIRCHE_KL"
    0x62,0x6b,0x5f,0x4b,0x49,0x52,0x43,0x48,0x45,0x5f,0x4b,0x4c,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x14,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // row 5: "bk_KIRCHE_GR"
    0x62,0x6b,0x5f,0x4b,0x49,0x52,0x43,0x48,0x45,0x5f,0x47,0x52,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x16,0x15,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // row 6: "bk_RATHAUS"
    0x62,0x6b,0x5f,0x52,0x41,0x54,0x48,0x41,0x55,0x53,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x24,0x25,0x26,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // row 7: "bk_STADTWACHE"
    0x62,0x6b,0x5f,0x53,0x54,0x41,0x44,0x54,0x57,0x41,0x43,0x48,0x45,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x2c,0x2d,0x2e,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // row 8: "bk_WIRTSHAUS_GR"
    0x62,0x6b,0x5f,0x57,0x49,0x52,0x54,0x53,0x48,0x41,0x55,0x53,0x5f,0x47,0x52,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x35,0x36,0x37,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // row 9: "bk_WOHNSITZ_KL"
    0x62,0x6b,0x5f,0x57,0x4f,0x48,0x4e,0x53,0x49,0x54,0x5a,0x5f,0x4b,0x4c,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x04,0x05,0x01,0x02,0x03,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // row 10: "bk_WOHNSITZ_GR"
    0x62,0x6b,0x5f,0x57,0x4f,0x48,0x4e,0x53,0x49,0x54,0x5a,0x5f,0x47,0x52,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x06,0x07,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    // row 11: "bk_ZUNFTHAUS"
    0x62,0x6b,0x5f,0x5a,0x55,0x4e,0x46,0x54,0x48,0x41,0x55,0x53,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x38,0x39,0x3a,0x3b,0x3c,0x3d,0x3e,0x3f,0x40,0x41,0x42,0x43,
};

// gilde.exe dword_582F68 — 43 {typeCode, stringIdx} pairs; terminator {0, -1}.
const TypeStringIdPair kTypeStringIdTable[kTypeStringIdPairs] = {
    { 0x13, 0x01 }, { 0x54, 0x02 }, { 0x15, 0x04 }, { 0x18, 0x0d },
    { 0x14, 0x09 }, { 0x16, 0x18 }, { 0x17, 0x0f }, { 0x37, 0x00 },
    { 0x12, 0x19 }, { 0x09, 0x01 }, { 0xcf, 0x20 }, { 0xd6, 0x1b },
    { 0xdd, 0x1f }, { 0x120, 0x0a }, { 0x12e, 0x1c }, { 0x10e, 0x1d },
    { 0x110, 0x01 }, { 0x114, 0x1e }, { 0x139, 0x1e }, { 0x138, 0x01 },
    { 0x137, 0x1d }, { 0xf2, 0x0e }, { 0xe6, 0x21 }, { 0xe5, 0x21 },
    { 0xf0, 0x0b }, { 0xf7, 0x1c }, { 0xb7, 0x15 }, { 0xba, 0x1a },
    { 0xbb, 0x05 }, { 0x4d, 0x07 }, { 0x5c, 0x14 }, { 0x60, 0x12 },
    { 0x65, 0x1a }, { 0x145, 0x02 }, { 0x146, 0x15 }, { 0x14c, 0x16 },
    { 0x74, 0x20 }, { 0x85, 0x20 }, { 0x118, 0x20 }, { 0x9b, 0x20 },
    { 0xa5, 0x1c }, { 0xf3, 0x22 }, { 0x00, 0xffffffffu },
};

// ===========================================================================
// Module state: array bindings + game hour + cross-module hooks.
// ===========================================================================
static Building3Arrays g_arrays;
void SetBuilding3Arrays(const Building3Arrays& a) { g_arrays = a; }
const Building3Arrays& Building3ArraysGet() { return g_arrays; }

static int g_gameHour = 0;
void SetBuilding3GameHour(int hour) { g_gameHour = hour; }
int  Building3GameHour() { return g_gameHour; }

static Building3Hooks  g_defaultHooks;
static Building3Hooks* g_hooks = &g_defaultHooks;
void SetBuilding3Hooks(Building3Hooks* hooks) {
    g_hooks = hooks ? hooks : &g_defaultHooks;
}
Building3Hooks* Building3HooksGet() { return g_hooks; }

// ---------------------------------------------------------------------------
// Internal array helpers.
// ---------------------------------------------------------------------------
static const std::uint8_t* TypeRec(std::uint32_t index) {     // dword_13CE294, 589
    if (!g_arrays.buildingTypeBase) return nullptr;
    return g_arrays.buildingTypeBase + 589u * index;
}
static int ObjectCategory(std::uint16_t objId) {              // dword_13CE27C, 65
    if (!g_arrays.objectTypeBase) return -1;
    return g_arrays.objectTypeBase[65u * objId];
}

// ===========================================================================
// Time-window / name / string-id table lookups
// ===========================================================================
// gilde.exe 0x51dc04 — VIBE_Building_CheckTimeWindowOpen
bool Building3_CheckTimeWindowOpen(std::uint8_t typeCode, int* outOpen, int* outClose) {
    // byte_63C70D == the first record's type byte == kOpenHoursTable[0].typeCode.
    // When it's 0 the whole table is "disabled" and the building is always open.
    if (kOpenHoursTable[0].typeCode == 0)
        return true;

    // Scan records; the original's continue-sentinel is the NEXT record's type
    // byte, so we walk while the current type byte is non-zero.
    for (int i = 0; i < kOpenHoursCount; ++i) {
        const OpenHoursRecord& rec = kOpenHoursTable[i];
        if (rec.typeCode == 0)
            break;                                   // sentinel: end of table
        if (typeCode == rec.typeCode) {
            if (outOpen)  *outOpen  = rec.openHour;
            if (outClose) *outClose = rec.closeHour;
            int hour = g_gameHour;
            if (hour < rec.openHour || hour >= rec.closeHour)
                return false;
            return true;
        }
    }
    // Not present in the table -> always open.
    return true;
}

// gilde.exe 0x50c738 — VIBE_Building_LookupTypeName
int Building3_LookupTypeName(std::uint8_t code, char* out) {
    for (int row = 0; row < kTypeNameRows; ++row) {
        const std::uint8_t* base = &kTypeNameTable[row * kTypeNameRowStride];
        for (int i = 0; i < kTypeNameCodeCount; ++i) {
            if (base[kTypeNameNameLen + i] == code) {
                if (out)
                    std::memcpy(out, base, kTypeNameNameLen);  // copy the 32-byte name
                return 1;
            }
        }
    }
    return 0;
}

// gilde.exe 0x587fcc — VIBE_Building_LookupTypeStringId
int Building3_LookupTypeStringId(std::uint16_t typeIndex, int objectCategory) {
    if (objectCategory != 2)                          // only containers carry a string id
        return -1;
    // First pair's type word must be non-zero (the original's `dx == 0` guard).
    if ((kTypeStringIdTable[0].typeCode & 0xFFFFu) == 0)
        return -1;
    for (int i = 0; i < kTypeStringIdPairs; ++i) {
        if ((kTypeStringIdTable[i].typeCode & 0xFFFFu) == typeIndex)
            return static_cast<int>(kTypeStringIdTable[i].stringIdx) + 1433;
        // continue-sentinel: stop when the NEXT pair's type word is 0.
        if (i + 1 < kTypeStringIdPairs &&
            (kTypeStringIdTable[i + 1].typeCode & 0xFFFFu) == 0)
            break;
    }
    return -1;
}

// ===========================================================================
// Scene-query occupant finders
// ===========================================================================
// gilde.exe 0x587674 — VIBE_Building_FindWorkProductObject
//   `*a1` is the building record's +0 building-type index; the original maps it
//   to a UI category via VIBE_Building_MapTypeToCategory and reads the matching
//   type-table record (dword_13CE294, +0 = KIND byte). We wire the category /
//   production predicate through the REAL siblings Building_MapKindToCategory /
//   Building_IsProductionKind (building_type.cpp), feeding the type record's
//   +0 KIND byte; the scene query is the only hooked leaf.
std::int32_t Building3_FindWorkProductObject(const std::uint8_t* building) {
    if (!building) return 0;
    Building3Hooks* hk = g_hooks;
    std::int32_t container;
    std::memcpy(&container, building + 93, 4);
    std::uint8_t typeIndex = building[0];
    const std::uint8_t* rec = TypeRec(typeIndex);
    std::uint8_t kind = rec ? rec[0] : 0;
    std::uint8_t cat = Building_MapKindToCategory(kind);   // real sibling 0x5878b0

    std::int32_t result = 0;
    if (cat == 1 || cat == 7 || cat == 8) {
        if (kind == 22)
            result = hk->GameObjectQueryFind(container, 1, 0, 0, 302);
        else if (kind == 4)
            result = hk->GameObjectQueryFind(container, 1, 0, 0, 84);
        if (result)
            return result;
        // scan the type record's +35 word[64] room list for the first slot whose
        // object category (objectTypeBase[65*slot]) is 2 and slot != 253.
        if (rec) {
            for (int i = 0; i < 64; ++i) {
                std::int16_t slotW;
                std::memcpy(&slotW, rec + 35 + 2 * i, 2);
                if (slotW != static_cast<std::int16_t>(0xFFFF)) {
                    std::uint16_t slot = static_cast<std::uint16_t>(slotW) & 0x7FFFu;
                    if (ObjectCategory(slot) == 2 && slot != 253)
                        return hk->GameObjectQueryFind(container, 1, 0, 0, slot);
                }
            }
        }
        return 0;
    }
    if (cat == 4)
        return hk->GameObjectQueryFind(container, 1, 0, 0, 247);
    // else: only production-type buildings have a work product.
    if (Building_IsProductionKind(kind))                   // real sibling 0x587f80
        return hk->GameObjectQueryFind(container, 1, 0, 0, 253);
    return 0;
}

// gilde.exe 0x586904 — VIBE_Building_FindActiveWorkSlot
std::int32_t Building3_FindActiveWorkSlot(std::int32_t selectedBuilding,
                                          std::int16_t actorId) {
    if (!selectedBuilding) return 0;
    Building3Hooks* hk = g_hooks;
    // The original reads the container handle from *(dword_6477A4 + 93). We pass
    // the resolved container as `selectedBuilding` for the hook to query against.
    std::int32_t container = selectedBuilding;

    std::int32_t slots[6];
    for (int i = 0; i < 6; ++i)
        slots[i] = hk->GameObjectQueryFind(container, 1, 0, 0, 146 + i);
    if (!slots[0]) return 0;
    for (int i = 0; i < 6; ++i) {
        if (!slots[i]) return 0;   // terminate at first empty slot (original's v6 == 0)
        if (hk->GameObjectQueryFind(slots[i], 2, 6, 0, actorId))
            return hk->GameObjectQueryFind(slots[i], 1, 0, 0, 278);
    }
    return 0;
}

// gilde.exe 0x587908 — VIBE_Building_FindNearestSameType
std::int32_t Building3_FindNearestSameType(const std::uint8_t* selfRec,
                                           std::uint8_t typeByte) {
    if (!selfRec) return 0;
    // The +97 field holds a native object/mesh pointer in this reconstruction
    // (the original's *(int*)(rec+97)); read it at native width to avoid the
    // 64-bit pointer-truncation trap.
    const float* selfMesh;
    std::memcpy(&selfMesh, selfRec + 97, sizeof selfMesh);
    if (!selfMesh) return 0;

    Building3Hooks* hk = g_hooks;
    std::int32_t selfCtx = reinterpret_cast<std::intptr_t>(selfRec) & 0x7FFFFFFF;

    float selfOrigin[3];
    hk->TransformPoint(selfMesh, selfMesh + 19 /* +76 */, selfOrigin);

    const std::uint8_t* best = nullptr;
    float bestDist = 100000000.0f;
    for (const std::uint8_t* it = hk->PersonQueryBegin(selfCtx, 1, 6, 0);
         it; it = hk->PersonIterNext()) {
        // skip production-type and type-10 records
        const std::uint8_t* tr = TypeRec(it[0]);
        if (tr && tr[0] == 6) continue;                 // IsProductionType
        if (tr && tr[0] == 10) continue;
        const float* mesh;
        std::memcpy(&mesh, it + 97, sizeof mesh);
        if (!mesh) continue;
        if (it == selfRec) continue;
        if (it[0] != typeByte) continue;

        float candOrigin[3];
        hk->TransformPoint(mesh, mesh + 19 /* +76 */, candOrigin);
        float dx = candOrigin[0] - selfOrigin[0];
        float dy = candOrigin[1] - selfOrigin[1];
        float dz = candOrigin[2] - selfOrigin[2];
        float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (dist < bestDist) {
            best = it;
            bestDist = dist;
        }
    }
    return best ? (reinterpret_cast<std::intptr_t>(best) & 0x7FFFFFFF) : 0;
}

// gilde.exe 0x587a28 — VIBE_Building_FindNearestVacantSameType
const std::uint8_t* Building3_FindNearestVacantSameType(std::uint8_t typeByte,
                                                        std::int32_t selfCtx,
                                                        const std::uint8_t* selfRec) {
    Building3Hooks* hk = g_hooks;
    const std::uint8_t* best = nullptr;
    float bestDist = 100000000.0f;
    for (const std::uint8_t* it = hk->PersonQueryBegin(selfCtx, 1, 6, 0);
         it; it = hk->PersonIterNext()) {
        const std::uint8_t* tr = TypeRec(it[0]);
        if (tr && tr[0] == 6) continue;                 // IsProductionType
        if (tr && tr[0] == 10) continue;
        const float* candMesh;
        std::memcpy(&candMesh, it + 97, sizeof candMesh);
        if (!candMesh) continue;
        if (it == selfRec) continue;
        if (it[0] != typeByte) continue;
        std::uint16_t ownerW;
        std::memcpy(&ownerW, it + 39, 2);
        if (ownerW != 0xFFFF) continue;                 // require vacant

        const float* selfMeshP = nullptr;
        if (selfRec) std::memcpy(&selfMeshP, selfRec + 97, sizeof selfMeshP);
        // The original reads the mesh world position directly at +76/+80/+84.
        float dx = candMesh[19] - (selfMeshP ? selfMeshP[19] : 0.0f);   // +76 / 4
        float dy = candMesh[20] - (selfMeshP ? selfMeshP[20] : 0.0f);   // +80 / 4
        float dz = candMesh[21] - (selfMeshP ? selfMeshP[21] : 0.0f);   // +84 / 4
        float dist = std::sqrt(dx * dx + dy * dy + dz * dz);
        if (dist < bestDist) {
            best = it;
            bestDist = dist;
        }
    }
    return best;
}

// ===========================================================================
// Occupant category sync — wired against the REAL BuildingType_GroupFromCode.
// ===========================================================================
// gilde.exe 0x58a044 — VIBE_Building_UpdateOccupantCategory
int Building3_UpdateOccupantCategory(int personIdx, std::int8_t recCode,
                                     std::uint8_t classByte, std::uint8_t directCat,
                                     std::uint8_t* familyRecord, int* outFamilyCat,
                                     int* outDirty, std::uint8_t* outDirectCat) {
    if (outDirty) *outDirty = 0;
    if (outDirectCat) *outDirectCat = directCat;

    if (!familyRecord) {
        // No family record: just stamp the direct category slot.
        if (outDirectCat) *outDirectCat = directCat;
        return 0;
    }

    if (classByte == 5 || classByte == 6 || classByte == 7) {
        // Group the occupant's own type code and the (signed) recCode, then write
        // the direct category into the family record's group slot, keeping the
        // stronger of {existing, incoming} (the original's <-rank compare).
        std::uint8_t group = BuildingType_GroupFromCode(static_cast<std::uint8_t>(recCode));
        familyRecord[112 + group] = directCat;          // v11[112]
        // The second clamp pass: re-derive the slot and only overwrite when the
        // incoming rank is stronger or the slot is empty.
        std::uint8_t group2 = BuildingType_GroupFromCode(static_cast<std::uint8_t>(recCode));
        std::int8_t existing = static_cast<std::int8_t>(familyRecord[109 + group2]);
        if (static_cast<int>(recCode) < existing || familyRecord[112 + group2] == 0)
            familyRecord[112 + group2] = directCat;
        if (outFamilyCat) *outFamilyCat = familyRecord[112 + group2];
        if (outDirty) *outDirty = 1;                     // dword_631754 = 1
        if (outDirectCat) *outDirectCat = directCat;
        return 67 * personIdx;
    }

    // Other classes: stamp the direct slot only.
    if (outDirectCat) *outDirectCat = directCat;
    return 67 * personIdx;
}

// ===========================================================================
// Lifecycle unlink / release / destroy
// ===========================================================================
// gilde.exe 0x586d6c — VIBE_Building_FreeAndUnlink
int Building3_FreeAndUnlink(std::uint8_t* rec, std::int32_t recId,
                            std::int32_t* indexArrayA, std::int32_t* indexArrayB,
                            int indexCount) {
    if (!rec) return -1;
    if (rec[0] == 0) return -2;
    Building3Hooks* hk = g_hooks;

    if (rec[0] == 30) {
        std::int32_t buf;
        std::memcpy(&buf, rec + 113, 4);
        if (buf) {
            hk->MemoryFreeDebug(buf, buf);
            std::int32_t zero = 0;
            std::memcpy(rec + 113, &zero, 4);
        }
        std::int32_t zero = 0;
        std::memcpy(rec + 113, &zero, 4);
    }

    // Unlink the record id from the two parallel index arrays (134-dword stride,
    // 768 entries in the original — i.e. i goes 0, 134, ... < 102912).
    for (int i = 0; i < indexCount; ++i) {
        if (indexArrayA && indexArrayA[i * 134] == recId) indexArrayA[i * 134] = 0;
        if (indexArrayB && indexArrayB[i * 134] == recId) indexArrayB[i * 134] = 0;
    }

    std::int32_t childList;
    std::memcpy(&childList, rec + 93, 4);
    if (childList)
        hk->GameObjectFreeChildList(reinterpret_cast<std::int32_t*>(rec + 93));
    rec[0] = 0;
    return 0;
}

// gilde.exe 0x589468 — VIBE_Building_ReleaseOccupantHoldings
const std::uint8_t* Building3_ReleaseOccupantHoldings(const std::uint8_t* rec,
                                                      std::uint16_t actorWord) {
    if (!rec) return rec;
    if (actorWord == 0xFFFF) return rec;
    Building3Hooks* hk = g_hooks;
    hk->CharacterChangePlayerAction(0, 0, 0, actorWord);
    hk->CharActionCancelEntityActions(rec);
    hk->EventCancelMatchingActors();
    // VIBE_Building_EmptyCallbackStub() — inert by definition.
    hk->OfficeReleaseCharacterHoldings(
        static_cast<std::int32_t>(reinterpret_cast<std::intptr_t>(rec) & 0x7FFFFFFF));
    return rec;
}

// gilde.exe 0x588d00 — VIBE_Building_DetachAndDestroyOccupant
std::int32_t Building3_DetachAndDestroyOccupant(std::int32_t rec,
                                                std::int32_t linkedRec36,
                                                std::uint8_t flags36,
                                                std::int32_t char59,
                                                std::int32_t char59Ptr136,
                                                std::int32_t activeSlot,
                                                std::int32_t objId) {
    Building3Hooks* hk = g_hooks;
    if (linkedRec36 && (flags36 & 2) != 0)
        hk->CommandQueueRequestEntity29(-1, linkedRec36);   // -> +132 in original
    if (char59) {
        std::uint32_t slot = hk->CharacterIndexFromPointer(char59Ptr136);
        hk->UniverseSwitchActiveSlot(slot);
        hk->CharacterDestroy(char59);
        hk->UniverseSwitchActiveSlot(static_cast<std::uint32_t>(activeSlot));
    }
    std::int32_t root = hk->GameObjectResolveRootContainer(rec);
    if (root)
        return hk->GameObjectRemoveById(nullptr, objId);
    return root;
}

// gilde.exe 0x591870 — VIBE_Building_CollectByCityHandle
int Building3_CollectByCityHandle(std::int32_t cityField, const std::int32_t* filter,
                                  std::uint32_t** out) {
    Building3Hooks* hk = g_hooks;
    const std::uint32_t* h = hk->HeFindFirstHandlerByFilter(1, 0, 35);
    if (!h) return 0;
    int count = 0;
    while (h) {
        if (count >= 9) return count;
        if (h[44] == static_cast<std::uint32_t>(cityField)) {
            if (!filter || h[43] == static_cast<std::uint32_t>(filter[1])) {
                if (out) out[count] = const_cast<std::uint32_t*>(h);
                ++count;
            }
        }
        h = hk->HeFindNextMatchingHandler();
    }
    return count;
}

// gilde.exe 0x5918e0 — VIBE_Building_CollectOwnedByPerson
int Building3_CollectOwnedByPerson(std::uint16_t ownerId, const std::uint8_t* person,
                                   std::int32_t cityField, bool cityWild,
                                   std::uint32_t** out) {
    Building3Hooks* hk = g_hooks;
    const std::uint32_t* h = hk->HeFindFirstHandlerByFilter(1, 0, 35);
    if (!h) return 0;
    int count = 0;
    while (count < 9) {
        const std::uint8_t* p = hk->PersonQueryBegin(
            person ? static_cast<std::int32_t>(reinterpret_cast<std::intptr_t>(person) & 0x7FFFFFFF) : 0,
            1, 1, static_cast<std::int32_t>(h[43]));
        if (p) {
            std::uint16_t pOwner;
            std::memcpy(&pOwner, p + 39, 2);
            bool personOk = (!person || person == p);
            bool cityOk = cityWild || (h[44] == static_cast<std::uint32_t>(cityField));
            if (pOwner == ownerId && personOk && cityOk) {
                if (out) out[count] = const_cast<std::uint32_t*>(h);
                ++count;
            }
        }
        h = hk->HeFindNextMatchingHandler();
        if (!h) return count;
    }
    return count;
}

// gilde.exe 0x5896fc — VIBE_Building_ResetAllBuildings
void Building3_ResetAllBuildings(std::int16_t* lightRecords, int* outSel0,
                                 int* outSel1, int* outSel2) {
    Building3Hooks* hk = g_hooks;
    for (int i = 0; i < 768; ++i)
        hk->BuildingRemoveAndCleanup(i, 2);
    for (int i = 0; i < 16; ++i) {
        std::int16_t* rec = lightRecords ? lightRecords + 82 * i : nullptr;
        hk->LightSetGrayColorThunk(0, 164, rec);
        if (rec) rec[0] = -1;
    }
    if (outSel0) *outSel0 = 0;     // dword_647724
    if (outSel1) *outSel1 = 0;     // dword_64771C
    if (outSel2) *outSel2 = 0;     // dword_647720
}

}  // namespace guild::sim
