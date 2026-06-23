// Unit tests for guild::io file_ops3 — VFS byte/line readers, chunk-table
// scanning, config-line reader, and the OS-backed working-dir / process-id
// helpers. Golden vectors for the chunk format and config trimming were computed
// with python3 (see the implementer report).
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

// --- in-memory filesystem (read-only fixtures) ------------------------------
struct MockFile : guild::shim::IFile {
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

struct MockListing : guild::shim::IDirListing {
    std::vector<std::string> names;
    std::vector<guild::shim::DirEntry> entries;
    std::size_t count() const override { return entries.size(); }
    const guild::shim::DirEntry& at(std::size_t i) const override { return entries[i]; }
};

struct MockFS : guild::shim::IFileSystem {
    std::map<std::string, std::string> files;
    struct Child { std::string name; bool isDir; };
    std::map<std::string, std::vector<Child>> dirs;

    void addDir(const std::string& p) { if (!dirs.count(p)) dirs[p] = {}; }
    void addFile(const std::string& d, const std::string& leaf) {
        addDir(d);
        dirs[d].push_back({leaf, false});
    }
    void addSub(const std::string& d, const std::string& leaf) {
        addDir(d);
        dirs[d].push_back({leaf, true});
        addDir(d + "/" + leaf);
    }
    guild::shim::IFile* open(const char* path, const char* /*mode*/) override {
        auto it = files.find(path);
        if (it == files.end()) return nullptr;
        MockFile* f = new MockFile();
        f->data = it->second;
        return f;
    }
    void close(guild::shim::IFile* f) override { delete f; }
    bool exists(const char* p) override { return files.count(p) != 0; }
    guild::shim::IDirListing* listDir(const char* path) override {
        auto it = dirs.find(path);
        if (it == dirs.end()) return nullptr;
        MockListing* l = new MockListing();
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

// A memory-backed VfsHandle holding `bytes`, opened via the VFS read path.
VfsHandle* OpenBytes(MockFS& fs, const std::string& bytes) {
    fs.files["fix.dat"] = bytes;
    VfsInit(&fs, false);
    // Open without transparent gzip ('N') so any leading bytes are taken raw.
    return VfsOpenFile("fix.dat", "rbN");
}

} // namespace

// --- Bio / Script primitives ------------------------------------------------
TEST(FileOps3Unit, BioAndTokenPrimitives) {
    MockFS fs;
    VfsHandle* h = OpenBytes(fs, std::string("\x05\x0A\x40" "ABCD", 7));
    CHECK(h != nullptr);

    // BioReadByte / BioReadDword (reused from worldio) return true on a full read.
    guild::u8 b = 0xFF;
    CHECK(BioReadByte(h, &b));
    CHECK_EQ((int)b, 5);

    // Next byte is 0x0A (10 <= 0x3A) -> ScriptReadToken returns it verbatim.
    CHECK_EQ((int)ScriptReadToken(h), 10);

    // Next byte 0x40 (64 > 0x3A) -> token folded to '\'' (39).
    CHECK_EQ((int)ScriptReadToken(h), 39);

    // 4-byte dword read: "ABCD" (little-endian on the host) -> 0x44434241.
    guild::u32 dw = 0;
    CHECK(BioReadDword(h, &dw));
    CHECK_EQ(dw, 0x44434241u);

    // EOF -> ScriptReadToken returns '+' (43); BioReadByte returns false.
    CHECK_EQ((int)ScriptReadToken(h), 43);
    CHECK(!BioReadByte(h, &b));
    VfsCloseStream(h);
}

// --- VfsReadByte / VfsReadStreamBool ----------------------------------------
TEST(FileOps3Unit, ReadByteAndBool) {
    MockFS fs;
    VfsHandle* h = OpenBytes(fs, std::string("\x01\x02", 2));
    CHECK_EQ(VfsReadByte(h), 1);
    CHECK_EQ(VfsReadByte(h), 2);
    CHECK_EQ(VfsReadByte(h), -1);   // EOF
    VfsCloseStream(h);

    h = OpenBytes(fs, std::string("\xAA\xBB\xCC\xDD", 4));
    guild::u8 buf[2];
    CHECK(VfsReadStreamBool(buf, 2, h, 1));
    CHECK(VfsReadStreamBool(buf, 2, h, 1));
    CHECK(!VfsReadStreamBool(buf, 2, h, 1));  // nothing left
    VfsCloseStream(h);
}

// --- VfsReadLine ------------------------------------------------------------
// NOTE: faithful to the original. After breaking on a line terminator the
// original's trailing CR/LF-skip loop reads one byte PAST the run, so the first
// character of the following line is over-consumed and lost. The golden outputs
// below were produced by a byte-exact python model of VIBE_Vfs_ReadLine.
TEST(FileOps3Unit, ReadLineSplitsAndTrims) {
    MockFS fs;
    // "one\r\ntwo\nthree" -> "one", "wo", "hree"  (the 't' after each break is lost).
    VfsHandle* h = OpenBytes(fs, std::string("one\r\ntwo\nthree"));
    char line[64];

    CHECK(VfsReadLine(line, 63, h) != nullptr);
    CHECK(std::strcmp(line, "one") == 0);

    CHECK(VfsReadLine(line, 63, h) != nullptr);
    CHECK(std::strcmp(line, "wo") == 0);

    CHECK(VfsReadLine(line, 63, h) != nullptr);
    CHECK(std::strcmp(line, "hree") == 0);

    CHECK(VfsReadLine(line, 63, h) == nullptr);  // EOF
    VfsCloseStream(h);

    // A leading CR yields an empty line and consumes the following 'X'.
    h = OpenBytes(fs, std::string("\rX"));
    CHECK(VfsReadLine(line, 63, h) != nullptr);
    CHECK_EQ((int)line[0], 0);
    CHECK(VfsReadLine(line, 63, h) == nullptr);  // 'X' was over-consumed -> EOF
    VfsCloseStream(h);
}

// --- VfsReadConfigLine ------------------------------------------------------
TEST(FileOps3Unit, ConfigLineTrimSkipUpper) {
    MockFS fs;
    // Every line after the first is prefixed with one extra leading space, which
    // VfsReadLine's trailing over-read consumes — so the meaningful content (and
    // the comment marker) survive intact. The reader right/left-trims and
    // upper-cases each kept line, and skips ';' comments and blank lines.
    std::string cfg = "  resolution=800x600\n fullscreen=yes\n ;comment\n   \n";
    VfsHandle* h = OpenBytes(fs, cfg);

    char* r = VfsReadConfigLine(h);
    CHECK(r != nullptr);
    CHECK(std::strcmp(r, "RESOLUTION=800X600") == 0);

    r = VfsReadConfigLine(h);
    CHECK(r != nullptr);
    CHECK(std::strcmp(r, "FULLSCREEN=YES") == 0);

    // Remaining: a comment line and a whitespace-only line -> both skipped -> EOF.
    CHECK(VfsReadConfigLine(h) == nullptr);
    VfsCloseStream(h);
}

// --- Chunk-table scanning ---------------------------------------------------
TEST(FileOps3Unit, FindChunkImmediateSentinel) {
    MockFS fs;
    // [HDR0]['-'][len=0][MAGIC]TRAILER  — sentinel right after the header.
    std::string f = "HDR0" + std::string("-") + Le32(0) + Le32(kVfsChunkMagic) + "TRAILER";
    fs.files["c.dat"] = f;
    VfsInit(&fs, false);

    VfsHandle* h = VfsFindChunkStart("c.dat");
    CHECK(h != nullptr);
    // Positioned just past the 4-byte magic: offset 4+1+4+4 = 13.
    CHECK_EQ(VfsTell(h), 13L);
    // The next bytes are "TRAILER" (VfsReadStream returns the byte count, 7).
    char t[8] = {0};
    CHECK_EQ(VfsReadStream(t, 7, h, 1), 7u);
    CHECK(std::memcmp(t, "TRAILER", 7) == 0);
    VfsCloseStream(h);

    // ScanChunkLength returns the byte offset of the sentinel record (4).
    CHECK_EQ(VfsScanChunkLength("c.dat"), 4L);
}

TEST(FileOps3Unit, FindChunkSkipsBodyChunk) {
    MockFS fs;
    // A non-'-'/'+' record (token 0x10) carrying an 8-byte body is skipped, then
    // the sentinel is found.
    std::string body = "PAYLOAD!";
    std::string recSkip = std::string(1, (char)0x10) + Le32((guild::u32)body.size()) + body;
    std::string sentinel = "-" + Le32(0) + Le32(kVfsChunkMagic);
    std::string f = "HDR0" + recSkip + sentinel + "END";
    fs.files["c.dat"] = f;
    VfsInit(&fs, false);

    VfsHandle* h = VfsFindChunkStart("c.dat");
    CHECK(h != nullptr);
    VfsCloseStream(h);

    // Sentinel record begins at offset 4 + recSkip.size() = 17.
    CHECK_EQ(VfsScanChunkLength("c.dat"), (long)(4 + recSkip.size()));
}

TEST(FileOps3Unit, FindChunkMismatchThenMatch) {
    MockFS fs;
    // A '-' record whose magic does NOT match (len=12 so the seek-back lands on
    // the next record), followed by the real sentinel.
    std::string wrong = "-" + Le32(12) + Le32(0x11111111u) + "12345678";  // 17 bytes
    std::string good  = "-" + Le32(0) + Le32(kVfsChunkMagic);             // 9 bytes
    std::string f = "HDR0" + wrong + good + "ZZ";
    fs.files["c.dat"] = f;
    VfsInit(&fs, false);

    // Matched sentinel is the SECOND '-' record at offset 4 + 17 = 21.
    CHECK_EQ(VfsScanChunkLength("c.dat"), 21L);
    VfsHandle* h = VfsFindChunkStart("c.dat");
    CHECK(h != nullptr);
    CHECK_EQ(VfsTell(h), 21L + 9L);   // just past the second record's magic
    VfsCloseStream(h);
}

TEST(FileOps3Unit, FindChunkNotFound) {
    MockFS fs;
    // header then immediate '+' -> premature end.
    std::string f = "HDR0+junk";
    fs.files["c.dat"] = f;
    VfsInit(&fs, false);

    CHECK(VfsFindChunkStart("c.dat") == nullptr);
    CHECK_EQ(VfsScanChunkLength("c.dat"), -1L);

    // Missing file -> open fails.
    CHECK(VfsFindChunkStart("nope.dat") == nullptr);
    CHECK_EQ(VfsScanChunkLength("nope.dat"), -1L);
}

TEST(FileOps3Unit, CheckChunkFlag) {
    MockFS fs;
    auto build = [&](guild::u8 tag, guild::u8 v) {
        return std::string("HDR0") + std::string(1, (char)1) + Le32(0) +
               std::string(1, (char)1) + Le32(0) + std::string(1, (char)tag) +
               std::string(1, (char)v) + "xx";
    };
    // tag==2, bit1 set -> false.
    fs.files["c.dat"] = build(2, 2);
    VfsInit(&fs, false);
    CHECK(!VfsCheckChunkFlag("c.dat"));

    // tag==2, bit1 clear -> true.
    fs.files["c.dat"] = build(2, 0);
    CHECK(VfsCheckChunkFlag("c.dat"));

    // tag!=2 -> the extra byte isn't consulted -> true.
    fs.files["c.dat"] = build(9, 2);
    CHECK(VfsCheckChunkFlag("c.dat"));

    // open failure -> true (default flag).
    CHECK(VfsCheckChunkFlag("missing.dat"));
}

// --- VfsFileExists (path tree) ----------------------------------------------
TEST(FileOps3Unit, FileExists) {
    MockFS fs;
    fs.addFile("data", "world.dat");
    fs.addSub("data", "scripts");
    fs.addFile("data/scripts", "main.scr");
    CHECK(VfsTreeInit(&fs, "data", false));

    CHECK(VfsFileExists("world.dat"));
    CHECK(VfsFileExists("scripts/main.scr"));
    CHECK(!VfsFileExists("nope.dat"));
    CHECK(!VfsFileExists("scripts/missing.scr"));

    VfsTreeShutdown();
}

// --- VfsGetProcessId / VfsGetWorkingDir via hooks ---------------------------
namespace {
guild::u32 HookPid() { return 0x1234; }
const char* g_cwd = "C:\\GAME\\WORK";
guild::u32 HookCwd(guild::u32 size, char* buf) {
    guild::u32 len = (guild::u32)std::strlen(g_cwd);
    if (size < len + 1) return 0;
    std::memcpy(buf, g_cwd, len + 1);
    return len;
}
char g_allocPool[256];
void* HookAlloc(guild::u32 /*size*/) { return g_allocPool; }
int g_einval = 0;
int g_einvalCode = 0;
void HookEinval(int code) { ++g_einval; g_einvalCode = code; }
} // namespace

TEST(FileOps3Unit, ProcessIdAndWorkingDir) {
    FileOps3Hooks h{};
    h.getProcessId = &HookPid;
    h.getCurrentDir = &HookCwd;
    h.allocMem = &HookAlloc;
    h.setErrnoEinval = &HookEinval;
    FileOps3Hooks prev = SetFileOps3Hooks(&h);

    CHECK_EQ(VfsGetProcessId(), 0x1234u);

    // Caller-provided buffer large enough.
    char buf[64];
    char* r = VfsGetWorkingDir(buf, sizeof(buf));
    CHECK(r == buf);
    CHECK(std::strcmp(buf, g_cwd) == 0);

    // Caller buffer too small -> nullptr + EINVAL (errno code 14, per 0x5eef0d).
    g_einval = 0;
    g_einvalCode = 0;
    CHECK(VfsGetWorkingDir(buf, 3) == nullptr);
    CHECK_EQ(g_einval, 1);
    CHECK_EQ(g_einvalCode, 14);

    // dst == null -> allocate via hook.
    char* a = VfsGetWorkingDir(nullptr, 0);
    CHECK(a == g_allocPool);
    CHECK(std::strcmp(a, g_cwd) == 0);

    // dst == null and allocation fails -> nullptr + errno code 5 (per 0x5eeef2).
    g_einval = 0;
    g_einvalCode = 0;
    FileOps3Hooks h2 = h;
    h2.allocMem = [](guild::u32) -> void* { return nullptr; };
    SetFileOps3Hooks(&h2);
    CHECK(VfsGetWorkingDir(nullptr, 0) == nullptr);
    CHECK_EQ(g_einval, 1);
    CHECK_EQ(g_einvalCode, 5);
    SetFileOps3Hooks(&h);

    SetFileOps3Hooks(&prev);

    // Restored inert default: process id 0.
    CHECK_EQ(VfsGetProcessId(), 0u);
}
