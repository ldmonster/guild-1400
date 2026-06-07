#include "sim/objectsearch.h"

#include <cstring>

// Faithful 1:1 port of the VIBE_ObjectSearch_* spatial queries + the object-ring
// iterator from gilde.exe.
//
// The originals scan the 256-slot object array (dword_13CE298, stride 169) in a
// pseudo-random order: a random start slot plus a random stride drawn from
// dword_478450 (16 odd values co-prime with 256), so every slot is visited once
// per pass. Each candidate record is tested by MatchEntityFilter, then (when a
// favourability range is active) by VIBE_Ai_ComputePersonFavorability. The
// favourability gate depends on AI/relation code outside this module, so the
// reimpl exposes the filter+probe core and DEFERS the favourability range (the
// callers pass it as an inactive [no-op] gate — see module report). The probe
// geometry, slot stride, alive test, id extraction, and filter logic are exact.

namespace guild::sim {

// gilde.exe dword_478450 (16 entries).
const int kObjectProbeStrides[kObjectProbeStrideCount] = {
    0x01, 0x03, 0x05, 0x07, 0x0B, 0x0D, 0x11, 0x13,
    0xED, 0xEF, 0xF3, 0xF5, 0xF9, 0xFB, 0xFD, 0xFF,
};

namespace {
inline u16 RdW(const u8* p, int off) {
    u16 v; std::memcpy(&v, p + off, 2); return v;
}
inline i32 RdD(const u8* p, int off) {
    i32 v; std::memcpy(&v, p + off, 4); return v;
}
} // namespace

// gilde.exe 0x559d98 — VIBE_ObjectSearch_MatchEntityFilter.
// a1 = &queryFaction (word), a2 = filter (16-byte), a3 = record (169-byte).
bool ObjectSearchMatchEntityFilter(const ObjectSearchContext& ctx,
                                   const EntityFilter* filter, const u8* a3) {
    if (!filter)
        return true;                       // a2 == 0 -> always match
    if (!a3)
        return false;

    // Reconstruct the packed 16-byte filter via a byte view for exact offsets.
    u8 fb[16];
    std::memset(fb, 0, sizeof(fb));
    std::memcpy(fb + 8, &filter->factionMask, 4);   // +8 dword
    fb[11] = static_cast<u8>(filter->requireStatus); // +11 sign byte
    fb[12] = filter->ownerMode;                      // +12
    fb[13] = filter->refineFlag;                     // +13
    // The original filter struct extends past +16 with the 4-word owner list;
    // we keep that list in `filter->ownerList` and index it directly below.

    // Empty filter: no faction mask AND no owner mode -> match.
    if (RdD(fb, 8) == 0 && fb[12] == 0)
        return true;

    bool v5 = false;
    int v17 = 1;
    int v16 = 0;

    // Status requirement: requireStatus < 0 rejects records that are either
    // flagged (record+90 bit0) or have no status dword (record+97 == 0).
    if (static_cast<i8>(fb[11]) < 0 &&
        ((a3[kObjFlags90] & 1) != 0 || RdD(a3, kObjStatus97) == 0))
        return false;

    // Faction membership: mask bit for the record's faction (via AiPlayer table).
    u32 mask = static_cast<u32>(RdD(fb, 8));
    if (mask == 0) {
        v16 = 1;
    } else {
        u8 factionBit = ctx.aiPlayerTable
                            ? ctx.aiPlayerTable[589 * a3[kObjAlive]]
                            : 0;
        if ((mask & (1u << factionBit)) != 0)
            v16 = 1;
    }

    // Owner-match mode.
    const u16 owner = RdW(a3, kObjOwner);
    const u16 q = ctx.queryFaction;
    u8 mode = fb[12];
    if (mode == 0) {
        v5 = true;
    } else {
        switch (mode) {
            case 1: v5 = (owner == 0xFFFF); break;
            case 2: v5 = (owner != 0xFFFF); break;
            case 3:
                if (owner == q || owner == 0xFFFF) v5 = true;
                else v5 = false;
                break;
            case 4: v5 = (owner == q); break;
            case 5: v5 = (owner != q); break;
            case 6:
                if (owner != q && owner != 0xFFFF) v5 = true;
                else v5 = false;
                break;
            case 7:
                if (owner != q && owner != ctx.extraFaction && owner != 0xFFFF)
                    v5 = true;
                else
                    v5 = false;
                break;
            default: v5 = true; break;
        }
    }

    // Refinement: when filter+13 bit0 set and the record has an owner, the
    // owner is matched against the 4-word owner list; on a hit v17 becomes
    // (listOwner ^ recordOwner) (the original's XOR-into-flag).
    if ((fb[13] & 1) != 0 && owner != 0xFFFF) {
        for (int j = 0; j < 4; ++j) {
            u16 listOwner = filter->ownerList[j];
            if (owner == listOwner && listOwner != 0xFFFF) {
                v17 = listOwner ^ owner;   // == 0 -> filtered out
                break;
            }
        }
    }

    return v16 && v5 && v17;
}

// gilde.exe 0x559ff8 — VIBE_ObjectSearch_MatchEntityFilterWithStatus.
// a1 = &queryFaction (word), a2 = filter (16-byte), a3 = record (169-byte).
//   if (!a2) return 1; if (!a3 || !a1) return 0;
//   if (!*(a2+8) && !*(a2+12)) return 1;
//   factionByte = *(aiPlayerTable + 589 * *a3);
//   if (((1<<factionByte) & 0xF82806F) && (*(a2+11) & 0x40)) return 0;   // (1)
//   if (*(char*)(a2+11) < 0 && (a3[90] & 1)) return 0;                   // (2)
//   v15 = (!mask || (mask & (1<<factionByte)));
//   ... owner-mode switch on *(a2+12) ...  ... owner-list XOR on *(a2+13) ...
//   return v15 && v6 && v16;
bool ObjectSearchMatchEntityFilterWithStatus(const ObjectSearchContext& ctx,
                                             const EntityFilter* filter,
                                             const u8* a3) {
    if (!filter)
        return true;
    if (!a3)
        return false;

    u8 fb[16];
    std::memset(fb, 0, sizeof(fb));
    std::memcpy(fb + 8, &filter->factionMask, 4);
    fb[11] = static_cast<u8>(filter->requireStatus);
    fb[12] = filter->ownerMode;
    fb[13] = filter->refineFlag;

    if (RdD(fb, 8) == 0 && fb[12] == 0)
        return true;

    u8 factionBit = ctx.aiPlayerTable
                        ? ctx.aiPlayerTable[589 * a3[kObjAlive]]
                        : 0;

    // (1) status-bitmask reject: faction in mask 0x0F82806F AND filter+11 bit0x40.
    if (((1u << factionBit) & 0x0F82806Fu) != 0 && (fb[11] & 0x40) != 0)
        return false;

    bool v6 = false;
    int  v16 = 1;
    int  v15 = 0;

    // (2) require-status sign: only the +90 flag bit0 is tested here.
    if (static_cast<i8>(fb[11]) < 0 && (a3[kObjFlags90] & 1) != 0)
        return false;

    u32 mask = static_cast<u32>(RdD(fb, 8));
    if (mask == 0 || (mask & (1u << factionBit)) != 0)
        v15 = 1;

    const u16 owner = RdW(a3, kObjOwner);
    const u16 q = ctx.queryFaction;
    u8 mode = fb[12];
    if (mode == 0) {
        v6 = true;
    } else {
        switch (mode) {
            case 1: v6 = (owner == 0xFFFF); break;
            case 2: v6 = (owner != 0xFFFF); break;
            case 3: v6 = (owner == q || owner == 0xFFFF); break;
            case 4: v6 = (owner == q); break;
            case 5: v6 = (owner != q); break;
            case 6: v6 = (owner != q && owner != 0xFFFF); break;
            case 7: v6 = (owner != q && owner != ctx.extraFaction && owner != 0xFFFF); break;
            default: v6 = true; break;
        }
    }

    if ((fb[13] & 1) != 0 && owner != 0xFFFF) {
        for (int j = 0; j < 4; ++j) {
            u16 listOwner = filter->ownerList[j];
            if (owner == listOwner && listOwner != 0xFFFF) {
                v16 = 0;
                break;
            }
        }
    }

    return v15 && v6 && v16;
}

// gilde.exe 0x47b1d0 — VIBE_ObjectSearch_FindNearestEntity (favourability gate
// deferred; see file header). Probes 256 slots from `probeStart` stepping by the
// chosen stride, returns the first match's id.
bool ObjectSearchFindNearestEntity(const ObjectSearchContext& ctx,
                                   const EntityFilter* filter, int strideIndex,
                                   int probeStart, i32* outId) {
    const int stride = kObjectProbeStrides[strideIndex & (kObjectProbeStrideCount - 1)];
    int v10 = probeStart % kSearchObjectCapacity;
    int budget = kSearchObjectCapacity;
    bool found = false;
    *outId = -1;

    do {
        const u8* rec = ctx.objectArray + kSearchObjectStride * v10;
        if (rec[kObjAlive]) {
            if (ObjectSearchMatchEntityFilter(ctx, filter, rec)) {
                found = true;
                *outId = RdD(rec, kObjId);
            }
        }
        --budget;
        v10 = (v10 + stride) % kSearchObjectCapacity;
    } while (!found && budget);

    return found;
}

// gilde.exe 0x47b308 — VIBE_ObjectSearch_FindEntitiesByCount.
int ObjectSearchFindEntitiesByCount(const ObjectSearchContext& ctx,
                                    const EntityFilter* filter, int strideIndex,
                                    int probeStart, int maxCount, i32* outIds) {
    const int stride = kObjectProbeStrides[strideIndex & (kObjectProbeStrideCount - 1)];
    int collected = 0;
    int budget = kSearchObjectCapacity;
    int v11 = probeStart % kSearchObjectCapacity;

    while (collected < maxCount && budget) {
        const u8* rec = ctx.objectArray + kSearchObjectStride * v11;
        if (rec[kObjAlive]) {
            if (ObjectSearchMatchEntityFilter(ctx, filter, rec)) {
                outIds[collected] = RdD(rec, kObjId);
                ++collected;
            }
        }
        --budget;
        v11 = (v11 + stride) % kSearchObjectCapacity;
    }
    return collected;
}

// gilde.exe 0x4784cc — VIBE_ObjectRing_AdvanceIterator.
//   v0 = *cursor (current record pointer);
//   if (count == 0) return base;
//   *cursor = &base[3 * ((bias + (v0 - base)/12) % count)];
//   return v0;
// We operate on byte offsets from base (record stride 12 == 3 dwords).
int ObjectRingAdvance(ObjectRing& ring) {
    int v0 = *ring.cursor;                  // current byte offset from base
    if (ring.count == 0) {
        *ring.cursor = 0;
        return 0;
    }
    int slot = (ring.bias + v0 / 12) % ring.count;
    *ring.cursor = 12 * slot;               // 3 dwords == 12 bytes
    return v0;
}

} // namespace guild::sim
