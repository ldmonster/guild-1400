// Unit tests for guild::io file_ops2 — CRT descriptor table, buffer helpers,
// and Win32-error->errno mapping. Golden vectors computed independently in python.
#include "io/file_ops2.h"
#include "crt/strtol.h"
#include "test.h"

using namespace guild::io;

// ---------------------------------------------------------------------------
// MapOsErrorToErrno — golden vectors (gilde.exe 0x1427871, table @0x1455170)
// ---------------------------------------------------------------------------
TEST(FileOps2, MapErrnoTableHits) {
    CHECK_EQ(MapOsErrorToErrno(2), 2);     // ERROR_FILE_NOT_FOUND -> ENOENT
    CHECK_EQ(MapOsErrorToErrno(5), 13);    // ERROR_ACCESS_DENIED  -> EACCES
    CHECK_EQ(MapOsErrorToErrno(1), 22);    // ERROR_INVALID_FUNCTION -> EINVAL
    CHECK_EQ(MapOsErrorToErrno(80), 17);   // ERROR_FILE_EXISTS -> EEXIST
    CHECK_EQ(MapOsErrorToErrno(1816), 12); // last table entry -> ENOMEM
    // _doserrno records the raw OS error.
    MapOsErrorToErrno(123u);
    CHECK_EQ(DosErrno(), 123);
}

TEST(FileOps2, MapErrnoRanges) {
    CHECK_EQ(MapOsErrorToErrno(0x13), 13);  // [0x13,0x24] -> EACCES
    CHECK_EQ(MapOsErrorToErrno(0x24), 13);
    CHECK_EQ(MapOsErrorToErrno(0xBC), 8);   // [0xBC,0xCA] -> ENOEXEC
    CHECK_EQ(MapOsErrorToErrno(0xCA), 8);
    CHECK_EQ(MapOsErrorToErrno(999), 22);   // default -> EINVAL
    CHECK_EQ(MapOsErrorToErrno(0xCB), 22);  // just past the ENOEXEC range
}

// ---------------------------------------------------------------------------
// FdTable alloc/get/set/free
// ---------------------------------------------------------------------------
TEST(FileOps2, AllocHandleSequential) {
    FdTable t;
    // No std-stream binding (raw alloc); InitIoTable consumes 0..2.
    InitIoTable(t);
    CHECK_EQ(t.count, FdTable::kInitialCap);

    int fd = AllocHandle(t);
    CHECK(fd >= 3);                         // 0..2 are the std streams
    // The slot is marked osHandle=-1 but NOT open yet (flags bit0 set by SetOsHandle?
    // No: the original marks it open via the flags byte elsewhere; AllocHandle only
    // clears osHandle). Bind a handle through the open path: set flags+handle.
    FdEntry* e = t.Entry(fd);
    CHECK(e != nullptr);
    e->flags |= kFdOpen;
    CHECK_EQ(SetOsHandle(t, static_cast<unsigned>(fd), 0x1234), 0);
    CHECK_EQ(GetOsHandle(t, fd), 0x1234);

    // Free returns the slot; GetOsHandle then reports the freed handle (-1) but the
    // slot remains "open" until CloseDescriptor clears the flags.
    CHECK_EQ(FreeHandle(t, static_cast<unsigned>(fd)), 0);
    CHECK_EQ(GetOsHandle(t, fd), -1);
}

TEST(FileOps2, GetOsHandleBadFd) {
    FdTable t;
    InitIoTable(t);
    guild::crt::Errno() = 0;
    CHECK_EQ(GetOsHandle(t, 9999), -1);     // out of range
    CHECK_EQ(guild::crt::Errno(), kEBADF);
    CHECK_EQ(DosErrno(), 0);
    // An allocated-but-not-open slot also fails (flags bit0 clear).
    int fd = AllocHandle(t);
    CHECK_EQ(GetOsHandle(t, fd), -1);
}

TEST(FileOps2, SetOsHandleRejectsOccupied) {
    FdTable t;
    InitIoTable(t);
    int fd = AllocHandle(t);
    t.Entry(fd)->flags |= kFdOpen;
    CHECK_EQ(SetOsHandle(t, static_cast<unsigned>(fd), 77), 0);
    // osHandle is no longer -1, so a second bind is rejected with EBADF.
    guild::crt::Errno() = 0;
    CHECK_EQ(SetOsHandle(t, static_cast<unsigned>(fd), 88), -1);
    CHECK_EQ(guild::crt::Errno(), kEBADF);
    CHECK_EQ(GetOsHandle(t, fd), 77);
}

TEST(FileOps2, FreeHandleBadFd) {
    FdTable t;
    InitIoTable(t);
    int fd = AllocHandle(t);          // open=false, osHandle=-1
    guild::crt::Errno() = 0;
    CHECK_EQ(FreeHandle(t, static_cast<unsigned>(fd)), -1); // not open
    CHECK_EQ(guild::crt::Errno(), kEBADF);
}

TEST(FileOps2, AllocGrowsBlocks) {
    FdTable t;
    InitIoTable(t);                   // 1 block, count 32
    // Consume the whole first block.
    int last = -1;
    for (int i = 0; i < 40; ++i) {
        int fd = AllocHandle(t);
        CHECK(fd >= 0);
        t.Entry(fd)->flags |= kFdOpen; // keep them in use so the next alloc advances
        last = fd;
    }
    CHECK(last >= 32);                // grew past the first block
    CHECK(t.count >= 64);            // a second block was allocated
}

// ---------------------------------------------------------------------------
// LockHandle — golden vectors
// ---------------------------------------------------------------------------
TEST(FileOps2, LockHandlePacking) {
    FdTable t;
    InitIoTable(t);
    int fd = AllocHandle(t);
    FdEntry* e = t.Entry(fd);
    e->flags = kFdOpen;               // 0x01, high bit clear

    // lock (0x4000) when not previously set -> returns 0x8000, sets bit 0x80.
    CHECK_EQ(LockHandle(t, static_cast<unsigned>(fd), 0x4000), 0x8000);
    CHECK_EQ(e->flags, 0x81);
    // lock again when already set -> high bit was set -> returns 0x4000
    // (the original's LOWORD(0xFFFFFFFF & 0xC000) + 0x8000 wraps to 0x4000).
    CHECK_EQ(LockHandle(t, static_cast<unsigned>(fd), 0x4000), 0x4000);
    CHECK_EQ(e->flags, 0x81);
    // unlock (0x8000) when set -> returns 0x4000, clears bit 0x80.
    CHECK_EQ(LockHandle(t, static_cast<unsigned>(fd), 0x8000), 0x4000);
    CHECK_EQ(e->flags, 0x01);
    // unlock when not set -> returns 0x8000.
    CHECK_EQ(LockHandle(t, static_cast<unsigned>(fd), 0x8000), 0x8000);
    CHECK_EQ(e->flags, 0x01);
}

TEST(FileOps2, LockHandleErrors) {
    FdTable t;
    InitIoTable(t);
    int fd = AllocHandle(t);
    t.Entry(fd)->flags = kFdOpen;
    guild::crt::Errno() = 0;
    CHECK_EQ(LockHandle(t, static_cast<unsigned>(fd), 0x1234), -1);  // bad op
    CHECK_EQ(guild::crt::Errno(), kEINVAL);
    guild::crt::Errno() = 0;
    CHECK_EQ(LockHandle(t, 9999u, 0x4000), -1);                      // bad fd
    CHECK_EQ(guild::crt::Errno(), kEBADF);
}

// ---------------------------------------------------------------------------
// Stream-buffer helpers
// ---------------------------------------------------------------------------
TEST(FileOps2, AllocStreamBuffer4K) {
    IoBuf b{};
    b.flags = 0x01;                   // opened for read (a real stream has r/w bit)
    int before = StreamBufferAllocCount();
    char* p = AllocStreamBuffer(&b);
    CHECK(p != nullptr);
    CHECK_EQ(b.bufSize, 4096);
    CHECK((b.flags & 0x08) != 0);     // owns heap buffer
    CHECK_EQ(b.ptr, p);
    CHECK_EQ(b.base, p);
    CHECK_EQ(b.cnt, 0);
    CHECK_EQ(StreamBufferAllocCount(), before + 1);

    // Free the heap buffer; flags 0x08/0x400 cleared, pointers nulled.
    CHECK_EQ(FreeStreamBuffer(&b), 0);
    CHECK_EQ(b.base, (char*)nullptr);
    CHECK_EQ(b.ptr, (char*)nullptr);
    CHECK((b.flags & 0x08) == 0);
}

TEST(FileOps2, FreeStreamBufferNoOp) {
    IoBuf b{};
    b.flags = 0;                      // no buffer owned
    CHECK_EQ(FreeStreamBuffer(&b), 0); // (flags & 0x83)==0 -> returns flags (0)
    b.flags = 0x01;                   // read-mode but no 0x08 owned buffer
    CHECK_EQ(FreeStreamBuffer(&b), 0x01);
}

// ---------------------------------------------------------------------------
// Commit / CloseDescriptor with hooks
// ---------------------------------------------------------------------------
static int g_closed = -1;
static int testCloseHandle(int h) { g_closed = h; return 1; }
static int testGetType(int) { return 0; }

TEST(FileOps2, CloseDescriptorViaHook) {
    OsHooks() = FdOsHooks{};          // reset
    OsHooks().closeHandle = &testCloseHandle;
    OsHooks().getFileType = &testGetType;
    g_closed = -1;

    FdTable t;
    InitIoTable(t);
    int fd = AllocHandle(t);
    FdEntry* e = t.Entry(fd);
    e->flags |= kFdOpen;
    SetOsHandle(t, static_cast<unsigned>(fd), 0x55);

    CHECK_EQ(CloseDescriptor(t, static_cast<unsigned>(fd)), 0);
    CHECK_EQ(g_closed, 0x55);          // the OS handle was closed
    CHECK_EQ(e->flags, 0);             // flags cleared
    OsHooks() = FdOsHooks{};           // restore inert defaults
}

TEST(FileOps2, CommitInert) {
    OsHooks() = FdOsHooks{};
    FdTable t;
    InitIoTable(t);
    int fd = AllocHandle(t);
    t.Entry(fd)->flags |= kFdOpen;
    SetOsHandle(t, static_cast<unsigned>(fd), 7);
    CHECK_EQ(Commit(t, fd), 0);        // inert commit succeeds
    guild::crt::Errno() = 0;
    CHECK_EQ(Commit(t, 9999), -1);     // bad fd
    CHECK_EQ(guild::crt::Errno(), kEBADF);
}
