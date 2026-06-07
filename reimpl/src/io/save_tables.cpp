// gilde.exe — guild::io  (MODULE: per-table savegame serializers, part 1)
//
// Faithful 1:1 reconstruction of the simpler scalar / fixed-grid save serializers.
// Each function reproduces the exact field order, sizes, and version gates of its
// original (addresses in save_tables.h). All I/O is raw through the VFS stream
// layer; records are addressed by raw byte offset exactly as the binary does.
#include "io/save_tables.h"
#include "io/save.h"   // SaveVersionGet / version gate constants

#include <cstring>

namespace guild::io {

namespace {
inline bool WR(VfsHandle* h, const void* p, guild::u32 n) {
    return VfsWriteStream(p, n, h, 1) == n;
}
inline bool RD(VfsHandle* h, void* p, guild::u32 n) {
    return VfsReadStream(p, n, h, 1) == n;
}
// Read/write a field at a byte offset within a raw record.
inline bool WRf(VfsHandle* h, const guild::u8* rec, int off, guild::u32 n) {
    return WR(h, rec + off, n);
}
inline bool RDf(VfsHandle* h, guild::u8* rec, int off, guild::u32 n) {
    return RD(h, rec + off, n);
}
} // namespace

// gilde.exe 0x5a3fa4 — VIBE_Save_WriteMapTileTable.
bool SaveWriteMapTileTable(VfsHandle* h, guild::u8* tileBase) {
    if (!h || !tileBase)
        return false;
    // count live slots (word @+0 != 0).
    guild::u32 count = 0;
    for (guild::u32 o = 0; o != kMapTileScanBytes; o += kMapTileStride) {
        guild::u16 marker;
        std::memcpy(&marker, tileBase + o, 2);
        if (marker)
            ++count;
    }
    if (!WR(h, &count, 4))
        return false;
    for (guild::u32 o = 0; o != kMapTileScanBytes; o += kMapTileStride) {
        guild::u8* r = tileBase + o;
        guild::u16 marker;
        std::memcpy(&marker, r, 2);
        if (!marker)
            continue;
        if (!WRf(h, r, 0, 2) || !WRf(h, r, 2, 4) || !WRf(h, r, 6, 4)
            || !WRf(h, r, 10, 4) || !WRf(h, r, 14, 4) || !WRf(h, r, 18, 1)
            || !WRf(h, r, 19, 1) || !WRf(h, r, 28, 0x1F))
            return false;
    }
    return true;
}

// Loader mirror (the read order used inside VIBE_Save_LoadCityRecords @0x5a8d3c):
// read `count`, then for each of `count` records read the same fields into the
// next free array slot. The original re-uses the next array slot in scan order;
// here we fill slots [0..count) since the writer emitted only live slots.
bool SaveLoadMapTileTable(VfsHandle* h, guild::u8* tileBase) {
    if (!h || !tileBase)
        return false;
    guild::u32 count = 0;
    if (!RD(h, &count, 4))
        return false;
    for (guild::u32 i = 0; i < count; ++i) {
        guild::u8* r = tileBase + (guild::u32)i * kMapTileStride;
        if (!RDf(h, r, 0, 2) || !RDf(h, r, 2, 4) || !RDf(h, r, 6, 4)
            || !RDf(h, r, 10, 4) || !RDf(h, r, 14, 4) || !RDf(h, r, 18, 1)
            || !RDf(h, r, 19, 1) || !RDf(h, r, 28, 0x1F))
            return false;
    }
    return true;
}

// gilde.exe 0x5a7520 — VIBE_Save_WriteHotkeyTable.
bool SaveWriteHotkeyTable(VfsHandle* h, const HotkeyTable& t) {
    if (!h)
        return false;
    if (!WR(h, &t.header, 4))
        return false;
    for (int i = 0; i < kHotkeyCount; ++i) {
        if (!WR(h, &t.colA[i], 4) || !WR(h, &t.colB[i], 4)
            || !WR(h, &t.colC[i], 4) || !WR(h, &t.colD[i], 4))
            return false;
    }
    return true;
}

// gilde.exe 0x5aba98 — VIBE_Save_LoadHotkeyTable.
bool SaveLoadHotkeyTable(VfsHandle* h, HotkeyTable& t) {
    if (!h)
        return false;
    if (!RD(h, &t.header, 4))
        return false;
    for (int i = 0; i < kHotkeyCount; ++i) {
        if (!RD(h, &t.colA[i], 4) || !RD(h, &t.colB[i], 4)
            || !RD(h, &t.colC[i], 4) || !RD(h, &t.colD[i], 4))
            return false;
    }
    return true;
}

// gilde.exe — version-keyed square side (LoadMapTiles @0x5aa7a8).
guild::u32 MapTilesSide(guild::u32 version) {
    if (version < 0x1003A)
        return 256;
    if (version >= 0x10042)
        return 768;
    return 512;
}

// gilde.exe 0x5a6258 — VIBE_Save_WriteMapTiles (always 768x768 per grid).
bool SaveWriteMapTiles(VfsHandle* h, const guild::u8* heightBase,
                       const guild::u8* overlayBase) {
    if (!h || !heightBase || !overlayBase)
        return false;
    for (int row = 0; row < kMapRowStride; ++row) {
        const guild::u8* p = heightBase + (std::size_t)row * kMapRowStride;
        for (int col = 0; col < kMapRowStride; ++col)
            if (!WR(h, p + col, 1))
                return false;
    }
    for (int row = 0; row < kMapRowStride; ++row) {
        const guild::u8* p = overlayBase + (std::size_t)row * kMapRowStride;
        for (int col = 0; col < kMapRowStride; ++col)
            if (!WR(h, p + col, 1))
                return false;
    }
    return true;
}

// gilde.exe 0x5aa7a8 — VIBE_Save_LoadMapTiles. `side` rows of `side` bytes are read
// into each grid; the row stride in the destination is always 768. The overlay
// grid is only present (read) when version >= 0x10036.
bool SaveLoadMapTiles(VfsHandle* h, guild::u8* heightBase, guild::u8* overlayBase,
                      guild::u32 version) {
    if (!h || !heightBase)
        return false;
    guild::u32 side = MapTilesSide(version);
    for (guild::u32 row = 0; row < side; ++row) {
        guild::u8* p = heightBase + (std::size_t)row * kMapRowStride;
        for (guild::u32 col = 0; col < side; ++col)
            if (!RD(h, p + col, 1))
                return false;
    }
    if (version < 0x10036)
        return true;
    if (!overlayBase)
        return false;
    for (guild::u32 row = 0; row < side; ++row) {
        guild::u8* p = overlayBase + (std::size_t)row * kMapRowStride;
        for (guild::u32 col = 0; col < side; ++col)
            if (!RD(h, p + col, 1))
                return false;
    }
    return true;
}

// gilde.exe 0x5a6ff4 — VIBE_Save_WriteHistoryAndCarts.
bool SaveWriteHistoryAndCarts(VfsHandle* h, const HistoryHeader& hdr,
                              const guild::u8* cartBase) {
    if (!h || !cartBase)
        return false;
    if (!WR(h, &hdr.flag, 1))
        return false;
    guild::u32 d1 = hdr.cur1 - hdr.base1;   // dword_633928 - dword_63391C
    if (!WR(h, &d1, 4))
        return false;
    guild::u32 d2 = hdr.cur2 - hdr.base2;   // dword_633934 - dword_63392C
    if (!WR(h, &d2, 4))
        return false;
    for (int i = 0; i < kHistoryFlagsLen; ++i)
        if (!WR(h, &hdr.flags[i], 1))
            return false;
    for (int k = 0; k < kCartCount; ++k) {
        const guild::u8* cart = cartBase + (std::size_t)k * kCartStride;
        if (!WR(h, cart, 4))                 // +0 dword
            return false;
        for (int i = 0; i < kCartEntryCount * 8; i += 8) {
            const guild::u8* e = cart + 4 + i;
            if (!WR(h, e, 4) || !WR(h, e + 4, 1)) // entry dword + byte
                return false;
        }
    }
    return true;
}

// gilde.exe 0x5ab59c — VIBE_Save_LoadHistoryAndCarts. cur1/cur2 are reconstructed
// as base + delta (the original: dword_633928 = dword_63391C + v8).
bool SaveLoadHistoryAndCarts(VfsHandle* h, HistoryHeader& hdr, guild::u8* cartBase) {
    if (!h || !cartBase)
        return false;
    if (!RD(h, &hdr.flag, 1))
        return false;
    guild::u32 d1 = 0;
    if (!RD(h, &d1, 4))
        return false;
    hdr.cur1 = hdr.base1 + d1;
    guild::u32 d2 = 0;
    if (!RD(h, &d2, 4))
        return false;
    hdr.cur2 = hdr.base2 + d2;
    for (int i = 0; i < kHistoryFlagsLen; ++i)
        if (!RD(h, &hdr.flags[i], 1))
            return false;
    for (int k = 0; k < kCartCount; ++k) {
        guild::u8* cart = cartBase + (std::size_t)k * kCartStride;
        if (!RD(h, cart, 4))
            return false;
        for (int i = 0; i < kCartEntryCount; ++i) {
            guild::u8* e = cart + 4 + i * 8;
            if (!RD(h, e, 4) || !RD(h, e + 4, 1))
                return false;
        }
    }
    return true;
}

// gilde.exe 0x5a6e1c — VIBE_Save_WriteAmtTable.
bool SaveWriteAmtTable(VfsHandle* h, guild::u8* amtBase) {
    if (!h || !amtBase)
        return false;
    // The original scans `dword_11AE6B0[v2]` with `v2 += 69` (a DWORD array; 69
    // dwords == 276 bytes per record == kAmtStride), counting records whose +0
    // dword != -1.
    guild::u32 count = 0;
    for (guild::u32 o = 0; o != kAmtScanBytes; o += kAmtStride) {
        guild::i32 v;
        std::memcpy(&v, amtBase + o, 4);
        if (v != -1)
            ++count;
    }
    if (!WR(h, &count, 4))
        return false;
    for (guild::u32 o = 0; o != kAmtScanBytes; o += kAmtStride) {
        guild::u8* r = amtBase + o;
        guild::i32 v;
        std::memcpy(&v, r, 4);
        if (v == -1)
            continue;
        if (!WRf(h, r, 0, 4) || !WRf(h, r, 8, 1) || !WRf(h, r, 20, 4)
            || !WRf(h, r, 38, 1) || !WRf(h, r, 48, 1)
            || (VfsWriteStream(r + 52, 0x40, h, 16) != 0x40 * 16)
            || !WRf(h, r, 116, 1) || !WRf(h, r, 124, 0x98)
            || !WRf(h, r, 24, 0xE) || !WRf(h, r, 12, 4))
            return false;
    }
    return true;
}

// gilde.exe 0x5ab3ac — VIBE_Save_LoadAmtTable. The +24 14-byte gametime block is
// read only at version >= 0x1003F (older saves get `gametimeDefault` stamped),
// and the +12 id dword only at version >= 0x10040 (older saves default to -1).
bool SaveLoadAmtTable(VfsHandle* h, guild::u8* amtBase, guild::u32 version,
                      const guild::u8* gametimeDefault) {
    if (!h || !amtBase)
        return false;
    guild::u32 count = 0;
    if (!RD(h, &count, 4))
        return false;
    for (guild::u32 i = 0; i < count; ++i) {
        guild::u8* r = amtBase + (std::size_t)i * kAmtStride;
        if (!RDf(h, r, 0, 4) || !RDf(h, r, 8, 1) || !RDf(h, r, 20, 4)
            || !RDf(h, r, 38, 1) || !RDf(h, r, 48, 1)
            || (VfsReadStream(r + 52, 0x40, h, 16) != 0x40 * 16)
            || !RDf(h, r, 116, 1) || !RDf(h, r, 124, 0x98))
            return false;
        if (version >= 0x1003F) {
            if (!RDf(h, r, 24, 0xE))
                return false;
        } else if (gametimeDefault) {
            std::memcpy(r + 24, gametimeDefault, 14); // qword_13CE852 + tail
        }
        if (version >= 0x10040) {
            if (!RDf(h, r, 12, 4))
                return false;
        } else {
            guild::i32 minus1 = -1;
            std::memcpy(r + 12, &minus1, 4);
        }
    }
    return true;
}

// gilde.exe 0x5a6324 — VIBE_Save_WriteGameGlobals.
bool SaveWriteGameGlobals(VfsHandle* h, const GameGlobalsBlock& g) {
    if (!h)
        return false;
    if (!WR(h, &g.d910, 4) || !WR(h, &g.d914, 4) || !WR(h, &g.d918, 4)
        || !WR(h, &g.d91C, 4) || !WR(h, &g.d920, 4) || !WR(h, &g.d924, 4)
        || !WR(h, &g.f928, 4) || !WR(h, &g.d92C, 4) || !WR(h, &g.d930, 4)
        || !WR(h, &g.d934, 4) || !WR(h, &g.f5234, 4)
        || !WR(h, &g.q5262_a, 4) || !WR(h, &g.q5262_b, 2) || !WR(h, &g.q5262_c, 4)
        || !WR(h, &g.b526C, 4) || !WR(h, &g.f641DA8, 4) || !WR(h, &g.f641DAC, 4)
        || !WR(h, &g.d5238, 4) || !WR(h, &g.b523C, 4) || !WR(h, &g.b5240, 4)
        || !WR(h, &g.b5244, 4) || !WR(h, &g.b5248, 4) || !WR(h, &g.b524C, 4)
        || !WR(h, &g.b5250, 4) || !WR(h, &g.d5254, 4) || !WR(h, &g.b5258, 4))
        return false;
    // 16 x 5-dword float matrix (stride 20). Each row written +0,+4,+8,+12,+16.
    for (int i = 0; i < 16; ++i) {
        const guild::u32* row = &g.matrix[i * 5];
        for (int j = 0; j < 5; ++j)
            if (!WR(h, &row[j], 4))
                return false;
    }
    if (!WR(h, g.block0, 0x44) || !WR(h, g.block1, 0x44) || !WR(h, g.block2, 0x44)
        || !WR(h, g.block3, 0x44) || !WR(h, g.block4, 0x44))
        return false;
    // 8 rows x 8 records (record stride 24), per record +0(2) +2(2) +4(1) +8(4).
    for (int r = 0; r < 8; ++r) {
        const guild::u8* rowBase = g.grid + (std::size_t)r * 8 * 24;
        for (int c = 0; c < 8; ++c) {
            const guild::u8* rec = rowBase + c * 24;
            if (!WR(h, rec + 0, 2) || !WR(h, rec + 2, 2) || !WR(h, rec + 4, 1)
                || !WR(h, rec + 8, 4))
                return false;
        }
    }
    return true;
}

// gilde.exe 0x5aa8dc — VIBE_Save_LoadGameGlobals (byte-identical field order).
bool SaveLoadGameGlobals(VfsHandle* h, GameGlobalsBlock& g) {
    if (!h)
        return false;
    if (!RD(h, &g.d910, 4) || !RD(h, &g.d914, 4) || !RD(h, &g.d918, 4)
        || !RD(h, &g.d91C, 4) || !RD(h, &g.d920, 4) || !RD(h, &g.d924, 4)
        || !RD(h, &g.f928, 4) || !RD(h, &g.d92C, 4) || !RD(h, &g.d930, 4)
        || !RD(h, &g.d934, 4) || !RD(h, &g.f5234, 4)
        || !RD(h, &g.q5262_a, 4) || !RD(h, &g.q5262_b, 2) || !RD(h, &g.q5262_c, 4)
        || !RD(h, &g.b526C, 4) || !RD(h, &g.f641DA8, 4) || !RD(h, &g.f641DAC, 4)
        || !RD(h, &g.d5238, 4) || !RD(h, &g.b523C, 4) || !RD(h, &g.b5240, 4)
        || !RD(h, &g.b5244, 4) || !RD(h, &g.b5248, 4) || !RD(h, &g.b524C, 4)
        || !RD(h, &g.b5250, 4) || !RD(h, &g.d5254, 4) || !RD(h, &g.b5258, 4))
        return false;
    for (int i = 0; i < 16; ++i) {
        guild::u32* row = &g.matrix[i * 5];
        for (int j = 0; j < 5; ++j)
            if (!RD(h, &row[j], 4))
                return false;
    }
    if (!RD(h, g.block0, 0x44) || !RD(h, g.block1, 0x44) || !RD(h, g.block2, 0x44)
        || !RD(h, g.block3, 0x44) || !RD(h, g.block4, 0x44))
        return false;
    for (int r = 0; r < 8; ++r) {
        guild::u8* rowBase = g.grid + (std::size_t)r * 8 * 24;
        for (int c = 0; c < 8; ++c) {
            guild::u8* rec = rowBase + c * 24;
            if (!RD(h, rec + 0, 2) || !RD(h, rec + 2, 2) || !RD(h, rec + 4, 1)
                || !RD(h, rec + 8, 4))
                return false;
        }
    }
    return true;
}

} // namespace guild::io
