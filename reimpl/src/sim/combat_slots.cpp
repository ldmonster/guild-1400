// gilde.exe — Combat slot / target-selection LEAVES (namespace guild::sim).
// See combat_slots.h for the table layouts and the per-function provenance.
//
// FIDELITY. Each leaf below is a 1:1 translation of the Hex-Rays pseudocode for
// the named address; the raw +N byte pokes the originals do against static
// globals are reproduced over caller-supplied table / struct views. The integer
// arithmetic (including the round-robin modulo and the unsigned compares) is
// preserved exactly.
#include "sim/combat_slots.h"

#include <cstdint>
#include <cstring>

namespace guild::sim {

// ---------------------------------------------------------------------------
// gilde.exe 0x57e76c — VIBE_Combat_FindAvailableSquadSlot
//
// Faithful translation of the two scan branches. The original juggles the wanted
// flag/type through register byte lanes (HIBYTE(v11)=dl wantType, v10[4]=bl
// wantFlag) and reinterprets the slot fill-count word + a3 as a dword whose top
// byte (>>24) equals wantFlag; that top-byte trick is reproduced directly as the
// owner-class-byte == wantFlag compare.  The relation-id is the searcher's
// *(u16*)(a1+39); 0xFFFF disables the relation gate.
//
//   wantType != 0 branch (slotIdx i, 0..767):
//     accept iff marker != -1 && (flags & 0x2000) == 0 && busyRank <= 1
//       && (reservedBy == wantType || reservedBy == 0) && lock == 0
//       && (double)fillCount >= requiredCap && ownerClassByte == wantFlag
//       && (g == 2 || groupClass == g)   where g = wantType ? IsTypeInGroup(wantType) : 0
//       && (rel == 0xFFFF || RelationMatrix(rel, i) > -30)
//   wantType == 0 && wantFlag != 0 branch:
//     accept iff marker != -1 && (flags & 0x2000) == 0 && busyRank <= 1
//       && ownerClassByte == wantFlag && lock == 0
//       && (g == 2 || groupClass == g)   where g = ClassifyTypeFlag(wantFlag)
//       && (rel == 0xFFFF || RelationMatrix(rel, i) > -30)
//   On accept: slot.flags |= 0x2000 (byte1 |= 0x20) and return &slot.
// ---------------------------------------------------------------------------
SlotRecord* FindAvailableSquadSlot(SlotRecord* table, u16 searcherRelationId,
                                   u8 wantType, u8 wantFlag, ISlotPolicy& policy) {
    if (wantType != 0) {
        for (int i = 0; i < kSlotCapacity; ++i) {
            SlotRecord& s = table[i];
            if (s.marker != -1
                && (s.flags & kSlotClaimed) == 0
                && s.busyRank <= 1
                && (s.reservedBy == wantType || s.reservedBy == 0)
                && s.lock == 0
                && static_cast<double>(s.fillCount) >= static_cast<double>(s.requiredCap)
                && s.ownerClassByte == wantFlag) {
                int g = policy.IsTypeInGroup(wantType);
                if ((g == 2 || s.groupClass == g)
                    && (searcherRelationId == 0xFFFF
                        || policy.RelationMatrix(searcherRelationId, i) > -30)) {
                    s.flags |= kSlotClaimed;
                    return &s;
                }
            }
        }
        return nullptr;
    }

    if (wantFlag == 0)
        return nullptr;

    int g = policy.ClassifyTypeFlag(wantFlag);
    for (int i = 0; i < kSlotCapacity; ++i) {
        SlotRecord& s = table[i];
        bool reject =
            s.marker == -1
            || (s.flags & kSlotClaimed) != 0
            || s.busyRank > 1
            || s.ownerClassByte != wantFlag
            || s.lock != 0
            || (g != 2 && s.groupClass != g)
            || (searcherRelationId != 0xFFFF
                && policy.RelationMatrix(searcherRelationId, i) <= -30);
        if (!reject) {
            s.flags |= kSlotClaimed;
            return &s;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x485ffc — VIBE_Combat_FindUnitByEntity
//   for each squad block (count == *(u8*)(dword_631208+48)):
//     for slot 0..15: if block[11*slot + 37] == *(DWORD*)(a1+4) return &that.
//   a1 is the live unit record; the matched key is unit+4 (== entityId here).
// ---------------------------------------------------------------------------
const i32* FindUnitByEntity(const i32* squadTable, int activeSquads, i32 entityId) {
    const i32* block = squadTable;
    for (int squad = 0; squad < activeSquads; ++squad) {
        for (int slot = 0; slot < 16; ++slot) {
            const i32* entry = &block[kOrderSlotDwordStride * slot + kOrderSlotBase];
            if (*entry == entityId)
                return entry;
        }
        block += kSquadBlockDwords;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x487090 — VIBE_Combat_GetUnitTarget
//   return *(DWORD*)(a1 + 512);
// ---------------------------------------------------------------------------
i32 GetUnitTarget(const u8* unit) {
    i32 v;
    std::memcpy(&v, unit + 512, sizeof(v));
    return v;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x57e92c — VIBE_Combat_ResolveTargetObjekt
//   typeCode = *(BYTE*)(589 * (char)entry[0] + typeTable);
//   handle   = *(DWORD*)(entry + 93);
//   if (typeCode == 2) {
//       r = QueryFind(handle, 1, 0, 19); if (r) return r;
//       return QueryFind(handle, 1, 0, 288);
//   }
//   r = QueryFind(handle, 1, 0, 18); if (r) return r;
//   return QueryFind(handle, 1, 0, 288);
// (The Hex-Rays `v2` artefact in the fallback paths is the same register holding
//  `entry`; both branches query the entry's own handle.)
// ---------------------------------------------------------------------------
const void* ResolveTargetObjekt(const u8* entry, const u8* typeTable,
                                IObjectQuery& query) {
    i8 personIdx = static_cast<i8>(entry[kEntryPersonIdxOff]);
    u8 typeCode = typeTable[kTypeTableStride * static_cast<int>(personIdx)];

    i32 handle;
    std::memcpy(&handle, entry + kEntryHandleOff, sizeof(handle));

    if (typeCode == 2) {
        const void* r = query.QueryFind(handle, /*prev*/ 0, /*kind*/ 19);
        if (r)
            return r;
        return query.QueryFind(handle, 0, 288);
    }
    const void* r = query.QueryFind(handle, 0, 18);
    if (r)
        return r;
    return query.QueryFind(handle, 0, 288);
}

// ---------------------------------------------------------------------------
// gilde.exe 0x57e9a4 — VIBE_Combat_PickActiveTargetEntry  (ring path only)
//   v7 = cursor;
//   while (count-- != 0) {
//       entry = base + 169*v7;
//       if (entry[0]) {
//           t = typeTable[589 * (char)entry[0]];   // person-type type code
//           if (t == 3 || t == 22 || t == 15) break;
//       }
//       v7 = (v7 + 1) % 256;
//   }
//   cursor = v7 + 1;
//   *outEntityRef = -1;
//   *outTypeId = *(DWORD*)(entry + 1);
//   return entry;
// (The objective path — squad+368 != 0 -> ResolveTargetObjekt — is render/scene
//  coupled and lives in the deferred list; this models the round-robin ring leaf,
//  which is the determinism-critical part.)
// ---------------------------------------------------------------------------
ActiveTargetResult PickActiveTargetEntry(const ActiveRing& ring) {
    ActiveTargetResult out;

    int v7 = *ring.cursor;
    // 0x57e9c6: v6 = 255. Each miss advances v7 (mod 256) then `if (!--v6)
    // return 0` — so the ring is probed at most 255 positions (cursor,
    // cursor+1, ... cursor+254), NOT 256. (Verified disasm 0x57e9c6/0x57ea0f.)
    int remaining = 255;                // v6 = 255
    const u8* entry = nullptr;
    bool found = false;

    while (true) {
        entry = ring.base + kActiveRingStride * v7;
        if (entry[0]) {
            // (char)entry[0] — the person-type index (signed multiply in orig).
            i8 typeIdx = static_cast<i8>(entry[0]);
            u8 typeCode = ring.typeTable[kTypeTableStride * static_cast<int>(typeIdx)];
            if (typeCode == kTypeActiveA || typeCode == kTypeActiveB ||
                typeCode == kTypeActiveC) {
                found = true;
                break;
            }
        }
        v7 = (v7 + 1) % kActiveRingCount;   // (v7 + 1) % 256
        if (--remaining == 0)               // if ( !--v6 ) return 0
            break;
    }

    if (!found) {
        // 256 tries exhausted without a hit -> nullptr (cursor untouched).
        out.entry = nullptr;
        return out;
    }

    *ring.cursor = v7 + 1;
    out.entry = entry;
    out.entityRef = -1;
    i32 typeId;
    std::memcpy(&typeId, entry + 1, sizeof(typeId));
    out.typeId = typeId;
    return out;
}

} // namespace guild::sim

// ===========================================================================
// DEFERRED (slot/target leaves still coupled to render/scene/AI glue):
//   0x57ea8c Combat_ResolveTargetEntityRef — spawns persons / picks event
//     buildings (Person_CreateAndSpawn / Amt_PickRandomEventBuildings /
//     Character_SpawnAtBuildingEntrance); world-mutation + spawn coupled.
//   0x57e50c Combat_FindNearestEnemyTarget — bone-chain transform probe
//     (Transform_PointThroughBoneChain) + Person iterator; render-transform coupled.
//   0x57829c Combat_SelectBeatingTarget — Person_QueryBegin/IterNext street-brawl
//     roster gather (RNG + scene iterator); iterator/scene coupled.
//   0x57eb64 Combat_AccumulateThreatStats — economy aggregation over the slot
//     table (Person_GetCashAmount / Building_Compute*Output / Coord_ConvertX);
//     building-model + coord coupled, into static stat accumulators.
//   0x485a54 Combat_FindObjectDef / 0x485b1c FindActiveTarget /
//   0x485b94 ResetObjectHighlights — scene GameObject query iteration over the
//     dword_630E4A object-def table + command queue; scene/command coupled.
//   0x489578 Combat_GatherPlayerUnits / 0x4897bc RegisterFlagCallback /
//   0x4895cc CreateFlagObject — scene-graph walk + flag-object spawn; scene coupled.
//
// ALREADY TRANSLATED ELSEWHERE (reused, not redefined here):
//   0x57e4c8 IsTargetUnderfull, 0x4897e0 CountActiveSlots,
//   0x57e714 AssignGuardTarget, 0x486460 GetSelectionFlag,
//   0x57829c SelectBeatingTarget  — all in src/sim/combat_orders.cpp.
//   0x486430 FindUnitById — in src/sim/combat.cpp (CombatField).
// ===========================================================================
