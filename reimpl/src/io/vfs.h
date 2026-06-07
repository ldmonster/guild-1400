#pragma once
// gilde.exe — guild::io  (MODULE: the virtual filesystem dispatch layer)
//
// The VFS transparently opens, by path, one of three kinds of backing store and
// presents a single uniform stream:
//   * a loose file on disk,
//   * a gzip-framed file (.gz framing; magic 1f 8b),
//   * a member of a PKZIP archive whose name is <stem><ext> where <ext> is one of
//     .BIN / .BIN0 .. .BIN5 (extension table @0x44E8F0).
//
// Reconstructed entry points (the dispatch + stream verbs the rest of the game
// calls through; the path-resolution tree walkers VIBE_Vfs_ScanDirectory etc. are
// out of scope for this slice and noted in the report):
//   VIBE_Vfs_Init            @0x451f98
//   VIBE_Vfs_OpenFile        @0x450bc8   (the transparent open dispatch)
//   VIBE_Vfs_OpenMemoryStream@0x451b30
//   VIBE_Vfs_ReadStream      @0x4514ac
//   VIBE_Vfs_WriteStream     @0x4517a8
//   VIBE_Vfs_Seek            @0x4518f0
//   VIBE_Vfs_Tell            @0x451aa4
//   VIBE_Vfs_CloseStream     @0x451354
//
// Decompression reuses guild::compress (Gunzip / InflateRaw); PKZIP local-file
// header parsing is done here and the deflate payload handed to InflateRaw.
//
// Real file access goes through shim::IFileSystem; the active filesystem is set
// at Init and stored module-globally (mirrors the original's global root state).
#include "io/file.h"
#include "guild/common/types.h"
#include "shim/IFileSystem.h"
#include <cstddef>

namespace guild::io {

// VIBE_Vfs_Init @0x451f98 — bind the host filesystem the VFS reads through and
// reset the open-stream counter. `caseInsensitive` mirrors byte_62EB84 (when set
// the original does not upper-case loose paths). Returns true.
bool VfsInit(guild::shim::IFileSystem* fs, bool caseInsensitive);

// VIBE_Vfs_Shutdown @0x452004 — clears the bound filesystem.
void VfsShutdown();

// Number of currently-open VFS streams (dword_62EB88).
int VfsOpenCount();

// VIBE_Vfs_OpenFile @0x450bc8 — open `path` with C-stdio-style `mode`.
//   mode chars consumed exactly as the original:
//     'R'/'B' -> read,  'W' -> write,  'T' -> text (clears binary flag),
//     'N' -> no transparent gzip.
// On read it transparently selects loose / gzip / zip-member backing and returns
// a 320-byte VfsHandle*, or nullptr on failure. The original's `flags` argument
// (a3) is folded into a default open context here.
VfsHandle* VfsOpenFile(const char* path, const char* mode);

// VIBE_Vfs_OpenMemoryStream @0x451b30 — wrap an in-memory buffer as a read (or
// write) stream. For read mode the buffer may be raw or gzip/deflate framed; the
// framing is detected from `mode` and the leading bytes exactly as the original.
VfsHandle* VfsOpenMemoryStream(guild::u8* buffer, guild::u32 length, const char* mode);

// VIBE_Vfs_ReadStream @0x4514ac — read `size*count` bytes into `dst`; returns the
// number of bytes read, or 0xFFFFFFFF (-1) on error.
guild::u32 VfsReadStream(void* dst, guild::u32 size, VfsHandle* h, guild::u32 count);

// VIBE_Vfs_WriteStream @0x4517a8 — write `size*count` bytes; returns bytes written
// or -1.
guild::u32 VfsWriteStream(const void* src, guild::u32 size, VfsHandle* h, guild::u32 count);

// VIBE_Vfs_Seek @0x4518f0 — whence is SEEK_SET/CUR/END; returns 0 / -1.
int VfsSeek(VfsHandle* h, long offset, guild::u32 whence);

// VIBE_Vfs_Tell @0x451aa4 — current position, or -1.
long VfsTell(VfsHandle* h);

// VIBE_Vfs_CloseStream @0x451354 — close the stream and free the handle.
int VfsCloseStream(VfsHandle* h);

} // namespace guild::io
