// End-to-end flow across guild::io file_ops4: open a text-mode stream through the
// fopen-mode parser, write lines (LF->CRLF on disk), reopen and read them back
// (CRLF->LF), truncate, and exercise the temp-FILE close path. A single in-memory
// OS-handle backend persists across the open/close so the bytes round-trip
// through the same descriptor-table machinery the original used.
#include "test.h"

#include "io/file_ops4.h"
#include "io/file_ops2.h"
#include "crt/strtol.h"

#include <cstring>
#include <map>
#include <string>

using namespace guild::io;

namespace {

struct OsFile { std::string data; long pos = 0; };

struct Backend {
    std::map<int, OsFile> files;
    std::map<std::string, int> byName;   // path -> osHandle (persisted)
    int nextHandle = 200;
    unsigned lastErr = 0;
    std::string pendingOpenPath;
};

Backend* g_b = nullptr;

long bSeek(int h, long off, int w) {
    auto& f = g_b->files[h];
    long base = (w == 0) ? 0 : (w == 1) ? f.pos : (long)f.data.size();
    long t = base + off;
    if (t < 0) { g_b->lastErr = 131; return -1; }
    f.pos = t; return t;
}
int bWrite(int h, const char* buf, unsigned n, int* wr) {
    auto& f = g_b->files[h];
    if ((size_t)f.pos > f.data.size()) f.data.resize(f.pos, '\0');
    if ((size_t)f.pos + n > f.data.size()) f.data.resize(f.pos + n);
    std::memcpy(&f.data[f.pos], buf, n);
    f.pos += n; *wr = (int)n; return 1;
}
int bRead(int h, char* buf, unsigned n, int* got) {
    auto& f = g_b->files[h];
    unsigned avail = (f.pos < (long)f.data.size()) ? (unsigned)(f.data.size() - f.pos) : 0;
    if (n > avail) n = avail;
    std::memcpy(buf, f.data.data() + f.pos, n);
    f.pos += n; *got = (int)n; return 1;
}
int bChsize(int h) { auto& f = g_b->files[h]; f.data.resize(f.pos); return 1; }
int bOpen(const char* path, unsigned, int, int disp, unsigned) {
    std::string p = path;
    auto it = g_b->byName.find(p);
    int h;
    if (it == g_b->byName.end()) {
        h = g_b->nextHandle++;
        g_b->byName[p] = h;
        g_b->files[h].data.clear();
    } else {
        h = it->second;
    }
    // disp 2 (CREATE_ALWAYS) / 5 (TRUNCATE_EXISTING) truncate
    if (disp == 2 || disp == 5) g_b->files[h].data.clear();
    g_b->files[h].pos = 0;
    return h;
}
unsigned bLastError() { return g_b->lastErr; }
int bFileType(int) { return 0x10000; }   // non-zero, not char/pipe => disk

FileOps4Hooks makeHooks() {
    FileOps4Hooks h;
    h.osSeek = &bSeek; h.osWrite = &bWrite; h.osRead = &bRead;
    h.osChsize = &bChsize; h.osOpen = &bOpen;
    h.lastError = &bLastError; h.fileType = &bFileType;
    return h;
}

} // namespace

TEST(FileOps4E2E, TextRoundTripThroughModeParser) {
    Backend b; g_b = &b;
    FdTable t;
    FileOps4Hooks h = makeHooks();
    FileOps4Hooks prev = SetOps4Hooks(&h);

    // 1. open "wt" (write text), write two LF-terminated lines.
    CrtFile wf{};
    CrtFile* wr = ParseModeAndOpen(t, "log.txt", "wt", 64, &wf);
    CHECK(wr == &wf);
    int wfd = wf.fd;
    // mark the descriptor text-mode (the mode parser only fills the FILE flags;
    // OpenWithMode set the fd flag — verify it is text).
    CHECK_EQ((int)(t.Entry(wfd)->flags & 0x80), 0x80);

    const char* line1 = "alpha\n";
    const char* line2 = "beta\n";
    CHECK_EQ(WriteFd(t, wfd, line1, 6), 6);
    CHECK_EQ(WriteFd(t, wfd, line2, 5), 5);
    // on disk the LFs are CRLF
    int diskHandle = b.byName["log.txt"];
    CHECK(b.files[diskHandle].data == std::string("alpha\r\nbeta\r\n"));
    CloseDescriptor(t, (unsigned)wfd);

    // 2. reopen "rt" (read text), read everything back with CRLF->LF.
    CrtFile rf{};
    CrtFile* rr = ParseModeAndOpen(t, "log.txt", "rt", 64, &rf);
    CHECK(rr == &rf);
    int rfd = rf.fd;
    char buf[64] = {};
    unsigned n = ReadTranslate(t, rfd, buf, 63);
    CHECK_EQ((int)n, 11);
    CHECK(std::string(buf, n) == "alpha\nbeta\n");
    CloseDescriptor(t, (unsigned)rfd);

    // 3. open binary, append a chunk, then truncate back.
    CrtFile bf{};
    CHECK(ParseModeAndOpen(t, "data.bin", "wb", 64, &bf) == &bf);
    int bfd = bf.fd;
    CHECK_EQ(WriteFd(t, bfd, "0123456789", 10), 10);
    CHECK_EQ(SeekDescriptor(t, bfd, 0, 2), 10);
    CHECK_EQ(ChangeSize(t, bfd, 4), 0);
    int bin = b.byName["data.bin"];
    CHECK_EQ((int)b.files[bin].data.size(), 4);
    CHECK(b.files[bin].data == "0123");
    // grow back to 7 — tail zero-filled
    CHECK_EQ(ChangeSize(t, bfd, 7), 0);
    CHECK_EQ((int)b.files[bin].data.size(), 7);
    CHECK_EQ((int)b.files[bin].data[6], 0);
    CloseDescriptor(t, (unsigned)bfd);

    SetOps4Hooks(&prev);
    g_b = nullptr;
}

TEST(FileOps4E2E, TempFileListLifecycle) {
    FileOps4Hooks prev = SetOps4Hooks(nullptr);   // inert defaults (flushAndFree -> 0)
    CrtFile a{}, c{};
    TempFileNode n2{nullptr, &c};
    TempFileNode n1{&n2, &a};
    TempFileListHead() = &n1;

    // tracked entries are flushed-and-freed (returns 0 with inert default)
    CHECK_EQ(CloseAndFreeEntry(&a), 0);
    CHECK_EQ(CloseAndFreeEntry(&c), 0);
    // an untracked FILE returns -1
    CrtFile stray{};
    CHECK_EQ(CloseAndFreeEntry(&stray), -1);

    TempFileListHead() = nullptr;
    SetOps4Hooks(&prev);
}
