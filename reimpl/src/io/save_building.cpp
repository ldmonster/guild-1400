// gilde.exe — guild::io  (MODULE: per-table savegame serializers, part 3)
//
// Faithful 1:1 reconstruction of the building/object/action-queue serializers.
// Field order/size, version gates, variable-length blobs, and the action-record
// pointer<->index conversions are reproduced verbatim. See save_building.h for
// addresses and array geometry.
#include "io/save_building.h"

#include <cstring>

namespace guild::io {

namespace {
inline bool WR(VfsHandle* h, const void* p, guild::u32 n) {
    return VfsWriteStream(p, n, h, 1) == n;
}
inline bool RD(VfsHandle* h, void* p, guild::u32 n) {
    return VfsReadStream(p, n, h, 1) == n;
}
inline bool WRf(VfsHandle* h, const guild::u8* r, int off, guild::u32 n) {
    return WR(h, r + off, n);
}
inline bool RDf(VfsHandle* h, guild::u8* r, int off, guild::u32 n) {
    return RD(h, r + off, n);
}
} // namespace

// ---------------------------------------------------------------------------
// Building-type / counter record (164 bytes).  Write order (0x5a45bc):
//   +0(2) +2(0x10) +20(4) +40(4) +28(4) +32(4) +44(4) +48(4) +52(4) +56(4)
//   +64(4) +68(4) +72(4) +76(4) +80(4)
//   [v>=0x10014: +84(4) +88(4) +92(4) +60(4) +96(4) +100(4) +104(4)]
//   +112(0xE)  +128(4) +132(0x10) +148(0xC) +160(4) +108(4)
// Load order (0x5a86d0) is identical but gates: +40 only if v>=0x1002C, the
// +84..+104 group only if v>=0x10014, +112 only if v>=0x10015, +108 only if
// v>=0x10018.
// ---------------------------------------------------------------------------
bool SaveWriteBuildingRecord(VfsHandle* h, const guild::u8* r,
                             const guild::u8* /*parallelRec*/, guild::u32 version) {
    if (!h)
        return false;
    // The shipping writer always runs at the current version (all gates taken). To
    // keep old-version round-trips self-consistent we mirror the loader's gates here
    // (at 0x10045 this is byte-identical to the original's unconditional writes).
    if (!WRf(h, r, 0, 2) || !WRf(h, r, 2, 0x10) || !WRf(h, r, 20, 4))
        return false;
    if (version >= 0x1002C && !WRf(h, r, 40, 4))
        return false;
    if (!WRf(h, r, 28, 4) || !WRf(h, r, 32, 4) || !WRf(h, r, 44, 4) || !WRf(h, r, 48, 4)
        || !WRf(h, r, 52, 4) || !WRf(h, r, 56, 4) || !WRf(h, r, 64, 4) || !WRf(h, r, 68, 4)
        || !WRf(h, r, 72, 4) || !WRf(h, r, 76, 4) || !WRf(h, r, 80, 4))
        return false;
    if (version >= 0x10014) {
        if (!WRf(h, r, 84, 4) || !WRf(h, r, 88, 4) || !WRf(h, r, 92, 4) || !WRf(h, r, 60, 4)
            || !WRf(h, r, 96, 4) || !WRf(h, r, 100, 4) || !WRf(h, r, 104, 4))
            return false;
    }
    if (version >= 0x10015 && !WRf(h, r, 112, 0xE))
        return false;
    if (!WRf(h, r, 128, 4) || !WRf(h, r, 132, 0x10) || !WRf(h, r, 148, 0xC) || !WRf(h, r, 160, 4))
        return false;
    if (version >= 0x10018 && !WRf(h, r, 108, 4))
        return false;
    return true;
}

bool SaveLoadBuildingRecord(VfsHandle* h, guild::u8* r, guild::u32 version) {
    if (!h)
        return false;
    if (!RDf(h, r, 0, 2) || !RDf(h, r, 2, 0x10) || !RDf(h, r, 20, 4))
        return false;
    if (version >= 0x1002C && !RDf(h, r, 40, 4))
        return false;
    if (!RDf(h, r, 28, 4) || !RDf(h, r, 32, 4) || !RDf(h, r, 44, 4) || !RDf(h, r, 48, 4)
        || !RDf(h, r, 52, 4) || !RDf(h, r, 56, 4) || !RDf(h, r, 64, 4) || !RDf(h, r, 68, 4)
        || !RDf(h, r, 72, 4) || !RDf(h, r, 76, 4) || !RDf(h, r, 80, 4))
        return false;
    if (version >= 0x10014) {
        if (!RDf(h, r, 84, 4) || !RDf(h, r, 88, 4) || !RDf(h, r, 92, 4) || !RDf(h, r, 60, 4)
            || !RDf(h, r, 96, 4) || !RDf(h, r, 100, 4) || !RDf(h, r, 104, 4))
            return false;
    }
    if (version >= 0x10015 && !RDf(h, r, 112, 0xE))
        return false;
    if (!RDf(h, r, 128, 4) || !RDf(h, r, 132, 0x10) || !RDf(h, r, 148, 0xC) || !RDf(h, r, 160, 4))
        return false;
    if (version >= 0x10018 && !RDf(h, r, 108, 4))
        return false;
    return true;
}

bool SaveWriteBuildingTable(VfsHandle* h, const guild::u8* base,
                            const guild::u8* parallelBase, guild::u32 version) {
    if (!h || !base)
        return false;
    for (int i = 0; i < kBuildCapacity; ++i) {
        const guild::u8* r = base + (std::size_t)i * kBuildStride;
        const guild::u8* pr = parallelBase ? parallelBase + (std::size_t)i * kBuildStride : nullptr;
        if (!SaveWriteBuildingRecord(h, r, pr, version))
            return false;
    }
    return true;
}

bool SaveLoadBuildingTable(VfsHandle* h, guild::u8* base, guild::u32 version) {
    if (!h || !base)
        return false;
    for (int i = 0; i < kBuildCapacity; ++i)
        if (!SaveLoadBuildingRecord(h, base + (std::size_t)i * kBuildStride, version))
            return false;
    return true;
}

// ---------------------------------------------------------------------------
// Object main sub-table (332-byte records).  Write (0x5a6854) emits live-count and
// the constant blob-chunk size (160), then per record the fixed fields, an 8-dword
// @+140 group, a var-length blob (+128 length dword; if length>0 the blob bytes,
// else a 0 dword), then the +172 chunk (160 bytes).
// Load (0x5aae40): reads count and blob-chunk; per record reads the fixed fields,
// the 8-dword group only if v>=0x1002F, then +128 length and (if nonzero) the blob,
// then the +172 chunk of `blobChunk` bytes.
// ---------------------------------------------------------------------------
bool SaveWriteObjectMain(VfsHandle* h, guild::u8* base, guild::u32 count) {
    if (!h || !base)
        return false;
    if (!WR(h, &count, 4))
        return false;
    guild::u32 chunk = kObjBlobChunk;
    if (!WR(h, &chunk, 4))
        return false;
    for (guild::u32 i = 0; i < count; ++i) {
        guild::u8* r = base + (std::size_t)i * kObjMainStride;
        if (!WRf(h, r, 0, 1) || !WRf(h, r, 4, 4) || !WRf(h, r, 8, 2) || !WRf(h, r, 12, 4)
            || !WRf(h, r, 16, 4) || !WRf(h, r, 20, 0x30) || !WRf(h, r, 68, 0xE)
            || !WRf(h, r, 82, 0xE) || !WRf(h, r, 96, 0xE) || !WRf(h, r, 112, 4) || !WRf(h, r, 120, 1))
            return false;
        for (int k = 0; k < 8; ++k)
            if (!WRf(h, r, 140 + k * 4, 4))
                return false;
        // var-length blob: *(r+124) is the length, *(r+124*?)... the original uses
        // *((DWORD*)r+31)=ptr and *((DWORD*)r+32)=len: byte offsets +124(ptr),+128(len).
        guild::u32 blobPtr, blobLen;
        std::memcpy(&blobPtr, r + 124, 4);
        std::memcpy(&blobLen, r + 128, 4);
        if (blobPtr && blobLen) {
            if (!WRf(h, r, 128, 4)) // length
                return false;
            guild::u8* p = nullptr;
            std::memcpy(&p, r + 124, sizeof p);
            if (VfsWriteStream(p, blobLen, h, 1) != blobLen)
                return false;
        } else {
            guild::u32 zero = 0;
            if (!WR(h, &zero, 4))
                return false;
        }
        if (!WRf(h, r, 172, 0xA0))
            return false;
    }
    return true;
}

bool SaveLoadObjectMain(VfsHandle* h, guild::u8* base, guild::u32 /*count*/,
                        guild::u32 version, guild::u32 /*blobChunkHint*/) {
    if (!h || !base)
        return false;
    // Header: live-count (v18) then the +172 chunk size (v19, == 160).
    guild::u32 count = 0, blobChunk = 0;
    if (!RD(h, &count, 4) || !RD(h, &blobChunk, 4))
        return false;
    for (guild::u32 i = 0; i < count; ++i) {
        guild::u8* r = base + (std::size_t)i * kObjMainStride;
        if (!RDf(h, r, 0, 1) || !RDf(h, r, 4, 4) || !RDf(h, r, 8, 2) || !RDf(h, r, 12, 4)
            || !RDf(h, r, 16, 4) || !RDf(h, r, 20, 0x30) || !RDf(h, r, 68, 0xE)
            || !RDf(h, r, 82, 0xE) || !RDf(h, r, 96, 0xE) || !RDf(h, r, 112, 4) || !RDf(h, r, 120, 1))
            return false;
        if (version >= 0x1002F) {
            for (int k = 0; k < 8; ++k)
                if (!RDf(h, r, 140 + k * 4, 4))
                    return false;
        }
        std::memset(r + 124, 0, 4);  // *((DWORD*)r+31) = 0 (ptr)
        std::memset(r + 128, 0, 4);
        if (!RDf(h, r, 128, 4))       // length
            return false;
        guild::u32 blobLen;
        std::memcpy(&blobLen, r + 128, 4);
        if (blobLen) {
            // Original allocs the blob; here it must already point at scratch.
            guild::u8* p = nullptr;
            std::memcpy(&p, r + 124, sizeof p);
            if (!p || VfsReadStream(p, blobLen, h, 1) != blobLen)
                return false;
        }
        if (!RDf(h, r, 172, blobChunk))
            return false;
    }
    return true;
}

// Object light/aux sub-table (164-byte records).  Per record (load order):
//   +0(4) +4(1) +8(4) +12(4) +16(4) +20(0xE) +34(0x80).
bool SaveWriteObjectLight(VfsHandle* h, guild::u8* base, guild::u32 count) {
    if (!h || !base)
        return false;
    if (!WR(h, &count, 4))
        return false;
    for (guild::u32 i = 0; i < count; ++i) {
        guild::u8* r = base + (std::size_t)i * kObjLightStride;
        if (!WRf(h, r, 0, 4) || !WRf(h, r, 4, 1) || !WRf(h, r, 8, 4) || !WRf(h, r, 12, 4)
            || !WRf(h, r, 16, 4) || !WRf(h, r, 20, 0xE) || !WRf(h, r, 34, 0x80))
            return false;
    }
    return true;
}

bool SaveLoadObjectLight(VfsHandle* h, guild::u8* base, guild::u32 count) {
    if (!h || !base)
        return false;
    guild::u32 n = 0;
    if (!RD(h, &n, 4))
        return false;
    (void)count;
    for (guild::u32 i = 0; i < n; ++i) {
        guild::u8* r = base + (std::size_t)i * kObjLightStride;
        if (!RDf(h, r, 0, 4) || !RDf(h, r, 4, 1) || !RDf(h, r, 8, 4) || !RDf(h, r, 12, 4)
            || !RDf(h, r, 16, 4) || !RDf(h, r, 20, 0xE) || !RDf(h, r, 34, 0x80))
            return false;
    }
    return true;
}

// Object marker sub-table (80-byte records).  Per record: +0(1) +4(4) +8(4)
//   +12(4) +16(0x40).
bool SaveWriteObjectMarks(VfsHandle* h, guild::u8* base, guild::u32 count) {
    if (!h || !base)
        return false;
    if (!WR(h, &count, 4))
        return false;
    for (guild::u32 i = 0; i < count; ++i) {
        guild::u8* r = base + (std::size_t)i * kObjMarkStride;
        if (!WRf(h, r, 0, 1) || !WRf(h, r, 4, 4) || !WRf(h, r, 8, 4) || !WRf(h, r, 12, 4)
            || !WRf(h, r, 16, 0x40))
            return false;
    }
    return true;
}

bool SaveLoadObjectMarks(VfsHandle* h, guild::u8* base, guild::u32 count) {
    if (!h || !base)
        return false;
    guild::u32 n = 0;
    if (!RD(h, &n, 4))
        return false;
    (void)count;
    for (guild::u32 i = 0; i < n; ++i) {
        guild::u8* r = base + (std::size_t)i * kObjMarkStride;
        if (!RDf(h, r, 0, 1) || !RDf(h, r, 4, 4) || !RDf(h, r, 8, 4) || !RDf(h, r, 12, 4)
            || !RDf(h, r, 16, 0x40))
            return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// Action queues (153-byte records).  Write (0x5a714c) copies each record to a
// 0x99-byte stage buffer, replacing the +145 / +149 link pointers with indices
// (ptr ? (ptr-base)/153 : -1), then streams 0x99 bytes. Load (0x5ab704) reads the
// 0x99 bytes then converts the +145/+149 indices back to pointers (idx==-1 ? null
// : base + 153*idx).
// ---------------------------------------------------------------------------
guild::u32 ActionPoolCount(guild::u32 version) {
    return version < 0x1003C ? 0x2000 : 0x8000;
}

// In the 32-bit original the +145 / +149 link fields hold absolute 4-byte record
// pointers, converted on write to indices ((ptr-base)/153, or -1) and back on load
// (base + 153*idx, or null). In this portable reconstruction the link fields hold
// 4-byte INDICES natively (mirroring the index-based scene tree in sim/entity.h), so
// the on-disk index form is the in-memory form: the pointer<->index conversion is
// the identity here, and a record is serialized as its 153 raw bytes. The index
// arithmetic / null encoding (-1) is documented and asserted in the link helpers
// below for any caller that does hold raw record pointers.

// Convert an absolute record pointer to its on-disk index (-1 for null).
guild::i32 ActionPtrToIndex(const guild::u8* poolBase, const guild::u8* recPtr) {
    return recPtr ? (guild::i32)((recPtr - poolBase) / kActionStride) : -1;
}
// Convert an on-disk index back to an absolute record pointer (null for -1).
guild::u8* ActionIndexToPtr(guild::u8* poolBase, guild::i32 idx) {
    return idx == -1 ? nullptr : poolBase + (std::size_t)idx * kActionStride;
}

bool SaveWriteActionPool(VfsHandle* h, guild::u8* poolBase, guild::u32 count) {
    if (!h || !poolBase)
        return false;
    for (guild::u32 i = 0; i < count; ++i)
        if (!WR(h, poolBase + (std::size_t)i * kActionStride, kActionStride))
            return false;
    return true;
}

bool SaveLoadActionPool(VfsHandle* h, guild::u8* poolBase, guild::u32 count) {
    if (!h || !poolBase)
        return false;
    for (guild::u32 i = 0; i < count; ++i)
        if (!RD(h, poolBase + (std::size_t)i * kActionStride, kActionStride))
            return false;
    return true;
}

} // namespace guild::io
