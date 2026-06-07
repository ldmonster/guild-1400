// Integration test: the file_ops3 high-level readers driven through the REAL
// reconstructed VFS sibling (io/vfs.cpp's VfsOpenFile/VfsReadStream/VfsSeek/
// VfsTell/VfsCloseStream, io/worldio.cpp's BioReadByte/BioReadDword, and
// io/vfs_tree.cpp's ResolvePath/VfsRoot — NONE mocked), reading from an in-memory
// host filesystem shim. This is the live wiring: file_ops3 sits directly on the
// VFS stream verbs, so every byte the token / line / chunk / config readers
// consume is produced by the genuine VFS dispatch, and VfsFileExists resolves
// through the real path tree built by the real directory scanner.
//
// The OS leaves file_ops3 *does* abstract (GetCurrentProcessId /
// GetCurrentDirectoryA via FileOps3Hooks) have NO reconstructed sibling — they are
// Win32 — so the working-dir helper is exercised through its inert default-hook
// path end to end (see the WorkingDirDefaultHookPath test below).
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

using namespace guild;
using namespace guild::io;

namespace {

// A minimal in-memory host filesystem supporting both open() and listDir() (the
// real VFS path-tree scanner needs the directory listing). NOT under test — it is
// the OS boundary the REAL VFS layers on top of.
struct MemFile : guild::shim::IFile {
    std::string data;
    std::size_t pos = 0;
    std::size_t read(void* d, std::size_t n) override {
        std::size_t avail = data.size() - pos;
        if (n > avail) n = avail;
        std::memcpy(d, data.data() + pos, n);
        pos += n;
        return n;
    }
    std::size_t write(const void*, std::size_t) override { return 0; }
    std::int64_t seek(std::int64_t o, int w) override {
        std::int64_t base = (w == SEEK_SET) ? 0
                          : (w == SEEK_CUR) ? (std::int64_t)pos
                                            : (std::int64_t)data.size();
        std::int64_t t = base + o;
        if (t < 0) return -1;
        pos = (std::size_t)t;
        return t;
    }
    std::int64_t tell() override { return (std::int64_t)pos; }
    std::int64_t size() override { return (std::int64_t)data.size(); }
};

struct MemListing : guild::shim::IDirListing {
    std::vector<std::string> names;
    std::vector<guild::shim::DirEntry> entries;
    std::size_t count() const override { return entries.size(); }
    const guild::shim::DirEntry& at(std::size_t i) const override { return entries[i]; }
};

struct MemFS : guild::shim::IFileSystem {
    std::map<std::string, std::string> files;
    struct Child { std::string name; bool isDir; };
    std::map<std::string, std::vector<Child>> dirs;
    void addDir(const std::string& p) { if (!dirs.count(p)) dirs[p] = {}; }
    void putFile(const std::string& path, const std::string& bytes) { files[path] = bytes; }
    void addDirFile(const std::string& dir, const std::string& leaf) {
        addDir(dir);
        dirs[dir].push_back({leaf, false});
    }
    guild::shim::IFile* open(const char* path, const char*) override {
        auto it = files.find(path);
        if (it == files.end()) return nullptr;
        MemFile* f = new MemFile();
        f->data = it->second;
        return f;
    }
    void close(guild::shim::IFile* f) override { delete f; }
    bool exists(const char* p) override { return files.count(p) != 0; }
    guild::shim::IDirListing* listDir(const char* path) override {
        auto it = dirs.find(path);
        if (it == dirs.end()) return nullptr;
        MemListing* l = new MemListing();
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

} // namespace

// VfsReadByte / VfsReadLine forward into the REAL VfsReadStream: a multi-line
// blob is consumed line by line through the genuine VFS cursor, then -1 at EOF.
// Each line carries a leading space consumed by VfsReadLine's documented trailing
// over-read so the content survives (the original line reader's behaviour).
TEST(FileOps3Itest, ReadByteAndLineThroughRealVfs) {
    MemFS fs;
    fs.putFile("lines.txt", " ALPHA\n BETA\n GAMMA\n");
    CHECK(VfsInit(&fs, true));

    VfsHandle* h = VfsOpenFile("lines.txt", "rbN");
    CHECK(h != nullptr);
    if (h) {
        char buf[32];
        char* l1 = VfsReadLine(buf, (int)sizeof(buf), h);
        CHECK(l1 != nullptr);
        if (l1) CHECK(std::strcmp(l1, " ALPHA") == 0);
        char* l2 = VfsReadLine(buf, (int)sizeof(buf), h);
        CHECK(l2 != nullptr);
        if (l2) CHECK(std::strcmp(l2, "BETA") == 0);   // leading space over-read
        char* l3 = VfsReadLine(buf, (int)sizeof(buf), h);
        CHECK(l3 != nullptr);
        if (l3) CHECK(std::strcmp(l3, "GAMMA") == 0);
        // Stream now at EOF: the real VfsReadStream returns 0 -> ReadByte -1.
        CHECK_EQ(VfsReadByte(h), -1);
        VfsCloseStream(h);
    }
    VfsShutdown();
}

// ScriptReadToken maps bytes through the REAL stream: a byte > ':' (0x3A) becomes
// '\'' (39), EOF becomes '+' (43), an in-range byte passes through verbatim.
TEST(FileOps3Itest, ScriptTokenThroughRealVfs) {
    MemFS fs;
    std::string data;
    data.push_back('-');                   // 0x2D, <= 0x3A -> verbatim
    data.push_back('Z');                   // 0x5A, > 0x3A  -> '\''
    fs.putFile("tok.dat", data);
    CHECK(VfsInit(&fs, true));

    VfsHandle* h = VfsOpenFile("tok.dat", "rbN");
    CHECK(h != nullptr);
    if (h) {
        CHECK_EQ((int)ScriptReadToken(h), kVfsTokenSection);  // '-' verbatim (45)
        CHECK_EQ((int)ScriptReadToken(h), kVfsTokenOther);    // 'Z' -> 39
        CHECK_EQ((int)ScriptReadToken(h), kVfsTokenEof);      // EOF -> 43
        VfsCloseStream(h);
    }
    VfsShutdown();
}

// Full chunk-table walk through the REAL VFS: VfsFindChunkStart / VfsScanChunkLength
// drive the real ScriptReadToken + BioReadDword + VfsSeek/VfsTell over a genuine
// stream, locating the 0xFAB50005 sentinel and reading its payload.
TEST(FileOps3Itest, ChunkScanThroughRealVfs) {
    MemFS fs;
    std::string body = "junkbody";                 // 8-byte body chunk
    // One non-'-' body chunk to skip: token byte 0x05, len dword, body.
    std::string skip = std::string(1, (char)0x05) +
                       Le32((guild::u32)body.size()) + body;
    // The sentinel record: '-' token, len dword (4), then the magic word.
    std::string sentinel = std::string("-") + Le32(4) + Le32(kVfsChunkMagic);
    std::string payload = Le32(0xCAFEF00Du);
    std::string blob = "HDR1" + skip + sentinel + payload;
    fs.putFile("chunks.dat", blob);
    CHECK(VfsInit(&fs, true));

    long off = VfsScanChunkLength("chunks.dat");
    CHECK_EQ(off, (long)(4 + skip.size()));

    VfsHandle* h = VfsFindChunkStart("chunks.dat");
    CHECK(h != nullptr);
    if (h) {
        long here = VfsTell(h);                     // positioned just past the magic
        guild::u32 got = 0;
        CHECK(BioReadDword(h, &got));               // REAL Bio reader
        CHECK_EQ(got, 0xCAFEF00Du);
        // Rewind via the real VfsSeek and re-read the first payload byte.
        CHECK_EQ(VfsSeek(h, here, 0 /*SEEK_SET*/), 0);
        CHECK_EQ(VfsReadByte(h), 0x0D);
        VfsCloseStream(h);
    }

    // A table that ends in '+' before any sentinel: real walk closes + fails.
    fs.putFile("none.dat", std::string("HDR1") + "+");
    CHECK(VfsFindChunkStart("none.dat") == nullptr);
    CHECK_EQ(VfsScanChunkLength("none.dat"), -1L);

    VfsShutdown();
}

// VfsReadConfigLine layers the trim/upper-case parser over the REAL line reader:
// blank and comment lines are skipped, content is upper-cased in place.
TEST(FileOps3Itest, ConfigLineThroughRealVfs) {
    MemFS fs;
    // Each content line carries a leading space (consumed by VfsReadLine's
    // trailing over-read), matching the real reader's documented behaviour.
    fs.putFile("cfg.ini",
               " width = 640  \n ; a comment\n   \n height=480\n");
    CHECK(VfsInit(&fs, true));

    VfsHandle* h = VfsOpenFile("cfg.ini", "rbN");
    CHECK(h != nullptr);
    if (h) {
        char* a = VfsReadConfigLine(h);
        CHECK(a != nullptr);
        if (a) CHECK(std::strcmp(a, "WIDTH = 640") == 0);
        char* b = VfsReadConfigLine(h);             // comment + blank skipped
        CHECK(b != nullptr);
        if (b) CHECK(std::strcmp(b, "HEIGHT=480") == 0);
        CHECK(VfsReadConfigLine(h) == nullptr);     // EOF
        VfsCloseStream(h);
    }
    VfsShutdown();
}

// VfsCheckChunkFlag reads the fixed record headers through the REAL Bio readers
// and tests bit 1 of the byte after a tag==2 record.
TEST(FileOps3Itest, CheckChunkFlagThroughRealVfs) {
    MemFS fs;
    // header(4) + byte + dword + byte + dword + tagByte(==2) + flagByte.
    auto makeFile = [&](char flagByte) {
        std::string b = "HDR1";
        b.push_back((char)0x10);                 // first byte
        b += Le32(0);                            // first dword
        b.push_back((char)0x11);                 // second byte
        b += Le32(0);                            // second dword
        b.push_back((char)2);                    // tag byte == 2 -> read flag byte
        b.push_back(flagByte);
        return b;
    };
    fs.putFile("set.dat", makeFile((char)0x02));   // bit 1 set -> flag false
    fs.putFile("clear.dat", makeFile((char)0x00)); // bit 1 clear -> flag true
    CHECK(VfsInit(&fs, true));

    CHECK(!VfsCheckChunkFlag("set.dat"));    // bit 1 set
    CHECK(VfsCheckChunkFlag("clear.dat"));   // bit 1 clear
    CHECK(VfsCheckChunkFlag("missing.dat")); // open fails -> true (default)
    VfsShutdown();
}

// VfsFileExists forwards into the REAL ResolvePath against the REAL VfsRoot tree
// built by the real directory scanner from the host listing (vfs_tree.cpp).
TEST(FileOps3Itest, FileExistsThroughRealVfsTree) {
    MemFS fs;
    fs.addDirFile("root", "alpha.dat");
    fs.addDirFile("root", "beta.dat");
    fs.putFile("root/alpha.dat", "aaa");
    fs.putFile("root/beta.dat", "bb");
    CHECK(VfsTreeInit(&fs, "root", false));
    CHECK(VfsFileExists("alpha.dat"));
    CHECK(VfsFileExists("beta.dat"));
    CHECK(!VfsFileExists("gamma.dat"));
    VfsTreeShutdown();
}

// The OS-boundary working-dir helper has NO reconstructed sibling (it abstracts
// GetCurrentDirectoryA). Exercise its INERT default-hook path end to end: the
// default getCurrentDir writes an empty string and returns 0, so VfsGetWorkingDir
// reports failure (nullptr) without touching the OS — the deterministic default.
TEST(FileOps3Itest, WorkingDirDefaultHookPath) {
    SetFileOps3Hooks(nullptr);   // restore inert defaults
    CHECK_EQ(VfsGetProcessId(), 0u);
    char wd[64];
    CHECK(VfsGetWorkingDir(wd, (guild::u32)sizeof(wd)) == nullptr);
}
