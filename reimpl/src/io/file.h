#pragma once
// gilde.exe — guild::io  (MODULE: the virtual filesystem, low-level file layer)
//
// This header declares the 320-byte VFS stream handle exactly as laid out by the
// original (VIBE_Vfs_OpenFile @0x450bc8 returns a MemPool_Alloc(320,16) block) and
// the loose-file backend that, in the original, wrapped the buffered C-stdio FILE*
// layer (VIBE_File_* @0x5d4444..). Here the real bytes come through
// shim::IFileSystem so no OS calls leak into game code.
//
// Handle byte layout (offsets recovered from VIBE_Vfs_OpenFile / ReadStream /
// WriteStream / CloseStream / Seek / Tell / OpenMemoryStream):
//
//   +0x000  char  name[256]  : the requested path, copied to the front of the
//                              block (used in log/error formatting; the original
//                              also (mis)uses it as a sprintf scratch buffer).
//   +0x100  ( dword #64 @ +256 ) backendStream / memCursor
//   +0x104  ( dword #65 @ +260 ) memRemaining
//   +0x108  ( dword #66 @ +264 ) memReadTotal / position
//   +0x10C  ( dword #67 @ +268 ) writeDst / inflate next_out
//   +0x110  ( dword #68 @ +272 ) writeRemaining
//   +0x114  ( dword #69 @ +276 ) writeTotal
//   +0x138  ( dword #78 @ +312 ) crc (deflate path)
//   +0x13C  ( byte  @ +316 )     flags
//
// flags bits (VIBE_Vfs_OpenFile / ReadStream / Seek / Tell / CloseStream):
//   0x01  kRead        opened read-only
//   0x02  kBinary      binary (cleared by 'T' text mode) — write framing flag
//   0x04  kGzInZip     "look for gz-magic in the first zip bytes" one-shot probe
//   0x08  kGzip        gzip / deflate framed stream
//   0x10  kZipMember   member of a PKZIP (.BIN*) archive
//   0x20  kMemory      in-memory stream (VIBE_Vfs_OpenMemoryStream)
//
#include "guild/common/types.h"
#include "shim/IFileSystem.h"
#include <cstddef>

namespace guild::io {

// flag bits on VfsHandle::flags (+0x13C)
enum VfsFlags : guild::u8 {
    kVfsRead      = 0x01,   // +0x13C bit0
    kVfsBinary    = 0x02,   // +0x13C bit1
    kVfsGzInZip   = 0x04,   // +0x13C bit2
    kVfsGzip      = 0x08,   // +0x13C bit3
    kVfsZipMember = 0x10,   // +0x13C bit4
    kVfsMemory    = 0x20,   // +0x13C bit5
};

// The 320-byte handle returned by Vfs open functions. The original is a 32-bit
// x86 record, so every field here is fixed-width and the whole struct is exactly
// 320 bytes (asserted in file.cpp). The slot at +0x100 held a 32-bit stream
// pointer in the binary; in this 64-bit reconstruction it is an opaque id, and
// the live backend object is held in a side registry (see vfs.cpp) so the byte
// layout stays identical to the original.
struct VfsHandle {
    char        name[256];      // +0x000  requested path / scratch
    guild::u32  backendStream;  // +0x100  (#64) loose/gzip/zip backend id, or mem cursor
    guild::u32  memRemaining;   // +0x104  (#65) bytes left in a memory stream
    guild::u32  memReadTotal;   // +0x108  (#66) total consumed (= tell)
    guild::u32  writeDst;       // +0x10C  (#67) write/deflate destination cursor
    guild::u32  writeRemaining;  // +0x110 (#68) bytes of room left at writeDst
    guild::u32  writeTotal;     // +0x114  (#69) total bytes written
    guild::u8   pad118[0x138 - 0x118]; // +0x118 (#70..#77) unused state slots
    guild::u32  crc;            // +0x138  (#78) running CRC-32 (deflate path)
    guild::u8   flags;          // +0x13C  see VfsFlags
    guild::u8   pad13d[3];      // +0x13D  alignment to 320
};

// --- loose-file backend ----------------------------------------------------
// Mirrors VIBE_File_OpenStream/Read/Seek/Close, but reads through IFileSystem.
// LooseFile wraps a shim::IFile* plus the owning IFileSystem* (for close).
struct LooseFile {
    guild::shim::IFileSystem* fs;
    guild::shim::IFile*       file;
};

// Open a loose file. `mode` is a C-stdio-style mode string ("rb","wb",...).
// Returns nullptr on failure. (VIBE_File_OpenStream @0x5d4488 + OpenCreate.)
LooseFile* FileOpen(guild::shim::IFileSystem* fs, const char* path, const char* mode);

// VIBE_File_Read @0x5d4770 — read up to count*size bytes; returns bytes read.
std::size_t FileRead(LooseFile* lf, void* dst, std::size_t size, std::size_t count);

// VIBE_File_Seek @0x5d45f8 — SEEK_SET/CUR/END; returns 0 on success, -1 on error.
int FileSeek(LooseFile* lf, long offset, int whence);

// VIBE_File_Tell — current offset, or -1.
long FileTell(LooseFile* lf);

// VIBE_File_CloseHandle @0x5fc9a0 — close and free.
void FileClose(LooseFile* lf);

} // namespace guild::io
