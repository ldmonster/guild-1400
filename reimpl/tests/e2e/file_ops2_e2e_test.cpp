// End-to-end: drive the CRT descriptor table the way the low-level open/close
// path does — InitIoTable binds the std streams via OS hooks, then a synthetic
// "open" allocates an fd, binds a handle, and a synthetic "close" releases it,
// with the Win32-error->errno mapping observed on a failed close.
#include "io/file_ops2.h"
#include "crt/strtol.h"
#include "test.h"

using namespace guild::io;

namespace {
// A fake OS handle source. openStd hands out 100,101,102 for stdin/out/err;
// getFileType classifies handle 102 (stderr) as a char device (type 2);
// closeHandle records which handles were closed.
int g_nextHandle = 200;
int g_lastClosed = -1;
int g_failCloseHandle = -1;

int fakeOpenStd(int stdId) {
    switch (stdId) {
        case -10: return 100;  // stdin
        case -11: return 101;  // stdout
        case -12: return 102;  // stderr
        default:  return -1;
    }
}
int fakeGetFileType(int h) { return (h == 102) ? 2 : 0; }  // stderr is a device
int fakeClose(int h) {
    g_lastClosed = h;
    if (h == g_failCloseHandle) {
        DosErrno() = 5;     // pretend ERROR_ACCESS_DENIED leaked through
        return 0;           // fail
    }
    return 1;
}
} // namespace

TEST(FileOps2E2E, InitBindUseClose) {
    OsHooks() = FdOsHooks{};
    OsHooks().openStd     = &fakeOpenStd;
    OsHooks().getFileType = &fakeGetFileType;
    OsHooks().closeHandle = &fakeClose;
    g_lastClosed = -1;
    g_failCloseHandle = -1;

    FdTable t;
    CHECK_EQ(InitIoTable(t), 0);
    CHECK_EQ(t.count, FdTable::kInitialCap);

    // fds 0..2 bound to the std handles, flags 0x81 base, with device classification.
    CHECK_EQ(GetOsHandle(t, 0), 100);
    CHECK_EQ(GetOsHandle(t, 1), 101);
    CHECK_EQ(GetOsHandle(t, 2), 102);
    CHECK((t.Entry(0)->flags & kFdAscii) != 0);
    // stderr (type 2) sets the 0x40 bit; stdin/out (type 0) also set 0x40.
    CHECK((t.Entry(2)->flags & 0x40) != 0);

    // ---- open() flow: AllocHandle -> SetOsHandle ----
    int fd = AllocHandle(t);
    CHECK(fd >= 3);
    FdEntry* e = t.Entry(fd);
    e->flags |= kFdOpen;                       // open() marks the slot in use
    int h = g_nextHandle++;                    // a freshly created OS handle
    CHECK_EQ(SetOsHandle(t, static_cast<unsigned>(fd), h), 0);
    CHECK_EQ(GetOsHandle(t, fd), h);

    // ---- commit + close() flow ----
    CHECK_EQ(Commit(t, fd), 0);                // inert commit ok
    CHECK_EQ(CloseDescriptor(t, static_cast<unsigned>(fd)), 0);
    CHECK_EQ(g_lastClosed, h);                 // the real handle was closed
    CHECK_EQ(t.Entry(fd)->flags, 0);           // slot released

    // ---- reuse: the freed fd is handed back out ----
    int fd2 = AllocHandle(t);
    CHECK_EQ(fd2, fd);                          // first free slot reused

    OsHooks() = FdOsHooks{};
}

TEST(FileOps2E2E, CloseFailureMapsErrno) {
    OsHooks() = FdOsHooks{};
    OsHooks().openStd     = &fakeOpenStd;
    OsHooks().getFileType = &fakeGetFileType;
    OsHooks().closeHandle = &fakeClose;

    FdTable t;
    InitIoTable(t);
    int fd = AllocHandle(t);
    t.Entry(fd)->flags |= kFdOpen;
    int h = 555;
    SetOsHandle(t, static_cast<unsigned>(fd), h);

    g_failCloseHandle = h;                      // make the close fail (OS error 5)
    guild::crt::Errno() = 0;
    CHECK_EQ(CloseDescriptor(t, static_cast<unsigned>(fd)), -1);
    // OS error 5 (ACCESS_DENIED) -> errno EACCES (13) via the mapping table.
    CHECK_EQ(guild::crt::Errno(), 13);
    CHECK_EQ(DosErrno(), 5);
    g_failCloseHandle = -1;
    OsHooks() = FdOsHooks{};
}
