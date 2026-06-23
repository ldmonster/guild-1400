#include "sim/building4.h"

#include <cstring>

#include "sim/building_type.h"   // REAL siblings: Building_MapKindToCategory (0x5878b0),
                                 // Building_IsProductionKind (0x587f80)

namespace guild::sim {

// ===========================================================================
// Recovered constant table (byte-for-byte from gilde.exe dword_6496A9).
// 23 records x 21 bytes. Layout per record:
//   +0..+2  padding (the original reads the type byte as dword>>24 = +3)
//   +3      building-type byte (matched against the building's +0)
//   +4..+19 up to eight little-endian proto-id WORDs (0 == empty slot)
//   +20     trailing pad
// ===========================================================================
const std::uint8_t kDefaultObjectsTable[kDefaultObjRecords * kDefaultObjStride + 2] = {
    0x00,0x00,0x00,0x29,0x00,0x00,0x00,0x00,0xd4,0x01,0x55,0x01,0x56,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x2a,0x00,0x00,0x00,0x00,0xd4,0x01,0x55,0x01,0x56,0x01,0x58,0x01,0x57,0x01,0x00,0x00,0x00,
    0x00,0x00,0x00,0x2b,0x00,0x00,0x00,0x00,0xd4,0x01,0x00,0x00,0x55,0x01,0x56,0x01,0x58,0x01,0x57,0x01,0x59,
    0x01,0x5a,0x01,0x32,0xd5,0x01,0x00,0x00,0x00,0x00,0x5b,0x01,0x5c,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x33,0xd5,0x01,0x00,0x00,0x5b,0x01,0x5c,0x01,0x5d,0x01,0x5e,0x01,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x34,0xd5,0x01,0x00,0x00,0x5b,0x01,0x5c,0x01,0x5d,0x01,0x5e,0x01,0x60,0x01,0x5f,0x01,0x00,
    0x00,0x00,0x00,0x21,0xd7,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x6d,0x01,0x6e,0x01,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x22,0xd7,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x6d,0x01,0x6e,0x01,0x6f,0x01,0x70,0x01,0x00,
    0x00,0x00,0x00,0x23,0xd7,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x6d,0x01,0x6e,0x01,0x6f,0x01,0x70,0x01,0x71,
    0x01,0x72,0x01,0x17,0xd8,0x01,0x00,0x00,0x00,0x00,0x73,0x01,0x74,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x18,0xd8,0x01,0x00,0x00,0x00,0x00,0x73,0x01,0x74,0x01,0x75,0x01,0x77,0x01,0x00,0x00,0x00,
    0x00,0x00,0x00,0x19,0xd8,0x01,0x00,0x00,0x00,0x00,0x73,0x01,0x74,0x01,0x75,0x01,0x77,0x01,0x78,0x01,0x76,
    0x01,0x00,0x00,0x2f,0xd6,0x01,0x00,0x00,0x00,0x00,0x61,0x01,0x62,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x30,0xd6,0x01,0x00,0x00,0x00,0x00,0x61,0x01,0x62,0x01,0x63,0x01,0x64,0x01,0x00,0x00,0x00,
    0x00,0x00,0x00,0x31,0xd6,0x01,0x00,0x00,0x00,0x00,0x61,0x01,0x62,0x01,0x63,0x01,0x64,0x01,0x65,0x01,0x66,
    0x01,0x00,0x00,0x35,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xd9,0x01,0x79,0x01,0x7a,0x01,0x00,0x00,0x00,
    0x00,0x00,0x00,0x36,0x00,0x00,0x00,0x00,0x00,0x00,0xd9,0x01,0x79,0x01,0x7a,0x01,0x7b,0x01,0x7c,0x01,0x00,
    0x00,0x00,0x00,0x37,0x00,0x00,0x00,0x00,0x00,0x00,0xd9,0x01,0x79,0x01,0x7a,0x01,0x7b,0x01,0x7c,0x01,0x7d,
    0x01,0x7e,0x01,0x14,0xda,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x67,0x01,0x68,0x01,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x15,0xda,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x67,0x01,0x68,0x01,0x69,0x01,0x6a,0x01,0x00,
    0x00,0x00,0x00,0x16,0xda,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x67,0x01,0x68,0x01,0x69,0x01,0x6a,0x01,0x6b,
    0x01,0x6c,0x01,0x1f,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0xcd,0x01,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,0x00,
    0x00,0x00,   // 2-byte slack: the last record's +22/+23 spill read
};

// ===========================================================================
// Hook plumbing.
// ===========================================================================
static Building4Hooks  g_defaultHooks;
static Building4Hooks* g_hooks = &g_defaultHooks;
void SetBuilding4Hooks(Building4Hooks* hooks) {
    g_hooks = hooks ? hooks : &g_defaultHooks;
}
Building4Hooks* Building4HooksGet() { return g_hooks; }

// Helper: read the little-endian proto WORD at record offset `off`.
static std::uint16_t RecWord(const std::uint8_t* rec, int off) {
    return static_cast<std::uint16_t>(rec[off] | (rec[off + 1] << 8));
}

// ===========================================================================
// gilde.exe 0x586df8 — VIBE_Building_EnsureDefaultObjects
//   Original: walks 23 records; for each whose +3 type byte equals the building's
//   +0 byte, ensure a proto-255 room object exists (add when missing) and then
//   ensure each non-zero proto WORD (record +4..+19, stride 2) exists under that
//   room (querying the room's sub-list, adding when missing). Returns the last
//   engine handle touched.
// ===========================================================================
std::int32_t Building4_EnsureDefaultObjects(const std::uint8_t* building) {
    if (!building) return 0;
    Building4Hooks* hk = g_hooks;

    std::int32_t container;
    std::memcpy(&container, building + 93, 4);     // rec+93 container handle
    std::int32_t buildingObjId;
    std::memcpy(&buildingObjId, building + 1, 4);   // rec+1 object id
    std::uint8_t typeByte = building[0];

    std::int32_t result = 0;
    for (int recIdx = 0; recIdx < kDefaultObjRecords; ++recIdx) {
        const std::uint8_t* rec = &kDefaultObjectsTable[recIdx * kDefaultObjStride];
        if (rec[kDefaultObjTypeOff] != typeByte)
            continue;

        // Ensure the proto-255 room object under the building's container.
        std::int32_t room = hk->GameObjectQueryFind(container, 2, 6, 0, 255);
        result = room;
        if (!room) {
            room = hk->GameObjectAddObjekt(buildingObjId, 255, 1, recIdx);
            result = room;
        }
        if (!room)
            continue;

        // Ensure each non-zero proto WORD exists under the room. The original
        // reads the room's child-list handle at room+5 (dword) for the sub-query.
        // We re-resolve it through the room handle the engine returned.
        for (int slot = 0; slot < kDefaultObjProtoCount; ++slot) {
            int off = kDefaultObjProtoOff + slot * 2;
            std::uint16_t proto = RecWord(rec, off);
            if (!proto)
                continue;
            std::int32_t found = hk->GameObjectQueryFind(room, 1, 0, 0, proto);
            result = found;
            if (!found) {
                result = hk->GameObjectAddObjekt(room, proto, 2, recIdx);
            }
        }
    }
    return result;
}

// ===========================================================================
// gilde.exe 0x586ed8 — VIBE_Building_InitWorkerCapacities
//   Resolved worker object records are passed in. For the proto-42 worker: if any
//   of cfg573/cfg574 is set OR cfg575 is unset, clamp the +28/+29 capacity bytes
//   to max(cfg573,2) and max(cfg574,2); otherwise write +28 = cfg575. For the
//   proto-278 worker: when cfg573 and cfg574 are both 0 and cfg575 is non-zero,
//   write +28 = cfg575.
// ===========================================================================
void Building4_InitWorkerCapacities(std::uint8_t* recBase42,
                                    std::uint8_t* recBase278,
                                    const WorkerCfg& cfg) {
    if (recBase42) {
        if (cfg.cfg573 || cfg.cfg574 || !cfg.cfg575) {
            recBase42[28] = (cfg.cfg573 >= 2u) ? cfg.cfg573 : 2;
            recBase42[29] = (cfg.cfg574 >= 2u) ? cfg.cfg574 : 2;
        } else {
            recBase42[28] = cfg.cfg575;
        }
    }
    if (recBase278) {
        if (!cfg.cfg573 && !cfg.cfg574 && cfg.cfg575) {
            recBase278[28] = cfg.cfg575;
        }
    }
}

// ===========================================================================
// gilde.exe 0x5877ac — VIBE_Building_FindStorableObject
//   The +0 byte is classified through the REAL Building_MapKindToCategory (the
//   original calls VIBE_Building_MapTypeToCategory but discards its result for the
//   branch, gating instead on the production predicate / a kind-4 test). We
//   reproduce the verbatim branch:
//     * production building -> query proto 253;
//     * kind 4              -> query proto 84;
//     * otherwise iterate group-4 objects skipping proto 253.
// ===========================================================================
std::int32_t Building4_FindStorableObject(std::uint8_t kindByte,
                                          std::int32_t container) {
    Building4Hooks* hk = g_hooks;
    (void)Building_MapKindToCategory(kindByte);   // original computes it first

    if (Building_IsProductionKind(kindByte))      // real sibling 0x587f80
        return hk->GameObjectQueryFind(container, 1, 0, 0, 253);
    if (kindByte == 4)
        return hk->GameObjectQueryFind(container, 1, 0, 0, 84);

    std::int32_t result = hk->GameObjectQueryFind(container, 1, 4, 0, 2);
    while (result) {
        // The original tests the found object's +0 proto word != 253; we model the
        // proto through the iterator return (handle carries the proto via the hook).
        if (result != 253)
            break;
        result = hk->GameObjectIterNext();
    }
    return result;
}

// ===========================================================================
// gilde.exe 0x58a294 — VIBE_Building_FindUpgradeStorage
//   Classify the +0 byte through the REAL Building_MapKindToCategory:
//     category 5 -> map the active person's class byte {30->2,31->3,32->4,33->5}
//                   to a good type, resolve its store + proto-322 object;
//     category 3 -> the active person's good store (type 1) + proto-277 object;
//     otherwise  -> 0.
// ===========================================================================
std::int32_t Building4_FindUpgradeStorage(std::uint8_t kindByte,
                                          std::uint8_t activePersonClass,
                                          std::int32_t ctx) {
    Building4Hooks* hk = g_hooks;
    std::uint8_t cat = Building_MapKindToCategory(kindByte);   // real sibling 0x5878b0

    if (cat == 5) {
        int goodType;
        switch (activePersonClass) {
            case 30: goodType = 2; break;
            case 31: goodType = 3; break;
            case 32: goodType = 4; break;
            case 33: goodType = 5; break;
            default: return 0;     // LABEL_5 with v3 == null
        }
        const std::uint8_t* store = hk->PersonQueryByGoodType(goodType, ctx);
        if (!store)
            return 0;
        std::int32_t storeContainer;
        std::memcpy(&storeContainer, store + 93, 4);
        return hk->GameObjectQueryFind(storeContainer, 2, 6, 0, 322);
    }
    if (cat == 3) {
        const std::uint8_t* store = hk->PersonQueryByGoodType(1, ctx);
        if (!store)
            return 0;
        std::int32_t storeContainer;
        std::memcpy(&storeContainer, store + 93, 4);
        return hk->GameObjectQueryFind(storeContainer, 2, 6, 0, 277);
    }
    return 0;
}

// ===========================================================================
// gilde.exe 0x588554 — VIBE_Building_AttachStorageRooms
//   For a storage-kind building (type-record +0 == 2): collect every group-4
//   sub-object of the container then BuildFlagNodeList each with the +39 owner
//   word. Returns the last engine handle touched.
// ===========================================================================
std::int32_t Building4_AttachStorageRooms(const std::uint8_t* building,
                                          bool isStorageKind) {
    if (!building) return 0;
    Building4Hooks* hk = g_hooks;

    std::int32_t container;
    std::memcpy(&container, building + 93, 4);
    std::uint16_t ownerWord;
    std::memcpy(&ownerWord, building + 39, 2);

    std::int32_t result = static_cast<std::int32_t>(589u * building[0]);
    if (!isStorageKind)
        return result;

    // Collect the group-4 sub-objects (original buffers up to 35 handles).
    std::int32_t handles[35];
    int count = 0;
    std::int32_t obj = hk->GameObjectQueryFind(container, 1, 4, 0, 2);
    while (obj && count < 35) {
        handles[count++] = obj;
        obj = hk->GameObjectIterNext();
    }
    result = obj;
    for (int i = 0; i < count; ++i) {
        result = hk->BuildFlagNodeList(handles[i], ownerWord);
    }
    return result;
}

// ===========================================================================
// gilde.exe 0x588ce4 — VIBE_Building_RemoveStorageRoom
//   Remove by proto; -2 from the engine -> -3, else 0.
// ===========================================================================
std::int32_t Building4_RemoveStorageRoom(std::int32_t childListField,
                                         std::int16_t proto, std::int32_t slot) {
    if (g_hooks->GameObjectRemoveByProt(childListField, proto, slot) == -2)
        return -3;
    return 0;
}

// ===========================================================================
// gilde.exe 0x58a154 — VIBE_Building_SyncProfessionState
//   Derive the profession code, push it at field 357, then push 0 at field 372.
// ===========================================================================
void Building4_SyncProfessionState(std::int32_t rec, std::int32_t objId,
                                   int workKind, std::uint8_t defaultProf) {
    Building4Hooks* hk = g_hooks;

    std::int32_t prof;
    if (workKind == 32) {
        prof = 12;
    } else if (workKind == 31) {
        prof = 11;
    } else {
        // gilde.exe 58a200: edx = *(int*)(rec+0x162) >> 24 (arithmetic sar), then
        // dec. The +357 byte is sign-extended, so model it through int8 before the
        // -1, matching the original's signed shift.
        prof = static_cast<int>(static_cast<std::int8_t>(defaultProf)) - 1;
    }

    hk->CommandBeginDeltaPacket(rec, objId);
    hk->CommandAppendCopiedField(1u, 1u, &prof, 357);
    hk->CommandQueueRequestState23();

    prof = 0;
    hk->CommandBeginDeltaPacket(rec, objId);
    hk->CommandAppendCopiedField(4u, 1u, &prof, 372);
    hk->CommandQueueRequestState23();
}

// ===========================================================================
// gilde.exe 0x46c97c — VIBE_Building_EvalBuyBuilding
//   Only when the target's +0 byte is 4: find a handler whose +43 dword equals
//   the target's +4 id; on a hit enqueue the buy and return 16, else 0.
// ===========================================================================
int Building4_EvalBuyBuilding(std::uint8_t targetKind, std::int32_t targetId,
                              std::int32_t actorId) {
    Building4Hooks* hk = g_hooks;
    if (targetKind != 4)
        return 0;

    const std::uint32_t* handler = hk->HeFindFirstHandlerByFilter(1, 0, 27);
    while (handler) {
        // handler +43 dword (== handler[43]) compared to target id.
        if (static_cast<std::int32_t>(handler[43]) == targetId)
            break;
        handler = hk->HeFindNextMatchingHandler();
    }
    if (!handler)
        return 0;

    hk->EnqueueBuyBuilding(targetId, actorId, handler,
                           static_cast<std::int32_t>(handler[44]),
                           targetId, 1);
    return 16;
}

}  // namespace guild::sim
