// gilde.exe — guild::io  (VFS mutation slice; see vfs_recon_mutate.h).
//
// 1:1 translation of:
//   VIBE_Vfs_CloseFileEntry  @0x450ad8  (delete loose file + drop VFS entry)
//
// The pure logic is reconstructed exactly; the coupled OS leaves are routed via
// hooks. (VIBE_Vfs_RemovePathEntry @0x4508dc was deferred — see the header note.)
#include "io/vfs_recon_mutate.h"
#include "io/vfs_tree.h"
#include "io/path.h"

#include <cstring>

namespace guild::io {

namespace {

bool g_deleteDefault(const char*)  { return false; }   // VIBE_File_Delete failed
// The +0x104 file-array block lives in vfs_tree's registry; releasing it there is
// the faithful MemPool_Free (the original frees the block here). FreeNodeArray
// also nulls the slot; CloseFileEntry nulls it again, which is harmless.
void g_freeArrayDefault(VfsNode* dir) { FreeNodeArray(dir); }

VfsDeleteHook    g_delete    = g_deleteDefault;
VfsFreeArrayHook g_freeArray = g_freeArrayDefault;

} // namespace

void SetVfsDeleteHook(VfsDeleteHook h)       { g_delete    = h ? h : g_deleteDefault; }
void SetVfsFreeArrayHook(VfsFreeArrayHook h) { g_freeArray = h ? h : g_freeArrayDefault; }

// ---------------------------------------------------------------------------
// gilde.exe 0x450ad8 — VIBE_Vfs_CloseFileEntry.
bool CloseFileEntry(const char* path, VfsNode* root) {
    VfsNode* dir = nullptr;
    VfsFileEntry* entry = ResolvePath(path, root, &dir);   // 0x450ae9

    // 0x450af? guard: entry must exist, be a loose file (archiveNode==0, +0x08),
    // dir must exist and be a finalized directory (+0x118 isDir set).
    if (!entry || entry->archiveNode || !dir || !dir->isDir)
        return false;                                       // 0x450b0e

    char full[256];                                         // v6
    // 0x450b29: BuildFullPath(entry, dir, full, dirSep=0). The original passes the
    // resolved file ENTRY as the "node"; the faithful reconstruction renders the
    // dir's path and appends the entry name — identical to the already-recovered
    // VIBE_Vfs_ResolveAndBuildPath @0x4500a0 (which re-resolves and builds the same
    // full path with dirSep=0). Reuse it to avoid duplicating the tree walk.
    if (!ResolveAndBuildPath(path, root, full, false))
        return false;
    ConvertSlashToBackslash(full);                          // 0x450b32

    // 0x450b4b: --dir->fileCount.
    VfsFileEntry* base = ArrayOf(dir->arrayOrArch);
    int removedIdx = (entry && base) ? static_cast<int>(entry - base) : 0;
    int newCount = static_cast<int>(--dir->countOrTime);    // pre-decrement

    if (newCount) {
        // 0x450bc1: MemMove(entry, entry+1, 24*(newCount - removedIdx)) — shift
        // the entries after the removed one down by one slot to compact the array.
        if (base) {
            int move = newCount - removedIdx;
            if (move > 0)
                std::memmove(&base[removedIdx], &base[removedIdx + 1],
                             static_cast<std::size_t>(move) * sizeof(VfsFileEntry));
        }
    } else {
        // 0x450b5b: free the array block and null the slot.
        g_freeArray(dir);
        dir->arrayOrArch = 0;                               // 0x450b67
    }

    return g_delete(full);              // VIBE_File_Delete(full) == 0 -> 0x450b10
}

} // namespace guild::io
