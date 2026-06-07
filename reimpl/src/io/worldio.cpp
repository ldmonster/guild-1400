// gilde.exe — guild::io  (MODULE: world / scene serialization — WorldIo_*)
//
// The VIBE_Bio_* primitives and the WorldIo scene-chunk header gate. All I/O is raw
// little-endian over the VFS stream layer (io/vfs). See worldio.h for addresses and
// the recovered tag/version constants.
#include "io/worldio.h"

namespace guild::io {

namespace {
inline bool WriteExact(VfsHandle* h, const void* src, guild::u32 n) {
    return VfsWriteStream(src, n, h, 1) == n;
}
inline bool ReadExact(VfsHandle* h, void* dst, guild::u32 n) {
    return VfsReadStream(dst, n, h, 1) == n;
}
} // namespace

// gilde.exe 0x5dc918 — VIBE_Bio_WriteDword.
bool BioWriteDword(VfsHandle* h, guild::u32 v) {
    return WriteExact(h, &v, 4);
}

// gilde.exe 0x5dc8cc — VIBE_Bio_WriteByte.
bool BioWriteByte(VfsHandle* h, guild::u8 v) {
    return WriteExact(h, &v, 1);
}

// gilde.exe 0x5dcac0 — VIBE_Bio_WriteDwordPair. Original builds {first, second} on
// the stack but the VfsWriteStream call has size 4 / count 1, so only `first` hits
// the stream. Reproduced exactly: `ignored` is accepted, never written.
bool BioWriteDwordPair(VfsHandle* h, guild::u32 first, guild::u32 ignored) {
    (void)ignored;
    return WriteExact(h, &first, 4);
}

// gilde.exe 0x5dc894 — VIBE_Bio_ReadDword (and 0x5dc8b0 ReadDwordSwapArgs, which is
// byte-identical: both are plain 4-byte reads; the "swap" name is vestigial).
bool BioReadDword(VfsHandle* h, guild::u32* out) {
    return out && ReadExact(h, out, 4);
}

// gilde.exe 0x5dc850 — VIBE_Bio_ReadByte.
bool BioReadByte(VfsHandle* h, guild::u8* out) {
    return out && ReadExact(h, out, 1);
}

// gilde.exe 0x5e639c — the scene-chunk acceptance gate from VIBE_WorldIo_-
// LoadSceneState: (tag & 0xFFFF0000) == 0x3A6E0000 && tag >= 980156601.
bool WorldIoSceneTagValid(guild::u32 tag) {
    return (tag & 0xFFFF0000u) == kSceneTagBase && tag >= kSceneTagMinVersion;
}

// gilde.exe 0x5e6250 — VIBE_Save_WriteSceneState leading word. The original
// pre-increments dword_64A050 (seeded at 980156602) before writing, yielding
// kSceneTagWriter = 980156603 for the first save; this slice emits that value.
bool WorldIoWriteSceneHeader(VfsHandle* h) {
    return BioWriteDword(h, kSceneTagWriter);
}

// gilde.exe 0x5e639c — read + validate the leading scene-chunk tag word.
bool WorldIoReadSceneHeader(VfsHandle* h, guild::u32* tagOut) {
    guild::u32 tag = 0;
    if (!BioReadDword(h, &tag))
        return false;
    if (tagOut)
        *tagOut = tag;
    return WorldIoSceneTagValid(tag);
}

} // namespace guild::io
