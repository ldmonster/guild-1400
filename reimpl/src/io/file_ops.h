#pragma once
// gilde.exe — guild::io  (MODULE: raw File_* OS layer + small Vfs helpers)
//
// This module reconstructs the self-contained pieces of the original raw
// file/OS layer (VIBE_File_*) and a few Vfs path helpers (VIBE_Vfs_*) that the
// transparent loose/gzip/zip core (vfs.cpp / file.cpp / vfs_tree.cpp) sits on
// top of. Everything that touched the real OS in the binary (CreateDirectoryA,
// GetFileAttributesA, FindFirstFileA, ...) is routed through the shim
// IFileSystem interface so no platform calls leak into game code; the pure
// flag/buffer/time math is translated verbatim.
//
// Recovered Win32 FIND/attribute layouts (used by the find functions):
//   The original consumed a WIN32_FIND_DATAA (0x140 bytes). The subset the
//   game stores per directory entry is mirrored by FindData below; the host
//   surfaces it through shim::DirEntry (name + isDir + DOS mtime).
//
#include "guild/common/types.h"
#include "shim/IFileSystem.h"
#include <cstddef>

namespace guild::io {

// ---------------------------------------------------------------------------
// open()/CreateFile flag mapping (pure arithmetic, no OS)
// ---------------------------------------------------------------------------

// gilde.exe 0x6062c0 — VIBE_File_MapCreateDisposition. Maps the high "create"
// nibble (0=open, 1=create, 2=create-new) to the Win32 (dwDesiredAccessMask,
// dwCreationDisposition) pair the original passed to CreateFileA.
//   disp==2 -> access = 0xC0000000 (GENERIC_READ|WRITE), creation = 128 (CREATE_NEW)
//   disp==1 -> access = 0x40000000 (GENERIC_WRITE),      creation = 128 (CREATE_ALWAYS-ish)
//   else    -> access = 0x80000000 (GENERIC_READ),       creation = 1   (OPEN_EXISTING)
// Returns the input disposition unchanged. (eax=disp, edx=*access, ebx=*creation)
int MapCreateDisposition(int disp, guild::u32* access, guild::u32* creation);

// gilde.exe 0x6062f4 — VIBE_File_MapAccessFlags. Decodes the low access bits of
// the open-mode byte and writes a small access code to *outAccess:
//   bits 0x70 select the access group; bits 0x07 are the read/write sub-bits.
//   group 0x00 -> *out = 1 (read); if sub-bits == 0 also OR 0x02
//   group 0x10 -> *out = 0
//   group 0x20 -> *out = 1
//   group 0x30 -> *out = 2
//   group 0x40 -> *out = 3
// Returns (modeByte & 0x70). (al=modeByte, edx=*outAccess)
guild::u32 MapAccessFlags(unsigned char modeByte, guild::u32* outAccess);

// ---------------------------------------------------------------------------
// Buffered-stream offset math (FILE*-style record; no OS)
// ---------------------------------------------------------------------------

// Minimal mirror of the original buffered-stream record. Only the four fields
// the buffer-offset helpers touch are modelled (the binary's _iobuf layout):
struct StreamBuf {
    char*       ptr;     // +0x00  current cursor in the buffer
    guild::i32  cnt;     // +0x04  bytes remaining ahead of ptr
    char*       base;    // +0x08  -> backing buffer descriptor (see BufBase)
    guild::u8   flags;   // +0x0C  bit 0x10 = "ungetc/lookahead present"
};
struct BufBase {
    char* unused0;       // +0x00
    char* unused4;       // +0x04
    char* start;         // +0x08  start of the data buffer
};

// gilde.exe 0x5d45a0 — VIBE_File_AdjustBufferOffset. Tries to satisfy a small
// relative seek of `delta` bytes entirely within the current read buffer.
// Returns 0 if the seek stayed inside the buffer (cursor/count updated, the
// 0x10 lookahead flag cleared), or 1 if it falls outside (caller must do a real
// OS seek). (eax=delta, edx=stream)
int AdjustBufferOffset(int delta, StreamBuf* s);

// gilde.exe 0x5d45e0 — VIBE_File_ResetBuffer. Discards the buffered data: clears
// the 0x10 flag, sets cnt=0 and rewinds ptr to base->start. (eax=stream)
StreamBuf* ResetBuffer(StreamBuf* s);

// ---------------------------------------------------------------------------
// DOS date/time conversion (pure bit packing; the binary used kernel32, but the
// transform is fully specified, so it is translated as the documented math)
// ---------------------------------------------------------------------------

// A broken-down local time, matching the SYSTEMTIME fields the conversions use.
struct DosTime {
    int year;    // full year, e.g. 2026
    int month;   // 1..12
    int day;     // 1..31
    int hour;    // 0..23
    int minute;  // 0..59
    int second;  // 0..59
};

// gilde.exe 0x5fe760 — VIBE_File_ConvertFileTimeToDos. Packs a local time into
// the DOS date and time WORDs (FileTimeToDosDateTime semantics):
//   *outDate = (year-1980)<<9 | month<<5 | day
//   *outTime = hour<<11 | minute<<5 | second/2
// Returns true if year is in the representable DOS range [1980,2107].
bool ConvertFileTimeToDos(const DosTime& t, guild::u16* outDate, guild::u16* outTime);

// gilde.exe 0x5fe788 — VIBE_File_ConvertDosToFileTime. Inverse of the above
// (DosDateTimeToFileTime semantics). Returns true and fills `out`.
bool ConvertDosToFileTime(guild::u16 date, guild::u16 time, DosTime* out);

// ---------------------------------------------------------------------------
// Directory enumeration / attribute mapping (host via IFileSystem)
// ---------------------------------------------------------------------------

// The per-entry attribute/time record the game keeps after a find. Mirrors the
// fields VIBE_File_CopyFindDataAttributes writes (a compact "stat"):
//   +0x00 attributes  (bit copy of FILE_ATTRIBUTE_* the game cares about)
//   +0x04 ctime, +0x08 atime, +0x0C mtime  (unix-ish, here the DOS mtime)
//   +0x10 size
//   +0x14 name[]
struct FindData {
    guild::u8  attributes;   // +0x00  archive/dir/hidden/readonly/system bits
    guild::u32 ctime;        // +0x04
    guild::u32 atime;        // +0x08
    guild::u32 mtime;        // +0x0C
    guild::u32 size;         // +0x10
    char       name[260];    // +0x14
};

// gilde.exe 0x5fe7f4 — VIBE_File_FindNextMatching. Attribute-mask filter applied
// to a find loop: a zero attribute word is treated as 0x80 (FILE_ATTRIBUTE_-
// NORMAL); the entry matches when (mask & attrs) != 0. `attrs` is the entry's
// attribute byte (in/out: defaulted to 0x80 if zero). Returns true on match.
// (Loop over FindNextFileA is the caller's job — here we test one entry.)
bool FindEntryMatches(int mask, guild::u32* attrs);

// gilde.exe 0x5eb894 — VIBE_File_CopyFindDataAttributes. Copies the Win32 find
// attribute bits the game keeps (0x20 archive, 0x10 dir, 0x02 hidden, 0x01
// readonly, 0x04 system) and the DOS mtime + name from a shim::DirEntry into a
// FindData. (Original mapped FILETIMEs via FileTimeToUnix; here the host already
// provides a DOS-packed mtime, stored into all three time slots.)
void CopyFindDataAttributes(const guild::shim::DirEntry& e, FindData* out);

// ---------------------------------------------------------------------------
// OS operations routed through IFileSystem
// ---------------------------------------------------------------------------

// NOTE: VIBE_File_CreateDirectory @0x5eb920 is already translated as
// guild::io::FileCreateDirectory in io/file_buffered.{h,cpp}; reuse that.

// gilde.exe 0x606520 — VIBE_File_CheckAccess. `mode` bit 0x02 requests write
// access; the original failed if the file was read-only. With IFileSystem we can
// only test existence (the read-only bit is not surfaced), so: missing -> -1,
// present -> 0. (eax=path, dl=mode)
int CheckAccess(guild::shim::IFileSystem* fs, const char* path, char mode);

// ---------------------------------------------------------------------------
// Vfs path helpers
// ---------------------------------------------------------------------------

// gilde.exe 0x5d911c — VIBE_Util_NibbleToHexChar. 0..15 -> '0'..'9','a'..'f'.
char NibbleToHexChar(int nibble);

// gilde.exe 0x5d9128 — VIBE_Vfs_BuildTempFileName. Builds "<tempDir>t<pid4hex>_<idx2hex>.tmp"
// into `out`. `pid` is folded to 16 bits ((pid|pid>>16) & 0xFFFF); `idx` is a
// per-call counter byte. `tempDir` is the (slash/backslash terminated) temp
// directory. Returns `out`. The 4 pid hex digits are written low-nibble-last
// (matching the original's pre-decrement `>>=4` loop into out[len+1..+4]).
char* BuildTempFileName(char* out, const char* tempDir, guild::u32 pid, int idx);

} // namespace guild::io
