// gilde.exe — guild::io  (MODULE: per-table savegame serializers, part 3)
//
// 1:1 reconstruction of the WRITE-side per-table serializers. Field order, sizes
// and version gates are reproduced verbatim from the decompilation; addresses are
// in save_serial3.h. All I/O is raw through the VFS stream layer and records are
// addressed by raw byte offset exactly as the binary does.
#include "io/save_serial3.h"

#include <cstring>

namespace guild::io {

namespace {
inline bool WR(VfsHandle* h, const void* p, guild::u32 n) {
    return VfsWriteStream(p, n, h, 1) == n;
}
inline bool WRf(VfsHandle* h, const guild::u8* rec, int off, guild::u32 n) {
    return WR(h, rec + off, n);
}

// Read a little-endian dword out of a raw record (the original reads through a
// typed pointer; we keep it byte-exact and host-endian-agnostic via memcpy).
inline guild::u32 RdU32(const guild::u8* p) {
    guild::u32 v;
    std::memcpy(&v, p, 4);
    return v;
}

// ---- WriteBuildingSlotTables helpers (write mirror of LoadBuildingSlotTables) --

// One 62-sub-record city-slot table: header (4 dwords) then 62 sub-records, each
// emitting +0(2) +4(4) +8(4) +12(4) +32(4) +36(4) +48(4) +52(4) +56(4) +60(2) +44(4)
// — the exact field order of the loader at 0x5aa058.
bool WriteCitySlotTable(VfsHandle* h, const guild::u8* t) {
    if (!WR(h, t + 0, 4) || !WR(h, t + 4, 4) || !WR(h, t + 8, 4) || !WR(h, t + 12, 4))
        return false;
    const guild::u8* v4 = t + 16;
    for (int s = 0; s < kBst_CitySlotSubCount; ++s) {
        if (!WR(h, v4 + 0, 2))
            return false;
        if (!WR(h, v4 + 4, 4) || !WR(h, v4 + 8, 4) || !WR(h, v4 + 12, 4)
            || !WR(h, v4 + 32, 4) || !WR(h, v4 + 36, 4) || !WR(h, v4 + 48, 4)
            || !WR(h, v4 + 52, 4) || !WR(h, v4 + 56, 4) || !WR(h, v4 + 60, 2))
            return false;
        if (!WR(h, v4 + 44, 4))
            return false;
        v4 += 128;
    }
    return true;
}

// One 756-byte city-info record (write mirror of LoadCityInfoRecord).
bool WriteCityInfoRecord(VfsHandle* h, const guild::u8* c, guild::u32 version) {
    if (!WR(h, c + 0, 0x20) || !WR(h, c + 64, 8) || !WR(h, c + 72, 1)
        || !WR(h, c + 76, 4) || !WR(h, c + 80, 4) || !WR(h, c + 84, 2)
        || !WR(h, c + 88, 8) || !WR(h, c + 96, 1) || !WR(h, c + 97, 1))
        return false;
    const guild::u8* p = c + 100;
    for (int i = 0; i < 10; ++i, p += 8)          // +100: 10 x 8 bytes
        if (!WR(h, p, 8))
            return false;
    p = c + 180;
    for (int i = 0; i < 10; ++i, p += 8)          // +180: 10 x 8 bytes
        if (!WR(h, p, 8))
            return false;
    if (!WR(h, c + 260, 1) || !WR(h, c + 261, 1))
        return false;
    p = c + 264;
    for (int i = 0; i < 4; ++i, p += 4)           // +264: 4 dwords
        if (!WR(h, p, 4))
            return false;
    p = c + 280;
    for (int i = 0; i < 4; ++i, p += 4)           // +280: 4 dwords
        if (!WR(h, p, 4))
            return false;
    if (!WR(h, c + 296, 4))
        return false;
    p = c + 300;
    for (int i = 0; i < 11; ++i, ++p)             // +300: 11 bytes
        if (!WR(h, p, 1))
            return false;
    if (!WR(h, c + 311, 1))
        return false;
    p = c + 312;
    for (int i = 0; i < 7; ++i, ++p)              // +312: 7 bytes
        if (!WR(h, p, 1))
            return false;
    if (!WR(h, c + 319, 1) || !WR(h, c + 320, 1) || !WR(h, c + 321, 1)
        || !WR(h, c + 322, 1))
        return false;
    p = c + 324;
    for (int i = 0; i < 8; ++i, p += 18)          // +324: 8 x 18 bytes
        if (!WR(h, p, 0x12))
            return false;
    if (!WR(h, c + 468, 4) || !WR(h, c + 472, 4) || !WR(h, c + 476, 0xD0))
        return false;
    // gilde.exe 0x5a623c — the WRITE path ALWAYS emits the +748 8-byte tail. There
    // is NO version gate on the writer (verified against the binary: the gate
    // `>= 0x10037` exists ONLY on the loader at 0x5aa77f). The writer always runs
    // at the current version (0x10045 >= 0x10037), so a current-version file always
    // carries the tail and the loader reads it back; for faithful 1:1 behavior the
    // write is unconditional regardless of the `version` argument.
    (void)version;
    if (!WR(h, c + 748, 8))
        return false;
    return true;
}

// ---- WriteObjectRecord hook state ----------------------------------------
ObjectRecordHooks g_objHooks{};   // inert default: resolveStock == nullptr

// The all-zero template tables dword_5A3430 / dword_5A343C / dword_5A344C are
// statically zero in the binary; reproduce them as zero buffers.
const guild::u8 kZeroTemplate[64] = {0};
} // namespace

ObjectRecordHooks SaveSetObjectRecordHooks(const ObjectRecordHooks& hooks) {
    ObjectRecordHooks prev = g_objHooks;
    g_objHooks = hooks;
    return prev;
}

// gilde.exe 0x5a5c1c — VIBE_Save_WriteBuildingSlotTables.
bool SaveWriteBuildingSlotTables(VfsHandle* h, const guild::u8* slotBase,
                                 const guild::u8* cityInfoBase, guild::u32 version) {
    if (!h || !slotBase || !cityInfoBase)
        return false;
    const guild::u8* t = slotBase;
    for (int i = 0; i < kBst_CitySlotTableCount; ++i) {     // v19 < 5
        if (!WriteCitySlotTable(h, t))
            return false;
        t += kBst_CitySlotTableStride;                      // += 7952
    }
    const guild::u8* c = cityInfoBase;
    for (int i = 0; i < kBst_CityInfoRecCount; ++i) {       // v20 < 4
        if (!WriteCityInfoRecord(h, c, version))
            return false;
        c += kBst_CityInfoStride;                           // += 756
    }
    return true;
}

// gilde.exe 0x5a55b0 — VIBE_Save_WriteObjectRecord.
//   write rec+0(4) rec+5(32) rec+44(4) rec+48(4) rec+140(2) rec+368(48) rec+304(64)
//   stock sub-record present? -> write sub+76(16) sub+132(12)  else 16+12 zero bytes
//   write flagByte(1)  &linkId(4)
//   mesh present (*((DWORD*)rec+25))? -> write meshPtr(64)      else 64 zero bytes
bool SaveWriteObjectRecord(VfsHandle* h, const guild::u8* rec, guild::i32 linkId,
                           const guild::u8* meshPtr, guild::u8 flagByte) {
    if (!h || !rec)
        return false;

    if (!WRf(h, rec, 0, 4) || !WRf(h, rec, 5, 32) || !WRf(h, rec, 44, 4)
        || !WRf(h, rec, 48, 4) || !WRf(h, rec, 140, 2) || !WRf(h, rec, 368, 48)
        || !WRf(h, rec, 304, 64))
        return false;

    // Stock sub-record probe (original: v4 = *((DWORD*)rec+34); ... v4+981 flag).
    const guild::u8* blockA = nullptr;
    const guild::u8* blockB = nullptr;
    bool hasStock = false;
    if (g_objHooks.resolveStock)
        hasStock = g_objHooks.resolveStock(rec, &blockA, &blockB);

    if (hasStock && blockA && blockB) {
        if (!WR(h, blockA, kObjRec_StockBlockA) || !WR(h, blockB, kObjRec_StockBlockB))
            return false;
    } else {
        // dword_5A3430 (16) then dword_5A343C (12), both all-zero in the binary.
        if (!WR(h, kZeroTemplate, kObjRec_StockBlockA)
            || !WR(h, kZeroTemplate, kObjRec_StockBlockB))
            return false;
    }

    if (!WR(h, &flagByte, 1) || !WR(h, &linkId, 4))
        return false;

    // Inline mesh-name block (original: *((DWORD*)rec+25) -> rec+100).
    guild::u32 meshSlot = RdU32(rec + 100);
    if (meshSlot != 0) {
        if (!meshPtr)                       // caller asserted non-null slot but no data
            return false;
        return WR(h, meshPtr, kObjRec_MeshNameLen);
    }
    // dword_5A344C: 64-byte zero template.
    return WR(h, kZeroTemplate, kObjRec_MeshNameLen);
}

} // namespace guild::io
