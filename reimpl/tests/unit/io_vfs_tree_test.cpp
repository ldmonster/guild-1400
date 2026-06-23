// Unit tests for guild::io VFS tree / path / buffered-file slice.
#include "test.h"

#include "io/path.h"
#include "io/vfs_tree.h"
#include "io/file_buffered.h"
#include "shim/IFileSystem.h"

#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace guild::io;

// ---------------------------------------------------------------------------
// A mock in-memory filesystem with directory listing, for the scanner + file
// layer. Paths use '/' separators; directories are keyed by their full path.
namespace {

struct MockFile : guild::shim::IFile {
    std::string data;
    std::size_t pos = 0;
    bool writable = false;
    std::string* sink = nullptr;   // for write-back on close

    std::size_t read(void* dst, std::size_t n) override {
        std::size_t avail = data.size() - pos;
        if (n > avail) n = avail;
        std::memcpy(dst, data.data() + pos, n);
        pos += n;
        return n;
    }
    std::size_t write(const void* src, std::size_t n) override {
        if (pos + n > data.size()) data.resize(pos + n);
        std::memcpy(&data[pos], src, n);
        pos += n;
        if (sink) *sink = data;
        return n;
    }
    std::int64_t seek(std::int64_t off, int whence) override {
        std::int64_t base = 0;
        if (whence == SEEK_SET) base = 0;
        else if (whence == SEEK_CUR) base = static_cast<std::int64_t>(pos);
        else if (whence == SEEK_END) base = static_cast<std::int64_t>(data.size());
        std::int64_t t = base + off;
        if (t < 0) return -1;
        pos = static_cast<std::size_t>(t);
        return t;
    }
    std::int64_t tell() override { return static_cast<std::int64_t>(pos); }
    std::int64_t size() override { return static_cast<std::int64_t>(data.size()); }
};

struct MockListing : guild::shim::IDirListing {
    std::vector<std::string> names;
    std::vector<guild::shim::DirEntry> entries;
    std::size_t count() const override { return entries.size(); }
    const guild::shim::DirEntry& at(std::size_t i) const override { return entries[i]; }
};

struct MockFS : guild::shim::IFileSystem {
    // file path -> contents
    std::map<std::string, std::string> files;
    // dir path -> list of (childLeafName, isDir, dosTime)
    struct Child { std::string name; bool isDir; guild::u32 t; };
    std::map<std::string, std::vector<Child>> dirs;

    void addDir(const std::string& path) {
        if (dirs.find(path) == dirs.end()) dirs[path] = {};
    }
    void addFile(const std::string& dir, const std::string& leaf,
                 const std::string& content, guild::u32 t) {
        addDir(dir);
        dirs[dir].push_back({leaf, false, t});
        files[dir + "/" + leaf] = content;
        files[dir + "\\" + leaf] = content; // tolerate either sep on open
    }
    void addSubdir(const std::string& dir, const std::string& leaf) {
        addDir(dir);
        dirs[dir].push_back({leaf, true, 0});
        addDir(dir + "/" + leaf);
    }

    guild::shim::IFile* open(const char* path, const char* mode) override {
        std::string p(path);
        bool w = mode && (mode[0] == 'w' || mode[0] == 'a');
        if (w) {
            MockFile* f = new MockFile();
            f->writable = true;
            f->sink = &files[p];
            files[p] = "";
            return f;
        }
        auto it = files.find(p);
        if (it == files.end()) return nullptr;
        MockFile* f = new MockFile();
        f->data = it->second;
        return f;
    }
    void close(guild::shim::IFile* f) override { delete f; }
    bool exists(const char* path) override { return files.count(path) != 0; }
    guild::shim::IDirListing* listDir(const char* path) override {
        auto it = dirs.find(path);
        if (it == dirs.end()) return nullptr;
        MockListing* l = new MockListing();
        l->names.reserve(it->second.size());
        for (auto& c : it->second) {
            l->names.push_back(c.name);
        }
        for (std::size_t i = 0; i < it->second.size(); ++i) {
            guild::shim::DirEntry e;
            e.name = l->names[i].c_str();
            e.isDir = it->second[i].isDir;
            e.dosTime = it->second[i].t;
            l->entries.push_back(e);
        }
        return l;
    }
    bool makeDir(const char* path) override { addDir(path); return true; }
};

} // namespace

// ---------------------------------------------------------------------------
// path slash conversion (1:1 with VIBE_Path_*)
TEST(IoVfsTreePath, SlashConversion) {
    char a[] = "a\\b\\c";
    CHECK(std::strcmp(ConvertBackslashToSlash(a), "a/b/c") == 0);
    char b[] = "a/b/c";
    CHECK(std::strcmp(ConvertSlashToBackslash(b), "a\\b\\c") == 0);
    char mixed[] = "x:\\engine/gfx\\tex";
    ConvertBackslashToSlash(mixed);
    CHECK(std::strcmp(mixed, "x:/engine/gfx/tex") == 0);
    // null/empty are safe
    CHECK(ConvertBackslashToSlash(nullptr) == nullptr);
    char empty[] = "";
    CHECK(std::strcmp(ConvertBackslashToSlash(empty), "") == 0);
}

// ---------------------------------------------------------------------------
// directory scan builds the expected sorted file index
TEST(IoVfsTreeScan, SortedIndex) {
    MockFS fs;
    fs.addDir("root");
    fs.addFile("root", "ZEBRA.TXT", "z", 100);
    fs.addFile("root", "alpha.txt", "a", 200);   // lower-case -> upper-cased
    fs.addFile("root", "Mango.dat", "m", 300);
    fs.addSubdir("root", "sub");
    fs.addFile("root/sub", "inner.bin", "i", 400);

    // case-insensitive=false -> names upper-cased, exact compare
    CHECK(VfsTreeInit(&fs, "root", false));
    VfsNode* root = VfsRoot();
    CHECK(root != nullptr);
    CHECK(root->isDir == 1);
    // root has 3 files, sorted: ALPHA.TXT, MANGO.DAT, ZEBRA.TXT
    CHECK_EQ(root->countOrTime, 3u);

    VfsNode* fd = nullptr;
    VfsFileEntry* e = FindFileRecursive("ALPHA.TXT", root, false, &fd);
    CHECK(e != nullptr);
    if (e) { CHECK(std::strcmp(e->name, "ALPHA.TXT") == 0); CHECK_EQ(e->dosTime, 200u); }

    e = FindFileRecursive("ZEBRA.TXT", root, false, &fd);
    CHECK(e != nullptr);
    if (e) CHECK_EQ(e->dosTime, 100u);

    // not in root, but recursive search finds it in sub/
    e = FindFileRecursive("MANGO.DAT", root, false, &fd);
    CHECK(e != nullptr);
    e = FindFileRecursive("INNER.BIN", root, false, &fd);
    CHECK(e == nullptr);                       // not in root directly
    e = FindFileRecursive("INNER.BIN", root, true, &fd);
    CHECK(e != nullptr);                       // found recursively
    if (e) CHECK_EQ(e->dosTime, 400u);

    VfsTreeShutdown();
    CHECK(VfsRoot() == nullptr);
}

// ResolvePath: normalize a dir path then find the leaf
TEST(IoVfsTreeResolve, PathResolution) {
    MockFS fs;
    fs.addDir("data");
    fs.addSubdir("data", "textures");
    fs.addFile("data/textures", "wall.tga", "w", 5);
    fs.addSubdir("data", "objects");
    fs.addFile("data/objects", "tree.obj", "t", 7);
    CHECK(VfsTreeInit(&fs, "data", false));
    VfsNode* root = VfsRoot();

    VfsNode* outDir = nullptr;
    // forward slash
    VfsFileEntry* e = ResolvePath("textures/wall.tga", root, &outDir);
    CHECK(e != nullptr);
    if (e) CHECK(std::strcmp(e->name, "WALL.TGA") == 0);
    // backslash equivalent
    e = ResolvePath("objects\\tree.obj", root, &outDir);
    CHECK(e != nullptr);
    if (e) CHECK_EQ(e->dosTime, 7u);
    // leading slash tolerated
    e = ResolvePath("/textures/wall.tga", root, &outDir);
    CHECK(e != nullptr);
    // missing component
    e = ResolvePath("nope/x.tga", root, &outDir);
    CHECK(e == nullptr);

    VfsTreeShutdown();
}

// NormalizeDirPath walks child-dir chain component by component
TEST(IoVfsTreeNormalize, DirWalk) {
    MockFS fs;
    fs.addDir("r");
    fs.addSubdir("r", "a");
    fs.addSubdir("r/a", "b");
    fs.addFile("r/a/b", "leaf.x", "L", 9);
    CHECK(VfsTreeInit(&fs, "r", false));
    VfsNode* root = VfsRoot();

    VfsNode* d = NormalizeDirPath("A/B/", root);
    CHECK(d != nullptr);
    if (d) CHECK(std::strcmp(d->name, "B") == 0);
    // without trailing slash still resolves
    VfsNode* d2 = NormalizeDirPath("A/B", root);
    CHECK(d2 == d);
    // bad component
    CHECK(NormalizeDirPath("A/zzz/", root) == nullptr);

    // BuildParentPath reconstructs the chain a/b/  (relative to root)
    if (d) {
        char buf[300];
        BuildParentPath(d, root, buf);
        // node 'B' parented by 'A' parented by root -> "AB"
        CHECK(std::strstr(buf, "B") != nullptr);
    }
    VfsTreeShutdown();
}

// case-insensitive mode keeps host case (no upper-casing)
TEST(IoVfsTreeCase, CaseInsensitive) {
    MockFS fs;
    fs.addDir("c");
    fs.addFile("c", "MixedCase.Txt", "x", 1);
    CHECK(VfsTreeInit(&fs, "c", true));
    VfsNode* root = VfsRoot();
    VfsNode* fd = nullptr;
    // stored with original case
    VfsFileEntry* e = FindFileRecursive("MixedCase.Txt", root, false, &fd);
    CHECK(e != nullptr);
    // case-insensitive lookup still matches
    e = FindFileRecursive("mixedcase.txt", root, false, &fd);
    CHECK(e != nullptr);
    VfsTreeShutdown();
}

// ---------------------------------------------------------------------------
// buffered file: read/seek/line-read/write roundtrip
TEST(IoVfsTreeFile, BinaryReadSeek) {
    MockFS fs;
    std::string content;
    for (int i = 0; i < 10000; ++i) content.push_back(static_cast<char>(i & 0xFF));
    fs.files["blob.bin"] = content;

    BufferedFile* f = FileOpenBuffered(&fs, "blob.bin", "rb");
    CHECK(f != nullptr);
    char buf[6000];
    std::size_t n = FileRead(buf, 1, 6000, f);   // spans multiple refills
    CHECK_EQ(n, 6000u);
    CHECK(std::memcmp(buf, content.data(), 6000) == 0);
    CHECK_EQ(FileTell(f), 6000L);

    // seek back to 100 and read 4
    CHECK_EQ(FileSeek(f, 100, SEEK_SET), 0);
    CHECK_EQ(FileTell(f), 100L);
    char four[4];
    CHECK_EQ(FileRead(four, 1, 4, f), 4u);
    CHECK(std::memcmp(four, content.data() + 100, 4) == 0);

    // SEEK_CUR
    CHECK_EQ(FileSeek(f, 10, SEEK_CUR), 0);
    CHECK_EQ(FileTell(f), 114L);

    // SEEK_END then read past EOF returns 0
    CHECK_EQ(FileSeek(f, 0, SEEK_END), 0);
    CHECK_EQ(FileRead(four, 1, 4, f), 0u);

    FileClose(f);
}

TEST(IoVfsTreeFile, TextLineRead) {
    MockFS fs;
    fs.files["lines.txt"] = "alpha\r\nbeta\ngamma\r\n";

    BufferedFile* f = FileOpenBuffered(&fs, "lines.txt", "rt");
    CHECK(f != nullptr);
    char line[64];
    // 1:1 with VIBE_Vfs_ReadLine @0x4516cc: the swallow loop @0x451758 over-reads
    // one byte past each CR/LF run and discards it, so every line after the first
    // loses its leading character. Faithful output of "alpha\r\nbeta\ngamma\r\n"
    // is therefore: "alpha", "eta", "amma".
    CHECK(FileReadLine(line, 63, f) != nullptr);
    CHECK(std::strcmp(line, "alpha") == 0);
    CHECK(FileReadLine(line, 63, f) != nullptr);
    CHECK(std::strcmp(line, "eta") == 0);   // 'b' consumed by prior swallow loop
    CHECK(FileReadLine(line, 63, f) != nullptr);
    CHECK(std::strcmp(line, "amma") == 0);  // 'g' consumed by prior swallow loop
    CHECK(FileReadLine(line, 63, f) == nullptr);   // EOF
    FileClose(f);
}

TEST(IoVfsTreeFile, WriteRoundtrip) {
    MockFS fs;
    BufferedFile* w = FileOpenBuffered(&fs, "out.bin", "wb");
    CHECK(w != nullptr);
    std::string payload;
    for (int i = 0; i < 9000; ++i) payload.push_back(static_cast<char>((i * 7) & 0xFF));
    std::size_t wn = FileWrite(payload.data(), 1, payload.size(), w);
    CHECK_EQ(wn, payload.size());
    FileClose(w);

    // read it back
    BufferedFile* r = FileOpenBuffered(&fs, "out.bin", "rb");
    CHECK(r != nullptr);
    std::vector<char> back(payload.size());
    std::size_t rn = FileRead(back.data(), 1, back.size(), r);
    CHECK_EQ(rn, payload.size());
    CHECK(std::memcmp(back.data(), payload.data(), payload.size()) == 0);
    FileClose(r);

    CHECK_EQ(FileCreateDirectory(&fs, "newdir"), 0);
}
