// gilde.exe — guild::io  (MODULE: savegames + world serialization)
//
// Faithful reconstruction of the load-bearing core of the save subsystem: the
// header (magic/version + metadata + thumbnail), the fixed field-by-field scalar
// block order, the version-gated read branches, and the pointer<->id relink pass.
// I/O goes through the VFS stream layer (io/vfs); see save.h for the recovered
// layout, version table, and source addresses.
#include "io/save.h"

#include <cstring>

namespace guild::io {

namespace {

// dword_649D4C @0x649D4C — the live save version word.
guild::u32 g_saveVersion = kSaveVersionCurrent;

// All header/scalar reads in the original are `VIBE_Vfs_ReadStream(p, n, h, 1)`
// guarded by `!result`, i.e. a full n-byte read is required. The VFS reader
// returns the byte count (== n) on success, 0 on short/EOF. Wrap that contract.
inline bool ReadExact(VfsHandle* h, void* dst, guild::u32 n) {
    return VfsReadStream(dst, n, h, 1) == n;
}
inline bool WriteExact(VfsHandle* h, const void* src, guild::u32 n) {
    return VfsWriteStream(src, n, h, 1) == n;
}

// VIBE_Render_UnpackColor @0x434f7c — split a 16-bit RGB565-ish pixel into three
// bytes (the writer's per-pixel thumbnail expansion). Here the thumbnail is passed
// through verbatim, so the unpack is identity over already-byte data; we keep the
// helper for provenance but the slice copies the supplied RGB bytes directly.

} // namespace

// gilde.exe 0x649D4C — read the live version word.
guild::u32 SaveVersionGet() { return g_saveVersion; }

// gilde.exe 0x649D4C — set the live version word.
void SaveVersionSet(guild::u32 v) { g_saveVersion = v; }

// gilde.exe 0x5a372c — VIBE_Save_WriteScenarioBlock.
//
// Original write order (each call a separate VIBE_Vfs_WriteStream with an explicit
// size; failure on any aborts with return 0):
//   magic u32 (sets dword_649D4C = 65605 first)   -> +0x00
//   flag  u8                                       -> +0x04
//   name  32                                       -> +0x05
//   meta  8   (v22)                                -> +0x28
//   season u8 (v23)                                -> +0x30
//   extra  u8 (v24)                                -> +0x31
//   gametime 14 (qword_13CE852)                    -> +0x32
//   scenario tag 16 (v25 "SCENARIO"+pad)           -> +0x40
//   flagA u8, flagB u8, flagC u8                   -> +0x50,+0x51,+0x52
//   wealth u32 (v28)                               -> +0x54
//   flagD u8 (byte_649D50)                         -> +0x58
//   ... (live writer then streams two entity ids, an 8-dword block, the thumbnail,
//        dword_63C744, and byte_122F198[0x60]; this slice writes the fixed header
//        fields + thumbnail and the trailing id/extra fields from `hdr`.)
bool SaveWriteScenarioBlock(VfsHandle* h, const SaveHeader& hdr,
                            const guild::u8* thumbnail) {
    if (!h)
        return false;

    // dword_649D4C = 65605 (0x10045): the writer always emits the current version.
    guild::u32 magic = kSaveVersionWriter;
    g_saveVersion = magic;

    if (!WriteExact(h, &magic, 4))            // +0x00
        return false;
    if (!WriteExact(h, &hdr.flagByte, 1))     // +0x04
        return false;
    if (!WriteExact(h, hdr.name, 0x20))       // +0x05  (32)
        return false;
    if (!WriteExact(h, hdr.timestamp, 8))     // +0x28
        return false;
    if (!WriteExact(h, &hdr.season, 1))       // +0x30
        return false;
    if (!WriteExact(h, &hdr.extraByte, 1))    // +0x31  (>=0x10033; writer is current)
        return false;
    if (!WriteExact(h, hdr.gameTime, 0xE))    // +0x32  (14)
        return false;
    if (!WriteExact(h, hdr.scenarioTag, 0x10))// +0x40  (16)
        return false;
    if (!WriteExact(h, &hdr.flagA, 1))        // +0x50
        return false;
    if (!WriteExact(h, &hdr.flagB, 1))        // +0x51
        return false;
    if (!WriteExact(h, &hdr.flagC, 1))        // +0x52
        return false;
    if (!WriteExact(h, &hdr.wealth, 4))       // +0x54
        return false;
    if (!WriteExact(h, &hdr.flagD, 1))        // +0x58
        return false;
    // byte_649D50 — UNGATED, written right after flagD (mirrors the reader).
    if (!WriteExact(h, &hdr.byte649D50, 1))   // +0x59
        return false;

    // >=0x1002B id pair (v29 twice in the original).
    if (!WriteExact(h, &hdr.idA, 4))          // +0x5C
        return false;
    if (!WriteExact(h, &hdr.idB, 4))          // +0x60
        return false;

    // >=0x10034: 8-dword extra block (loop of 8 4-byte reads on load).
    if (!WriteExact(h, hdr.thumbExtra, 8 * 4))// +0x64
        return false;

    // 0xE100 thumbnail blob. The live writer rebuilds it from the framebuffer; this
    // slice streams the supplied bytes (or zeros) so the round-trip is byte-exact.
    if (thumbnail) {
        if (!WriteExact(h, thumbnail, kThumbnailBytes))
            return false;
    } else {
        static guild::u8 zero[256];
        std::memset(zero, 0, sizeof zero);
        guild::u32 left = kThumbnailBytes;
        while (left) {
            guild::u32 chunk = left < sizeof zero ? left : (guild::u32)sizeof zero;
            if (!WriteExact(h, zero, chunk))
                return false;
            left -= chunk;
        }
    }

    // >=0x10038 trailing dword (dword_63C744 in the original) and >=0x10039 name96.
    if (!WriteExact(h, &hdr.field132, 4))     // +0x84
        return false;
    if (!WriteExact(h, hdr.name96, 0x60))     // +0x88  (96)
        return false;
    return true;
}

// gilde.exe 0x5a7af0 — VIBE_Save_LoadHeaderAndThumbnail.
//
// Reads the header field-by-field with version gates. Sets dword_649D4C from the
// file's magic. The original allocates a 0xE100 scratch buffer for the thumbnail,
// reads it (when present), then decodes it into the live image; here we hand the
// raw bytes back through `thumbnail` (or skip-read them when it is null).
bool SaveLoadHeaderAndThumbnail(VfsHandle* h, SaveHeader& hdr, guild::u8* thumbnail) {
    if (!h)
        return false;

    if (!ReadExact(h, &hdr.magic, 4))         // +0x00
        return false;
    g_saveVersion = hdr.magic;                // dword_649D4C = *(_DWORD*)a2

    if (!ReadExact(h, &hdr.flagByte, 1)       // +0x04
        || !ReadExact(h, hdr.name, 0x20)      // +0x05
        || !ReadExact(h, hdr.timestamp, 8)    // +0x28
        || !ReadExact(h, &hdr.season, 1)) {   // +0x30
        return false;
    }

    if (g_saveVersion >= kVerExtraByte49) {   // >=0x10033
        if (!ReadExact(h, &hdr.extraByte, 1)) // +0x31
            return false;
    }

    if (g_saveVersion >= kVerGameTime50) {    // >=0x10028
        if (!ReadExact(h, hdr.gameTime, 0xE)      // +0x32
            || !ReadExact(h, hdr.scenarioTag, 0x10) // +0x40
            || !ReadExact(h, &hdr.flagA, 1)         // +0x50
            || !ReadExact(h, &hdr.flagB, 1)         // +0x51
            || !ReadExact(h, &hdr.flagC, 1)         // +0x52
            || !ReadExact(h, &hdr.wealth, 4)        // +0x54
            || !ReadExact(h, &hdr.flagD, 1)) {      // +0x58
            return false;
        }

        // byte_649D50 — written UNGATED right after flagD by the writer (a separate
        // VIBE_Vfs_WriteStream after the if/else block). Must be consumed here or the
        // whole stream after the header is shifted by 1 byte.
        if (!ReadExact(h, &hdr.byte649D50, 1))      // +0x59
            return false;

        if (g_saveVersion >= kVerWealthIds) {  // >=0x1002B
            if (!ReadExact(h, &hdr.idA, 4))    // +0x5C
                return false;
            if (!ReadExact(h, &hdr.idB, 4))    // +0x60
                return false;
        } else {
            hdr.idA = 0xFFFFFFFFu;             // *((_DWORD*)a2+23) = -1
            hdr.idB = 0xFFFFFFFFu;             // *((_DWORD*)a2+24) = -1
        }

        if (g_saveVersion >= kVerThumbExtra100) { // >=0x10034
            for (int i = 0; i < 8; ++i) {          // +0x64 .. +0x80
                if (!ReadExact(h, &hdr.thumbExtra[i], 4))
                    return false;
            }
        }

        // thumbnail (0xE100). On a current-version file it is always present here.
        if (thumbnail) {
            if (!ReadExact(h, thumbnail, kThumbnailBytes))
                return false;
        } else {
            guild::u8 skip[256];
            guild::u32 left = kThumbnailBytes;
            while (left) {
                guild::u32 chunk = left < sizeof skip ? left : (guild::u32)sizeof skip;
                if (!ReadExact(h, skip, chunk))
                    return false;
                left -= chunk;
            }
        }
    }

    // LABEL_9: floor check.
    if (g_saveVersion < kSaveVersionMin)      // <0x10025
        return false;

    if (g_saveVersion >= kVerField132) {      // >=0x10038
        if (!ReadExact(h, &hdr.field132, 4))  // +0x84
            return false;
    } else {
        hdr.field132 = 2;                     // *((_DWORD*)a2+33) = 2
    }

    if (g_saveVersion < kVerName136) {        // <0x10039: copy "Savegame"
        std::memset(hdr.name96, 0, sizeof hdr.name96);
        std::strncpy(hdr.name96, "Savegame", sizeof hdr.name96 - 1);
        return true;
    }
    return ReadExact(h, hdr.name96, 0x60);    // +0x88 (96)
}

// gilde.exe 0x5a3501.. — scalar block writer (the fixed field order in
// VIBE_Save_WriteGameFile right after the header). Every field is a single
// VIBE_Vfs_WriteStream; the live writer always emits unk6477A8/g649894/g632240
// because it runs at the current version (so all gates are taken).
bool SaveWriteScalarBlock(VfsHandle* h, const SaveScalarBlock& blk) {
    if (!h)
        return false;
    return WriteExact(h, &blk.g649890, 4)    // dword_649890
        && WriteExact(h, &blk.g632244, 4)    // dword_632244
        && WriteExact(h, blk.gameTime, 14)   // qword_13CE852
        && WriteExact(h, &blk.season, 1)     // byte_6477A1
        && WriteExact(h, &blk.g6498E4, 4)    // *(dword_6498E4+4) -> player id
        && WriteExact(h, &blk.g64771C, 4)    // dword_64771C
        && WriteExact(h, &blk.g647720, 4)    // dword_647720
        && WriteExact(h, &blk.g647724, 4)    // dword_647724
        && WriteExact(h, &blk.unk6477A8, 4)  // byte_6477A8
        && WriteExact(h, blk.gB56450, 24)    // dword_B56450
        && WriteExact(h, &blk.g649894, 4)    // dword_649894
        && WriteExact(h, &blk.g632240, 4);   // dword_632240
}

// gilde.exe 0x5a76d6.. — scalar block reader from VIBE_Save_LoadGameFile, honoring
// the version gates. Must run after SaveLoadHeaderAndThumbnail (which sets the
// version word from the file).
bool SaveLoadScalarBlock(VfsHandle* h, SaveScalarBlock& blk) {
    if (!h)
        return false;

    if (!ReadExact(h, &blk.g649890, 4)       // dword_649890
        || !ReadExact(h, &blk.g632244, 4)    // dword_632244
        || !ReadExact(h, blk.gameTime, 14)   // qword_13CE852
        || !ReadExact(h, &blk.season, 1)     // byte_6477A1
        || !ReadExact(h, &blk.g6498E4, 4)    // dword_6498E4
        || !ReadExact(h, &blk.g64771C, 4)    // dword_64771C
        || !ReadExact(h, &blk.g647720, 4)    // dword_647720
        || !ReadExact(h, &blk.g647724, 4)) { // dword_647724
        return false;
    }

    if (g_saveVersion >= kVerOptUnk6477A8) { // >=0x10022
        if (!ReadExact(h, &blk.unk6477A8, 4))
            return false;
    }

    if (!ReadExact(h, blk.gB56450, 24))      // dword_B56450 (always)
        return false;

    if (g_saveVersion >= kVerGlobal649894) { // >=0x10030
        if (!ReadExact(h, &blk.g649894, 4))
            return false;
    }

    if (g_saveVersion < kVerGlobal632240) {  // <0x1003D
        blk.g632240 = 1000000;               // dword_632240 = 1000000
    } else {
        if (!ReadExact(h, &blk.g632240, 4))
            return false;
    }
    return true;
}

// --- pointer<->id relink ---------------------------------------------------
// Helpers to access the 10-byte relink record's tag and 32-bit pointer/id slot.
namespace {
inline guild::u8&  RelinkTag_(guild::u8* table, std::size_t off) {
    return table[off + kRelinkTagOffset];
}
// The relink table has a 10-byte stride, so the 4-byte pointer/id slot at
// (+kRelinkPtrOffset) is naturally unaligned. The original reads/writes it via a
// raw *(DWORD*) (unaligned access is fine on x86); we keep byte-identical
// semantics but avoid a misaligned u32 reference (UB) by going through memcpy.
inline guild::u32 RelinkPtrGet_(const guild::u8* table, std::size_t off) {
    guild::u32 v;
    std::memcpy(&v, table + off + kRelinkPtrOffset, 4);
    return v;
}
inline void RelinkPtrSet_(guild::u8* table, std::size_t off, guild::u32 v) {
    std::memcpy(table + off + kRelinkPtrOffset, &v, 4);
}
} // namespace

// gilde.exe 0x5abd8d — VIBE_Save_RelinkLoadedPointers (tail loop). Walk the
// 32768x10 relink table; for each non-zero entry resolve its id to a pointer-token
// via the tag-dispatched resolver. The original iterates byte offset n by 10 up to
// 327680, reads the id at (table+n) [+6 here], dispatches on byte_B5FB61[n] [+1].
void RelinkTableIdsToPointers(guild::u8* table, const RelinkResolvers& r) {
    if (!table)
        return;
    for (guild::u32 n = 0; n != kRelinkBytes; n += kRelinkStride) {
        guild::u32 id = RelinkPtrGet_(table, n);
        if (id == 0)                          // if (*(table+n)) — skip empty
            continue;
        switch (RelinkTag_(table, n)) {
        case kRelinkPerson:
            if (r.person)   RelinkPtrSet_(table, n, r.person(id, r.ctx));
            break;
        case kRelinkBuilding:
            if (r.building) RelinkPtrSet_(table, n, r.building(id, r.ctx));
            break;
        case kRelinkObject:
            if (r.object)   RelinkPtrSet_(table, n, r.object(id, r.ctx));
            break;
        case kRelinkCutscene:
            if (r.cutscene) RelinkPtrSet_(table, n, r.cutscene(id, r.ctx));
            break;
        default:
            break;
        }
    }
}

// gilde.exe 0x5a3e4c — VIBE_Save_RelinkPersonRecords (tail loop). Byte-identical to
// the load-side loop above: same table, same tag dispatch, same resolvers. The
// engine uses it to restore pointers after a save write.
void RelinkTableRestore(guild::u8* table, const RelinkResolvers& r) {
    RelinkTableIdsToPointers(table, r);
}

} // namespace guild::io
