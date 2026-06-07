#pragma once
// gilde.exe — guild::io  (MODULE: VFS byte/line readers, chunk-table scanning,
// config-line reader, and the OS-backed working-dir / process-id helpers)
//
// This slice sits on top of the already-reconstructed VFS stream verbs
// (VfsOpenFile / VfsReadStream / VfsSeek / VfsTell / VfsCloseStream, see vfs.h)
// and the path-tree resolver (ResolvePath, see vfs_tree.h). It reconstructs:
//
//   VIBE_Bio_ReadByte            @0x5dc850   read 1 byte through the VFS stream
//   VIBE_Bio_ReadDwordSwapArgs   @0x5dc8b0   read 4 raw bytes through the stream
//   VIBE_Script_ReadToken        @0x5e3bd0   read one "token" byte (EOF -> '+', >':' -> '\'')
//   VIBE_Vfs_ReadByte            @0x451698   read 1 byte, -1 on EOF
//   VIBE_Vfs_ReadLine            @0x4516cc   read one CR/LF-terminated line
//   VIBE_Vfs_ReadStreamBool      @0x5a75f4   VfsReadStream != 0
//   VIBE_Vfs_FindChunkStart      @0x5f86fc   scan a chunk table for the 0xFAB50005 record
//   VIBE_Vfs_ScanChunkLength     @0x5f8570   like FindChunkStart but returns the byte offset
//   VIBE_Vfs_CheckChunkFlag      @0x5f8640   read fixed record headers, test a flag bit
//   VIBE_Vfs_ReadConfigLine      @0x5dc790   read+trim+upper-case the next non-comment line
//   VIBE_Vfs_FileExists          @0x5dc770   ResolvePath(path) != null
//   VIBE_Vfs_GetProcessId        @0x5fc7b0   GetCurrentProcessId()
//   VIBE_Vfs_GetWorkingDir       @0x5eeeb0   GetCurrentDirectoryA into caller/alloc buffer
//
// The chunk magic word is 0xFAB50005 (the original's signed literal -88801275).
//
// OS / not-yet-reconstructed leaves (GetCurrentProcessId, GetCurrentDirectoryA,
// VIBE_File_MapLastError, VIBE_Runtime_SetErrnoEinval, VIBE_Memory_AllocFromFreeList)
// are routed through FileOps3Hooks, an installable struct whose default
// implementation is inert/deterministic and defined in file_ops3.cpp. Tests
// install their own backend; no src/ file references a test-defined symbol.
#include "io/file.h"
#include "guild/common/types.h"

namespace guild::io {

// The chunk-table sentinel record id (original signed literal -88801275).
inline constexpr guild::u32 kVfsChunkMagic = 0xFAB50005u;

// Tokens returned by VfsScriptReadToken.
inline constexpr int kVfsTokenEof     = 43; // '+'  (returned on EOF)
inline constexpr int kVfsTokenSection = 45; // '-'  (section marker)
inline constexpr int kVfsTokenOther   = 39; // '\'' (byte value > ':')

// --- installable hooks for OS / not-yet-reconstructed leaves ----------------
struct FileOps3Hooks {
    // GetCurrentProcessId(). Default returns 0.
    guild::u32 (*getProcessId)() = nullptr;
    // GetCurrentDirectoryA(size, buf) -> length written (excl. NUL), 0 on failure.
    guild::u32 (*getCurrentDir)(guild::u32 size, char* buf) = nullptr;
    // VIBE_File_MapLastError() — translate the OS error into errno. Default: no-op.
    void (*mapLastError)() = nullptr;
    // VIBE_Runtime_SetErrnoEinval(). Default: no-op.
    void (*setErrnoEinval)() = nullptr;
    // VIBE_Memory_AllocFromFreeList(size). Default returns nullptr.
    void* (*allocMem)(guild::u32 size) = nullptr;
};

// Install a hook backend; pass nullptr to restore the inert defaults. Returns
// the previously-installed hooks.
FileOps3Hooks SetFileOps3Hooks(const FileOps3Hooks* hooks);

// --- byte / dword stream readers (Bio / Script primitives) ------------------
// NOTE: VIBE_Bio_ReadByte @0x5dc850 and VIBE_Bio_ReadDwordSwapArgs @0x5dc8b0 are
// already reconstructed as guild::io::BioReadByte / BioReadDword in worldio.cpp;
// this module reuses them (include "io/worldio.h").

// VIBE_Script_ReadToken @0x5e3bd0 — read one byte; on EOF return '+' (43); if the
// byte is > ':' (0x3A) return '\'' (39); otherwise return the byte.
guild::u8 ScriptReadToken(VfsHandle* h);

// --- line / byte readers ----------------------------------------------------

// VIBE_Vfs_ReadByte @0x451698 — read one byte; returns it (0..255) or -1 at EOF.
int VfsReadByte(VfsHandle* h);

// VIBE_Vfs_ReadLine @0x4516cc — read up to maxLen bytes of one line into dst
// (NUL-terminated). CR (13) is treated as a line break; trailing CR/LF runs are
// consumed. Returns dst, or nullptr if nothing could be read. (This is the
// VfsHandle-stream variant; the BufferedFile variant is FileReadLine.)
char* VfsReadLine(char* dst, int maxLen, VfsHandle* h);

// VIBE_Vfs_ReadStreamBool @0x5a75f4 — VfsReadStream(...) != 0.
bool VfsReadStreamBool(void* dst, guild::u32 size, VfsHandle* h, guild::u32 count);

// --- chunk-table scanning ---------------------------------------------------

// VIBE_Vfs_FindChunkStart @0x5f86fc — open `path` "rb", skip the 4-byte header,
// then walk the chunk table looking for the sentinel record 0xFAB50005. Returns
// the still-open VfsHandle* positioned just past the sentinel, or nullptr if the
// table ends ('-' with no following sentinel after a '+' close) / open fails.
// On the '+' (premature end) path it closes the stream and returns nullptr.
VfsHandle* VfsFindChunkStart(const char* path);

// VIBE_Vfs_ScanChunkLength @0x5f8570 — like VfsFindChunkStart, but returns the
// byte offset (VfsTell) of the matched sentinel record and ALWAYS closes the
// stream. Returns -1 if not found / open fails.
long VfsScanChunkLength(const char* path);

// VIBE_Vfs_CheckChunkFlag @0x5f8640 — open `path` "rb", read the fixed record
// headers, and if the third record tag byte is 2, test bit 1 of the following
// byte. Returns true unless that flag bit is set (or open fails -> true).
bool VfsCheckChunkFlag(const char* path);

// --- config-line reader -----------------------------------------------------

// VIBE_Vfs_ReadConfigLine @0x5dc790 — read the next non-blank, non-comment
// (';'-prefixed) line, right-trim CR/LF/TAB/SPACE, skip leading whitespace, and
// upper-case the remainder in place. Returns a pointer to the upper-cased text
// (a module-static 256-byte scratch buffer), or nullptr at EOF.
char* VfsReadConfigLine(VfsHandle* h);

// --- path existence / OS helpers --------------------------------------------

// VIBE_Vfs_FileExists @0x5dc770 — true iff `path` resolves to a file entry in the
// bound VFS tree (ResolvePath against VfsRoot()).
bool VfsFileExists(const char* path);

// VIBE_Vfs_GetProcessId @0x5fc7b0 — GetCurrentProcessId() via the hook.
guild::u32 VfsGetProcessId();

// VIBE_Vfs_GetWorkingDir @0x5eeeb0 — fetch the process working directory.
//   * If `dst` is non-null, copy into it (capacity `size`); fail (nullptr) if the
//     directory is longer than `size`.
//   * If `dst` is null, allocate max(len+1, size) bytes via the hook and copy.
// Returns the destination buffer, or nullptr on failure.
char* VfsGetWorkingDir(char* dst, guild::u32 size);

} // namespace guild::io
