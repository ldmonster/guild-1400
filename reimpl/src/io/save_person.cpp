// gilde.exe — guild::io  (MODULE: per-table savegame serializers, part 2)
//
// Faithful 1:1 reconstruction of the two large record serializers:
//   VIBE_Save_WritePersonTable / LoadPersonTable      (169-byte object array)
//   VIBE_Save_WriteGameStateHeader (person/scene record body)
// Field order, sizes, version gates, counter biases, and pointer->id conversions
// are reproduced verbatim. See save_person.h for addresses and array geometry.
#include "io/save_person.h"

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
// Person / object table (169-byte records).  The write field order (0x5a4134):
//   +0(1) +1(4) +5(0x20) +37(2) +39(2) +41(2) +43(4) +47(1) +52(1) +53(4)
//   +57(4) +61(4) +65(4) +69(4) +73(4) +90(2) +92(1)
//   if (*r == 30): 64 plantmap sub-records (stride 24) at *(r+113):
//      sub +0(4) +4(4) +8(1) +9(1) +10(2) +16(4) +12(1) +13(1)
//   else: +101(0x30)
//   then +153(0x10).
// The loader (0x5a8190) version gates: +73 read iff v>=0x10028; a discard dword
// read iff v<0x10032; +153 read 0x10 iff v>=0x10043 (else default).
// ---------------------------------------------------------------------------
static bool WriteOneObject(VfsHandle* h, guild::u8* r) {
    if (!WRf(h, r, 0, 1) || !WRf(h, r, 1, 4) || !WRf(h, r, 5, 0x20)
        || !WRf(h, r, 37, 2) || !WRf(h, r, 39, 2) || !WRf(h, r, 41, 2)
        || !WRf(h, r, 43, 4) || !WRf(h, r, 47, 1) || !WRf(h, r, 52, 1)
        || !WRf(h, r, 53, 4) || !WRf(h, r, 57, 4) || !WRf(h, r, 61, 4)
        || !WRf(h, r, 65, 4) || !WRf(h, r, 69, 4) || !WRf(h, r, 73, 4)
        || !WRf(h, r, 90, 2) || !WRf(h, r, 92, 1))
        return false;
    if (r[0] == kKindPlant) {
        // *(r+113) holds a heap pointer to the 0x600-byte plantmap.
        guild::u8* p = nullptr;
        std::memcpy(&p, r + 113, sizeof p);
        for (int s = 0; s < kPlantBytes; s += kPlantStride) {
            guild::u8* sub = p + s;
            if (!WR(h, sub + 0, 4) || !WR(h, sub + 4, 4) || !WR(h, sub + 8, 1)
                || !WR(h, sub + 9, 1) || !WR(h, sub + 10, 2) || !WR(h, sub + 16, 4)
                || !WR(h, sub + 12, 1) || !WR(h, sub + 13, 1))
                return false;
        }
    } else {
        if (!WRf(h, r, 101, 0x30))
            return false;
    }
    return WRf(h, r, 153, 0x10);
}

bool SaveWritePersonRecords(VfsHandle* h, guild::u8* objBase, guild::u32 count) {
    if (!h || !objBase)
        return false;
    if (!WR(h, &count, 4))
        return false;
    for (guild::u32 i = 0; i < count; ++i)
        if (!WriteOneObject(h, objBase + (std::size_t)i * kObjStride))
            return false;
    return true;
}

static bool LoadOneObject(VfsHandle* h, guild::u8* r, guild::u32 version) {
    if (!RDf(h, r, 0, 1) || !RDf(h, r, 1, 4) || !RDf(h, r, 5, 0x20)
        || !RDf(h, r, 37, 2) || !RDf(h, r, 39, 2) || !RDf(h, r, 41, 2)
        || !RDf(h, r, 43, 4) || !RDf(h, r, 47, 1) || !RDf(h, r, 52, 1)
        || !RDf(h, r, 53, 4) || !RDf(h, r, 57, 4) || !RDf(h, r, 61, 4)
        || !RDf(h, r, 65, 4))
        return false;
    guild::i32 v65;
    std::memcpy(&v65, r + 65, 4);
    if (v65 > 4) {                 // *(r+65) > 4 -> clamp to 2
        v65 = 2;
        std::memcpy(r + 65, &v65, 4);
    }
    if (!RDf(h, r, 69, 4))
        return false;
    if (version >= 0x10028) {
        if (!RDf(h, r, 73, 4))
            return false;
    }
    if (version < 0x10032) {       // discard dword (read into scratch)
        guild::u8 discard[4];
        if (!RD(h, discard, 4))
            return false;
    }
    if (!RDf(h, r, 90, 2) || !RDf(h, r, 92, 1))
        return false;
    if (r[0] == kKindPlant) {
        // The original allocs a 0x600 plantmap at *(r+113); here it must already
        // point at a 0x600 scratch (caller-provided). Each sub +0(4) +4(4) +8(1)
        // +9(1) +10(2) +16(4) +12(1) +13(1), then +20 of each sub is zeroed.
        guild::u8* p = nullptr;
        std::memcpy(&p, r + 113, sizeof p);
        for (int s = 0; s < kPlantBytes; s += kPlantStride) {
            guild::u8* sub = p + s;
            if (!RD(h, sub + 0, 4) || !RD(h, sub + 4, 4) || !RD(h, sub + 8, 1)
                || !RD(h, sub + 9, 1) || !RD(h, sub + 10, 2) || !RD(h, sub + 16, 4)
                || !RD(h, sub + 12, 1) || !RD(h, sub + 13, 1))
                return false;
            std::memset(sub + 20, 0, 4); // *(v5 + ... - 4) = 0
        }
    } else {
        if (!RDf(h, r, 101, 0x30))
            return false;
    }
    if (version >= 0x10043) {
        if (!RDf(h, r, 153, 0x10))
            return false;
    } else {
        // VIBE_Light_SetGrayColorThunk(0,16,r+153); then *(r+153)|=1; *(r+165)=-1.
        std::memset(r + 153, 0, 16);
        r[153] |= 1;
        guild::i32 m1 = -1;
        std::memcpy(r + 165, &m1, 4);
    }
    guild::i32 fiveK = 5000;
    std::memcpy(r + 48, &fiveK, 4);   // *(r+48) = 5000
    guild::i32 m1 = -1;
    std::memcpy(r + 149, &m1, 4);     // *(r+149) = -1
    return true;
}

bool SaveLoadPersonRecords(VfsHandle* h, guild::u8* objBase, guild::u32 /*count*/,
                           guild::u32 version) {
    if (!h || !objBase)
        return false;
    guild::u32 n = 0;
    if (!RD(h, &n, 4))
        return false;
    for (guild::u32 i = 0; i < n; ++i)
        if (!LoadOneObject(h, objBase + (std::size_t)i * kObjStride, version))
            return false;
    return true;
}

// Full table entry points (with the trailing 96-byte extra table). These delegate
// to the per-record helpers; the extra table is a flat array of 96-byte records:
//   count (4), then per record: +0(0x40) +64(0x10) +80(0x10).
bool SaveWritePersonTable(VfsHandle* h, guild::u8* objBase,
                          const guild::u8* extra96Base, guild::u32 extra96Count) {
    if (!h || !objBase)
        return false;
    // count live records (alive byte @+0 != 0).
    guild::u32 live = 0;
    for (guild::u32 o = 0; o != kObjScanBytes; o += kObjStride)
        if (objBase[o])
            ++live;
    if (!WR(h, &live, 4))
        return false;
    for (guild::u32 o = 0; o != kObjScanBytes; o += kObjStride) {
        guild::u8* r = objBase + o;
        if (r[0] && !WriteOneObject(h, r))
            return false;
    }
    if (!WR(h, &extra96Count, 4))
        return false;
    for (guild::u32 i = 0; i < extra96Count; ++i) {
        const guild::u8* e = extra96Base + (std::size_t)i * kExtra96Stride;
        if (!WR(h, e + 0, 0x40) || !WR(h, e + 64, 0x10) || !WR(h, e + 80, 0x10))
            return false;
    }
    return true;
}

bool SaveLoadPersonTable(VfsHandle* h, guild::u8* objBase, guild::u32 version,
                         guild::u8* (*allocPlant)(void* ctx), void* allocCtx,
                         guild::u8* extra96Base, guild::u32* extra96CountOut) {
    if (!h || !objBase)
        return false;
    guild::u32 n = 0;
    if (!RD(h, &n, 4))
        return false;
    for (guild::u32 i = 0; i < n; ++i) {
        guild::u8* r = objBase + (std::size_t)i * kObjStride;
        // Peek the first byte before LoadOneObject by reading it ourselves? The
        // original reads +0 first, then (if ==30) allocates the plantmap. We mirror
        // that by pre-binding the plant pointer when a slot turns out kind-30: the
        // allocator is invoked lazily inside the helper via the +113 slot, so seed
        // it here from `allocPlant` for every slot (cheap; only used if kind-30).
        if (allocPlant) {
            guild::u8* p = allocPlant(allocCtx);
            std::memcpy(r + 113, &p, sizeof p);
        }
        if (!LoadOneObject(h, r, version))
            return false;
    }
    guild::u32 extra = 0;
    if (!RD(h, &extra, 4))
        return false;
    if (extra96CountOut)
        *extra96CountOut = extra;
    for (guild::u32 i = 0; i < extra; ++i) {
        guild::u8* e = extra96Base + (std::size_t)i * kExtra96Stride;
        if (!RD(h, e + 0, 0x40) || !RD(h, e + 64, 0x10) || !RD(h, e + 80, 0x10))
            return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// GameState header preamble (0x5a4938 leading block).
//   word_63CC5C (2), live count (4), id6498E8-or-(-1) (4), id6498EC-or-(-1) (4),
//   then 8 handler ids (each -1 if the slot ptr is null).
// ---------------------------------------------------------------------------
bool SaveWriteGameStateHeaderPreamble(VfsHandle* h, const GameStateHeaderPreamble& p,
                                      guild::u32 liveCount) {
    if (!h)
        return false;
    if (!WR(h, &p.marker63CC5C, 2) || !WR(h, &liveCount, 4)
        || !WR(h, &p.id6498E8, 4) || !WR(h, &p.id6498EC, 4))
        return false;
    for (int i = 0; i < 8; ++i)
        if (!WR(h, &p.handlerIds[i], 4))
            return false;
    return true;
}

bool SaveLoadGameStateHeaderPreamble(VfsHandle* h, GameStateHeaderPreamble& p,
                                     guild::u32* liveCountOut) {
    if (!h)
        return false;
    guild::u32 live = 0;
    if (!RD(h, &p.marker63CC5C, 2) || !RD(h, &live, 4)
        || !RD(h, &p.id6498E8, 4) || !RD(h, &p.id6498EC, 4))
        return false;
    if (liveCountOut)
        *liveCountOut = live;
    for (int i = 0; i < 8; ++i)
        if (!RD(h, &p.handlerIds[i], 4))
            return false;
    return true;
}

// ---------------------------------------------------------------------------
// One 536-byte person/scene record (the per-slot body of WriteGameStateHeader).
// Field order with byte offsets (v7 is a __int16*, so (char*)v7+N == byte off N,
// *((_DWORD*)v7+K) == byte off 4*K):
//   +0(2) marker      +520(4)            +2(1)  +4(4)  +8(1)  +9(1)  +10(2)
//   +12(1) +13(1) +16(4) +20(4) +24(4) +28(4) +32(4) +36(4) +40(2) +44(4)
//   +48(16) +64(16) +80(2)
//   +84(4)  = *(+84 dword) - 1342        (counter bias A)
//   +88(1) +92(32) +124(4) +128(5) +136(168) +356(1) +357(1) +358(1) +359(1)
//   +360(1) +361(1)
//   link@+364 (idx91): id or -1          link@+368 (idx92): id or -1
//   +372(4)
//   link@+380 (idx95): id or -1
//   +384(1)
//   +396(4) = *(+396 dword) - 1468        (counter bias B)
//   +400 +404 +408 +412 +416 +420 +424 +428 (4 each)  +432(1) +433(1)
//   +436(16) +456(4) +460(4) +464(16) +480(4) +484(4) +488(4) +492(4) +453(1)
//   link@+388 (idx97): id or -1
//   +496(24) +524(4) +528(1) +529(1) +530(1) +531(1) +532(1)
// ---------------------------------------------------------------------------
bool SaveWritePersonSceneRecord(VfsHandle* h, const guild::u8* r,
                                const PersonSceneLinks& links) {
    if (!h)
        return false;
    if (!WRf(h, r, 0, 2) || !WRf(h, r, 520, 4) || !WRf(h, r, 2, 1) || !WRf(h, r, 4, 4)
        || !WRf(h, r, 8, 1) || !WRf(h, r, 9, 1) || !WRf(h, r, 10, 2) || !WRf(h, r, 12, 1)
        || !WRf(h, r, 13, 1) || !WRf(h, r, 16, 4) || !WRf(h, r, 20, 4) || !WRf(h, r, 24, 4)
        || !WRf(h, r, 28, 4) || !WRf(h, r, 32, 4) || !WRf(h, r, 36, 4) || !WRf(h, r, 40, 2)
        || !WRf(h, r, 44, 4) || !WRf(h, r, 48, 16) || !WRf(h, r, 64, 16) || !WRf(h, r, 80, 2))
        return false;
    guild::i32 cA;
    std::memcpy(&cA, r + 84, 4);
    cA -= kCounterBiasA;
    if (!WR(h, &cA, 4))
        return false;
    if (!WRf(h, r, 88, 1) || !WRf(h, r, 92, 32) || !WRf(h, r, 124, 4) || !WRf(h, r, 128, 5)
        || !WRf(h, r, 136, 168) || !WRf(h, r, 356, 1) || !WRf(h, r, 357, 1)
        || !WRf(h, r, 358, 1) || !WRf(h, r, 359, 1) || !WRf(h, r, 360, 1) || !WRf(h, r, 361, 1))
        return false;
    if (!WR(h, &links.idAt91, 4) || !WR(h, &links.idAt92, 4))
        return false;
    if (!WRf(h, r, 372, 4))
        return false;
    if (!WR(h, &links.idAt95, 4))
        return false;
    if (!WRf(h, r, 384, 1))
        return false;
    guild::i32 cB;
    std::memcpy(&cB, r + 396, 4);
    cB -= kCounterBiasB;
    if (!WR(h, &cB, 4))
        return false;
    if (!WRf(h, r, 400, 4) || !WRf(h, r, 404, 4) || !WRf(h, r, 408, 4) || !WRf(h, r, 412, 4)
        || !WRf(h, r, 416, 4) || !WRf(h, r, 420, 4) || !WRf(h, r, 424, 4) || !WRf(h, r, 428, 4)
        || !WRf(h, r, 432, 1) || !WRf(h, r, 433, 1) || !WRf(h, r, 436, 16) || !WRf(h, r, 456, 4)
        || !WRf(h, r, 460, 4) || !WRf(h, r, 464, 16) || !WRf(h, r, 480, 4) || !WRf(h, r, 484, 4)
        || !WRf(h, r, 488, 4) || !WRf(h, r, 492, 4) || !WRf(h, r, 453, 1))
        return false;
    if (!WR(h, &links.idAt97, 4))
        return false;
    return WRf(h, r, 496, 24) && WRf(h, r, 524, 4) && WRf(h, r, 528, 1)
        && WRf(h, r, 529, 1) && WRf(h, r, 530, 1) && WRf(h, r, 531, 1) && WRf(h, r, 532, 1);
}

bool SaveLoadPersonSceneRecord(VfsHandle* h, guild::u8* r, PersonSceneLinks* out) {
    if (!h)
        return false;
    if (!RDf(h, r, 0, 2) || !RDf(h, r, 520, 4) || !RDf(h, r, 2, 1) || !RDf(h, r, 4, 4)
        || !RDf(h, r, 8, 1) || !RDf(h, r, 9, 1) || !RDf(h, r, 10, 2) || !RDf(h, r, 12, 1)
        || !RDf(h, r, 13, 1) || !RDf(h, r, 16, 4) || !RDf(h, r, 20, 4) || !RDf(h, r, 24, 4)
        || !RDf(h, r, 28, 4) || !RDf(h, r, 32, 4) || !RDf(h, r, 36, 4) || !RDf(h, r, 40, 2)
        || !RDf(h, r, 44, 4) || !RDf(h, r, 48, 16) || !RDf(h, r, 64, 16) || !RDf(h, r, 80, 2))
        return false;
    guild::i32 cA = 0;
    if (!RD(h, &cA, 4))
        return false;
    cA += kCounterBiasA;
    std::memcpy(r + 84, &cA, 4);
    if (!RDf(h, r, 88, 1) || !RDf(h, r, 92, 32) || !RDf(h, r, 124, 4) || !RDf(h, r, 128, 5)
        || !RDf(h, r, 136, 168) || !RDf(h, r, 356, 1) || !RDf(h, r, 357, 1)
        || !RDf(h, r, 358, 1) || !RDf(h, r, 359, 1) || !RDf(h, r, 360, 1) || !RDf(h, r, 361, 1))
        return false;
    PersonSceneLinks links{};
    if (!RD(h, &links.idAt91, 4) || !RD(h, &links.idAt92, 4))
        return false;
    if (!RDf(h, r, 372, 4))
        return false;
    if (!RD(h, &links.idAt95, 4))
        return false;
    if (!RDf(h, r, 384, 1))
        return false;
    guild::i32 cB = 0;
    if (!RD(h, &cB, 4))
        return false;
    cB += kCounterBiasB;
    std::memcpy(r + 396, &cB, 4);
    if (!RDf(h, r, 400, 4) || !RDf(h, r, 404, 4) || !RDf(h, r, 408, 4) || !RDf(h, r, 412, 4)
        || !RDf(h, r, 416, 4) || !RDf(h, r, 420, 4) || !RDf(h, r, 424, 4) || !RDf(h, r, 428, 4)
        || !RDf(h, r, 432, 1) || !RDf(h, r, 433, 1) || !RDf(h, r, 436, 16) || !RDf(h, r, 456, 4)
        || !RDf(h, r, 460, 4) || !RDf(h, r, 464, 16) || !RDf(h, r, 480, 4) || !RDf(h, r, 484, 4)
        || !RDf(h, r, 488, 4) || !RDf(h, r, 492, 4) || !RDf(h, r, 453, 1))
        return false;
    if (!RD(h, &links.idAt97, 4))
        return false;
    if (!RDf(h, r, 496, 24) || !RDf(h, r, 524, 4) || !RDf(h, r, 528, 1)
        || !RDf(h, r, 529, 1) || !RDf(h, r, 530, 1) || !RDf(h, r, 531, 1) || !RDf(h, r, 532, 1))
        return false;
    if (out)
        *out = links;
    return true;
}

} // namespace guild::io
