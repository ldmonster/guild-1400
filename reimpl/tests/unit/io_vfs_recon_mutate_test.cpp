// Unit tests for the guild::io VFS mutation slice (vfs_recon_mutate):
//   VIBE_Vfs_CloseFileEntry  @0x450ad8  (delete loose file + drop VFS entry)
//
// Golden vectors for the 24-byte sorted file-array compact-or-free behaviour.
#include "test.h"

#include "io/vfs_recon_mutate.h"
#include "io/vfs_tree.h"

#include "shim/IFileSystem.h"

#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace guild::io;

// ---------------------------------------------------------------------------
// A minimal mock FS so VfsTreeInit can build a real finalized tree for the
// CloseFileEntry array tests.
namespace {

struct RMListing : guild::shim::IDirListing {
    std::vector<std::string> names;
    std::vector<guild::shim::DirEntry> entries;
    std::size_t count() const override { return entries.size(); }
    const guild::shim::DirEntry& at(std::size_t i) const override { return entries[i]; }
};

struct RMFS : guild::shim::IFileSystem {
    struct Child { std::string name; bool isDir; };
    std::map<std::string, std::vector<Child>> dirs;

    void addDir(const std::string& p) { if (!dirs.count(p)) dirs[p] = {}; }
    void addFile(const std::string& dir, const std::string& leaf) {
        addDir(dir); dirs[dir].push_back({leaf, false});
    }

    guild::shim::IFile* open(const char*, const char*) override { return nullptr; }
    void close(guild::shim::IFile* f) override { delete f; }
    bool exists(const char*) override { return false; }
    guild::shim::IDirListing* listDir(const char* path) override {
        auto it = dirs.find(path);
        if (it == dirs.end()) return nullptr;
        RMListing* l = new RMListing();
        for (auto& c : it->second) l->names.push_back(c.name);
        for (std::size_t i = 0; i < it->second.size(); ++i) {
            guild::shim::DirEntry e;
            e.name = l->names[i].c_str();
            e.isDir = it->second[i].isDir;
            l->entries.push_back(e);
        }
        return l;
    }
    bool makeDir(const char*) override { return true; }
};

// --- shared capture state for the delete hook ------------------------------
std::vector<std::string> g_deleted;    // delete calls, in order

void resetCapture() { g_deleted.clear(); }

bool hookDelete(const char* p)  { g_deleted.push_back(p); return true; }

} // namespace

// ===========================================================================
// CloseFileEntry — compacts the sorted file array and deletes from disk.
TEST(VfsReconMutate, CloseFileEntryCompactsArray) {
    resetCapture();
    SetVfsDeleteHook(hookDelete);

    RMFS fs;
    fs.addDir("root");
    fs.addFile("root", "ALPHA.TXT");
    fs.addFile("root", "BETA.TXT");
    fs.addFile("root", "GAMMA.TXT");
    fs.addFile("root", "DELTA.TXT");

    CHECK(VfsTreeInit(&fs, "root", false));
    VfsNode* r = VfsRoot();
    CHECK(r != nullptr);

    // Sorted array: ALPHA, BETA, DELTA, GAMMA (count 4).
    CHECK_EQ(DirFileCount(r), 4);

    // Remove BETA (index 1). Array should become ALPHA, DELTA, GAMMA (count 3),
    // with the on-disk delete invoked exactly once.
    bool ok = CloseFileEntry("BETA.TXT", r);
    CHECK(ok);
    CHECK_EQ(g_deleted.size(), static_cast<std::size_t>(1));
    CHECK_EQ(DirFileCount(r), 3);

    VfsFileEntry* a0 = DirFileAt(r, 0);
    VfsFileEntry* a1 = DirFileAt(r, 1);
    VfsFileEntry* a2 = DirFileAt(r, 2);
    CHECK(a0 && std::strcmp(a0->name, "ALPHA.TXT") == 0);
    CHECK(a1 && std::strcmp(a1->name, "DELTA.TXT") == 0);
    CHECK(a2 && std::strcmp(a2->name, "GAMMA.TXT") == 0);

    // BETA no longer resolvable; ALPHA/DELTA/GAMMA still are.
    VfsNode* od = nullptr;
    CHECK(ResolvePath("BETA.TXT", r, &od) == nullptr);
    CHECK(ResolvePath("DELTA.TXT", r, &od) != nullptr);

    VfsTreeShutdown();
    SetVfsDeleteHook(nullptr);
}

// CloseFileEntry — removing the last entry frees the array (count -> 0) and the
// disk delete still fires.
TEST(VfsReconMutate, CloseFileEntryEmptiesArray) {
    resetCapture();
    SetVfsDeleteHook(hookDelete);

    RMFS fs;
    fs.addDir("d");
    fs.addFile("d", "ONLY.DAT");

    CHECK(VfsTreeInit(&fs, "d", false));
    VfsNode* r = VfsRoot();
    CHECK(r != nullptr);
    CHECK_EQ(DirFileCount(r), 1);

    bool ok = CloseFileEntry("ONLY.DAT", r);
    CHECK(ok);
    CHECK_EQ(g_deleted.size(), static_cast<std::size_t>(1));
    CHECK_EQ(DirFileCount(r), 0);
    CHECK(r->arrayOrArch == 0);          // slot nulled after free

    VfsNode* od = nullptr;
    CHECK(ResolvePath("ONLY.DAT", r, &od) == nullptr);

    VfsTreeShutdown();
    SetVfsDeleteHook(nullptr);
}

// CloseFileEntry — unknown path / missing entry returns false and does not delete.
TEST(VfsReconMutate, CloseFileEntryUnknownPath) {
    resetCapture();
    SetVfsDeleteHook(hookDelete);

    RMFS fs;
    fs.addDir("x");
    fs.addFile("x", "A.TXT");

    CHECK(VfsTreeInit(&fs, "x", false));
    VfsNode* r = VfsRoot();

    CHECK(!CloseFileEntry("NOPE.TXT", r));
    CHECK(g_deleted.empty());
    CHECK_EQ(DirFileCount(r), 1);        // untouched

    VfsTreeShutdown();
    SetVfsDeleteHook(nullptr);
}
