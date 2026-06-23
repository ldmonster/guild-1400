// gilde.exe — guild::io  (file_ops2: CRT file-descriptor table + low-level File_* ops)
//
// Faithful 1:1 reconstruction of the Watcom/Borland-style low-level file layer:
// the two-level descriptor table (dword_1464F20 / dword_1465020), its bookkeeping
// (alloc/free/get/set/lock/commit/close), the stream-buffer allocation helpers,
// and the Win32-error -> errno table. OS leaves (open-std, file-type probe,
// low-level close/commit, lock hook) are reached through an installable hooks
// struct with inert defaults so the logic is testable without the platform.
//
// References of record (Hex-Rays pseudocode):
//   VIBE_File_InitIoTable       0x14266b1
//   VIBE_File_AllocHandle       0x14276ae
//   VIBE_File_SetOsHandle       0x1427743
//   VIBE_File_GetOsHandle       0x1427834
//   VIBE_File_FreeHandle        0x14277ba
//   VIBE_File_LockHandle        0x1429b12
//   VIBE_File_Commit            0x1422b3f
//   VIBE_File_CloseDescriptor   0x1424618
//   VIBE_File_InvokeLockHook    0x14246f6
//   VIBE_File_AllocStreamBuffer 0x1427f81
//   VIBE_File_FreeStreamBuffer  0x14246cb
//   VIBE_File_MapOsErrorToErrno 0x1427871
#include "io/file_ops2.h"
#include "crt/strtol.h"   // guild::crt::Errno()

#include <cstdlib>

namespace guild::io {

// --- errno / _doserrno ------------------------------------------------------
// dword_145A1DC is the CRT errno slot (shared with the rest of the runtime);
// dword_145A1E0 is _doserrno (last OS error).
static guild::i32 g_doserrno = 0;          // dword_145A1E0
guild::i32& DosErrno() { return g_doserrno; }

static inline int& Errno() { return guild::crt::Errno(); }

// --- hooks ------------------------------------------------------------------
static FdOsHooks g_hooks;                  // inert defaults
FdOsHooks& OsHooks() { return g_hooks; }

// dword_1452BE4 — std-stream rebind-notify gate. The original only fires the
// notify hook (dword_146777C) when this equals 1; in the shipped binary the
// static initializer leaves it at 2 (get_bytes 0x1452BE4 -> 02 00 00 00), so the
// notify path in SetOsHandle/FreeHandle is dead. Modeled as the binary's value.
static const int kStdNotifyMode = 2;       // dword_1452BE4 (== 2 in gilde.exe)

// inert-default wrappers (match "no console / regular disk file" semantics)
static int hookOpenStd(int stdId) {
    return g_hooks.openStd ? g_hooks.openStd(stdId) : -1;
}
static int hookGetFileType(int osHandle) {
    return g_hooks.getFileType ? g_hooks.getFileType(osHandle) : 0; // 0 = disk file
}
static int hookCloseHandle(int osHandle) {
    return g_hooks.closeHandle ? g_hooks.closeHandle(osHandle) : 1; // succeed
}
static void hookNotify(int stdId, int osHandle) {
    if (g_hooks.notifyStdRebind) g_hooks.notifyStdRebind(stdId, osHandle);
}

// --- FdTable accessors ------------------------------------------------------
void FdTable::Free() {
    for (int i = 0; i < kBlocks; ++i) {
        std::free(blocks[i]);
        blocks[i] = nullptr;
    }
    count = 0;
}

FdEntry* FdTable::Entry(int fd) {
    if (fd < 0 || static_cast<unsigned>(fd) >= static_cast<unsigned>(count))
        return nullptr;
    FdEntry* blk = blocks[fd >> 5];        // dword_1464F20[fd>>5]
    if (!blk) return nullptr;
    return &blk[fd & 0x1F];                // + 8 * (fd & 0x1F)
}

// Allocate one 256-byte block (32 entries) and initialise every entry to free:
// osHandle=-1, flags=0, peekChar=10. (The original's per-entry init loop.)
static FdEntry* allocBlock() {
    auto* blk = static_cast<FdEntry*>(std::malloc(FdTable::kPerBlock * sizeof(FdEntry)));
    if (!blk) return nullptr;
    for (int i = 0; i < FdTable::kPerBlock; ++i) {
        blk[i].osHandle = -1;              // *(_DWORD *)v0 = -1
        blk[i].flags    = 0;               // *(_BYTE *)(v0 + 4) = 0
        blk[i].peekChar = 10;              // *(_BYTE *)(v0 + 5) = 10
        blk[i].pad6 = blk[i].pad7 = 0;
    }
    return blk;
}

// gilde.exe 0x14266b1 — VIBE_File_InitIoTable
int InitIoTable(FdTable& t) {
    // dword_1464F20[0] = VIBE_Mem_Alloc(256); dword_1465020 = 32; init slots.
    t.blocks[0] = allocBlock();
    t.count = FdTable::kInitialCap;

    // The original then inherits any handles passed in the startup info block
    // (the dword_14677E0 "STARTUPINFO" probe). With no inherited handles in this
    // reconstruction, that path is empty; we go straight to the std-stream setup.

    // Bind fds 0..2 to the three standard streams.
    for (int m = 0; m < 3; ++m) {
        FdEntry& e = t.blocks[0][m];       // dword_1464F20[0] + 8*m
        if (e.osHandle == -1) {
            e.flags = static_cast<guild::u8>(-127);    // 0x81 = kFdOpen|kFdAscii
            int stdId = (m == 0) ? -10 : (m == 1 ? -11 : -12);  // -10/-11/-12
            int h = hookOpenStd(stdId);
            e.osHandle = h;                            // *(_DWORD *)v11 = v14
            int type = (h == -1) ? 0 : hookGetFileType(h);
            if (h == -1 || type == 0 || type == 2) {
                e.flags |= 0x40;                       // kFdEof / device-ish
            } else if (type == 3) {
                e.flags |= 0x08;                       // kFdDevice (pipe)
            }
        } else {
            e.flags |= 0x80;                           // kFdAscii (inherited)
        }
    }
    return 0;                              // dword_14677F0(dword_1465020) -> 0
}

// gilde.exe 0x14276ae — VIBE_File_AllocHandle
int AllocHandle(FdTable& t) {
    int blockIdx = 0;
    int base = 0;                          // v2: fd of first entry in this block
    for (; blockIdx < FdTable::kBlocks; ++blockIdx, base += FdTable::kPerBlock) {
        FdEntry* blk = t.blocks[blockIdx];
        if (!blk) {
            // *v3 == 0: this block is unallocated -> grow here.
            FdEntry* fresh = allocBlock();
            if (!fresh) return -1;
            t.count += FdTable::kPerBlock;             // dword_1465020 += 32
            t.blocks[blockIdx] = fresh;
            return FdTable::kPerBlock * blockIdx;      // 32 * v1
        }
        for (int i = 0; i < FdTable::kPerBlock; ++i) {
            if ((blk[i].flags & kFdOpen) == 0) {       // free slot
                blk[i].osHandle = -1;
                int fd = base + i;
                if (fd != -1) return fd;
            }
        }
        // (next block; the original stops at &dword_1465020 i.e. kBlocks entries)
    }
    return -1;
}

// gilde.exe 0x1427743 — VIBE_File_SetOsHandle
int SetOsHandle(FdTable& t, unsigned int fd, int osHandle) {
    FdEntry* e = (fd < static_cast<unsigned>(t.count)) ? t.Entry(static_cast<int>(fd)) : nullptr;
    if (e && e->osHandle == -1) {
        // Original: if ( dword_1452BE4 == 1 ) { notify rebind of a std stream }.
        // dword_1452BE4 is 2 in the binary, so this branch never executes.
        if (kStdNotifyMode == 1) {
            if (fd == 0)      hookNotify(-10, osHandle);
            else if (fd == 1) hookNotify(-11, osHandle);
            else if (fd == 2) hookNotify(-12, osHandle);
        }
        e->osHandle = osHandle;
        return 0;
    }
    g_doserrno = 0;
    Errno() = kEBADF;
    return -1;
}

// gilde.exe 0x1427834 — VIBE_File_GetOsHandle
int GetOsHandle(FdTable& t, int fd) {
    if (static_cast<unsigned>(fd) < static_cast<unsigned>(t.count)) {
        FdEntry* e = t.Entry(fd);
        if (e && (e->flags & kFdOpen) != 0)
            return e->osHandle;
    }
    g_doserrno = 0;
    Errno() = kEBADF;
    return -1;
}

// gilde.exe 0x14277ba — VIBE_File_FreeHandle
int FreeHandle(FdTable& t, unsigned int fd) {
    FdEntry* e = (fd < static_cast<unsigned>(t.count)) ? t.Entry(static_cast<int>(fd)) : nullptr;
    if (!e || (e->flags & kFdOpen) == 0 || e->osHandle == -1) {
        g_doserrno = 0;
        Errno() = kEBADF;
        return -1;
    }
    // Original: if ( dword_1452BE4 == 1 ) { notify }. dword_1452BE4 is 2 -> dead.
    if (kStdNotifyMode == 1) {
        if (fd == 0)      hookNotify(-10, 0);
        else if (fd == 1) hookNotify(-11, 0);
        else if (fd == 2) hookNotify(-12, 0);
    }
    e->osHandle = -1;                      // *(_DWORD *)(...) = -1 (flags untouched)
    return 0;
}

// gilde.exe 0x1429b12 — VIBE_File_LockHandle
int LockHandle(FdTable& t, unsigned int fd, int op) {
    FdEntry* e = (fd < static_cast<unsigned>(t.count)) ? t.Entry(static_cast<int>(fd)) : nullptr;
    if (e && (e->flags & kFdOpen) != 0) {
        unsigned char v3 = e->flags;
        int prevSet = v3 & 0x80;           // v4 = v3 & 0x80
        unsigned char v5;
        if (op == 0x8000) {                // unlock: clear high bit
            v5 = static_cast<unsigned char>(v3 & 0x7F);
        } else if (op == 0x4000) {         // lock: set high bit
            v5 = static_cast<unsigned char>(v3 | 0x80);
        } else {
            Errno() = kEINVAL;
            return -1;
        }
        // Original: v6 = -(v4 != 0); LOWORD(v6) = v6 & 0xC000; return v6 + 0x8000.
        // For prevSet: v6 = 0xFFFFFFFF -> low word 0xC000 -> 0xFFFFC000 -> +0x8000
        //   wraps to 0x00004000. For !prevSet: v6 = 0 -> 0 -> 0x8000.
        guild::u32 v6 = (prevSet != 0) ? 0xFFFFFFFFu : 0u;
        e->flags = v5;
        v6 = (v6 & 0xFFFF0000u) | (v6 & 0xC000u);   // LOWORD(v6) = v6 & 0xC000
        v6 = v6 + 0x8000u;                          // 32-bit wraparound
        return static_cast<int>(v6);
    }
    Errno() = kEBADF;
    return -1;
}

// gilde.exe 0x1422b3f — VIBE_File_Commit
int Commit(FdTable& t, int fd) {
    FdEntry* e = (static_cast<unsigned>(fd) < static_cast<unsigned>(t.count)) ? t.Entry(fd) : nullptr;
    if (!e || (e->flags & kFdOpen) == 0) {
        Errno() = kEBADF;
        return -1;
    }
    int osHandle = GetOsHandle(t, fd);
    int result;
    if (g_hooks.commit ? g_hooks.commit(osHandle) : 1) {   // dword_14676AC -> ok
        result = 0;
    } else {
        result = static_cast<int>(g_doserrno);             // dword_14676F8()
    }
    if (result) {
        g_doserrno = result;
        Errno() = kEBADF;
        return -1;
    }
    return 0;
}

// gilde.exe 0x1424618 — VIBE_File_CloseDescriptor
int CloseDescriptor(FdTable& t, unsigned int fd) {
    FdEntry* e = (fd < static_cast<unsigned>(t.count)) ? t.Entry(static_cast<int>(fd)) : nullptr;
    if (e && (e->flags & kFdOpen) != 0) {
        unsigned osErr;
        // No real handle, OR fd is a std-alias of fd 2/1 that share a handle, OR
        // the low-level close succeeds -> osErr 0; otherwise capture the OS error.
        if (GetOsHandle(t, static_cast<int>(fd)) == -1
            || ((fd == 1 || fd == 2)
                && GetOsHandle(t, 1) == GetOsHandle(t, 2))
            || hookCloseHandle(GetOsHandle(t, static_cast<int>(fd)))) {
            osErr = 0;
        } else {
            osErr = static_cast<unsigned>(g_doserrno);     // dword_14676F8()
        }
        FreeHandle(t, fd);
        e->flags = 0;                      // *(_BYTE *)(*v1 + v2 + 4) = 0
        if (osErr == 0) return 0;
        MapOsErrorToErrno(osErr);
        return -1;
    }
    g_doserrno = 0;
    Errno() = kEBADF;
    return -1;
}

// gilde.exe 0x14246f6 — VIBE_File_InvokeLockHook
bool InvokeLockHook(int fd) {
    return g_hooks.lockHook && g_hooks.lockHook(fd);
}

// --- stream-buffer helpers --------------------------------------------------
static int g_streamBufAllocCount = 0;      // dword_145A25C
int& StreamBufferAllocCount() { return g_streamBufAllocCount; }

// gilde.exe 0x1427f81 — VIBE_File_AllocStreamBuffer
char* AllocStreamBuffer(IoBuf* b) {
    ++g_streamBufAllocCount;
    char* mem = static_cast<char*>(std::malloc(4096));
    b->base = mem;                         // a1[2] = v1
    if (mem) {
        b->flags |= 0x08;                  // a1[3] |= 8 (owns heap buffer)
        b->bufSize = 4096;                 // a1[6] = 4096
    } else {
        b->flags |= 0x04;                  // a1[3] |= 4 (unbuffered)
        b->base = b->smallBuf;             // a1[2] = a1 + 5 (2-byte inline buffer)
        b->bufSize = 2;                    // a1[6] = 2
    }
    char* result = b->base;                // result = a1[2]
    b->cnt = 0;                            // a1[1] = 0
    b->ptr = result;                       // *a1 = result
    return result;
}

// gilde.exe 0x14246cb — VIBE_File_FreeStreamBuffer
int FreeStreamBuffer(IoBuf* b) {
    guild::u32 flags = b->flags;           // result = a1[3]
    if ((flags & 0x83) != 0 && (flags & 0x08) != 0) {
        if (b->base != b->smallBuf)        // only the heap buffer is freed
            std::free(b->base);
        b->flags &= 0xFFFFFBF7u;           // *((_WORD*)a1+6) &= 0xFBF7 -> clear 0x408
        b->ptr = nullptr;                  // *a1 = 0
        b->base = nullptr;                 // a1[2] = 0
        b->cnt = 0;                        // a1[1] = 0
        return 0;
    }
    return static_cast<int>(flags);
}

// --- Win32 error -> errno table --------------------------------------------
// gilde.exe 0x1455170 (unk_1455170 .. dword_14552D8) — 45 (oserror, errno) pairs.
namespace {
struct ErrPair { unsigned os; int err; };
constexpr ErrPair kErrnoTable[] = {
    {   1, 22}, {   2,  2}, {   3,  2}, {   4, 24}, {   5, 13},
    {   6,  9}, {   7, 12}, {   8, 12}, {   9, 12}, {  10,  7},
    {  11,  8}, {  12, 22}, {  13, 22}, {  15,  2}, {  16, 13},
    {  17, 18}, {  18,  2}, {  33, 13}, {  53,  2}, {  65, 13},
    {  67,  2}, {  80, 17}, {  82, 13}, {  83, 13}, {  87, 22},
    {  89, 11}, { 108, 13}, { 109, 32}, { 112, 28}, { 114,  9},
    {   6, 22}, { 128, 10}, { 129, 10}, { 130,  9}, { 131, 22},
    { 132, 13}, { 145, 41}, { 158, 13}, { 161,  2}, { 164, 11},
    { 167, 13}, { 183, 17}, { 206,  2}, { 215, 11}, {1816, 12},
};
constexpr int kErrnoTableLen = sizeof(kErrnoTable) / sizeof(kErrnoTable[0]);
} // namespace

// gilde.exe 0x1427871 — VIBE_File_MapOsErrorToErrno
int MapOsErrorToErrno(unsigned int osError) {
    g_doserrno = static_cast<guild::i32>(osError);     // dword_145A1E0 = a1
    for (int i = 0; i < kErrnoTableLen; ++i) {         // linear scan to dword_14552D8
        if (osError == kErrnoTable[i].os) {
            Errno() = kErrnoTable[i].err;
            return kErrnoTable[i].err;
        }
    }
    int e;
    if (osError >= 0x13 && osError <= 0x24) {          // [19,36] -> EACCES
        e = 13;
    } else if (osError >= 0xBC && osError <= 0xCA) {   // [188,202] -> ENOEXEC
        e = 8;
    } else {
        e = 22;                                        // EINVAL
    }
    Errno() = e;
    return e;
}

} // namespace guild::io
