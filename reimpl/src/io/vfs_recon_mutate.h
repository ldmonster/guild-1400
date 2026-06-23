#pragma once
// gilde.exe — guild::io  (VFS mutation slice: directory-hierarchy creation and
// loose-file deletion that mutate the in-memory VFS tree built by vfs_tree.cpp).
//
// This slice reconstructs VIBE_Vfs_CloseFileEntry from the "VIBE_Vfs" cluster: a
// tree-mutating operation (delete a loose file + drop its VFS index entry). The
// pure logic (path resolve via the already-recovered ResolvePath/ResolveAndBuildPath,
// the 24-byte file-array compact-or-free) is translated 1:1; the coupled OS leaves
// (rule-4 boundary: file Delete and the registry-backed array free) are routed
// through the hooks declared below so the portable build needs no third-party libs.
//
// Recovered entry point (this slice):
//   VIBE_Vfs_CloseFileEntry   @0x450ad8  resolve a loose (non-archive) file path,
//                                        delete it on disk, and remove its entry
//                                        from its directory's sorted file array
//                                        (compact-or-free). [misnamed in the symbol
//                                        dump: this DELETES a file + drops the entry.]
//
// (VIBE_Vfs_RemovePathEntry @0x4508dc was investigated and DEFERRED — see the
// manifest. Its directory-level extraction is tied to the original's trailing-slash
// node-name representation, which the existing reimpl tree deliberately diverges
// from; a 1:1 reconstruction cannot be guaranteed on the current model.)
//
// Coupled leaves (live in the binary as Win32 / CRT calls) are reached through
// these hooks so the logic stays portable and testable:
//   VIBE_Vfs_CloseHandleThunk@0x5eb960 -> VIBE_File_Delete (returns 0 on success)
//   (array free)             VIBE_Memory_FreeDebug @0x43923c -> free +0x104 block
#include "guild/common/types.h"
#include "io/vfs_tree.h"

namespace guild::io {

// --- rule-4 boundary hooks (defaults are no-ops returning "failure") ----------
// Delete the file at `path`. Returns true on success (mirrors VIBE_File_Delete==0).
using VfsDeleteHook    = bool (*)(const char* path);
// Free the registry-backed 24-byte file-array block owned by directory node `dir`
// (its +0x104 slot). The host owns the array registry (see vfs_tree.cpp), so the
// real free is delegated rather than approximated. After this returns the caller
// clears dir->arrayOrArch.
using VfsFreeArrayHook = void (*)(VfsNode* dir);

void SetVfsDeleteHook(VfsDeleteHook h);
void SetVfsFreeArrayHook(VfsFreeArrayHook h);

// gilde.exe 0x450ad8 — VIBE_Vfs_CloseFileEntry  (__usercall, al = result, eax=path,
// edx=root). Resolve `path` against `root` to a loose file entry (one with no
// archive node), build its on-disk full path, delete it, then remove its entry
// from the owning directory's sorted 24-byte file array (compacting the array, or
// freeing it when it becomes empty). Returns true iff the on-disk delete succeeded.
bool CloseFileEntry(const char* path, VfsNode* root);

} // namespace guild::io
