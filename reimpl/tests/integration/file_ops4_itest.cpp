#include "test.h"

// Integration: drive file_ops4's descriptor-level verbs (OpenWithMode / WriteFd /
// SeekDescriptor / ReadTranslate) against the REAL reconstructed descriptor-table
// sibling (io/file_ops2.cpp's FdTable + AllocHandle 0x14276ae / SetOsHandle
// 0x1427743 / GetOsHandle 0x1427834 / CloseDescriptor 0x1424618 /
// MapOsErrorToErrno). NOT a mock: OpenWithMode allocates a real fd slot in the
// genuine FdTable and binds the OS handle through the real SetOsHandle exactly as
// the live CRT wiring; every later verb resolves the same fd through the real
// GetOsHandle / FdEntry. The OS leaves below the descriptor layer (osOpen / osRead
// / osWrite / osSeek / GetFileType) are Win32 primitives with NO reconstructed
// sibling, so they are backed by a small in-memory OS file via FileOps4Hooks.
#include "io/file_ops4.h"
#include "io/file_ops2.h"   // REAL sibling: FdTable / AllocHandle / GetOsHandle / ...
#include "crt/strtol.h"     // REAL: crt::Errno() (the per-thread errno slot)

#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::io;

namespace {
// A single in-memory OS file standing in for the Win32 primitives (the OS boundary
// file_ops4 layers on; NOT under test). One handle id == 100.
struct OsFile {
    std::vector<char> data;
    long pos = 0;
    bool open = false;
} g_os;
constexpr int kOsHandle = 100;
unsigned g_lastErr = 0;

int OsOpen(const char*, unsigned, int, int, unsigned) {
    g_os = OsFile{};
    g_os.open = true;
    g_lastErr = 0;
    return kOsHandle;                 // a valid OS handle
}
// GetFileType: a real disk file is FILE_TYPE_DISK == 1 (the switch `default` path);
// 0 (FILE_TYPE_UNKNOWN) is the error case in OpenWithMode, and 2/3 are char/pipe.
int OsFileType(int) { return 1; }

long OsSeek(int h, long off, int whence) {
    if (h != kOsHandle || !g_os.open) { g_lastErr = 6; return -1; }
    long base = (whence == 0) ? 0
              : (whence == 1) ? g_os.pos
                              : static_cast<long>(g_os.data.size());   // SEEK_END
    long t = base + off;
    if (t < 0) { g_lastErr = 131; return -1; }   // ERROR_NEGATIVE_SEEK
    g_os.pos = t;
    g_lastErr = 0;
    return t;
}
int OsWrite(int h, const char* buf, unsigned n, int* written) {
    if (h != kOsHandle || !g_os.open) { g_lastErr = 6; return 0; }
    if (static_cast<long>(g_os.data.size()) < g_os.pos + static_cast<long>(n))
        g_os.data.resize(g_os.pos + n);
    std::memcpy(g_os.data.data() + g_os.pos, buf, n);
    g_os.pos += n;
    *written = static_cast<int>(n);
    g_lastErr = 0;
    return 1;
}
int OsRead(int h, char* buf, unsigned n, int* got) {
    if (h != kOsHandle || !g_os.open) { g_lastErr = 6; return 0; }
    long avail = static_cast<long>(g_os.data.size()) - g_os.pos;
    if (avail < 0) avail = 0;
    unsigned take = (n < static_cast<unsigned>(avail)) ? n : static_cast<unsigned>(avail);
    std::memcpy(buf, g_os.data.data() + g_os.pos, take);
    g_os.pos += take;
    *got = static_cast<int>(take);
    g_lastErr = 0;
    return 1;
}
unsigned LastErr() { return g_lastErr; }

FileOps4Hooks MakeHooks() {
    FileOps4Hooks h{};                // all-null inert defaults
    h.osOpen   = &OsOpen;
    h.osSeek   = &OsSeek;
    h.osWrite  = &OsWrite;
    h.osRead   = &OsRead;
    h.fileType = &OsFileType;
    h.lastError = &LastErr;
    return h;
}
} // namespace

// OpenWithMode allocates a fd through the REAL FdTable, binds the OS handle via the
// REAL SetOsHandle; the fd's bound handle is the one GetOsHandle returns. Then a
// binary write -> seek-to-start -> read round-trips through that real fd.
TEST(FileOps4Itest, OpenWriteSeekReadRoundTripsRealFd) {
    g_os = OsFile{};
    FileOps4Hooks h = MakeHooks();
    FileOps4Hooks prev = SetOps4Hooks(&h);

    FdTable t;
    InitIoTable(t);                   // real table init (binds std fds 0..2)

    // O_RDWR | O_BINARY | O_CREAT, share-none, writable pmode.
    guild::u16 oflag = static_cast<guild::u16>(kO_RDWR | kO_BINARY | kO_CREAT);
    int fd = OpenWithMode(t, "scratch.bin", oflag, /*share*/16, /*pmode*/0);

    CHECK(fd >= 0);                   // a real fd was allocated
    if (fd >= 0) {
        // The REAL descriptor table bound our OS handle to this fd.
        CHECK_EQ(GetOsHandle(t, fd), kOsHandle);
        FdEntry* e = t.Entry(fd);
        CHECK(e != nullptr);
        if (e) {
            CHECK((e->flags & 0x01) != 0);   // slot open
            CHECK((e->flags & 0x80) == 0);   // binary -> no text bit
        }

        const char msg[] = "Gilde";   // 5 bytes
        int wrote = WriteFd(t, fd, msg, 5);
        CHECK_EQ(wrote, 5);

        // Seek back to start through the real fd (clears the pending-read bit).
        int off = SeekDescriptor(t, fd, 0, 0);
        CHECK_EQ(off, 0);

        char rbuf[8] = {0};
        unsigned got = ReadTranslate(t, fd, rbuf, 5);
        CHECK_EQ(got, 5u);
        CHECK_EQ(std::memcmp(rbuf, "Gilde", 5), 0);

        // The bytes really landed in the backing OS file.
        CHECK_EQ(g_os.data.size(), static_cast<size_t>(5));
    }

    t.Free();
    SetOps4Hooks(&prev);
}

// A verb on a fd whose slot is NOT open routes through the real table's bounds
// check and sets errno EBADF (9) via the real descriptor layer.
TEST(FileOps4Itest, SeekBadFdSetsEbadfThroughRealTable) {
    FileOps4Hooks h = MakeHooks();
    FileOps4Hooks prev = SetOps4Hooks(&h);

    FdTable t;
    InitIoTable(t);

    crt::Errno() = 0;
    int off = SeekDescriptor(t, /*fd*/200, 0, 0);   // out of range / not open
    CHECK_EQ(off, -1);
    CHECK_EQ(crt::Errno(), 9);                       // EBADF

    t.Free();
    SetOps4Hooks(&prev);
}

// Append mode (O_APPEND): WriteFd seeks to end first, so two writes concatenate in
// the real backing file and the second write lands after the first.
TEST(FileOps4Itest, AppendWriteSeeksToEnd) {
    g_os = OsFile{};
    FileOps4Hooks h = MakeHooks();
    FileOps4Hooks prev = SetOps4Hooks(&h);

    FdTable t;
    InitIoTable(t);

    guild::u16 oflag = static_cast<guild::u16>(kO_RDWR | kO_BINARY | kO_CREAT | kO_APPEND);
    int fd = OpenWithMode(t, "log.bin", oflag, 16, 0);
    CHECK(fd >= 0);
    if (fd >= 0) {
        FdEntry* e = t.Entry(fd);
        CHECK(e != nullptr);
        if (e) CHECK((e->flags & 0x20) != 0);   // O_APPEND flag set on the real entry

        CHECK_EQ(WriteFd(t, fd, "AB", 2), 2);
        // Move the cursor backwards; append must still write to the end.
        SeekDescriptor(t, fd, 0, 0);
        CHECK_EQ(WriteFd(t, fd, "CD", 2), 2);

        SeekDescriptor(t, fd, 0, 0);
        char rbuf[8] = {0};
        unsigned got = ReadTranslate(t, fd, rbuf, 4);
        CHECK_EQ(got, 4u);
        CHECK_EQ(std::memcmp(rbuf, "ABCD", 4), 0);
    }

    t.Free();
    SetOps4Hooks(&prev);
}

// osOpen failure -> OpenWithMode releases the allocated slot via the REAL
// CloseDescriptor and maps the OS error, returning -1.
TEST(FileOps4Itest, OpenFailureReleasesRealSlot) {
    FileOps4Hooks h = MakeHooks();
    h.osOpen = [](const char*, unsigned, int, int, unsigned) -> int { return -1; };
    h.lastError = []() -> unsigned { return 2; };   // ERROR_FILE_NOT_FOUND
    FileOps4Hooks prev = SetOps4Hooks(&h);

    FdTable t;
    InitIoTable(t);

    int fd = OpenWithMode(t, "missing", static_cast<guild::u16>(kO_RDONLY | kO_BINARY), 16, 0);
    CHECK_EQ(fd, -1);

    t.Free();
    SetOps4Hooks(&prev);
}
