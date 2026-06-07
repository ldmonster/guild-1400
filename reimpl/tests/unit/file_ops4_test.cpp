// Unit tests for guild::io file_ops4 — descriptor-level OS I/O verbs and the CRT
// FILE-table layer. A small in-memory OS-handle backend stands in for the Win32
// read/write/lseek/chsize/open primitives; the file-descriptor table from
// file_ops2 carries fd->osHandle bindings. Golden vectors for the text-mode
// LF<->CRLF translation and the fopen-mode parser were computed by hand against
// the original pseudocode.
#include "test.h"

#include "io/file_ops4.h"
#include "io/file_ops2.h"
#include "crt/strtol.h"

#include <cstring>
#include <map>
#include <string>
#include <vector>

using namespace guild::io;

namespace {

// --- in-memory OS-handle backend --------------------------------------------
struct OsFile {
    std::string data;
    long pos = 0;
};

struct OsBackend {
    std::map<int, OsFile> files;   // osHandle -> file
    int nextHandle = 100;
    unsigned lastErr = 0;
    int fileType = 0;              // FILE_TYPE_DISK
    int openHandle = -2;           // sentinel: osOpen returns this if >= -1

    int create(const std::string& contents = "") {
        int h = nextHandle++;
        files[h].data = contents;
        files[h].pos = 0;
        return h;
    }
};

OsBackend* g_be = nullptr;

long beSeek(int h, long off, int whence) {
    auto it = g_be->files.find(h);
    if (it == g_be->files.end()) { g_be->lastErr = 6; return -1; }
    long base = (whence == 0) ? 0 : (whence == 1) ? it->second.pos
                                                   : (long)it->second.data.size();
    long t = base + off;
    if (t < 0) { g_be->lastErr = 131; return -1; }
    it->second.pos = t;
    return t;
}
int beWrite(int h, const char* buf, unsigned n, int* written) {
    auto it = g_be->files.find(h);
    if (it == g_be->files.end()) { g_be->lastErr = 6; return 0; }
    OsFile& f = it->second;
    if ((size_t)f.pos > f.data.size()) f.data.resize(f.pos, '\0');
    if ((size_t)f.pos + n > f.data.size()) f.data.resize(f.pos + n);
    std::memcpy(&f.data[f.pos], buf, n);
    f.pos += n;
    *written = (int)n;
    return 1;
}
int beRead(int h, char* buf, unsigned n, int* got) {
    auto it = g_be->files.find(h);
    if (it == g_be->files.end()) { g_be->lastErr = 6; return 0; }
    OsFile& f = it->second;
    unsigned avail = (f.pos < (long)f.data.size()) ? (unsigned)(f.data.size() - f.pos) : 0;
    if (n > avail) n = avail;
    std::memcpy(buf, f.data.data() + f.pos, n);
    f.pos += n;
    *got = (int)n;
    return 1;
}
int beChsize(int h) {
    auto it = g_be->files.find(h);
    if (it == g_be->files.end()) { g_be->lastErr = 6; return 0; }
    it->second.data.resize(it->second.pos);
    return 1;
}
int beOpen(const char*, unsigned, int, int, unsigned) {
    return g_be->create();
}
unsigned beLastError() { return g_be->lastErr; }
int beFileType(int) { return g_be->fileType; }

FileOps4Hooks makeHooks() {
    FileOps4Hooks h;
    h.osSeek = &beSeek;
    h.osWrite = &beWrite;
    h.osRead = &beRead;
    h.osChsize = &beChsize;
    h.osOpen = &beOpen;
    h.lastError = &beLastError;
    h.fileType = &beFileType;
    return h;
}

// Bind a fresh fd to an OS handle in the table with the given flags byte.
int bindFd(FdTable& t, OsBackend& be, int osHandle, guild::u8 flags) {
    int fd = AllocHandle(t);
    SetOsHandle(t, (unsigned)fd, osHandle);
    t.Entry(fd)->flags = flags;
    (void)be;
    return fd;
}

struct Fixture {
    OsBackend be;
    FdTable   t;
    FileOps4Hooks prev;
    Fixture() {
        g_be = &be;
        FileOps4Hooks h = makeHooks();
        prev = SetOps4Hooks(&h);
    }
    ~Fixture() { SetOps4Hooks(&prev); g_be = nullptr; }
};

} // namespace

// --- SeekDescriptor ---------------------------------------------------------
TEST(FileOps4, SeekDescriptorBasic) {
    Fixture fx;
    int h = fx.be.create("0123456789");
    int fd = bindFd(fx.t, fx.be, h, 0x01 | 0x02);
    CHECK_EQ(SeekDescriptor(fx.t, fd, 3, 0), 3);
    CHECK_EQ(SeekDescriptor(fx.t, fd, 2, 1), 5);
    CHECK_EQ(SeekDescriptor(fx.t, fd, 0, 2), 10);
    // success clears the 0x02 pending-read flag
    CHECK_EQ((int)(fx.t.Entry(fd)->flags & 0x02), 0);
}

TEST(FileOps4, SeekDescriptorBadFd) {
    Fixture fx;
    guild::crt::Errno() = 0;
    CHECK_EQ(SeekDescriptor(fx.t, 999, 0, 0), -1);
    CHECK_EQ(guild::crt::Errno(), 9);   // EBADF
}

// --- WriteFd: binary --------------------------------------------------------
TEST(FileOps4, WriteFdBinary) {
    Fixture fx;
    int h = fx.be.create();
    int fd = bindFd(fx.t, fx.be, h, 0x01);   // binary
    const char* s = "hello\nworld";
    int w = WriteFd(fx.t, fd, s, (unsigned)std::strlen(s));
    CHECK_EQ(w, 11);
    CHECK(fx.be.files[h].data == std::string("hello\nworld"));
}

// --- WriteFd: text LF -> CRLF -----------------------------------------------
TEST(FileOps4, WriteFdTextCrlf) {
    Fixture fx;
    int h = fx.be.create();
    int fd = bindFd(fx.t, fx.be, h, 0x01 | 0x80);   // text mode
    const char* s = "a\nb\n";
    int w = WriteFd(fx.t, fd, s, (unsigned)std::strlen(s));
    // returns bytes consumed from caller buffer (excludes the 2 inserted CRs)
    CHECK_EQ(w, 4);
    CHECK(fx.be.files[h].data == std::string("a\r\nb\r\n"));
}

TEST(FileOps4, WriteFdZeroLen) {
    Fixture fx;
    int h = fx.be.create();
    int fd = bindFd(fx.t, fx.be, h, 0x01);
    CHECK_EQ(WriteFd(fx.t, fd, "x", 0), 0);
}

// --- ReadTranslate: binary --------------------------------------------------
TEST(FileOps4, ReadTranslateBinary) {
    Fixture fx;
    int h = fx.be.create("abcdef");
    int fd = bindFd(fx.t, fx.be, h, 0x01);   // binary
    char buf[16] = {};
    unsigned n = ReadTranslate(fx.t, fd, buf, 6);
    CHECK_EQ((int)n, 6);
    CHECK(std::string(buf, n) == "abcdef");
}

// --- ReadTranslate: text CRLF -> LF -----------------------------------------
TEST(FileOps4, ReadTranslateTextCrlf) {
    Fixture fx;
    int h = fx.be.create("a\r\nb\r\nc");
    int fd = bindFd(fx.t, fx.be, h, 0x01 | 0x80);   // text mode
    char buf[32] = {};
    unsigned n = ReadTranslate(fx.t, fd, buf, 16);
    // "a\r\nb\r\nc" (7 raw bytes) -> "a\nb\nc" (5 bytes) within one buffer
    CHECK_EQ((int)n, 5);
    CHECK(std::string(buf, n) == "a\nb\nc");
}

// --- ReadTranslate: ^Z ends the text stream ---------------------------------
TEST(FileOps4, ReadTranslateCtrlZ) {
    Fixture fx;
    char raw[] = {'h','i', 26, 'x','y'};
    int h = fx.be.create(std::string(raw, sizeof(raw)));
    int fd = bindFd(fx.t, fx.be, h, 0x01 | 0x80);
    char buf[16] = {};
    unsigned n = ReadTranslate(fx.t, fd, buf, 16);
    CHECK_EQ((int)n, 2);
    CHECK(std::string(buf, n) == "hi");
}

TEST(FileOps4, ReadTranslateBadFd) {
    Fixture fx;
    guild::crt::Errno() = 0;
    char buf[4];
    CHECK_EQ((int)ReadTranslate(fx.t, 999, buf, 4), -1);
    CHECK_EQ(guild::crt::Errno(), 9);
}

// --- ChangeSize: grow then shrink -------------------------------------------
TEST(FileOps4, ChangeSizeGrow) {
    Fixture fx;
    int h = fx.be.create("abc");
    int fd = bindFd(fx.t, fx.be, h, 0x01);
    CHECK_EQ(ChangeSize(fx.t, fd, 8), 0);
    CHECK_EQ((int)fx.be.files[h].data.size(), 8);
    CHECK(fx.be.files[h].data.compare(0, 3, "abc") == 0);
    for (int i = 3; i < 8; ++i) CHECK_EQ((int)fx.be.files[h].data[i], 0);
    // position restored to original (0)
    CHECK_EQ((int)fx.be.files[h].pos, 0);
}

TEST(FileOps4, ChangeSizeShrink) {
    Fixture fx;
    int h = fx.be.create("abcdefgh");
    int fd = bindFd(fx.t, fx.be, h, 0x01);
    fx.be.files[h].pos = 0;
    CHECK_EQ(ChangeSize(fx.t, fd, 3), 0);
    CHECK_EQ((int)fx.be.files[h].data.size(), 3);
    CHECK(fx.be.files[h].data == "abc");
}

// --- DetectDeviceType / LockAndCommit / ValidateHandleMode (lowio table) ----
TEST(FileOps4, DetectDeviceType) {
    Fixture fx;
    LowioTable lt;
    int h = fx.be.create();
    lt.handles[1] = h; lt.count = 4;
    fx.be.fileType = 0;
    CHECK_EQ((int)DetectDeviceType(lt, 1), 0);
    fx.be.fileType = 2;   // FILE_TYPE_CHAR
    CHECK_EQ((int)DetectDeviceType(lt, 1), 1);
}

TEST(FileOps4, LockAndCommitProbesStdFds) {
    Fixture fx;
    LowioTable lt;
    int h = fx.be.create();
    lt.count = 4;
    lt.handles[0] = h;
    lt.info[0] = 0x03;   // byte0 access bits (read+write), not yet probed
    fx.be.fileType = 2;
    guild::u32 info = LockAndCommit(lt, 0);
    // returns the full info dword; byte1 now has 0x40 (probed) + 0x20 (device)
    CHECK_EQ((int)(info & 0xFF), 0x03);              // byte0 access preserved
    CHECK_EQ((int)((info >> 8) & 0x60), 0x60);       // byte1 probed+device
}

TEST(FileOps4, ValidateHandleModeOk) {
    Fixture fx;
    LowioTable lt;
    lt.count = 4;
    lt.info[1] = 0x03;   // byte0: readable(0x01)+writable(0x02)
    fx.be.fileType = 0;
    // want writable (0x02): satisfiable
    CHECK_EQ(ValidateHandleMode(lt, 1, 0x02), 0);
}

TEST(FileOps4, ValidateHandleModeWriteMismatch) {
    Fixture fx;
    guild::crt::Errno() = 0;
    LowioTable lt;
    lt.count = 4;
    lt.info[1] = 0x02;   // byte0: writable only, not readable
    // want must-be-readable (0x01) while actual lacks it -> EINVAL
    CHECK_EQ(ValidateHandleMode(lt, 1, 0x01), -1);
    CHECK_EQ(guild::crt::Errno(), 22);
}

TEST(FileOps4, SetDescriptorEntry) {
    LowioTable lt;
    SetDescriptorEntry(lt, 2, 0x1234);
    // nonzero value forces byte1 0x40 (probed)
    CHECK_EQ((int)(lt.info[2] & 0x4000), 0x4000);
    CHECK_EQ((int)(lt.info[2] & 0xFF), 0x34);
    SetDescriptorEntry(lt, 2, 0);
    CHECK_EQ((int)lt.info[2], 0);
}

// --- OpenWithMode -----------------------------------------------------------
TEST(FileOps4, OpenWithModeReadDisk) {
    Fixture fx;
    fx.be.fileType = 0x10000;   // any non-zero non-2/3 => disk, switch default
    // O_RDONLY, OPEN_EXISTING share=DENYNONE(64)
    int fd = OpenWithMode(fx.t, "x.dat", kO_RDONLY, 64, 0);
    CHECK(fd >= 0);
    CHECK_EQ((int)(fx.t.Entry(fd)->flags & 0x01), 1);   // open
}

TEST(FileOps4, OpenWithModeBadAccess) {
    Fixture fx;
    guild::crt::Errno() = 0;
    // oflag & 3 == 3 is invalid
    CHECK_EQ(OpenWithMode(fx.t, "x", 3, 64, 0), -1);
    CHECK_EQ(guild::crt::Errno(), 22);
}

TEST(FileOps4, OpenWithModeBadShare) {
    Fixture fx;
    guild::crt::Errno() = 0;
    CHECK_EQ(OpenWithMode(fx.t, "x", kO_RDONLY, 99, 0), -1);
    CHECK_EQ(guild::crt::Errno(), 22);
}

// --- ParseModeAndOpen -------------------------------------------------------
TEST(FileOps4, ParseModeReadBinary) {
    Fixture fx;
    fx.be.fileType = 0x10000;
    CrtFile f{};
    CrtFile* r = ParseModeAndOpen(fx.t, "x.bin", "rb", 64, &f);
    CHECK(r == &f);
    CHECK(f.fd >= 0);
    CHECK_EQ((int)(f.flags & 1), 1);   // read flag
}

TEST(FileOps4, ParseModeWritePlus) {
    Fixture fx;
    fx.be.fileType = 0x10000;
    int before = StreamOpenCount();
    CrtFile f{};
    CrtFile* r = ParseModeAndOpen(fx.t, "x", "w+", 64, &f);
    CHECK(r == &f);
    CHECK_EQ((int)(f.flags & 0x80), 0x80);   // '+' sets the 0x80 update bit
    CHECK_EQ(StreamOpenCount(), before + 1);
}

TEST(FileOps4, ParseModeBadString) {
    Fixture fx;
    CrtFile f{};
    CHECK(ParseModeAndOpen(fx.t, "x", "z", 64, &f) == nullptr);   // bad lead char
}

// --- FindFirstEntry / FindClose ---------------------------------------------
namespace {
struct FindState { bool isDir; bool hit; };
FindState g_find;
void* myFindFirst(const char*, guild::shim::DirEntry* first) {
    if (!g_find.hit) return nullptr;
    first->name = "match.txt";
    first->isDir = g_find.isDir;
    first->dosTime = 0;
    return reinterpret_cast<void*>(0x1234);
}
int myFindClose(void* h) { return h ? 1 : 0; }
} // namespace

TEST(FileOps4, FindFirstEntryHit) {
    Fixture fx;
    FileOps4Hooks h = makeHooks();
    h.findFirst = &myFindFirst;
    h.findClose = &myFindClose;
    SetOps4Hooks(&h);
    g_find = {false, true};   // regular file matches mask 0x37
    FindData fd{};
    void* handle = FindFirstEntry("*.txt", &fd);
    CHECK(handle == reinterpret_cast<void*>(0x1234));
    CHECK(std::strcmp(fd.name, "match.txt") == 0);
    CHECK_EQ(FindClose(handle), 0);   // myFindClose returns 1 -> 1-1==0
}

TEST(FileOps4, FindFirstEntryMiss) {
    Fixture fx;
    FileOps4Hooks h = makeHooks();
    h.findFirst = &myFindFirst;
    h.findClose = &myFindClose;
    SetOps4Hooks(&h);
    g_find = {false, false};   // findFirst returns null
    FindData fd{};
    void* handle = FindFirstEntry("*.none", &fd);
    CHECK(handle == reinterpret_cast<void*>((intptr_t)-1));
}

// --- CloseAndFreeEntry (dword_1408760 temp list) ----------------------------
namespace {
int g_flushed = 0;
int myFlushAndFree(CrtFile*) { ++g_flushed; return 0; }
} // namespace

TEST(FileOps4, CloseAndFreeEntryFound) {
    Fixture fx;
    FileOps4Hooks h = makeHooks();
    h.flushAndFree = &myFlushAndFree;
    SetOps4Hooks(&h);
    g_flushed = 0;
    CrtFile fa{}, fb{};
    TempFileNode nb{nullptr, &fb};
    TempFileNode na{&nb, &fa};
    TempFileListHead() = &na;
    CHECK_EQ(CloseAndFreeEntry(&fb), 0);   // second node
    CHECK_EQ(g_flushed, 1);
    TempFileListHead() = nullptr;
}

TEST(FileOps4, CloseAndFreeEntryNotFound) {
    Fixture fx;
    CrtFile fa{}, other{};
    TempFileNode na{nullptr, &fa};
    TempFileListHead() = &na;
    CHECK_EQ(CloseAndFreeEntry(&other), -1);
    TempFileListHead() = nullptr;
}

TEST(FileOps4, CloseAndFreeEntryEmptyList) {
    Fixture fx;
    CrtFile f{};
    TempFileListHead() = nullptr;
    CHECK_EQ(CloseAndFreeEntry(&f), -1);
}
