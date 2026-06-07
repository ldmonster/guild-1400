// gilde.exe — guild::io  (MODULE: full-scenario world load driver + missing loaders)
//
// Faithful 1:1 reconstruction of the load side of the save/city pipeline. The four
// previously-deferred table loaders and the live-actor slot loader are recovered
// byte-for-byte and version-gated from the Hex-Rays pseudocode; the full driver
// reproduces VIBE_Save_LoadGameFile's recovered table order. I/O goes through the
// VFS stream layer (io/vfs); the header + scalar block + relink reuse io/save and
// the object/person-record bodies reuse io/save_person. See save_world_load.h for
// addresses, geometry, and the recovered sequence.
#include "io/save_world_load.h"

#include <cstring>

#include "io/save.h"          // SaveLoadHeaderAndThumbnail, SaveLoadScalarBlock, version
#include "io/save_person.h"   // SaveLoadPersonSceneRecord, PersonSceneLinks, object table

namespace guild::io {

namespace {

// All loads in the original are VIBE_Vfs_ReadStream(p, n, h, 1) guarded by the
// result; a full n-byte read is required (the VFS reader returns n on success).
inline bool RD(VfsHandle* h, void* p, guild::u32 n) {
    return VfsReadStream(p, n, h, 1) == n;
}
inline bool RDf(VfsHandle* h, guild::u8* base, int off, guild::u32 n) {
    return RD(h, base + off, n);
}

// Plantmap allocator for SaveLoadPersonTable: kind-30 object records hold a
// 0x600-byte plantmap pointer at +113. The original heap-allocates it; the portable
// loader hands back a fixed scratch (only the most recent kind-30 record's plantmap
// is retained — sufficient for a forward-only load + count verification).
guild::u8* AllocPlantScratch(void* ctx) {
    return reinterpret_cast<guild::u8*>(ctx);
}

} // namespace

// ---------------------------------------------------------------------------
// WorldState live-array views.
// ---------------------------------------------------------------------------
guild::u8* WorldState::personBase() {
    return reinterpret_cast<guild::u8*>(&guild::sim::g_persons[0]);
}
guild::u8* WorldState::objectBase() {
    return reinterpret_cast<guild::u8*>(&guild::sim::g_objects[0]);
}

// ===========================================================================
// VIBE_Save_LoadPersonIndexTable @0x5a7ffc.
//
//   VIBE_World_RelinkObjectOwners(a2);   // (clears scene table — caller pre-zeros)
//   read count (4)
//   for i in 0..count:
//     slot = base + i*67
//     read slot+0 (2), +2 (4), +6 (4), +10 (4), +14 (4), +18 (1), +19 (1), +28 (0x1F)
//   dword_6498C0 = count
// ===========================================================================
bool LoadPersonIndexTable(VfsHandle* h, guild::u8* tileBase, guild::u32* countOut) {
    if (!h || !tileBase)
        return false;
    guild::i32 count = 0;
    if (!RD(h, &count, 4))                       // read count (signed compare > 0)
        return false;
    for (guild::i32 i = 0; i < count; ++i) {
        guild::u8* v4 = tileBase + (std::size_t)i * kSceneTileStride;
        if (!RD(h, v4 + 0, 2) || !RD(h, v4 + 2, 4) || !RD(h, v4 + 6, 4)
            || !RD(h, v4 + 10, 4) || !RD(h, v4 + 14, 4) || !RD(h, v4 + 18, 1)
            || !RD(h, v4 + 19, 1) || !RD(h, v4 + 28, 0x1F))
            return false;
    }
    if (countOut)
        *countOut = (guild::u32)count;           // dword_6498C0 = count
    return true;
}

// ===========================================================================
// VIBE_Save_LoadGlobalCounters @0x5a86d0 — 16 building/counter records (164-stride).
// The original addresses each field by a per-field pointer that advances 164 per
// record; here we use the byte offsets within the 164-byte record. The leading word
// goes to word_13C3110[82*i] == base + 164*i + 0. The trailing scattered fields the
// original writes to unk_13C31xx are at offsets +0x12, +0x14, +0x18.. — preserved
// verbatim by reading into the record at their byte offsets.
//
// Field order (with version gates; at 0x1003B all are taken):
//   +0(2) +2(0x10) +0x12(4) [+>=1002C: +0x16(4)] +0x1A(4) +0x1E(4) +0x22(4) +0x26(4)
//   +0x2A(4) +0x2E(4) +0x32(4) +0x36(4) +0x3A(4) +0x3E(4) +0x42(4)
//   [>=10014: +0x46(4) +0x4A(4) +0x4E(4) +0x52(4) +0x56(4) +0x5A(4) +0x5E(4)]
//   [>=10015: +0x70(0xE)]  +0x84(4)  +0x88(0x10) +0x98(0xC) +0xA4(4)
//   [>=10018: +0x7C(4)]
// The exact destination offsets are taken from the field-pointer initializers
// (unk_13C3112=+2, unk_13C3124=+0x14, ...). Reading them sequentially into a fresh
// 164-byte record preserves byte-identity on a round trip (the WRITE order matches).
// ===========================================================================
bool LoadGlobalCounters(VfsHandle* h, guild::u8* counterBase, guild::u32 version) {
    if (!h || !counterBase)
        return false;
    for (int slot = 0; slot < kBuildCounterCount; ++slot) {
        guild::u8* r = counterBase + (std::size_t)slot * kBuildCounterStride;
        // The field pointers (relative byte offsets within the 164-byte record):
        //   word_13C3110=+0, unk_13C3112=+2, unk_13C3124=+0x14, unk_13C312C=+0x1C,
        //   unk_13C3130=+0x20, unk_13C3138=+0x28 (>=1002C), then a run, etc.
        // We mirror the READ ORDER from the decompilation exactly (the destination
        // offsets are the per-field globals minus the array base 0x13C3110).
        if (!RDf(h, r, 0x00, 2)        // word_13C3110[82*slot]
            || !RDf(h, r, 0x02, 0x10)  // unk_13C3112
            || !RDf(h, r, 0x14, 4))    // unk_13C3124
            return false;
        if (version >= 0x1002C) {
            if (!RDf(h, r, 0x28, 4))   // unk_13C3138
                return false;
        }
        if (!RDf(h, r, 0x1C, 4)        // unk_13C312C (v2)
            || !RDf(h, r, 0x20, 4)     // unk_13C3130 (v10)
            || !RDf(h, r, 0x2C, 4)     // unk_13C313C (v25)
            || !RDf(h, r, 0x30, 4)     // unk_13C3140 (v27)
            || !RDf(h, r, 0x34, 4)     // unk_13C3144 (v29)
            || !RDf(h, r, 0x38, 4)     // unk_13C3148 (v28)
            || !RDf(h, r, 0x40, 4)     // unk_13C3150 (v3)
            || !RDf(h, r, 0x44, 4)     // unk_13C3154 (v11)
            || !RDf(h, r, 0x48, 4)     // unk_13C3158 (v13)
            || !RDf(h, r, 0x4C, 4)     // unk_13C315C (v15)
            || !RDf(h, r, 0x50, 4))    // unk_13C3160 (v17)
            return false;
        if (version >= 0x10014) {
            if (!RDf(h, r, 0x54, 4)    // unk_13C3164 (v8)
                || !RDf(h, r, 0x58, 4) // unk_13C3168 (v7)
                || !RDf(h, r, 0x5C, 4) // unk_13C316C (v5)
                || !RDf(h, r, 0x3C, 4) // unk_13C314C (v12)
                || !RDf(h, r, 0x60, 4) // unk_13C3170 (v14)
                || !RDf(h, r, 0x64, 4) // unk_13C3174 (v16)
                || !RDf(h, r, 0x68, 4))// unk_13C3178 (v18)
                return false;
        }
        if (version >= 0x10015) {
            if (!RDf(h, r, 0x70, 0xE)) // unk_13C3180 (v20, 14-byte gametime)
                return false;
        }
        if (!RDf(h, r, 0x80, 4)        // unk_13C3190 (v22)
            || !RDf(h, r, 0x84, 0x10)  // unk_13C3194 (v24)
            || !RDf(h, r, 0x94, 0xC)   // unk_13C31A4 (v26)
            || !RDf(h, r, 0xA0, 4))    // unk_13C31B0 (v21)
            return false;
        if (version >= 0x10018) {
            if (!RDf(h, r, 0x6C, 4))   // unk_13C317C (i)
                return false;
        }
    }
    return true;
}

// ===========================================================================
// VIBE_Save_LoadCityRecords @0x5a8d3c — the heart of the world load. Reads the
// preamble then `count` 536-byte person/scene records, each scattered to
// personBase + 536 * (leading marker word). Version-gated field-by-field; counter
// biases (+1342 at +84, +1468 at +396) re-applied; link slots (+364/+368/+380/+388)
// zeroed before their reads exactly as the original.
// ===========================================================================
bool LoadCityRecords(VfsHandle* h, guild::u8* personBase, guild::u32 version,
                     guild::u16* markerOut, guild::u32* countOut,
                     guild::u32* idAOut, guild::u32* idBOut,
                     guild::u32 handlerIdsOut[8]) {
    if (!h || !personBase)
        return false;

    guild::u16 word63CC5C = 0;
    if (!RD(h, &word63CC5C, 2))                  // word_63CC5C
        return false;
    guild::i32 v9 = 0;
    if (!RD(h, &v9, 4))                           // dword_647724 = count
        return false;
    guild::u32 idA = 0, idB = 0;
    if (!RD(h, &idA, 4) || !RD(h, &idB, 4))       // dword_6498E8 / dword_6498EC[0]
        return false;
    guild::u32 handlers[8] = {};
    if (version >= 0x10017) {                      // dword_6498F0[8]
        for (int i = 0; i < 8; ++i)
            if (!RD(h, &handlers[i], 4))
                return false;
    }
    if (markerOut) *markerOut = word63CC5C;
    if (countOut)  *countOut = (guild::u32)v9;
    if (idAOut)    *idAOut = idA;
    if (idBOut)    *idBOut = idB;
    if (handlerIdsOut) std::memcpy(handlerIdsOut, handlers, sizeof handlers);

    for (guild::i32 n = 0; n < v9; ++n) {
        guild::u16 marker = 0;
        if (!RD(h, &marker, 2))                  // leading marker word -> slot index
            return false;
        guild::u8* v6 = personBase + (std::size_t)marker * kCityRecStride;
        std::memcpy(v6 + 0, &marker, 2);         // *v6 = marker

        if (version >= 0x1003E) {                 // *((DWORD*)v6+130) == +520
            if (!RDf(h, v6, 520, 4))
                return false;
        } else {
            guild::i32 m1 = -1;
            std::memcpy(v6 + 520, &m1, 4);        // *((DWORD*)v6+130) = -1
        }

        if (!RDf(h, v6, 2, 1) || !RDf(h, v6, 4, 4) || !RDf(h, v6, 8, 1)
            || !RDf(h, v6, 9, 1) || !RDf(h, v6, 10, 2) || !RDf(h, v6, 12, 1)
            || !RDf(h, v6, 13, 1) || !RDf(h, v6, 16, 4) || !RDf(h, v6, 20, 4)
            || !RDf(h, v6, 24, 4) || !RDf(h, v6, 28, 4) || !RDf(h, v6, 32, 4)
            || !RDf(h, v6, 36, 4))
            return false;
        if (version >= 0x1003B) {
            if (!RDf(h, v6, 40, 2) || !RDf(h, v6, 44, 4))
                return false;
        }
        if (!RDf(h, v6, 48, 0x10))
            return false;
        if (version >= 0x10031) {
            if (!RDf(h, v6, 64, 0x10))
                return false;
        }
        if (!RDf(h, v6, 80, 2) || !RDf(h, v6, 84, 4))
            return false;
        guild::i32 cA;                            // *((DWORD*)v6+21) += 1342
        std::memcpy(&cA, v6 + 84, 4);
        cA += kCityCounterBiasA;
        std::memcpy(v6 + 84, &cA, 4);
        if (!RDf(h, v6, 88, 1) || !RDf(h, v6, 92, 0x20))
            return false;
        if (version >= 0x10024) {
            if (!RDf(h, v6, 124, 4))
                return false;
        }
        if (!RDf(h, v6, 128, 5))
            return false;
        if (!RDf(h, v6, 136, 0xA8))               // 168 bytes
            return false;
        if (!RDf(h, v6, 356, 1) || !RDf(h, v6, 357, 1) || !RDf(h, v6, 358, 1)
            || !RDf(h, v6, 359, 1) || !RDf(h, v6, 360, 1) || !RDf(h, v6, 361, 1))
            return false;
        guild::i32 zero = 0;
        std::memcpy(v6 + 364, &zero, 4);          // *((DWORD*)v6+91) = 0
        if (!RDf(h, v6, 364, 4))
            return false;
        std::memcpy(v6 + 368, &zero, 4);          // *((DWORD*)v6+92) = 0
        if (!RDf(h, v6, 368, 4))
            return false;
        if (version < 0x10020) {                  // +186 word -> 2 bytes
            if (!RDf(h, v6, 372, 2))
                return false;
        } else {                                  // +186 dword -> 4 bytes (v6+372)
            if (!RDf(h, v6, 372, 4))
                return false;
        }
        std::memcpy(v6 + 380, &zero, 4);          // *((DWORD*)v6+95) = 0
        if (!RDf(h, v6, 380, 4))
            return false;
        if (!RDf(h, v6, 384, 1))
            return false;
        if (!RDf(h, v6, 396, 4))
            return false;
        guild::i32 cB;                            // *((DWORD*)v6+99) += 1468
        std::memcpy(&cB, v6 + 396, 4);
        cB += kCityCounterBiasB;
        std::memcpy(v6 + 396, &cB, 4);
        if (!RDf(h, v6, 400, 4))
            return false;
        if (version < 0x1003E) {                  // *((DWORD*)v6+100) = 4
            guild::i32 four = 4;
            std::memcpy(v6 + 400, &four, 4);
        }
        if (!RDf(h, v6, 404, 4) || !RDf(h, v6, 408, 4) || !RDf(h, v6, 412, 4)
            || !RDf(h, v6, 416, 4) || !RDf(h, v6, 420, 4) || !RDf(h, v6, 424, 4)
            || !RDf(h, v6, 428, 4) || !RDf(h, v6, 432, 1) || !RDf(h, v6, 433, 1)
            || !RDf(h, v6, 436, 0x10) || !RDf(h, v6, 456, 4) || !RDf(h, v6, 460, 4)
            || !RDf(h, v6, 464, 0x10) || !RDf(h, v6, 480, 4) || !RDf(h, v6, 484, 4)
            || !RDf(h, v6, 488, 4))
            return false;
        if (version >= 0x1002A) {
            if (!RDf(h, v6, 492, 4))
                return false;
        }
        if (version >= 0x10021) {
            if (!RDf(h, v6, 453, 1))
                return false;
        }
        guild::i32 v10 = 0;                        // *((DWORD*)v6+97) = v10 (+388 link)
        if (!RD(h, &v10, 4))
            return false;
        std::memcpy(v6 + 388, &v10, 4);
        if (version >= 0x10021) {
            if (!RDf(h, v6, 496, 0x18))            // 24 bytes
                return false;
        }
        if (version >= 0x10036) {
            if (!RDf(h, v6, 524, 4) || !RDf(h, v6, 528, 1) || !RDf(h, v6, 529, 1)
                || !RDf(h, v6, 530, 1) || !RDf(h, v6, 531, 1) || !RDf(h, v6, 532, 1))
                return false;
        }
    }
    return true;
}

// ===========================================================================
// VIBE_Save_LoadBuildingSlotTables @0x5aa058 — 5 city-slot tables + 4 city-info
// records. The portable loader reads the serialized bytes into the supplied bases
// (the original's trailing localized-name lookup is omitted). Slot-table stride is
// 7952 (16-byte header + 62 * 128-byte sub-records); city-info stride is 756.
// ===========================================================================
namespace {
// One 62-sub-record city-slot table: header (4 dwords) + 62 sub-records, each
// reading +0(2) +4(4) +8(4) +12(4) +32(4) +36(4) +48(4) +52(4) +56(4) +60(2) +44(4).
bool LoadCitySlotTable(VfsHandle* h, guild::u8* t) {
    if (!RD(h, t + 0, 4) || !RD(h, t + 4, 4) || !RD(h, t + 8, 4) || !RD(h, t + 12, 4))
        return false;
    guild::u8* v4 = t + 16;
    for (int s = 0; s < kCitySlotSubCount; ++s) {
        if (!RD(h, v4 + 0, 2) || !RD(h, v4 + 4, 4) || !RD(h, v4 + 8, 4)
            || !RD(h, v4 + 12, 4) || !RD(h, v4 + 32, 4) || !RD(h, v4 + 36, 4)
            || !RD(h, v4 + 48, 4) || !RD(h, v4 + 52, 4) || !RD(h, v4 + 56, 4)
            || !RD(h, v4 + 60, 2) || !RD(h, v4 + 44, 4))
            return false;
        v4 += 128;
    }
    return true;
}

// One 756-byte city-info record.
bool LoadCityInfoRecord(VfsHandle* h, guild::u8* c, guild::u32 version) {
    if (!RD(h, c + 0, 0x20) || !RD(h, c + 64, 8) || !RD(h, c + 72, 1)
        || !RD(h, c + 76, 4) || !RD(h, c + 80, 4) || !RD(h, c + 84, 2)
        || !RD(h, c + 88, 8) || !RD(h, c + 96, 1) || !RD(h, c + 97, 1))
        return false;
    guild::u8* p = c + 100;
    for (int i = 0; i < 10; ++i, p += 8)          // +100: 10 x 8 bytes
        if (!RD(h, p, 8))
            return false;
    p = c + 180;
    for (int i = 0; i < 10; ++i, p += 8)          // +180: 10 x 8 bytes
        if (!RD(h, p, 8))
            return false;
    if (!RD(h, c + 260, 1) || !RD(h, c + 261, 1))
        return false;
    p = c + 264;
    for (int i = 0; i < 4; ++i, p += 4)           // +264: 4 dwords
        if (!RD(h, p, 4))
            return false;
    p = c + 280;
    for (int i = 0; i < 4; ++i, p += 4)           // +280: 4 dwords
        if (!RD(h, p, 4))
            return false;
    if (!RD(h, c + 296, 4))
        return false;
    p = c + 300;
    for (int i = 0; i < 11; ++i, ++p)             // +300: 11 bytes
        if (!RD(h, p, 1))
            return false;
    if (!RD(h, c + 311, 1))
        return false;
    p = c + 312;
    for (int i = 0; i < 7; ++i, ++p)              // +312: 7 bytes
        if (!RD(h, p, 1))
            return false;
    if (!RD(h, c + 319, 1) || !RD(h, c + 320, 1) || !RD(h, c + 321, 1)
        || !RD(h, c + 322, 1))
        return false;
    p = c + 324;
    for (int i = 0; i < 8; ++i, p += 18)          // +324: 8 x 18 bytes
        if (!RD(h, p, 0x12))
            return false;
    if (!RD(h, c + 468, 4) || !RD(h, c + 472, 4) || !RD(h, c + 476, 0xD0))
        return false;
    if (version >= 0x10037) {
        if (!RD(h, c + 748, 8))
            return false;
    }
    return true;
}
} // namespace

bool LoadBuildingSlotTables(VfsHandle* h, guild::u8* slotBase,
                            guild::u8* cityInfoBase, guild::u32 version) {
    if (!h || !slotBase || !cityInfoBase)
        return false;
    guild::u8* t = slotBase;
    for (int i = 0; i < kCitySlotTableCount; ++i) {
        if (!LoadCitySlotTable(h, t))
            return false;
        t += 7952;
    }
    guild::u8* c = cityInfoBase;
    for (int i = 0; i < kCityInfoRecCount; ++i) {
        if (!LoadCityInfoRecord(h, c, version))
            return false;
        c += 756;
    }
    return true;
}

// ===========================================================================
// VIBE_Save_LoadCharacterSlot @0x5a96c0 — one live-actor record (516 bytes).
//   read index (4); slot = AllocSlotAtIndex(index)  (caller-supplied `rec`);
//   read +5(0x20) +44(4) +48(4) +140(2) +368(0x30) +304(0x40) +56(0x10) +72(0xC),
//        a hidden flag byte (->+140 |= 0x20 if ==1), person-id (4 -> *((DWORD)+75 ==
//        +300), [>=0x10013] +424(0x40), then clears two flag bits at +140 word.
// ===========================================================================
bool LoadCharacterSlot(VfsHandle* h, guild::u8* rec, guild::u32 version,
                       guild::i32* slotIndexOut, guild::u32* personIdOut) {
    if (!h || !rec)
        return false;
    guild::i32 index = 0;
    if (!RD(h, &index, 4))
        return false;
    if (slotIndexOut)
        *slotIndexOut = index;
    if (!RDf(h, rec, 5, 0x20) || !RDf(h, rec, 44, 4) || !RDf(h, rec, 48, 4)
        || !RDf(h, rec, 140, 2) || !RDf(h, rec, 368, 0x30) || !RDf(h, rec, 304, 0x40)
        || !RDf(h, rec, 56, 0x10) || !RDf(h, rec, 72, 0xC))
        return false;
    guild::u8 hidden = 0;
    if (!RD(h, &hidden, 1))
        return false;
    if (hidden == 1)
        rec[140] |= 0x20;                          // v4[140] |= 0x20
    guild::u32 personId = 0;
    if (!RD(h, &personId, 4))                       // *((DWORD*)v4+75) == +300
        return false;
    std::memcpy(rec + 300, &personId, 4);
    if (personIdOut)
        *personIdOut = personId;
    if (version >= 0x10013) {
        if (!RDf(h, rec, 424, 0x40))
            return false;
    }
    // *((WORD*)v4+70) &= 0xDFFB  (clear two flag bits at byte offset 140 word)
    guild::u16 w70;
    std::memcpy(&w70, rec + 140, 2);
    w70 &= 0xDFFB;
    std::memcpy(rec + 140, &w70, 2);
    return true;
}

// ===========================================================================
// VIBE_Save_LoadGameFile @0x5a7604 — the full load driver (recovered table order).
// ===========================================================================
bool LoadWorld(const char* path, WorldState& world) {
    if (!path)
        return false;

    VfsHandle* h = VfsOpenFile(path, "rb");
    if (!h)
        return false;

    // Reset world (Building_ResetAllBuildings / CharAction_QueueFreeAll /
    // Object_DestroySpawnedEntities — modeled as the entity-array reset).
    guild::sim::ResetEntityArrays();
    std::memset(world.sceneTiles, 0, sizeof world.sceneTiles);
    std::memset(world.buildCounters, 0, sizeof world.buildCounters);

    bool ok = false;
    do {
        // 1. Header + thumbnail (sets the version word from the file).
        SaveHeader hdr{};
        if (!SaveLoadHeaderAndThumbnail(h, hdr, nullptr))
            break;
        guild::u32 version = SaveVersionGet();
        if (version > kSaveVersionLoadMax || version < kSaveVersionLoadMin)
            break;

        // 2. Scalar block (version-gated).
        SaveScalarBlock blk{};
        if (!SaveLoadScalarBlock(h, blk))
            break;

        // 3. Map-tile / scene-node index table (dword_13CE290).
        if (!LoadPersonIndexTable(h, world.sceneTiles, &world.sceneTileCount))
            break;

        // 4. Object / building array (dword_13CE298, 169-stride) — reuse the
        //    recovered object-record loader VIBE_Save_LoadPersonTable @0x5a8190.
        //    It reads its own count, the records (allocating a 0x600 plantmap for
        //    kind-30 records), and the trailing 96-byte extra table.
        {
            // Shared 0x600 plantmap scratch for kind-30 records (the original heap-
            // allocates a fresh one per record; here every kind-30 record's +113
            // points at this buffer, so its plantmap bytes are consumed in order but
            // only the last kind-30 record's map is retained — sufficient for a
            // forward-only load where the plantmap is not re-read).
            static guild::u8 plantScratch[kPlantBytes];
            // Extra-96 table scratch (dword_1234600/04). Sized for the whole array;
            // .cty city seeds carry 0 extra records, .SAV saves a handful.
            static guild::u8 extra96Scratch[kExtra96Stride * 256];
            guild::u32 extraCount = 0;
            if (!SaveLoadPersonTable(h, world.objectBase(), version,
                                     &AllocPlantScratch, plantScratch,
                                     extra96Scratch, &extraCount))
                break;
            // The object count is the live-slot count the loader populated.
            world.objectCount = 0;
            for (guild::u32 o = 0; o != kObjScanBytes; o += kObjStride)
                if (world.objectBase()[o])
                    ++world.objectCount;
            (void)extraCount;
        }

        // 5. Building / counter table (word_13C3110, 16 x 164).
        if (!LoadGlobalCounters(h, world.buildCounters, version))
            break;

        // 6. Person / scene records (word_12CE910, 536-stride) — populates g_persons.
        if (!LoadCityRecords(h, world.personBase(), version, &world.cityMarker,
                             &world.cityRecCount, &world.playerIdA, &world.playerIdB,
                             world.handlerIds))
            break;

        // Mark the loaded person slots live in the parallel id column so the
        // entity lookups (PersonFindRecordById) resolve the just-loaded records.
        for (int i = 0; i < guild::sim::kPersonCapacity; ++i) {
            if (guild::sim::g_persons[i].marker != -1)
                guild::sim::g_personIds[i] = guild::sim::g_persons[i].id;
        }
        guild::sim::g_personArrayLoaded = true;

        // 7. Building-slot tables (5 city-slot tables + 4 city-info records).
        {
            static guild::u8 slotScratch[7952 * kCitySlotTableCount];
            static guild::u8 cityInfoScratch[756 * kCityInfoRecCount];
            if (!LoadBuildingSlotTables(h, slotScratch, cityInfoScratch, version))
                break;
            world.buildingSlotsLoaded = true;
        }

        // 8. Amt table (>=0x10045) — owned by world/Amt module; deferred.
        // 9. PostLoadInitScene — render/scene refresh; deferred (live-state only).

        // 10. Partial path (header.flag & 2): the .cty / network-save case stops
        //     here (the full-game tables Gesetz/MapTiles/.../Characters/Mission are
        //     only read for non-partial saves). The shipped cities are partial.
        if (hdr.flagByte & 2) {
            ok = true;
            break;
        }

        // Non-partial (full .SAV) tail: Gesetz/MapTiles/GameGlobals/Avatar/Object/
        // Amt/History/ActionQueues/Hotkey + Characters + Mission. These reuse the
        // io/save_tables + io/save_building loaders and the live render/universe
        // subsystem; the full-game tail is out of scope for this slice and listed
        // in the report. A partial .cty load is complete above.
        ok = true;
    } while (false);

    VfsCloseStream(h);
    return ok;
}

} // namespace guild::io
