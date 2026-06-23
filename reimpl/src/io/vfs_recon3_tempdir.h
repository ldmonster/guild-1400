#pragma once
// gilde.exe — guild::io  (VFS path slice: temp-directory resolution)
//
// This recon3 slice reconstructs VIBE_Vfs_GetTempDir @0x5fc8d0 from the
// "VIBE_Vfs" cluster. It is genuine VFS path-resolution / normalization logic:
//
//   - scan a fixed, ordered environment-variable name table
//       {"TMP","TEMP","TMPDIR","TEMPDIR"}  (NULL-terminated, @0x64abfc ->
//        strings @0x62c554/0x62c558/0x62c560/0x62c568)
//   - for the first name that resolves to a non-empty env value whose
//     length+1 <= 0x104 (i.e. value length <= 0x103), canonicalize it through
//     GetFullPath into the static cache buffer (cap 0x103) and stop
//   - if no env candidate produced a path, fall back to the process working
//     directory (VIBE_Vfs_GetWorkingDir)
//   - finally ensure the cached path is separator-terminated: if its last
//     character is neither '\\' nor '/', append a single '\\'
//   - the result is memoized in a 0x104-byte static cache so the table scan /
//     working-dir fallback runs only once per process
//
// The pure logic above (table scan order, the <= 0x103 length gate, the byte
// copy of the working dir, and the trailing-separator normalization) is
// translated 1:1. The OS leaves it touches are not the game's data structures
// and are not OS-portable, so they are routed through an inert-default hooks
// struct (rule-4/6 boundary): getenv (VIBE_Crt_FindEnvVar @0x606580) and the
// GetFullPathNameA-backed canonicalizer (VIBE_File_GetFullPath @0x6065e0). The
// working-directory fallback reuses the already-reconstructed
// guild::io::VfsGetWorkingDir (file_ops3.cpp, @0x5eeeb0) directly (rule 13).
//
// This supplies the `tempDir` argument consumed by the already-present
// VIBE_Vfs_BuildTempFileName (guild::io::BuildTempFileName, file_ops.cpp,
// @0x5d9128), which builds "<tempDir>t<pid4hex>_<idx2hex>.tmp".
//
// NOT in this slice (CRT/libc stdio internals that carry a misleading
// VIBE_Vfs_ prefix in the symbol dump but operate on the libc FILE struct, the
// IFileSystem shim boundary, and belong to the CRT/file-stdio clusters):
//   VIBE_Vfs_WriteBuffered      @0x5dc0d0  = libc fwrite (FILE buffer mutation)
//   VIBE_Vfs_PrepareTextRead    @0x5e9da4  = file-backed vfscanf core (cookies)
//   VIBE_Vfs_ReadByteWithEof    @0x5e9d80  = getc cookie (sets FILE EOF flag)
//   VIBE_Vfs_SeekThunk          @0x5e9d9c  = ungetc cookie
//   VIBE_Vfs_OpenTempFile       @0x5fc7c0  = libc tmpfile (FILE alloc + flags)
//   VIBE_Vfs_ReadFile_Thunk     @0x140b080 = DRM-overlay import thunk -> ReadFile
// See the final report for addresses + reasons.

#include "guild/common/types.h"

namespace guild::io {

// Inert-default hooks for the non-portable OS leaves of VIBE_Vfs_GetTempDir.
// The portable/headless build links the defaults (no env, no canonicalization)
// and gets a deterministic working-dir-derived path; a real backend overrides
// them. None of these touch the game's VFS node tree.
struct VfsTempDirHooks {
    // VIBE_Crt_FindEnvVar @0x606580 — getenv(name); returns the value string or
    // nullptr when unset. Inert default: always unset.
    const char* (*findEnvVar)(const char* name) = nullptr;

    // VIBE_File_GetFullPath @0x6065e0 — canonicalize `src` into `dst` (capacity
    // `cap`, here 0x103) the way GetFullPathNameA does, returning dst on success
    // or nullptr on failure. Inert default: failure (leaves dst untouched), so
    // the working-dir fallback runs.
    char* (*getFullPath)(char* dst, const char* src, guild::u32 cap) = nullptr;
};

// Install/replace the hooks (returns the previous set). nullptr-keeps any field
// is NOT done here — pass a fully-populated struct.
VfsTempDirHooks VfsSetTempDirHooks(const VfsTempDirHooks& h);

// Reset the memoized cache (test seam; the original never clears it — it is a
// process-lifetime static). Lets golden tests exercise the resolution path
// repeatedly with different hook configurations.
void VfsResetTempDirCache();

// gilde.exe 0x5fc8d0 — VIBE_Vfs_GetTempDir. Returns a pointer to the static,
// separator-terminated temp-directory path (computed once, then memoized).
char* VfsGetTempDir();

} // namespace guild::io
