#pragma once
// gilde.exe — guild::io  (MODULE: world / scene serialization — WorldIo_*)
//
// WorldIo_* serializes per-scene state (`.cty` city seed / scene blocks) on top of
// a thin binary-IO layer (VIBE_Bio_*), which is itself a 1:1 wrapper over the VFS
// stream verbs: every value is written/read raw little-endian via
// VIBE_Vfs_WriteStream / VIBE_Vfs_ReadStream. There is no framing or compression
// at this layer.
//
// Reconstructed:
//   VIBE_Bio_WriteByte       @0x5dc8cc  (1-byte raw write)
//   VIBE_Bio_WriteDword      @0x5dc918  (4-byte raw write, LE)
//   VIBE_Bio_WriteDwordPair  @0x5dcac0  (writes ONLY the first dword — quirk)
//   VIBE_Bio_ReadByte        @0x5dc850
//   VIBE_Bio_ReadDword       @0x5dc894  (== ReadDwordSwapArgs @0x5dc8b0)
//   VIBE_WorldIo_SaveSceneState @0x5e6250  (scene chunk: header magic + fog + lights)
//   VIBE_WorldIo_LoadSceneState @0x5e639c  (version-gated mirror)
//
// The scene-state chunk begins with a 32-bit tag/version word. The loader accepts
// it iff (tag & 0xFFFF0000) == kSceneTagBase and tag >= kSceneTagMinVersion; the
// writer emits kSceneTagWriter (an incrementing counter seeded at this value). This
// header gate is recovered byte-for-byte; the body (fog params, 7 light rigs, then
// a 0-terminated object list) is owned by the render/scenegraph modules and is out
// of scope for this slice (listed in the report).
#include "guild/common/types.h"
#include "io/vfs.h"

namespace guild::io {

// --- Bio binary-IO primitives (raw little-endian over the VFS stream) ------
// VIBE_Bio_WriteDword @0x5dc918 — write a 32-bit value (LE). Returns true on full
// write. (The original returns the VfsWriteStream byte count.)
bool BioWriteDword(VfsHandle* h, guild::u32 v);

// VIBE_Bio_WriteByte @0x5dc8cc — write a single byte.
bool BioWriteByte(VfsHandle* h, guild::u8 v);

// VIBE_Bio_WriteDwordPair @0x5dcac0 — the original lays out two dwords on the stack
// but writes only the FIRST (size 4, count 1). Faithfully writes one dword; the
// second argument is accepted and ignored to match the call sites.
bool BioWriteDwordPair(VfsHandle* h, guild::u32 first, guild::u32 ignored);

// VIBE_Bio_ReadDword @0x5dc894 (== ReadDwordSwapArgs @0x5dc8b0) — read a 32-bit LE
// value into *out. Returns true on a full 4-byte read.
bool BioReadDword(VfsHandle* h, guild::u32* out);

// VIBE_Bio_ReadByte @0x5dc850 — read one byte into *out.
bool BioReadByte(VfsHandle* h, guild::u8* out);

// --- scene-state chunk header (the version gate) ---------------------------
// Tag/version word recovered from VIBE_WorldIo_LoadSceneState @0x5e639c:
//   if ((tag & 0xFFFF0000) == 0x3A6C0000 && tag >= 980156601) { ...parse body... }
// (the original clears LOWORD and compares the high half to 980156416 = 0x3A6C0000,
//  then compares the whole word to 980156601.) 980156603 (0x3A6C00BB) is the value
// the writer emits (dword_64A050, seeded here).
constexpr guild::u32 kSceneTagBase       = 0x3A6C0000; // (tag & 0xFFFF0000) must equal
constexpr guild::u32 kSceneTagMinVersion = 980156601;  // 0x3A6C00B9
constexpr guild::u32 kSceneTagWriter     = 980156603;  // 0x3A6C00BB

// True iff `tag` passes the loader's scene-chunk acceptance gate.
bool WorldIoSceneTagValid(guild::u32 tag);

// Write/read just the leading scene-chunk tag word (the self-contained header of
// VIBE_WorldIo_SaveSceneState / LoadSceneState). The full body is out of scope.
bool WorldIoWriteSceneHeader(VfsHandle* h);
bool WorldIoReadSceneHeader(VfsHandle* h, guild::u32* tagOut /*may be null*/);

} // namespace guild::io
