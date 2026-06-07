#pragma once
// gilde.exe — guild::io  (MODULE: file_ops4 — descriptor-level OS I/O verbs and
// the CRT-FILE-table layer, batch 4 of VIBE_File_* / VIBE_Vfs_*)
//
// This slice sits directly on top of the file-descriptor table reconstructed in
// file_ops2 (FdTable / FdEntry / AllocHandle / GetOsHandle / SetOsHandle /
// CloseDescriptor / LockHandle / MapOsErrorToErrno, see file_ops2.h). It
// reconstructs the verbs that turn an fd into an actual OS read/write/seek/
// truncate, the open()-with-mode dispatcher, the fopen()-mode parser, and the
// temp-file bookkeeping that the CRT keeps in a per-process linked list of FILE
// records (dword_1408760).
//
// Reconstructed here:
//   VIBE_File_DetectDeviceType   @0x6063d0   GetFileType(fd) == 2 (char device)
//   VIBE_File_LockAndCommit      @0x606350   refresh per-fd device flags, return flags
//   VIBE_File_ValidateHandleMode @0x5fea04   compare requested vs actual access bits
//   VIBE_File_Seek_278d8         @0x14278d8  lseek(fd) through the descriptor table
//   VIBE_File_Write              @0x1422b96  write(fd) with optional LF->CRLF translate
//   VIBE_File_ReadTranslate      @0x1425d19  read(fd) with optional CRLF->LF translate
//   VIBE_File_ChangeSize         @0x14296b3  ftruncate/grow a file to a new length
//   VIBE_File_OpenWithMode       @0x1427cc8  open() — map oflag/share/pmode to OS open
//   VIBE_File_ParseModeAndOpen   @0x142431b  parse an fopen() mode string, open, fill FILE
//   VIBE_File_FindFirstEntry     @0x5eb830   FindFirstFile + first attribute match
//   VIBE_File_FindClose          @0x5eb940   FindClose
//   VIBE_Vfs_CloseAndFreeEntry   @0x5d90c0   unlink a FILE* from dword_1408760, flush+free
//
// OS / not-yet-reconstructed leaves (the low-level read/write/lseek/chsize/open
// primitives, FindFirstFile/FindClose, the flush-and-free of a FILE record, and
// the global-lock enter/leave) are routed through FileOps4Hooks, an installable
// struct whose default implementation is inert/deterministic and defined in
// file_ops4.cpp. Tests install their own backend; no src/ file references a
// test-defined symbol.
#include "guild/common/types.h"
#include "io/file_ops2.h"
#include "io/file_ops.h"      // FindData, CopyFindDataAttributes, FindEntryMatches
#include "shim/IFileSystem.h"

namespace guild::io {

// ---------------------------------------------------------------------------
// Recovered constants
// ---------------------------------------------------------------------------
// errno values touched by this slice (CRT <errno.h> numbering).
enum FileOps4Errno : int {
    kEACCES2 = 13,  // EACCES (also used as "broken pipe / no-space" sentinel here)
    kENOSPC  = 28,  // disk full
    kEIO     = 5,   // _doserrno when the OS write failed with ERROR_ACCESS_DENIED
};

// OS GetFileType() return codes (FILE_TYPE_*).
enum OsFileType : int {
    kFileTypeDisk = 0,   // FILE_TYPE_DISK
    kFileTypeChar = 2,   // FILE_TYPE_CHAR  (console/printer => "device")
    kFileTypePipe = 3,   // FILE_TYPE_PIPE
};

// open() oflag bits (the MSVC-style _O_* values the mode parser / open dispatcher
// use). Only the subset the original tests is modelled.
enum OFlag : int {
    kO_RDONLY = 0x0000,
    kO_WRONLY = 0x0001,
    kO_RDWR   = 0x0002,
    kO_APPEND = 0x0008,
    kO_CREAT  = 0x0100,
    kO_TRUNC  = 0x0200,
    kO_EXCL   = 0x0400,
    kO_TEXT   = 0x4000,
    kO_BINARY = 0x8000,
};

// FdEntry +0x04 flag bits used by this slice (mirrors file_ops2's FdFlags plus a
// couple this layer also toggles).
enum FileOps4FdFlags : guild::u8 {
    kFdfOpen     = 0x01,   // slot in use
    kFdfRead     = 0x02,   // last op was a read / "no pending data" after seek
    kFdfCtrlZ    = 0x04,   // saw a ^Z (text EOF) in the last read
    kFdfDevice   = 0x08,   // pipe
    kFdfAppend   = 0x20,   // O_APPEND
    kFdfChar     = 0x40,   // console device
    kFdfText     = 0x80,   // text-mode (high bit) — LF<->CRLF translate on
};

// ---------------------------------------------------------------------------
// Buffered FILE record (the _iobuf the CRT FILE* points at), as touched by the
// fopen()-mode parser and the temp-file path. Only the dword prefix the original
// indexes is modelled (a4[0..7]).
// ---------------------------------------------------------------------------
struct CrtFile {
    char*      ptr;     // +0x00 (a4[0]) buffer cursor
    guild::i32 cnt;     // +0x04 (a4[1]) bytes ahead of cursor
    char*      base;    // +0x08 (a4[2]) buffer base
    guild::u32 flags;   // +0x0C (a4[3]) stream flags (the parsed fopen mode bits)
    guild::i32 fd;      // +0x10 (a4[4]) underlying descriptor
    guild::i32 spare5;  // +0x14 (a4[5])
    guild::i32 spare6;  // +0x18 (a4[6])
    guild::i32 hold;    // +0x1C (a4[7]) ungetc holding slot
};

// ---------------------------------------------------------------------------
// Installable OS / cross-module leaves
// ---------------------------------------------------------------------------
struct FileOps4Hooks {
    // Low-level lseek(osHandle, off, whence) -> new absolute offset, or -1 on error.
    // (dword_1467774.) Default: error (-1).
    long (*osSeek)(int osHandle, long off, int whence) = nullptr;
    // Low-level write(osHandle, buf, n, &written) -> nonzero on success, 0 on error
    // (with the OS error in lastError()). (dword_14677B4.) Default: error (0).
    int (*osWrite)(int osHandle, const char* buf, unsigned n, int* written) = nullptr;
    // Low-level read(osHandle, buf, n, &got) -> nonzero on success, 0 on error.
    // (dword_1467758.) Default: error (0).
    int (*osRead)(int osHandle, char* buf, unsigned n, int* got) = nullptr;
    // Low-level chsize/SetEndOfFile(osHandle) at the current offset -> nonzero ok.
    // (dword_1467814.) Default: error (0).
    int (*osChsize)(int osHandle) = nullptr;
    // CreateFile()-style open: returns an OS handle (>=0) or -1. `access` is the
    // GENERIC_* mask, `share` the share mask, `disp` the creation disposition
    // 1..5, `attrs` the flags-and-attributes. (dword_1467670.) Default: -1.
    int (*osOpen)(const char* path, unsigned access, int share, int disp,
                  unsigned attrs) = nullptr;
    // GetLastError() for the low-level primitives (dword_14676F8). Default 0.
    unsigned (*lastError)() = nullptr;
    // GetFileType(osHandle) -> FILE_TYPE_* (dword_14676F0 in OpenWithMode). Def 0.
    int (*fileType)(int osHandle) = nullptr;

    // VIBE_File_FindFirstEntry: open a find for `pattern`, returning an opaque
    // handle (nullptr => not found) and the first matching entry. Default: none.
    void* (*findFirst)(const char* pattern, guild::shim::DirEntry* first) = nullptr;
    // VIBE_File_FindClose: close a find handle. Returns nonzero on success.
    int (*findClose)(void* handle) = nullptr;
    // VIBE_Resource_FlushAndFree @0x5d9104: flush+free a FILE record removed from
    // the temp list. Returns 0 on success. Default: 0.
    int (*flushAndFree)(CrtFile* f) = nullptr;

    // Global CRT-table lock enter/leave (off_64A920 / off_64A924 around the temp
    // FILE-list walk; off_64A910 / off_64A914 elsewhere). Default: no-op.
    void (*lockEnter)() = nullptr;
    void (*lockLeave)() = nullptr;
};
FileOps4Hooks& Ops4Hooks();
// Install a backend; nullptr restores inert defaults. Returns the previous hooks.
FileOps4Hooks SetOps4Hooks(const FileOps4Hooks* hooks);

// ---------------------------------------------------------------------------
// The temp-file FILE linked list (dword_1408760). Each node: {next, file}. The
// CRT keeps every FILE it opened for tmpfile()/tmpnam() here so it can flush and
// remove them on close. Modelled as an intrusive singly-linked list.
// ---------------------------------------------------------------------------
struct TempFileNode {
    TempFileNode* next = nullptr;   // +0x00
    CrtFile*      file = nullptr;   // +0x04
};
// The head pointer (dword_1408760). Tests manipulate this directly.
TempFileNode*& TempFileListHead();

// ---------------------------------------------------------------------------
// The Win32 "lowio" descriptor table — distinct from the CRT FdTable above. The
// original keeps it in three flat globals: dword_64ADBC (one OS HANDLE per fd),
// off_64AE64 (one info dword per fd: byte0 = access/mode flags, byte1 = device-
// probe flags 0x40 probed / 0x20 char-device), and dword_64AE10 (capacity). The
// 0x6063xx/0x606440 ops manipulate this table; we model it explicitly.
// ---------------------------------------------------------------------------
struct LowioTable {
    static constexpr int kMax = 256;
    int        handles[kMax];   // dword_64ADBC[fd]  (OS handle, -1 = none)
    guild::u32 info[kMax];      // off_64AE64[fd]    (byte0 access, byte1 dev flags)
    int        count = 0;       // dword_64AE10      (capacity / open count)
    LowioTable() { for (int i = 0; i < kMax; ++i) { handles[i] = -1; info[i] = 0; } }
};

// off_64AE64 byte1 device-probe flags.
enum LowioDevFlags : guild::u32 {
    kLowioProbed  = 0x4000,   // byte1 0x40 — GetFileType already run
    kLowioDevice  = 0x2000,   // byte1 0x20 — handle is a char device
};

// ---------------------------------------------------------------------------
// Functions
// ---------------------------------------------------------------------------

// gilde.exe 0x6063d0 — VIBE_File_DetectDeviceType. True iff GetFileType(handles[fd])
// is FILE_TYPE_CHAR (a console/device). (eax = fd)
bool DetectDeviceType(LowioTable& t, int fd);

// gilde.exe 0x606350 — VIBE_File_LockAndCommit. For fds 0..2 that have not yet had
// their device flag probed (byte1 0x40 clear): mark probed, and if DetectDeviceType
// is true also set byte1 0x20. Returns the fd's full info dword (0 if out of range).
guild::u32 LockAndCommit(LowioTable& t, unsigned int fd);

// gilde.exe 0x5fea04 — VIBE_File_ValidateHandleMode. Compare the requested access
// byte `want` against the fd's actual access byte (byte0 of LockAndCommit's dword).
// Returns 0 if the request is satisfiable, else -1 with errno=EINVAL. Bits:
// 0xC0 text/binary match, 0x01 must-be-readable, 0x02 must-be-writable.
int ValidateHandleMode(LowioTable& t, unsigned int fd, guild::u8 want);

// gilde.exe 0x6063a8 — VIBE_File_SetDescriptorEntry. Store fd's info dword: when
// nonzero, force the byte1 0x40 (probed) bit; when zero, clear the entry.
void SetDescriptorEntry(LowioTable& t, int fd, guild::u32 value);

// gilde.exe 0x14278d8 — VIBE_File_Seek (descriptor variant). lseek(fd, off, whence)
// through the table; clears the 0x02 (pending-read) flag on success. Returns the
// new offset, or -1 with errno (EBADF for a bad fd, mapped OS error otherwise).
int SeekDescriptor(FdTable& t, int fd, int off, int whence);

// gilde.exe 0x1422b96 — VIBE_File_Write. Write `n` bytes of `buf` to fd. For a
// text-mode fd (flag 0x80 set i.e. flags byte negative) each '\n' is expanded to
// "\r\n". O_APPEND fds (0x20) seek to end first. Returns bytes consumed from `buf`
// (i.e. excluding inserted CRs), 0 for a 0-length or text-^Z write, or -1 on error
// (errno EBADF / ENOSPC / EACCES).
int WriteFd(FdTable& t, int fd, const char* buf, unsigned n);

// gilde.exe 0x1425d19 — VIBE_File_ReadTranslate. Read up to `n` bytes into `buf`
// from fd. For a text-mode fd (flag 0x48), "\r\n" pairs collapse to "\n", a lone
// trailing '\r' is held in the fd's peekChar, and a ^Z (0x1A) ends the stream.
// Returns the number of bytes delivered (0 at EOF / broken pipe), or -1 on error.
unsigned ReadTranslate(FdTable& t, int fd, char* buf, int n);

// gilde.exe 0x14296b3 — VIBE_File_ChangeSize. Set fd's file length to `newSize`:
// grow by writing zero bytes, or shrink via the OS truncate. Restores the original
// file position. Returns 0 on success, -1 with errno on failure.
int ChangeSize(FdTable& t, int fd, int newSize);

// gilde.exe 0x1427cc8 — VIBE_File_OpenWithMode. Translate the MSVC oflag word
// `oflag`, share mode `shflag` (16/32/48/64) and pmode `pmode` into an OS open,
// allocate an fd, bind it, and set its text/append/char flags. Returns the fd, or
// -1 with errno (EINVAL bad arg, EMFILE no slots, mapped OS error on open failure).
int OpenWithMode(FdTable& t, const char* path, guild::u16 oflag, int shflag, char pmode);

// gilde.exe 0x142431b — VIBE_File_ParseModeAndOpen. Parse an fopen() mode string
// (`r`/`w`/`a` plus `+`/`b`/`t`/`c`/`n`/`S`/`R`/`T`/`D`), open the path via
// OpenWithMode, and fill the FILE record `f`. Returns `f` on success, nullptr on a
// bad mode string or open failure.
CrtFile* ParseModeAndOpen(FdTable& t, const char* path, const char* mode, int shflag,
                          CrtFile* f);
int& StreamOpenCount();   // dword_145A25C — number of FILE records opened

// gilde.exe 0x5eb830 — VIBE_File_FindFirstEntry. Begin a directory find for
// `pattern`; skip to the first entry matching the default mask (0x37), copy its
// attributes into `out` and return the find handle (closed by the caller via
// FindClose), or -1 (cast) if nothing matched / the find failed.
void* FindFirstEntry(const char* pattern, FindData* out);

// gilde.exe 0x5eb940 — VIBE_File_FindClose. Close a find handle. Returns 0 on
// success, -1 on failure (FindClose() - 1).
int FindClose(void* handle);

// gilde.exe 0x5d90c0 — VIBE_Vfs_CloseAndFreeEntry. Find the temp-list node whose
// FILE is `f`, and if present flush+free it (VIBE_Resource_FlushAndFree). Returns
// that call's result, or -1 if `f` is not a tracked temp FILE. Holds the CRT lock.
int CloseAndFreeEntry(CrtFile* f);

} // namespace guild::io
