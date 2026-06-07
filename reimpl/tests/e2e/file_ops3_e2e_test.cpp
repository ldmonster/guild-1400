// e2e: drive a full VFS read session across the file_ops3 verbs. Mount an
// in-memory filesystem holding (a) a chunk-table binary and (b) a config text
// file, then: locate the chunk sentinel, read its payload via the Bio/Script
// primitives, rewind via Tell/Seek, and parse the config through the
// trim/upper-case line reader. Also confirm path-tree existence and the
// hook-backed working-dir helper in the same run.
#include "test.h"

#include "io/file_ops3.h"
#include "io/vfs.h"
#include "io/vfs_tree.h"
#include "io/worldio.h"
#include "shim/IFileSystem.h"

#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace guild::io;

namespace {

struct E3File : guild::shim::IFile {
    std::string data;
    std::size_t pos = 0;
    std::size_t read(void* d, std::size_t n) override {
        std::size_t a = data.size() - pos;
        if (n > a) n = a;
        std::memcpy(d, data.data() + pos, n);
        pos += n;
        return n;
    }
    std::size_t write(const void*, std::size_t) override { return 0; }
    std::int64_t seek(std::int64_t o, int w) override {
        std::int64_t b = (w == SEEK_SET) ? 0
                       : (w == SEEK_CUR) ? (std::int64_t)pos
                                         : (std::int64_t)data.size();
        std::int64_t t = b + o;
        if (t < 0) return -1;
        pos = (std::size_t)t;
        return t;
    }
    std::int64_t tell() override { return (std::int64_t)pos; }
    std::int64_t size() override { return (std::int64_t)data.size(); }
};

struct E3Listing : guild::shim::IDirListing {
    std::vector<std::string> names;
    std::vector<guild::shim::DirEntry> entries;
    std::size_t count() const override { return entries.size(); }
    const guild::shim::DirEntry& at(std::size_t i) const override { return entries[i]; }
};

struct E3FS : guild::shim::IFileSystem {
    std::map<std::string, std::string> files;
    struct Child { std::string name; bool isDir; };
    std::map<std::string, std::vector<Child>> dirs;
    void addDir(const std::string& p) { if (!dirs.count(p)) dirs[p] = {}; }
    void addFile(const std::string& d, const std::string& leaf) {
        addDir(d);
        dirs[d].push_back({leaf, false});
    }
    guild::shim::IFile* open(const char* path, const char*) override {
        auto it = files.find(path);
        if (it == files.end()) return nullptr;
        E3File* f = new E3File();
        f->data = it->second;
        return f;
    }
    void close(guild::shim::IFile* f) override { delete f; }
    bool exists(const char* p) override { return files.count(p) != 0; }
    guild::shim::IDirListing* listDir(const char* path) override {
        auto it = dirs.find(path);
        if (it == dirs.end()) return nullptr;
        E3Listing* l = new E3Listing();
        for (auto& c : it->second) l->names.push_back(c.name);
        for (std::size_t i = 0; i < it->second.size(); ++i) {
            guild::shim::DirEntry e;
            e.name = l->names[i].c_str();
            e.isDir = it->second[i].isDir;
            l->entries.push_back(e);
        }
        return l;
    }
};

std::string Le32(guild::u32 x) {
    char b[4] = {(char)(x & 0xFF), (char)((x >> 8) & 0xFF),
                 (char)((x >> 16) & 0xFF), (char)((x >> 24) & 0xFF)};
    return std::string(b, 4);
}

const char* g_e3cwd = "D:\\GILDE";
guild::u32 E3Cwd(guild::u32 size, char* buf) {
    guild::u32 len = (guild::u32)std::strlen(g_e3cwd);
    if (size < len + 1) return 0;
    std::memcpy(buf, g_e3cwd, len + 1);
    return len;
}
guild::u32 E3Pid() { return 0xABCD; }

} // namespace

TEST(FileOps3E2E, ChunkConfigAndPathFlow) {
    E3FS fs;

    // --- (a) a chunk-table binary: skip one body chunk, then the sentinel ----
    // The sentinel's payload is the dword 0xCAFEF00D right after the magic.
    std::string body = "junkbody";                       // 8-byte body chunk
    std::string skip = std::string(1, (char)0x05) +      // token <= 0x3A, not '-'/'+'
                       Le32((guild::u32)body.size()) + body;
    std::string sentinel = std::string("-") + Le32(4) + Le32(kVfsChunkMagic);
    std::string payload = Le32(0xCAFEF00Du);
    std::string chunkFile = "HDR1" + skip + sentinel + payload;
    fs.files["table.dat"] = chunkFile;

    VfsInit(&fs, false);

    // Scan returns the sentinel record offset; Find returns a positioned stream.
    long off = VfsScanChunkLength("table.dat");
    CHECK_EQ(off, (long)(4 + skip.size()));

    VfsHandle* h = VfsFindChunkStart("table.dat");
    CHECK(h != nullptr);
    // Stream sits right after the 4-byte magic; the next dword is the payload.
    long here = VfsTell(h);
    guild::u32 got = 0;
    CHECK(BioReadDword(h, &got));        // reads the little-endian payload dword
    CHECK_EQ(got, 0xCAFEF00Du);

    // Rewind to the recorded position and re-read the first payload byte.
    CHECK_EQ(VfsSeek(h, here, 0 /*SEEK_SET*/), 0);
    CHECK_EQ(VfsReadByte(h), 0x0D);
    VfsCloseStream(h);

    // --- (b) a config file parsed line by line -------------------------------
    // Each line carries one extra leading space, consumed by VfsReadLine's
    // trailing over-read so the content survives (see the unit-test note).
    std::string cfg =
        " resolution = 800x600  \n"
        " fullscreen=yes\n"
        " ; trailing comment\n"
        "   \n";
    fs.files["cfg.ini"] = cfg;

    VfsHandle* c = VfsOpenFile("cfg.ini", "rbN");
    CHECK(c != nullptr);
    char* l1 = VfsReadConfigLine(c);
    CHECK(l1 != nullptr);
    CHECK(std::strcmp(l1, "RESOLUTION = 800X600") == 0);
    char* l2 = VfsReadConfigLine(c);
    CHECK(l2 != nullptr);
    CHECK(std::strcmp(l2, "FULLSCREEN=YES") == 0);
    CHECK(VfsReadConfigLine(c) == nullptr);   // comment + blank -> EOF
    VfsCloseStream(c);

    // --- (c) hook-backed process id + working dir in the same session --------
    FileOps3Hooks hk{};
    hk.getProcessId = &E3Pid;
    hk.getCurrentDir = &E3Cwd;
    FileOps3Hooks prev = SetFileOps3Hooks(&hk);
    CHECK_EQ(VfsGetProcessId(), 0xABCDu);
    char wd[64];
    CHECK(VfsGetWorkingDir(wd, sizeof(wd)) == wd);
    CHECK(std::strcmp(wd, g_e3cwd) == 0);
    SetFileOps3Hooks(&prev);

    VfsShutdown();

    // --- (d) path-tree existence over a scanned mock tree --------------------
    fs.addFile("root", "alpha.dat");
    fs.addFile("root", "beta.dat");
    CHECK(VfsTreeInit(&fs, "root", false));
    CHECK(VfsFileExists("alpha.dat"));
    CHECK(VfsFileExists("beta.dat"));
    CHECK(!VfsFileExists("gamma.dat"));
    VfsTreeShutdown();
}
