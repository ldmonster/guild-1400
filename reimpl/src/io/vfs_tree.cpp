#include "io/vfs_tree.h"
#include "io/path.h"

#include <cstdint>
#include <cstring>
#include <new>
#include <unordered_map>

namespace guild::io {

// .BIN extension table @0x44E8F0 — exact bytes recovered via get_bytes.
const char kBinExtTable[7][6] = {
    {'.', 'B', 'I', 'N', '5', '\0'},
    {'.', 'B', 'I', 'N', '4', '\0'},
    {'.', 'B', 'I', 'N', '3', '\0'},
    {'.', 'B', 'I', 'N', '2', '\0'},
    {'.', 'B', 'I', 'N', '1', '\0'},
    {'.', 'B', 'I', 'N', '0', '\0'},
    {'.', 'B', 'I', 'N', '\0', '\0'},
};

// --- module-global state (mirrors dword_62EB78 / byte_62EB84) ---------------
namespace {
guild::shim::IFileSystem* g_fs = nullptr;
VfsNode* g_root = nullptr;            // dword_62EB78
bool     g_caseInsensitive = false;  // byte_62EB84

// The original stored 32-bit VfsNode* values directly in the +0x104..+0x114
// link slots. On a 64-bit host those slots cannot hold a native pointer, so we
// keep the byte layout identical (a u32 per slot) but use the slot value as a
// non-zero registry id; the live VfsNode* is held in a side table. This mirrors
// the technique already used by VfsHandle in vfs.cpp.
guild::u32 g_nextNodeId = 1;
std::unordered_map<guild::u32, VfsNode*> g_nodeReg;

guild::u32 RegNode(VfsNode* p) {
    if (!p) return 0;
    guild::u32 id = g_nextNodeId++;
    if (g_nextNodeId == 0) g_nextNodeId = 1;
    g_nodeReg[id] = p;
    return id;
}
VfsNode* Node(guild::u32 id) {
    if (!id) return nullptr;
    auto it = g_nodeReg.find(id);
    return it == g_nodeReg.end() ? nullptr : it->second;
}
guild::u32 IdOf(VfsNode* p) {
    if (!p) return 0;
    for (auto& kv : g_nodeReg) if (kv.second == p) return kv.first;
    return RegNode(p);
}
void UnregNode(VfsNode* p) {
    if (!p) return;
    for (auto it = g_nodeReg.begin(); it != g_nodeReg.end(); ++it)
        if (it->second == p) { g_nodeReg.erase(it); return; }
}

// Same trick for the flattened 24-byte file-array block held in the dir node's
// +0x104 slot (it is a heap pointer in the original, stored in a u32 field here).
guild::u32 g_nextArrayId = 1;
std::unordered_map<guild::u32, char*> g_arrayReg;
guild::u32 RegArray(char* p) {
    if (!p) return 0;
    guild::u32 id = g_nextArrayId++;
    if (g_nextArrayId == 0) g_nextArrayId = 1;
    g_arrayReg[id] = p;
    return id;
}
VfsFileEntry* ArrayById(guild::u32 id) {
    if (!id) return nullptr;
    auto it = g_arrayReg.find(id);
    return it == g_arrayReg.end() ? nullptr : reinterpret_cast<VfsFileEntry*>(it->second);
}
void FreeArray(guild::u32 id) {
    auto it = g_arrayReg.find(id);
    if (it != g_arrayReg.end()) { delete[] it->second; g_arrayReg.erase(it); }
}

// VIBE_Util_StrToUpper @0x5e9f50 — upper-case in place (ASCII).
void StrToUpper(char* s) {
    for (; *s; ++s) {
        char c = *s;
        if (c >= 'a' && c <= 'z')
            *s = static_cast<char>(c - 32);
    }
}

// Case-sensitivity-aware compare. case-insensitive -> StrCmpNoCase_Thunk; else
// exact StrCmp. Returns strcmp-style sign.
int VfsStrCmp(const char* a, const char* b) {
    if (g_caseInsensitive) {
        for (;; ++a, ++b) {
            unsigned char ca = static_cast<unsigned char>(*a);
            unsigned char cb = static_cast<unsigned char>(*b);
            if (ca >= 'a' && ca <= 'z') ca = static_cast<unsigned char>(ca - 32);
            if (cb >= 'a' && cb <= 'z') cb = static_cast<unsigned char>(cb - 32);
            if (ca != cb) return ca < cb ? -1 : 1;
            if (ca == 0) return 0;
        }
    }
    return std::strcmp(a, b);
}

// VIBE_Util_StrCmpNoCase — ASCII case-insensitive compare (always), independent of
// the VFS case mode. Returns 0 on equal.
int VfsStrCmpNoCase(const char* a, const char* b) {
    for (;; ++a, ++b) {
        unsigned char ca = static_cast<unsigned char>(*a);
        unsigned char cb = static_cast<unsigned char>(*b);
        if (ca >= 'a' && ca <= 'z') ca = static_cast<unsigned char>(ca - 32);
        if (cb >= 'a' && cb <= 'z') cb = static_cast<unsigned char>(cb - 32);
        if (ca != cb) return ca < cb ? -1 : 1;
        if (ca == 0) return 0;
    }
}

// Allocate a zeroed node + register it so its slot ids resolve.
VfsNode* AllocNode() {
    VfsNode* n = new (std::nothrow) VfsNode();
    if (n) { std::memset(n, 0, sizeof(VfsNode)); RegNode(n); }
    return n;
}

// VIBE_Util_StrNCopyPad — copy up to `max` chars, always NUL-terminating.
void StrNCopyPad(char* dst, const char* src, std::size_t max) {
    std::size_t i = 0;
    for (; i < max && src[i]; ++i) dst[i] = src[i];
    dst[i] = '\0';
}
} // namespace

bool VfsCaseInsensitive() { return g_caseInsensitive; }
VfsNode* VfsRoot() { return g_root; }

// ---------------------------------------------------------------------------
// gilde.exe 0x44ebcc — VIBE_Vfs_GetOrCreateSubDir  (__usercall, eax=name, edx=parent).
VfsNode* GetOrCreateSubDir(const char* name, VfsNode* parent) {
    if (parent) {
        VfsNode* cur = Node(parent->childOrF2);       // +268 first child
        while (cur) {
            if (VfsStrCmp(name, cur->name) == 0)
                return cur;
            cur = Node(cur->siblingOrF1);              // +264 sibling
        }
    }
    VfsNode* node = AllocNode();
    if (!node) return nullptr;
    StrNCopyPad(node->name, name, sizeof(node->name) - 1);
    node->isDir = 0;                                   // v6[280] = 0
    node->parentOrExt = IdOf(parent);                  // +272 = parent
    if (!parent)
        return node;
    if (!parent->childOrF2) {                          // first child
        parent->childOrF2 = IdOf(node);
        return node;
    }
    VfsNode* tail = Node(parent->childOrF2);
    while (tail->siblingOrF1)
        tail = Node(tail->siblingOrF1);
    tail->siblingOrF1 = IdOf(node);
    return node;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x44ed58 — VIBE_Vfs_AddFileSorted  (the pre-flatten leaf-insert path).
void AddFileSorted(const char* name, VfsNode* dir, guild::u32 f2,
                   VfsNode* archiveNode, guild::u32 f1, guild::u32 binExtIdx,
                   guild::u32 dosTime) {
    if (!dir) return;
    VfsNode* prev = nullptr;
    VfsNode* cur = Node(dir->leafNext);                // +276 leaf chain
    while (cur) {
        if (VfsStrCmp(name, cur->name) == 0) {
            guild::u32 oldTime = cur->countOrTime;     // +256
            if (dosTime > oldTime || dosTime == 0xFFFFFFFFu || binExtIdx < cur->parentOrExt) {
                cur->countOrTime = dosTime;
                cur->arrayOrArch = IdOf(archiveNode);
                cur->childOrF2   = f1;
                cur->siblingOrF1 = f2;
                cur->parentOrExt = binExtIdx;
            } else if (oldTime == dosTime && !archiveNode) {
                cur->arrayOrArch = 0;
                cur->childOrF2   = 0;
                cur->siblingOrF1 = 0;
                cur->parentOrExt = 0;
            }
            return;
        }
        prev = cur;
        cur = Node(cur->leafNext);
    }
    VfsNode* leaf = AllocNode();
    if (!leaf) return;
    StrNCopyPad(leaf->name, name, 255);
    leaf->leafNext     = 0;
    leaf->arrayOrArch  = IdOf(archiveNode);            // +260 (#65)
    leaf->childOrF2    = f1;                            // +268 (#67) = a3
    leaf->siblingOrF1  = f2;                            // +264 (#66) = a5
    leaf->parentOrExt  = binExtIdx;                     // +272 (#68)
    leaf->countOrTime  = dosTime;                       // +256 (#64) = a7
    if (dir->leafNext) {
        if (prev) prev->leafNext = IdOf(leaf);
        else dir->leafNext = IdOf(leaf);
    } else {
        dir->leafNext = IdOf(leaf);
    }
}

// ---------------------------------------------------------------------------
// gilde.exe 0x44f6b0 — VIBE_Vfs_BuildFinishedFileList.
namespace {
int FileEntryCmp(const VfsFileEntry& a, const VfsFileEntry& b) {
    return VfsStrCmp(a.name, b.name);
}
void SortEntries(VfsFileEntry* arr, guild::u32 n) {
    for (guild::u32 i = 1; i < n; ++i) {
        VfsFileEntry key = arr[i];
        guild::u32 j = i;
        while (j > 0 && FileEntryCmp(arr[j - 1], key) > 0) {
            arr[j] = arr[j - 1];
            --j;
        }
        arr[j] = key;
    }
}
} // namespace

void BuildFinishedFileList(VfsNode* dir, bool recurse) {
    VfsNode* node = dir;
    while (node) {
        VfsNode* head = Node(node->leafNext);          // +276
        if (head && !node->isDir) {
            guild::u32 count = 0;
            std::size_t nameBytes = 0;
            for (VfsNode* l = head; l; l = Node(l->leafNext)) {
                nameBytes += std::strlen(l->name) + 1;
                ++count;
            }
            node->countOrTime = count;                 // +256
            // Original packs count*24 entries + a name pool in one block. Our
            // VfsFileEntry has a native (8-byte) name pointer, so we size by
            // sizeof(VfsFileEntry); the on-disk 24-byte stride is documented in
            // the header. Names live in a separate pool keyed off the same id.
            const std::size_t stride = sizeof(VfsFileEntry);
            char* block = new char[stride * count + nameBytes];
            VfsFileEntry* arr = reinterpret_cast<VfsFileEntry*>(block);
            char* np = block + stride * count;
            node->arrayOrArch = RegArray(block);
            guild::u32 i = 0;
            for (VfsNode* l = head; l;) {
                std::strcpy(np, l->name);
                arr[i].name        = np;
                arr[i].dosTime     = l->countOrTime;   // leaf +256
                arr[i].archiveNode = l->arrayOrArch;   // leaf +260
                arr[i].field16     = l->childOrF2;     // leaf +268
                arr[i].field12     = l->siblingOrF1;   // leaf +264
                arr[i].binExtIdx   = l->parentOrExt;   // leaf +272
                np += std::strlen(l->name) + 1;
                ++i;
                VfsNode* next = Node(l->leafNext);
                UnregNode(l);
                delete l;                              // MemPool_Free(leaf)
                l = next;
            }
            node->leafNext = 0;
            SortEntries(arr, count);
        } else if (!head) {
            node->countOrTime = 0;
        }
        node->isDir = 1;
        if (!recurse)
            break;
        VfsNode* child = Node(node->childOrF2);        // +268
        if (child)
            BuildFinishedFileList(child, true);
        VfsNode* sibling = Node(node->siblingOrF1);    // +264
        if (!sibling)
            break;
        node = sibling;
    }
}

// ---------------------------------------------------------------------------
// gilde.exe 0x44eca8 — VIBE_Vfs_FindFileRecursive.
VfsFileEntry* FindFileRecursive(const char* name, VfsNode* dir, bool recurse,
                                VfsNode** foundDir) {
    if (!name || !*name)
        return nullptr;
    VfsFileEntry* result = nullptr;
    if (dir && static_cast<int>(dir->countOrTime) > 0 && dir->isDir) {
        VfsFileEntry* arr = ArrayById(dir->arrayOrArch);
        if (arr) {
            int lo = 0, hi = static_cast<int>(dir->countOrTime) - 1;
            while (lo <= hi) {
                int mid = (lo + hi) >> 1;
                int c = VfsStrCmp(name, arr[mid].name);
                if (c == 0) { result = &arr[mid]; break; }
                if (c < 0) hi = mid - 1; else lo = mid + 1;
            }
        }
    }
    if (result) {
        if (foundDir) *foundDir = dir;
        return result;
    }
    if (!dir || !recurse)
        return nullptr;
    VfsNode* child = Node(dir->childOrF2);             // +268
    while (child) {
        result = FindFileRecursive(name, child, true, foundDir);
        if (result)
            return result;
        child = Node(child->siblingOrF1);              // +264
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x44f88c — VIBE_Vfs_NormalizeDirPath.
VfsNode* NormalizeDirPath(const char* path, VfsNode* start) {
    if (!path) return nullptr;
    if (!*path) return start;

    char work[256];
    StrNCopyPad(work, path, sizeof(work) - 1);
    if (!g_caseInsensitive)
        StrToUpper(work);
    std::size_t wl = std::strlen(work);
    if (wl == 0 || work[wl - 1] != '/') {
        if (wl + 1 < sizeof(work)) { work[wl] = '/'; work[wl + 1] = '\0'; }
    }

    char comp[264];
    std::strcpy(comp, work);
    const char* sep = std::strchr(comp, '/');
    if (!sep)
        return start;

    VfsNode* dir = start;
    while (dir) {
        char name[264];
        std::size_t clen = static_cast<std::size_t>(sep - comp);
        std::memcpy(name, comp, clen);
        name[clen] = '\0';
        VfsNode* match = nullptr;
        for (VfsNode* c = Node(dir->childOrF2); c; c = Node(c->siblingOrF1)) {
            if (VfsStrCmp(name, c->name) == 0) { match = c; break; }
        }
        dir = match;
        if (!dir)
            return nullptr;
        // sep points into comp; this is an in-place left-shift of the tail.
        // strcpy with overlapping src/dst is UB — memmove is byte-identical here.
        std::memmove(comp, sep + 1, std::strlen(sep + 1) + 1);
        sep = std::strchr(comp, '/');
        if (!sep)
            return dir;
    }
    return dir;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x44fa5c — VIBE_Vfs_ResolvePath.
VfsFileEntry* ResolvePath(const char* path, VfsNode* root, VfsNode** outDir) {
    char work[256];
    StrNCopyPad(work, path, sizeof(work) - 1);
    if (!g_caseInsensitive)
        StrToUpper(work);
    ConvertBackslashToSlash(work);

    char* p = work;
    while (*p == '/') ++p;
    if (p != work) {
        char tmp[256];
        std::strcpy(tmp, p);
        std::strcpy(work, tmp);
    }

    VfsNode* dir = root;
    char leaf[256];
    std::strcpy(leaf, work);
    const char* firstSlash = std::strchr(work, '/');
    if (firstSlash) {
        std::strcpy(leaf, firstSlash + 1);
        char dirPath[256];
        std::size_t dl = static_cast<std::size_t>(firstSlash + 1 - work);
        std::memcpy(dirPath, work, dl);
        dirPath[dl] = '\0';
        dir = NormalizeDirPath(dirPath, root);
        if (!dir)
            return nullptr;
    }
    bool recurse = (leaf[0] == '*');
    const char* searchName = recurse ? (leaf + 1) : leaf;
    VfsNode* fd = nullptr;
    VfsFileEntry* e = FindFileRecursive(searchName, dir, recurse, &fd);
    if (e && outDir) *outDir = fd;
    return e;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x44fdb4 — VIBE_Vfs_BuildParentPath.
char* BuildParentPath(VfsNode* node, VfsNode* stop, char* out) {
    if (!node) return nullptr;
    std::strcpy(out, node->name);
    VfsNode* cur = node;
    while (cur) {
        if (stop == Node(cur->parentOrExt))
            break;
        cur = Node(cur->parentOrExt);                  // +272
        if (!cur) break;
        char tmp[268];
        std::strcpy(tmp, cur->name);
        std::strcat(tmp, out);
        std::strcpy(out, tmp);
    }
    return out;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x44fbcc — VIBE_Vfs_BuildFullPath.
char* BuildFullPath(VfsNode* node, VfsNode* stop, char* out, bool dirSep) {
    if (!node) return nullptr;
    if (stop == node)
        return out;
    std::strcpy(out, node->name);
    if (!dirSep && std::strlen(out) > 0)
        out[std::strlen(out) - 1] = '\0';
    VfsNode* cur = node;
    while (cur->parentOrExt) {
        if (stop == Node(cur->parentOrExt))
            break;
        cur = Node(cur->parentOrExt);
        char tmp[268];
        std::strcpy(tmp, cur->name);
        std::strcat(tmp, out);
        std::strcpy(out, tmp);
    }
    return out;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x450868 — VIBE_Vfs_FreeNodeTree.
void FreeNodeTree(VfsNode* node) {
    while (node) {
        VfsNode* leaf = Node(node->leafNext);          // +276
        while (leaf) {
            VfsNode* next = Node(leaf->leafNext);
            UnregNode(leaf);
            delete leaf;
            leaf = next;
        }
        VfsNode* sibling = Node(node->siblingOrF1);    // +264
        if (node->arrayOrArch) {                        // +260 file array
            FreeArray(node->arrayOrArch);
            node->arrayOrArch = 0;
        }
        VfsNode* child = Node(node->childOrF2);         // +268
        if (child)
            FreeNodeTree(child);
        UnregNode(node);
        delete node;
        node = sibling;
    }
}

// ---------------------------------------------------------------------------
static int BinExtIndex(const char* dotPart) {
    for (int i = 0; i < kBinExtCount; ++i) {
        if (VfsStrCmp(dotPart, kBinExtTable[i]) == 0)
            return i;
    }
    return -1;
}

// gilde.exe 0x450234 — VIBE_Vfs_ScanDirectory.
VfsNode* ScanDirectory(const char* dirPath, VfsNode* parent) {
    if (!g_fs)
        return nullptr;

    char path[256];
    StrNCopyPad(path, dirPath, sizeof(path) - 1);
    if (!g_caseInsensitive)
        StrToUpper(path);
    if (!parent)
        ConvertBackslashToSlash(path);

    char subName[256];
    {
        char tmp[256];
        std::strcpy(tmp, path);
        std::size_t tl = std::strlen(tmp);
        if (tl && tmp[tl - 1] == '/') tmp[tl - 1] = '\0';
        char* slash = std::strrchr(tmp, '/');
        std::strcpy(subName, slash ? slash + 1 : tmp);
    }
    VfsNode* dir = GetOrCreateSubDir(subName, parent);
    if (!dir)
        return nullptr;

    guild::shim::IDirListing* listing = g_fs->listDir(dirPath);
    if (listing) {
        for (std::size_t i = 0; i < listing->count(); ++i) {
            const guild::shim::DirEntry& e = listing->at(i);
            if (std::strcmp(e.name, ".") == 0 || std::strcmp(e.name, "..") == 0)
                continue;
            if (e.isDir) {
                char childPath[256];
                std::strcpy(childPath, dirPath);
                std::size_t cl = std::strlen(childPath);
                if (cl && childPath[cl - 1] != '/') { childPath[cl] = '/'; childPath[cl + 1] = '\0'; }
                std::strcat(childPath, e.name);
                ScanDirectory(childPath, dir);
            } else {
                char fileName[256];
                std::strcpy(fileName, e.name);
                if (!g_caseInsensitive)
                    StrToUpper(fileName);
                const char* dot = std::strchr(fileName, '.');
                if (dot && BinExtIndex(dot) >= 0) {
                    // .BIN archive: record it as a child dir so its members can
                    // be addressed by path (member listing is the zip layer's
                    // job at open time).
                    ScanDirectory(fileName, dir);
                }
                AddFileSorted(fileName, dir, 0, nullptr, 0, 0, e.dosTime);
            }
        }
        delete listing;
    }

    if (!parent && dir)
        BuildFinishedFileList(dir, true);
    return dir;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x451f98 — VIBE_Vfs_Init.
bool VfsTreeInit(guild::shim::IFileSystem* fs, const char* rootPath,
                 bool caseInsensitive) {
    g_fs = fs;
    g_caseInsensitive = caseInsensitive;               // byte_62EB84
    if (!fs) { g_root = nullptr; return false; }
    g_root = ScanDirectory(rootPath, nullptr);         // dword_62EB78
    return g_root != nullptr;
}

// gilde.exe 0x452004 — VIBE_Vfs_Shutdown.
void VfsTreeShutdown() {
    if (g_root) {
        FreeNodeTree(g_root);
        g_root = nullptr;
    }
    g_fs = nullptr;
}

// ---------------------------------------------------------------------------
// File-array accessors. The internal id registry stays private; these expose just
// enough for the save-browser enumerator / WalkFileTree to read a dir's files.
VfsFileEntry* ArrayOf(guild::u32 arrayId) { return ArrayById(arrayId); }

void FreeNodeArray(VfsNode* dir) {
    if (!dir || !dir->arrayOrArch) return;
    FreeArray(dir->arrayOrArch);
    dir->arrayOrArch = 0;
}

int DirFileCount(VfsNode* dir) {
    return dir ? static_cast<int>(dir->countOrTime) : 0;   // +0x100
}
VfsFileEntry* DirFileAt(VfsNode* dir, int index) {
    if (!dir) return nullptr;
    if (index < 0 || index >= static_cast<int>(dir->countOrTime))
        return nullptr;
    VfsFileEntry* arr = ArrayById(dir->arrayOrArch);        // +0x104
    return arr ? &arr[index] : nullptr;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x44f698 — VIBE_Vfs_GetFileName_Thunk: compare a search key against a
// file entry's name. The original is `VIBE_Util_StrCmp(a1, *a2)` where *a2 is the
// entry's name pointer (entry +0).
int GetFileNameCmp(const char* key, const VfsFileEntry* e) {
    return std::strcmp(key, e->name);
}

// ---------------------------------------------------------------------------
// gilde.exe 0x4500a0 — VIBE_Vfs_ResolveAndBuildPath.
//   result = ResolvePath(path, root, &dir); if (result) BuildFullPath(result, dir,
//   out, dirSep), return out; else return null.
char* ResolveAndBuildPath(const char* path, VfsNode* root, char* out, bool dirSep) {
    VfsNode* dir = nullptr;
    VfsFileEntry* e = ResolvePath(path, root, &dir);
    if (!e)
        return nullptr;
    // The original passes the resolved file entry to BuildFullPath; here the entry's
    // owning node chain is the dir, so we render the dir's path then append the file
    // name to reproduce the full resolved path.
    BuildFullPath(dir, dir ? Node(dir->parentOrExt) : nullptr, out, dirSep);
    std::strcat(out, e->name);
    return out;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x44fe88 — VIBE_Vfs_MatchPathPrefix.
char* MatchPathPrefix(const char* path, VfsNode* node, char* out) {
    char work[256];                                         // v33
    StrNCopyPad(work, path, sizeof(work) - 1);
    if (!g_caseInsensitive)                                 // if (!byte_62EB84)
        StrToUpper(work);
    ConvertBackslashToSlash(work);

    VfsNode* root = g_root;                                 // v36 = dword_62EB78
    if (root) {
        while (true) {
            int wl = static_cast<int>(std::strlen(work));   // v7/v8
            int rootLen = static_cast<int>(std::strlen(root->name)); // strlen(v36)+1-1
            // Match `work` against root->name up to rootLen chars (case-aware).
            int m = 0;                                      // v11
            if (wl > 0) {
                const char* rp = root->name;
                if (g_caseInsensitive) {
                    while (m < rootLen && m < wl) {
                        unsigned char ca = static_cast<unsigned char>(*rp);
                        unsigned char cb = static_cast<unsigned char>(work[m]);
                        if (ca >= 'a' && ca <= 'z') ca = static_cast<unsigned char>(ca - 32);
                        if (cb >= 'a' && cb <= 'z') cb = static_cast<unsigned char>(cb - 32);
                        if (ca != cb) break;
                        ++m; ++rp;
                    }
                } else {
                    while (m < rootLen && m < wl) {
                        if (*rp != work[m]) break;
                        ++m; ++rp;
                    }
                }
            }
            if (m != rootLen)                               // not a full prefix
                return nullptr;
            // tail = work + m; rewrite work to the tail.
            char tail[256];                                 // v32
            StrNCopyPad(tail, &work[m], sizeof(tail) - 1);
            std::strcpy(work, tail);
            // Walk UP node's parent chain to the child of the current root candidate.
            VfsNode* v22 = node;
            while (v22) {
                if (root == Node(v22->parentOrExt))         // *(v22+272) == v36
                    break;
                v22 = Node(v22->parentOrExt);
            }
            root = v22;                                     // v36 = v22
            if (!v22)
                break;                                      // goto LABEL_24
        }
    }
    // LABEL_24: copy the remaining work into out.
    std::strcpy(out, work);
    return out;
}

// ---------------------------------------------------------------------------
// gilde.exe 0x451e78 — VIBE_Vfs_WalkFileTree.
bool WalkFileTree(VfsNode* start, bool walkSiblings, VfsFileVisitor fn,
                  const char* extFilter, void* ctx) {
    if (!fn)                                                // if (!a3) return 0;
        return false;
    VfsNode* v5 = start ? start : g_root;                  // if (!a1) v5 = g_vfsRoot
    while (true) {
        if (!v5 || !v5->isDir)                              // !v5 || !*(v5+280)
            return true;                                    // return 1
        if (extFilter) {                                    // if (a4) break;
            VfsFileEntry* arr = ArrayOf(v5->arrayOrArch);   // *(v5+260)
            int n = static_cast<int>(v5->countOrTime);      // *(v5+256)
            for (int i = 0; arr && i < n; ++i) {
                const char* dot = std::strchr(arr[i].name, '.');
                if (dot) {
                    // !StrCmpNoCase(dot+1, extFilter) — bare extension match
                    if (VfsStrCmpNoCase(dot + 1, extFilter) == 0) {
                        if (!fn(&arr[i], ctx))
                            return false;
                    }
                }
            }
        } else {                                            // unfiltered: visit all
            int n = static_cast<int>(v5->countOrTime);
            VfsFileEntry* arr = ArrayOf(v5->arrayOrArch);
            for (int i = 0; i < n; ++i) {
                const VfsFileEntry* e = arr ? &arr[i] : nullptr;
                if (!fn(e, ctx))                            // result = a3(); if (!result) return
                    return false;
            }
        }
        // LABEL_10: recurse into the child subtree, then advance to sibling.
        VfsNode* child = Node(v5->childOrF2);               // *(v5+268)
        if (child)
            WalkFileTree(child, true, fn, extFilter, ctx);
        if (walkSiblings)                                   // if (a2)
            v5 = Node(v5->siblingOrF1);                     // *(v5+264)
        else
            v5 = nullptr;
    }
}

} // namespace guild::io
