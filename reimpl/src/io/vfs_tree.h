#pragma once
// gilde.exe — guild::io  (MODULE: the VFS directory-tree / in-memory file index)
//
// VIBE_Vfs_Init scans a real directory tree and builds an in-memory index: a
// tree of *directory* nodes, each holding a sorted array of the loose files it
// contains. PKZIP archives (.BIN/.BIN0-5) are recursed into as virtual
// subdirectories. Path lookups walk this tree.
//
// Recovered entry points (this slice):
//   VIBE_Vfs_GetOrCreateSubDir   @0x44ebcc   create/find a child directory node
//   VIBE_Vfs_FindFileRecursive   @0x44eca8   binary-search a dir's file array
//   VIBE_Vfs_AddFileSorted       @0x44ed58   insert a file leaf onto a dir node
//   VIBE_Vfs_BuildFinishedFileList@0x44f6b0  flatten leaf list -> sorted array
//   VIBE_Vfs_NormalizeDirPath    @0x44f88c   walk slash-separated dir components
//   VIBE_Vfs_ResolvePath         @0x44fa5c   resolve a path to (file, dir)
//   VIBE_Vfs_BuildFullPath       @0x44fbcc   reconstruct a node's full path
//   VIBE_Vfs_BuildParentPath     @0x44fdb4   reconstruct a node's parent path
//   VIBE_Vfs_FreeNodeTree        @0x450868   recursively free the tree
//   VIBE_Vfs_ScanDirectory       @0x450234   recursive FindFirst/Next scan
//   VIBE_Vfs_Init                @0x451f98   bind FS + scan root
//   VIBE_Vfs_Shutdown            @0x452004   free the tree
//
// Globals mirrored: g_vfsRoot (dword_62EB78), g_vfsCaseInsensitive
// (byte_62EB84 — when set the original does NOT upper-case names, i.e. it keeps
// the host's case and compares case-insensitively).
#include "guild/common/types.h"
#include "shim/IFileSystem.h"

namespace guild::io {

// .BIN extension table @0x44E8F0 (6-byte stride, 7 entries, highest first):
//   ".BIN5" ".BIN4" ".BIN3" ".BIN2" ".BIN1" ".BIN0" ".BIN"
extern const char kBinExtTable[7][6];
constexpr int kBinExtCount = 7;

// A VFS tree node. Originally a MemPool block: directory nodes are 284 bytes
// (VIBE_Vfs_GetOrCreateSubDir allocs 0x11C), transient file-leaf nodes are 280
// bytes (VIBE_Vfs_AddFileSorted allocs 280). The two roles share the same
// field offsets; the names below reflect both roles where they diverge.
//
// As a DIRECTORY node (in the persistent tree):
//   +256 fileCount, +260 fileArray (24-byte entries), +264 nextSibling,
//   +268 firstChild, +272 parent, +276 leafList (pre-flatten), +280 isDir.
// As a transient FILE-LEAF node (linked on a dir's +276 list before flatten):
//   +256 dosTime, +260 archiveNode, +264 field264, +268 field268,
//   +272 binExtIdx, +276 nextLeaf.
struct VfsNode {
    char        name[256];   // +0x000  leaf/dir name (NUL-padded copy, 255 max)
    guild::u32  countOrTime; // +0x100  (#64) dir: file count   / leaf: dosTime
    guild::u32  arrayOrArch; // +0x104  (#65) dir: file array   / leaf: archiveNode
    guild::u32  siblingOrF1; // +0x108  (#66) dir: next sibling / leaf: field a5
    guild::u32  childOrF2;   // +0x10C  (#67) dir: first child   / leaf: field a3
    guild::u32  parentOrExt; // +0x110  (#68) dir: parent ptr    / leaf: binExtIdx
    guild::u32  leafNext;    // +0x114  (#69) dir: leaf list head / leaf: next leaf
    guild::u8   isDir;       // +0x118  finalized/dir flag (set by BuildFinishedFileList)
    guild::u8   pad119[3];   // +0x119  alignment
};

// A finalized 24-byte file-array entry (VIBE_Vfs_BuildFinishedFileList /
// FindFileRecursive). Offsets confirmed from BuildFinishedFileList writes.
struct VfsFileEntry {
    char*       name;        // +0x00  pointer into the entry block's name pool
    guild::u32  dosTime;     // +0x04  DOS-packed mtime (from leaf +256)
    guild::u32  archiveNode; // +0x08  archive node ptr (from leaf +260), or 0
    guild::u32  field12;     // +0x0C  (from leaf +264)
    guild::u32  field16;     // +0x10  (from leaf +268)
    guild::u32  binExtIdx;   // +0x14  .BIN extension index (from leaf +272)
};

// --- module-global tree state (mirrors the binary) -------------------------
// VIBE_Vfs_Init binds the host filesystem and the case mode, then scans.
bool VfsTreeInit(guild::shim::IFileSystem* fs, const char* rootPath,
                 bool caseInsensitive);
// VIBE_Vfs_Shutdown — frees the tree and clears state.
void VfsTreeShutdown();
// Accessors for the mirrored globals (testing / introspection).
VfsNode* VfsRoot();
bool     VfsCaseInsensitive();

// --- tree operations (exposed for testing; pointers are real VfsNode*) -----

// VIBE_Vfs_GetOrCreateSubDir @0x44ebcc — return the child directory `name` of
// `parent`, creating it if absent. `parent` may be null (then a detached node).
VfsNode* GetOrCreateSubDir(const char* name, VfsNode* parent);

// VIBE_Vfs_AddFileSorted @0x44ed58 — attach a file leaf named `name` onto
// directory node `dir`'s pre-flatten leaf list (or update an existing leaf).
// `archiveNode`/`f1`/`f2`/`binExtIdx`/`dosTime` populate the leaf fields.
void AddFileSorted(const char* name, VfsNode* dir, guild::u32 f2,
                   VfsNode* archiveNode, guild::u32 f1, guild::u32 binExtIdx,
                   guild::u32 dosTime);

// VIBE_Vfs_BuildFinishedFileList @0x44f6b0 — flatten each dir node's leaf list
// into a sorted VfsFileEntry array, freeing the leaves; recurse if `recurse`.
void BuildFinishedFileList(VfsNode* dir, bool recurse);

// VIBE_Vfs_FindFileRecursive @0x44eca8 — binary-search `name` in `dir`'s file
// array; if `recurse`, descend into child dirs. Returns the entry, and sets
// `*foundDir` to the owning dir. Null if not found.
VfsFileEntry* FindFileRecursive(const char* name, VfsNode* dir, bool recurse,
                                VfsNode** foundDir);

// VIBE_Vfs_NormalizeDirPath @0x44f88c — given a slash-separated directory path
// (e.g. "a/b/c/"), walk `start`'s child-dir chain component by component.
// Returns the deepest matched dir node, or null if a component is missing.
VfsNode* NormalizeDirPath(const char* path, VfsNode* start);

// VIBE_Vfs_ResolvePath @0x44fa5c — resolve a full path to a file entry. Returns
// the entry and sets *outDir to its dir; null if not found.
VfsFileEntry* ResolvePath(const char* path, VfsNode* root, VfsNode** outDir);

// VIBE_Vfs_BuildFullPath @0x44fbcc — write `node`'s path (relative to `stop`)
// into `out`. If `withChild` is true the result ends with the child separator.
char* BuildFullPath(VfsNode* node, VfsNode* stop, char* out, bool dirSep);

// VIBE_Vfs_BuildParentPath @0x44fdb4 — write the concatenation of `node`'s
// ancestor names (up to but excluding `stop`) into `out`.
char* BuildParentPath(VfsNode* node, VfsNode* stop, char* out);

// VIBE_Vfs_FreeNodeTree @0x450868 — recursively free a dir-node subtree.
void FreeNodeTree(VfsNode* node);

// VIBE_Vfs_ScanDirectory @0x450234 — recursively scan `dirPath` under host FS,
// building dir nodes + file leaves under `parent` (null = create root). Returns
// the dir node the scan populated.
VfsNode* ScanDirectory(const char* dirPath, VfsNode* parent);

// --- file-array accessors (the dir node's finalized +0x104 array) ----------
// The finalized file array is a registry-backed heap block (see vfs_tree.cpp).
// These give read access to it without exposing the internal id registry, so the
// save-browser enumerator (and friends) can walk a directory's files the way the
// original does (count @+0x100, array @+0x104, 24-byte entries).
int           DirFileCount(VfsNode* dir);
VfsFileEntry* DirFileAt(VfsNode* dir, int index);
VfsFileEntry* ArrayOf(guild::u32 arrayId);   // raw +0x104 array-id resolver

// Free the registry-backed +0x104 file-array block owned by `dir` (if any) and
// null the slot. The block is allocated in BuildFinishedFileList; this lets a
// mutation that empties a directory release it without exposing the id registry.
void FreeNodeArray(VfsNode* dir);

// VIBE_Vfs_GetFileName_Thunk @0x44f698 — comparator used to bsearch a file array:
// compares the search key `key` against entry `e`'s name (e->name == *e). Returns
// strcmp-style sign.
int GetFileNameCmp(const char* key, const VfsFileEntry* e);

// VIBE_Vfs_ResolveAndBuildPath @0x4500a0 — resolve `path` to a file entry, then
// write that entry's full path into `out` (BuildFullPath, ending in the child
// separator iff `dirSep`). Returns `out` on success, null if the path is unknown.
char* ResolveAndBuildPath(const char* path, VfsNode* root, char* out, bool dirSep);

// VIBE_Vfs_MatchPathPrefix @0x44fe88 — given an absolute `path`, peel the longest
// matching ancestor directory prefix and write the remaining tail into `out`. The
// match starts from `node` and walks UP its parent chain (slot +0x110) until the
// current root candidate's path is a prefix of `path`; the unmatched suffix is the
// result. If no root is bound the whole (normalized) path is copied to `out`.
// Returns `out`, or null if a bound root failed to match. (Normalization upper-cases
// when NOT case-insensitive and converts backslashes to slashes, like ResolvePath.)
char* MatchPathPrefix(const char* path, VfsNode* node, char* out);

// VIBE_Vfs_WalkFileTree @0x451e78 — depth-first walk of the dir tree rooted at
// `start` (null == g_vfsRoot). For every file the visitor `fn(entry, ctx)` is
// invoked; returning false aborts the whole walk (which then returns false). If
// `extFilter` is non-null only files whose extension (substring after the last '.')
// case-insensitively matches it are visited. `walkSiblings` mirrors the original
// dl flag: when false, only the start node (not its siblings) is walked. Returns
// true if the walk completed without an abort.
using VfsFileVisitor = bool (*)(const VfsFileEntry* entry, void* ctx);
bool WalkFileTree(VfsNode* start, bool walkSiblings, VfsFileVisitor fn,
                  const char* extFilter, void* ctx);

} // namespace guild::io
