// End-to-end: drive the file_ops OS layer over an in-memory IFileSystem mock.
// Flow: makeDir -> CheckAccess(missing/present) -> listDir -> map find data ->
//       attribute-mask filter, mirroring the original VFS directory scan path.
#include "io/file_ops.h"
#include "io/file_buffered.h"   // FileCreateDirectory (VIBE_File_CreateDirectory)
#include "shim/IFileSystem.h"
#include "test.h"

#include <cstring>
#include <string>
#include <vector>

using namespace guild::io;
using guild::shim::DirEntry;
using guild::shim::IDirListing;
using guild::shim::IFile;
using guild::shim::IFileSystem;

namespace {

// Minimal in-memory backend: tracks created directories and a fixed set of
// directory entries, enough for the CreateDirectory / CheckAccess / listDir
// surface this module uses.
struct MockListing : IDirListing {
    std::vector<DirEntry>    entries;
    std::vector<std::string> names;  // owns the name storage
    std::size_t count() const override { return entries.size(); }
    const DirEntry& at(std::size_t i) const override { return entries[i]; }
};

struct MockFs : IFileSystem {
    std::vector<std::string> dirs;
    // listDir payload: leaf name, isDir, dosTime
    struct Item { std::string name; bool isDir; guild::u32 dosTime; };
    std::vector<Item> items;

    IFile* open(const char*, const char*) override { return nullptr; }
    void   close(IFile*) override {}
    bool   exists(const char* path) override {
        for (auto& d : dirs)
            if (d == path) return true;
        for (auto& it : items)
            if (it.name == path) return true;
        return false;
    }
    bool makeDir(const char* path) override {
        dirs.push_back(path);
        return true;
    }
    IDirListing* listDir(const char*) override {
        auto* l = new MockListing();
        l->names.reserve(items.size());
        for (auto& it : items)
            l->names.push_back(it.name);
        for (std::size_t i = 0; i < items.size(); ++i)
            l->entries.push_back(DirEntry{l->names[i].c_str(), items[i].isDir,
                                          items[i].dosTime});
        return l;
    }
};

} // namespace

TEST(FileOpsE2E, ScanFlow) {
    MockFs fs;

    // 1) Directory does not exist yet -> CheckAccess fails, CreateDirectory ok.
    CHECK_EQ(CheckAccess(&fs, "save", 0x02), -1);
    CHECK_EQ(FileCreateDirectory(&fs, "save"), 0);
    CHECK_EQ(CheckAccess(&fs, "save", 0x02), 0);   // now present

    // 2) Populate the directory listing the host surfaces.
    guild::u16 d = 0, t = 0;
    ConvertFileTimeToDos(DosTime{2026, 6, 5, 12, 0, 0}, &d, &t);
    guild::u32 dos = (static_cast<guild::u32>(d) << 16) | t;
    fs.items.push_back({"game1.sav", false, dos});
    fs.items.push_back({"subdir",    true,  0});

    IDirListing* listing = fs.listDir("save");
    CHECK(listing != nullptr);
    CHECK_EQ(listing->count(), (std::size_t)2);

    // 3) Map each entry into a FindData and apply the attribute-mask filter the
    //    scanner uses (mask 0x37 = readonly|hidden|system|dir|archive).
    int matched = 0;
    for (std::size_t i = 0; i < listing->count(); ++i) {
        FindData fd{};
        CopyFindDataAttributes(listing->at(i), &fd);
        guild::u32 attrs = fd.attributes;
        if (FindEntryMatches(0x37, &attrs))
            ++matched;
    }
    CHECK_EQ(matched, 2);   // file (0x20 archive) + dir (0x10) both pass 0x37

    // 4) Verify the regular file mapped to ARCHIVE and carried the DOS mtime,
    //    and the directory mapped to DIRECTORY.
    FindData file{}, sub{};
    CopyFindDataAttributes(listing->at(0), &file);
    CopyFindDataAttributes(listing->at(1), &sub);
    CHECK_EQ(file.attributes, 0x20);
    CHECK_EQ(file.mtime, dos);
    CHECK(std::strcmp(file.name, "game1.sav") == 0);
    CHECK_EQ(sub.attributes, 0x10);

    // 5) A dir-only mask (0x10) keeps just the subdirectory.
    int dirsOnly = 0;
    for (std::size_t i = 0; i < listing->count(); ++i) {
        FindData fd{};
        CopyFindDataAttributes(listing->at(i), &fd);
        guild::u32 attrs = fd.attributes;
        if (FindEntryMatches(0x10, &attrs))
            ++dirsOnly;
    }
    CHECK_EQ(dirsOnly, 1);

    delete listing;
}

TEST(FileOpsE2E, TempNameAfterMkdir) {
    MockFs fs;
    CHECK_EQ(FileCreateDirectory(&fs, "tmp"), 0);
    char path[64];
    BuildTempFileName(path, "tmp/", 0xCAFEBABEu, 0x07);
    // fold(0xCAFEBABE) = 0xBABE | 0xCAFE = 0xFAFE -> "fafe"
    CHECK(std::strcmp(path, "tmp/tfafe_07.tmp") == 0);
}
