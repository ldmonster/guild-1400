// e2e: mount a mock directory tree, scan it into the VFS index, resolve several
// paths, and read the files through the buffered file layer, verifying both the
// recovered index and the byte contents.
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

namespace {

struct E2EFile : guild::shim::IFile {
    std::string data; std::size_t pos = 0; std::string* sink = nullptr;
    std::size_t read(void* d, std::size_t n) override {
        std::size_t a = data.size() - pos; if (n > a) n = a;
        std::memcpy(d, data.data() + pos, n); pos += n; return n;
    }
    std::size_t write(const void* s, std::size_t n) override {
        if (pos + n > data.size()) data.resize(pos + n);
        std::memcpy(&data[pos], s, n); pos += n; if (sink) *sink = data; return n;
    }
    std::int64_t seek(std::int64_t o, int w) override {
        std::int64_t b = (w == SEEK_SET) ? 0 : (w == SEEK_CUR ? (std::int64_t)pos : (std::int64_t)data.size());
        std::int64_t t = b + o; if (t < 0) return -1; pos = (std::size_t)t; return t;
    }
    std::int64_t tell() override { return (std::int64_t)pos; }
    std::int64_t size() override { return (std::int64_t)data.size(); }
};

struct E2EListing : guild::shim::IDirListing {
    std::vector<std::string> names; std::vector<guild::shim::DirEntry> entries;
    std::size_t count() const override { return entries.size(); }
    const guild::shim::DirEntry& at(std::size_t i) const override { return entries[i]; }
};

struct E2EFS : guild::shim::IFileSystem {
    std::map<std::string, std::string> files;
    struct Child { std::string name; bool isDir; guild::u32 t; };
    std::map<std::string, std::vector<Child>> dirs;

    void dir(const std::string& p) { if (!dirs.count(p)) dirs[p] = {}; }
    void file(const std::string& d, const std::string& leaf, const std::string& c, guild::u32 t) {
        dir(d); dirs[d].push_back({leaf, false, t});
        files[d + "/" + leaf] = c;
    }
    void subdir(const std::string& d, const std::string& leaf) {
        dir(d); dirs[d].push_back({leaf, true, 0}); dir(d + "/" + leaf);
    }
    guild::shim::IFile* open(const char* path, const char* mode) override {
        std::string p(path); bool w = mode && (mode[0]=='w'||mode[0]=='a');
        if (w) { E2EFile* f = new E2EFile(); f->sink = &files[p]; files[p] = ""; return f; }
        auto it = files.find(p); if (it == files.end()) return nullptr;
        E2EFile* f = new E2EFile(); f->data = it->second; return f;
    }
    void close(guild::shim::IFile* f) override { delete f; }
    bool exists(const char* p) override { return files.count(p) != 0; }
    guild::shim::IDirListing* listDir(const char* path) override {
        auto it = dirs.find(path); if (it == dirs.end()) return nullptr;
        E2EListing* l = new E2EListing();
        for (auto& c : it->second) l->names.push_back(c.name);
        for (std::size_t i = 0; i < it->second.size(); ++i) {
            guild::shim::DirEntry e; e.name = l->names[i].c_str();
            e.isDir = it->second[i].isDir; e.dosTime = it->second[i].t;
            l->entries.push_back(e);
        }
        return l;
    }
    bool makeDir(const char* p) override { dir(p); return true; }
};

// resolve a VFS path to a full host path so we can re-open the loose file.
// (Mirrors the engine: resolve through the tree to confirm presence, then
// build the host path the loose-file layer opens.)
std::string hostPath(const std::string& root, const std::string& vfsPath) {
    std::string p = vfsPath;
    for (auto& ch : p) if (ch == '\\') ch = '/';
    return root + "/" + p;
}

} // namespace

TEST(IoVfsTreeE2E, ScanResolveRead) {
    E2EFS fs;
    // Build a small asset tree resembling the engine's gfx root.
    fs.dir("gfx");
    fs.subdir("gfx", "textures");
    fs.file("gfx/textures", "wall.tga", "TGA-WALL-DATA", 11);
    fs.file("gfx/textures", "floor.tga", "TGA-FLOOR-DATA-XYZ", 12);
    fs.subdir("gfx", "scripts");
    fs.file("gfx/scripts", "main.scr", "line1\r\nline2\r\nline3\r\n", 13);
    fs.file("gfx", "gilde.gfx", std::string(5000, 'G'), 14);

    // Scan the whole tree (case-insensitive=false -> names upper-cased).
    CHECK(VfsTreeInit(&fs, "gfx", false));
    VfsNode* root = VfsRoot();
    CHECK(root != nullptr);
    CHECK_EQ(root->countOrTime, 1u);              // only gilde.gfx loose at root

    // Resolve several paths through the index.
    VfsNode* od = nullptr;
    VfsFileEntry* wall = ResolvePath("textures/wall.tga", root, &od);
    CHECK(wall != nullptr);
    if (wall) CHECK_EQ(wall->dosTime, 11u);

    VfsFileEntry* floor = ResolvePath("TEXTURES\\FLOOR.TGA", root, &od);
    CHECK(floor != nullptr);
    if (floor) CHECK_EQ(floor->dosTime, 12u);

    VfsFileEntry* scr = ResolvePath("scripts/main.scr", root, &od);
    CHECK(scr != nullptr);

    VfsFileEntry* gfx = ResolvePath("gilde.gfx", root, &od);
    CHECK(gfx != nullptr);
    if (gfx) CHECK_EQ(gfx->dosTime, 14u);

    CHECK(ResolvePath("textures/missing.tga", root, &od) == nullptr);

    // Read a resolved file through the buffered layer and verify contents.
    {
        BufferedFile* f = FileOpenBuffered(&fs, hostPath("gfx", "textures/wall.tga").c_str(), "rb");
        CHECK(f != nullptr);
        char buf[64];
        std::size_t n = FileRead(buf, 1, sizeof(buf), f);
        CHECK_EQ(n, std::strlen("TGA-WALL-DATA"));
        CHECK(std::memcmp(buf, "TGA-WALL-DATA", n) == 0);
        FileClose(f);
    }
    // Big file spanning multiple buffer refills.
    {
        BufferedFile* f = FileOpenBuffered(&fs, hostPath("gfx", "gilde.gfx").c_str(), "rb");
        CHECK(f != nullptr);
        std::vector<char> all(5000);
        std::size_t n = FileRead(all.data(), 1, 5000, f);
        CHECK_EQ(n, 5000u);
        bool allG = true; for (char c : all) if (c != 'G') allG = false;
        CHECK(allG);
        FileClose(f);
    }
    // Line-read the script through the text-mode buffered layer.
    {
        BufferedFile* f = FileOpenBuffered(&fs, hostPath("gfx", "scripts/main.scr").c_str(), "rt");
        CHECK(f != nullptr);
        char line[64];
        CHECK(FileReadLine(line, 63, f) != nullptr); CHECK(std::strcmp(line, "line1") == 0);
        CHECK(FileReadLine(line, 63, f) != nullptr); CHECK(std::strcmp(line, "line2") == 0);
        CHECK(FileReadLine(line, 63, f) != nullptr); CHECK(std::strcmp(line, "line3") == 0);
        CHECK(FileReadLine(line, 63, f) == nullptr);
        FileClose(f);
    }

    VfsTreeShutdown();
    CHECK(VfsRoot() == nullptr);
}

// Write a file through the buffered layer, then scan + resolve + read it back.
TEST(IoVfsTreeE2E, WriteThenScan) {
    E2EFS fs;
    fs.dir("save");
    // Write a payload through the buffered write path.
    BufferedFile* w = FileOpenBuffered(&fs, "save/state.dat", "wb");
    CHECK(w != nullptr);
    std::string payload;
    for (int i = 0; i < 8200; ++i) payload.push_back(static_cast<char>((i * 13 + 5) & 0xFF));
    CHECK_EQ(FileWrite(payload.data(), 1, payload.size(), w), payload.size());
    FileClose(w);
    // Register it in the directory listing so the scan picks it up.
    fs.dirs["save"].push_back({"state.dat", false, 42});

    CHECK(VfsTreeInit(&fs, "save", false));
    VfsNode* root = VfsRoot();
    VfsNode* od = nullptr;
    VfsFileEntry* e = ResolvePath("state.dat", root, &od);
    CHECK(e != nullptr);
    if (e) CHECK_EQ(e->dosTime, 42u);

    BufferedFile* r = FileOpenBuffered(&fs, "save/state.dat", "rb");
    CHECK(r != nullptr);
    std::vector<char> back(payload.size());
    CHECK_EQ(FileRead(back.data(), 1, back.size(), r), payload.size());
    CHECK(std::memcmp(back.data(), payload.data(), payload.size()) == 0);
    FileClose(r);

    VfsTreeShutdown();
}
