#pragma once
// gilde.exe — guild::io  (MODULE: file_ops2 — CRT file-descriptor table + low-level
// File_* OS ops, batch 2 of VIBE_File_* / VIBE_Vfs_*)
//
// The original ships a Watcom/Borland-style C runtime whose low-level open()/read()/
// write()/close() route through a *file-descriptor table*: a two-level array that
// maps a small integer fd to an OS HANDLE plus a per-fd flags byte. This module
// reconstructs that table and the bookkeeping around it 1:1, plus the pure
// Win32-error->errno mapping table and the loose OS file operations (delete/rename),
// all routed through shim::IFileSystem so no platform calls leak into game code.
//
// Recovered descriptor-table geometry (from VIBE_File_InitIoTable @0x14266b1,
// AllocHandle @0x14276ae, Get/SetOsHandle @0x1427834/0x1427743, FreeHandle
// @0x14277ba):
//   dword_1464F20[]  : 32 block pointers (the "page directory"). Index = fd>>5.
//   dword_1465020    : count of *allocated* descriptor slots (grows by 32).
//   Each block is 256 bytes = 32 entries of 8 bytes:
//       +0x00 dword  osHandle   (the OS HANDLE; -1 == no handle bound)
//       +0x04 byte   flags      (bit0 0x01 = in-use/open; 0x08 device/pipe;
//                                0x20 device-detected; 0x40 EOF-ish; 0x80 ascii/atty;
//                                0x7F .. high-bit toggled by LockHandle)
//       +0x05 byte   peekChar   (CR/LF text-translate lookahead; init 10 = '\n')
//       +0x06 word   (padding)
//   dword_145A1DC    : the CRT errno slot (== guild::crt::Errno()).
//   dword_145A1E0    : the CRT _doserrno slot (last OS error).
//
#include "guild/common/types.h"
#include "shim/IFileSystem.h"
#include <cstddef>

namespace guild::io {

// ---------------------------------------------------------------------------
// errno / _doserrno model
// ---------------------------------------------------------------------------
// The table writes dword_145A1DC (errno) and dword_145A1E0 (_doserrno). errno is
// the shared per-thread slot guild::crt::Errno(); _doserrno lives here.
guild::i32& DosErrno();   // dword_145A1E0

// errno values used by this module (match the CRT's <errno.h> numbering).
enum FileErrno : int {
    kEBADF  = 9,    // bad file descriptor
    kEINVAL = 22,   // invalid argument
};

// ---------------------------------------------------------------------------
// File-descriptor table
// ---------------------------------------------------------------------------
// One 8-byte entry per fd.
struct FdEntry {
    guild::i32 osHandle;   // +0x00  bound OS handle id (-1 = none)
    guild::u8  flags;      // +0x04  in-use(0x01)/device(0x08)/dev-detected(0x20)/eof(0x40)/ascii(0x80)
    guild::u8  peekChar;   // +0x05  CR/LF lookahead (init 0x0A)
    guild::u8  pad6;       // +0x06
    guild::u8  pad7;       // +0x07
};

// FdEntry flag bits (+0x04).
enum FdFlags : guild::u8 {
    kFdOpen        = 0x01,   // slot in use / fd open
    kFdDevice      = 0x08,   // character device / pipe
    kFdDevDetected = 0x20,   // device type already probed
    kFdEof         = 0x40,
    kFdAscii       = 0x80,   // console / atty (high bit, toggled by LockHandle)
};

// The two-level descriptor table. Models dword_1464F20[] (block pointers) and
// dword_1465020 (allocated count). Each block holds 32 FdEntry. A fresh table
// has 0 blocks; InitIoTable() lazily allocates the first.
struct FdTable {
    static constexpr int kBlocks      = 32;   // dword_1464F20 size
    static constexpr int kPerBlock    = 32;   // entries per 256-byte block
    static constexpr int kInitialCap  = 32;   // dword_1465020 starts at 32

    FdEntry* blocks[kBlocks] = {};   // dword_1464F20 (nullptr = unallocated)
    int      count = 0;              // dword_1465020 (allocated slot count)

    ~FdTable() { Free(); }
    void Free();                     // release all blocks (test teardown)
    FdEntry* Entry(int fd);          // -> entry for fd, or nullptr if out of range
};

// Cross-module OS leaves. The descriptor table's init/close paths call a handful
// of OS primitives (open the std handles, probe device type, low-level close).
// They are not part of this module; install real backends from app code, or leave
// the inert defaults (which behave like "no console, regular file") for tests.
struct FdOsHooks {
    // VIBE_File_InitIoTable: open one of the three standard streams. `stdId` is
    // -10/-11/-12 (stdin/stdout/stderr). Returns an OS handle or -1.
    int (*openStd)(int stdId) = nullptr;
    // VIBE_File_*: classify an OS handle. Returns 0=disk file, 2=char device,
    // 3=pipe (matching GetFileType's FILE_TYPE_*). Default: 0 (disk).
    int (*getFileType)(int osHandle) = nullptr;
    // VIBE_File_CloseDescriptor: low-level CloseHandle. Returns nonzero on success.
    int (*closeHandle)(int osHandle) = nullptr;
    // VIBE_File_CloseDescriptor / FreeHandle notify hook (dword_146777C). Receives
    // the std id (-10/-11/-12) and new handle on rebind. Default: ignored.
    void (*notifyStdRebind)(int stdId, int osHandle) = nullptr;
    // VIBE_File_InvokeLockHook (dword_145A26C). Returns nonzero to claim the fd.
    int (*lockHook)(int fd) = nullptr;
    // VIBE_File_Commit: low-level FlushFileBuffers/_commit. Returns nonzero on ok.
    int (*commit)(int osHandle) = nullptr;
};
FdOsHooks& OsHooks();

// gilde.exe 0x14266b1 — VIBE_File_InitIoTable. Allocate the first descriptor block,
// mark every slot free (osHandle=-1, flags=0, peekChar=10), then bind fds 0..2 to
// the std handles via OsHooks().openStd, probing device type. (Returns 0.)
int InitIoTable(FdTable& t);

// gilde.exe 0x14276ae — VIBE_File_AllocHandle. Find the first free slot (flags
// bit0 clear), mark its osHandle=-1, and return its fd; grow by a new 32-entry
// block if all are in use. Returns -1 on out-of-memory. (eax = fd)
int AllocHandle(FdTable& t);

// gilde.exe 0x1427743 — VIBE_File_SetOsHandle. Bind fd's osHandle to `osHandle`
// (only valid when the slot's osHandle is currently -1). Returns 0, or -1 with
// errno=EBADF on a bad/occupied fd.
int SetOsHandle(FdTable& t, unsigned int fd, int osHandle);

// gilde.exe 0x1427834 — VIBE_File_GetOsHandle. Return fd's bound osHandle, or -1
// with errno=EBADF if the fd is out of range or not open.
int GetOsHandle(FdTable& t, int fd);

// gilde.exe 0x14277ba — VIBE_File_FreeHandle. Release fd (osHandle must be set and
// the slot open): sets osHandle=-1, returns 0. Returns -1/EBADF otherwise. (The
// flags byte is left intact, matching the original.)
int FreeHandle(FdTable& t, unsigned int fd);

// gilde.exe 0x1429b12 — VIBE_File_LockHandle. `op` 0x4000 sets the 0x80 (ascii)
// bit, 0x8000 clears it. Returns the packed previous state: bit15 always set, and
// bit14 set iff the bit was previously set (i.e. 0xC000 if previously set, 0x8000
// if not). Returns -1 with errno on a bad fd (EBADF) or bad op (EINVAL).
int LockHandle(FdTable& t, unsigned int fd, int op);

// gilde.exe 0x1422b3f — VIBE_File_Commit. Flush fd's OS buffers via OsHooks().
// commit. Returns 0 on success, -1 with errno=EBADF on a bad fd or failure.
int Commit(FdTable& t, int fd);

// gilde.exe 0x1424618 — VIBE_File_CloseDescriptor. Low-level close: if fd has a
// real handle that isn't a shared std-handle alias, close it via OsHooks();
// release the slot and clear its flags. Returns 0 / -1 (errno).
int CloseDescriptor(FdTable& t, unsigned int fd);

// gilde.exe 0x14246f6 — VIBE_File_InvokeLockHook. Return OsHooks().lockHook(fd)
// (0 if no hook installed).
bool InvokeLockHook(int fd);

// ---------------------------------------------------------------------------
// Stream-buffer allocation helpers (FILE*-style record at *a1)
// ---------------------------------------------------------------------------
// Minimal mirror of the buffered-FILE record these helpers touch. Layout matches
// the original 32-bit _iobuf prefix (dword indices 0..6) used by AllocStreamBuffer
// / FreeStreamBuffer (a1[2] = buffer base, a1[3] = flags, a1[6] = buffer size).
struct IoBuf {
    char*      ptr;        // +0x00  (a1[0]) cursor
    guild::i32 cnt;        // +0x04  (a1[1]) bytes ahead of cursor
    char*      base;       // +0x08  (a1[2]) buffer base (or &smallBuf on OOM)
    guild::u32 flags;      // +0x0C  (a1[3]) flags (0x04 unbuffered, 0x08 owned-buf)
    guild::i32 spare4;     // +0x10  (a1[4]) fd (unused here)
    char       smallBuf[2];// +0x14  (a1[5]) 2-byte fallback buffer (a1+5)
    guild::u8  smallPad[2];
    guild::i32 bufSize;    // +0x18? (a1[6]) buffer capacity
};

// gilde.exe 0x1427f81 — VIBE_File_AllocStreamBuffer. Allocate a 4096-byte buffer
// (flags|=0x08, size=4096). On OOM falls back to the 2-byte inline buffer at a1+5
// (flags|=0x04, size=2). Sets ptr=base, cnt=0. Returns the buffer base. The global
// "buffers allocated" counter (dword_145A25C) is incremented.
char* AllocStreamBuffer(IoBuf* b);
int& StreamBufferAllocCount();   // dword_145A25C (test inspection)

// gilde.exe 0x14246cb — VIBE_File_FreeStreamBuffer. If the record owns a heap
// buffer (flags & 0x83 and flags & 0x08), free it and clear the 0x408 buffer-state
// bits, base/ptr/cnt. Returns 0 if freed, else the unchanged flags.
int FreeStreamBuffer(IoBuf* b);

// ---------------------------------------------------------------------------
// Win32-error -> errno mapping (pure table lookup)
// ---------------------------------------------------------------------------
// gilde.exe 0x1427871 — VIBE_File_MapOsErrorToErrno. Set _doserrno=osError, look
// osError up in the 45-entry mapping table (errno_table) and set errno to the
// mapped value. Ranges: [0x13,0x24]->EACCES(13); [0xBC,0xCA]->ENOEXEC(8);
// otherwise EINVAL(22). Returns the resulting errno.
int MapOsErrorToErrno(unsigned int osError);

} // namespace guild::io
