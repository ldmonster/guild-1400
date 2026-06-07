// gilde.exe — guild::io  (file_ops4: descriptor-level OS I/O verbs + CRT FILE table).
// See file_ops4.h. Each function is a 1:1 reconstruction of the named address.
//
// Substrate reused from file_ops2.h:
//   FdTable / FdEntry, AllocHandle, GetOsHandle, SetOsHandle, CloseDescriptor,
//   LockHandle, MapOsErrorToErrno, DosErrno (dword_145A1E0). errno itself is the
//   shared per-thread CRT slot guild::crt::Errno() (dword_145A1DC).
#include "io/file_ops4.h"
#include "crt/strtol.h"   // guild::crt::Errno()

#include <cstring>

namespace guild::io {

namespace {

inline int& Errno() { return guild::crt::Errno(); }

// ---- inert default hooks ---------------------------------------------------
long DefaultOsSeek(int, long, int)                     { return -1; }
int  DefaultOsWrite(int, const char*, unsigned, int*)  { return 0; }
int  DefaultOsRead(int, char*, unsigned, int*)         { return 0; }
int  DefaultOsChsize(int)                              { return 0; }
int  DefaultOsOpen(const char*, unsigned, int, int, unsigned) { return -1; }
unsigned DefaultLastError()                            { return 0; }
int  DefaultFileType(int)                              { return 0; }
void* DefaultFindFirst(const char*, guild::shim::DirEntry*) { return nullptr; }
int  DefaultFindClose(void*)                           { return 1; }
int  DefaultFlushAndFree(CrtFile*)                     { return 0; }
void DefaultLock()                                     {}

FileOps4Hooks MakeDefaults() {
    FileOps4Hooks h;
    h.osSeek       = &DefaultOsSeek;
    h.osWrite      = &DefaultOsWrite;
    h.osRead       = &DefaultOsRead;
    h.osChsize     = &DefaultOsChsize;
    h.osOpen       = &DefaultOsOpen;
    h.lastError    = &DefaultLastError;
    h.fileType     = &DefaultFileType;
    h.findFirst    = &DefaultFindFirst;
    h.findClose    = &DefaultFindClose;
    h.flushAndFree = &DefaultFlushAndFree;
    h.lockEnter    = &DefaultLock;
    h.lockLeave    = &DefaultLock;
    return h;
}

FileOps4Hooks g_ops4 = MakeDefaults();

// dword_1408760 — head of the temp-FILE linked list.
TempFileNode* g_tempHead = nullptr;

// dword_145A25C — count of FILE records opened (also bumped by file_ops2's
// AllocStreamBuffer; here it is the mode-parser's own copy for inspection).
int g_streamOpenCount = 0;

// dword_145A4B0 — the process default open-mode word (_fmode-like). When it
// equals 0x8000 the default is binary; otherwise text. Default 0x4000 (text).
unsigned g_defaultFmode = 0x4000;

} // namespace

FileOps4Hooks& Ops4Hooks() { return g_ops4; }

FileOps4Hooks SetOps4Hooks(const FileOps4Hooks* hooks) {
    FileOps4Hooks prev = g_ops4;
    if (!hooks) { g_ops4 = MakeDefaults(); return prev; }
    g_ops4 = *hooks;
    FileOps4Hooks d = MakeDefaults();
    if (!g_ops4.osSeek)       g_ops4.osSeek       = d.osSeek;
    if (!g_ops4.osWrite)      g_ops4.osWrite      = d.osWrite;
    if (!g_ops4.osRead)       g_ops4.osRead       = d.osRead;
    if (!g_ops4.osChsize)     g_ops4.osChsize     = d.osChsize;
    if (!g_ops4.osOpen)       g_ops4.osOpen       = d.osOpen;
    if (!g_ops4.lastError)    g_ops4.lastError    = d.lastError;
    if (!g_ops4.fileType)     g_ops4.fileType     = d.fileType;
    if (!g_ops4.findFirst)    g_ops4.findFirst    = d.findFirst;
    if (!g_ops4.findClose)    g_ops4.findClose    = d.findClose;
    if (!g_ops4.flushAndFree) g_ops4.flushAndFree = d.flushAndFree;
    if (!g_ops4.lockEnter)    g_ops4.lockEnter    = d.lockEnter;
    if (!g_ops4.lockLeave)    g_ops4.lockLeave    = d.lockLeave;
    return prev;
}

TempFileNode*& TempFileListHead() { return g_tempHead; }
int& StreamOpenCount() { return g_streamOpenCount; }

// ----------------------------------------------------------------------------
// gilde.exe 0x6063d0 — VIBE_File_DetectDeviceType
//   GetFileType(dword_64ADBC[fd]) == FILE_TYPE_CHAR, under the global lock.
bool DetectDeviceType(LowioTable& t, int fd) {
    Ops4Hooks().lockEnter();                                  // off_64A910
    int osHandle = (fd >= 0 && fd < LowioTable::kMax) ? t.handles[fd] : -1;
    bool isChar = (Ops4Hooks().fileType(osHandle) == kFileTypeChar);
    Ops4Hooks().lockLeave();                                  // off_64A914
    return isChar;
}

// gilde.exe 0x606350 — VIBE_File_LockAndCommit
guild::u32 LockAndCommit(LowioTable& t, unsigned int fd) {
    if (fd >= static_cast<unsigned>(t.count))                 // a1 >= dword_64AE10
        return 0;
    if (static_cast<int>(fd) < 3) {                           // (int)a1 < 3
        guild::u32& info = t.info[fd];                        // off_64AE64[fd]
        if ((info & 0x4000) == 0) {                           // byte1 0x40 clear: not probed
            info |= 0x4000u;                                  // mark probed
            if (DetectDeviceType(t, static_cast<int>(fd)))
                info |= 0x2000u;                              // byte1 0x20: char device
        }
    }
    return t.info[fd];                                        // *((_DWORD*)off_64AE64 + fd)
}

// gilde.exe 0x5fea04 — VIBE_File_ValidateHandleMode
// Original: v2 = requested bits (dl), v3 = (low byte of) LockAndCommit(a1).
int ValidateHandleMode(LowioTable& t, unsigned int fd, guild::u8 want) {
    guild::u8 v2 = want;
    guild::u8 v3 = static_cast<guild::u8>(LockAndCommit(t, fd));   // byte0 (access bits)
    int v4 = 0;
    if (((v2 ^ v3) & 0xC0) != 0)
        v4 = 6;
    if ((v2 & 1) != 0 && (v3 & 1) == 0)
        v4 = 6;
    if (((v2 & 2) == 0 || (v3 & 2) != 0) && v4 != 6)
        return 0;
    Errno() = kEINVAL;                                        // VIBE_Runtime_SetErrnoEinval
    return -1;
}

// gilde.exe 0x6063a8 — VIBE_File_SetDescriptorEntry
void SetDescriptorEntry(LowioTable& t, int fd, guild::u32 value) {
    if (fd < 0 || fd >= LowioTable::kMax)
        return;
    if (value)
        t.info[fd] = value | 0x4000u;                         // BYTE1(a2) |= 0x40
    else
        t.info[fd] = 0;
}

// gilde.exe 0x14278d8 — VIBE_File_Seek (descriptor variant)
int SeekDescriptor(FdTable& t, int fd, int off, int whence) {
    FdEntry* e = (static_cast<unsigned>(fd) < static_cast<unsigned>(t.count))
                     ? t.Entry(fd) : nullptr;
    if (e && (e->flags & 1) != 0) {                           // slot open
        int osHandle = GetOsHandle(t, fd);                    // sets EBADF on bad fd
        if (osHandle == -1) {
            Errno() = kEBADF;                                 // dword_145A1DC = 9
        } else {
            long v6 = Ops4Hooks().osSeek(osHandle, off, whence);  // dword_1467774
            unsigned v7 = (v6 == -1) ? Ops4Hooks().lastError() : 0u;
            if (v7 == 0) {
                e->flags &= ~2u;                              // clear pending-read
                return static_cast<int>(v6);
            }
            MapOsErrorToErrno(v7);
        }
    } else {
        DosErrno() = 0;                                       // dword_145A1E0 = 0
        Errno() = kEBADF;                                     // dword_145A1DC = 9
    }
    return -1;
}

// gilde.exe 0x1422b96 — VIBE_File_Write
int WriteFd(FdTable& t, int fd, const char* a2, unsigned a3) {
    FdEntry* e = (static_cast<unsigned>(fd) < static_cast<unsigned>(t.count))
                     ? t.Entry(fd) : nullptr;
    if (!e || (e->flags & 1) == 0) {
        DosErrno() = 0;
        Errno() = 9;                                          // EBADF
        return -1;
    }

    int v15 = 0;        // total bytes written to OS
    int v13 = 0;        // count of inserted '\r' bytes
    if (a3 == 0)
        return 0;

    guild::u8 f0 = e->flags;
    if ((f0 & 0x20) != 0)                                     // O_APPEND
        SeekDescriptor(t, fd, 0, 2);

    unsigned v17 = 0;   // OS error
    int osHandle = e->osHandle;
    int written = 0;
    if ((e->flags & 0x80u) == 0) {                            // binary (flags >= 0)
        if (Ops4Hooks().osWrite(osHandle, a2, a3, &written)) {
            v17 = 0;
            v15 = written;
        } else {
            v17 = Ops4Hooks().lastError();
        }
    } else {                                                  // text: LF -> CRLF
        char v12[1028];
        const char* v16 = a2;
        v17 = 0;
        bool errored = false;
        while (true) {
            char* v8 = v12;
            do {
                if (static_cast<unsigned>(v16 - a2) >= a3)
                    break;
                char c = *v16++;
                if (c == 10) {                                // '\n'
                    ++v13;
                    *v8++ = 13;                               // '\r'
                }
                *v8++ = c;
            } while (v8 - v12 < 1024);
            int v11 = static_cast<int>(v8 - v12);
            if (!Ops4Hooks().osWrite(osHandle, v12, static_cast<unsigned>(v11),
                                     &written)) {
                v17 = Ops4Hooks().lastError();
                errored = true;
                break;
            }
            v15 += written;
            if (written < v11 || static_cast<unsigned>(v16 - a2) >= a3)
                break;
        }
        (void)errored;
    }

    if (v15)
        return v15 - v13;
    if (v17) {
        if (v17 == 5) {                                       // ERROR_ACCESS_DENIED
            Errno() = 9;                                      // EBADF
            DosErrno() = 5;
        } else {
            MapOsErrorToErrno(v17);
        }
    } else {
        if ((e->flags & 0x40) != 0 && *a2 == 26)              // ^Z on a char device
            return 0;
        Errno() = 28;                                         // ENOSPC
        DosErrno() = 0;
    }
    return -1;
}

// gilde.exe 0x1425d19 — VIBE_File_ReadTranslate
unsigned ReadTranslate(FdTable& t, int fd, char* dst, int a3) {
    FdEntry* e = (static_cast<unsigned>(fd) < static_cast<unsigned>(t.count))
                     ? t.Entry(fd) : nullptr;
    if (!e || (e->flags & 1) == 0) {                          // bad / not open
        DosErrno() = 0;
        Errno() = 9;
        return static_cast<unsigned>(-1);
    }

    unsigned v23 = 0;             // total raw bytes in buffer
    char* v7 = dst;              // output cursor
    char* v8 = dst;              // OS read destination cursor
    if (a3 == 0 || (e->flags & 2) != 0)                       // 0-length, or "read EOF" sticky
        return 0;

    if ((e->flags & 0x48) != 0) {                             // text-ish: replay peekChar
        guild::u8 v9 = e->peekChar;
        if (v9 != 10) {
            --a3;
            *dst = static_cast<char>(v9);
            v8 = dst + 1;
            v23 = 1;
            e->peekChar = 10;
        }
    }

    int got = 0;
    if (!Ops4Hooks().osRead(e->osHandle, v8, static_cast<unsigned>(a3), &got)) {
        unsigned v11 = Ops4Hooks().lastError();
        if (v11 == 5) {                                       // sharing violation
            Errno() = 9;
            DosErrno() = 5;
            return static_cast<unsigned>(-1);
        }
        if (v11 == 109)                                       // ERROR_BROKEN_PIPE
            return 0;
        MapOsErrorToErrno(v11);
        return static_cast<unsigned>(-1);
    }

    v23 += static_cast<unsigned>(got);
    if ((e->flags & 0x80) == 0)                               // binary: no translation
        return v23;

    // text translation
    if (got && *dst == 10)
        e->flags |= 4u;                                       // 0x04 ctrl-z/lf state
    else
        e->flags &= 0xFBu;

    char* v25 = dst;
    char* end = dst + v23;
    if (dst < end) {
        char* v17end = end;
        do {
            char v19 = *v25;
            if (v19 == 26) {                                  // ^Z -> EOF
                if ((e->flags & 0x40) == 0)
                    e->flags |= 2u;
                return static_cast<unsigned>(v7 - dst);
            }
            if (v19 == 13) {                                  // CR
                if (v25 < v17end - 1) {
                    if (v25[1] == 10) {                       // CRLF -> LF
                        v25 += 2;
                        *v7 = 10;
                        ++v7;
                        v17end = dst + v23;
                        continue;
                    }
                    *v7++ = 13;
                    ++v25;
                } else {                                      // CR at buffer tail
                    ++v25;
                    char v24 = 0;
                    int got2 = 0;
                    if ((!Ops4Hooks().osRead(e->osHandle, &v24, 1, &got2)
                             && Ops4Hooks().lastError())
                        || got2 == 0) {
                        *v7 = 13;                             // lone CR
                        ++v7;
                    } else if ((e->flags & 0x48) != 0) {
                        if (v24 == 10) {                      // CRLF across reads
                            *v7 = 10;
                            ++v7;
                        } else {
                            *v7++ = 13;
                            e->peekChar = static_cast<guild::u8>(v24);
                        }
                    } else {
                        if (v7 == dst && v24 == 10) {
                            *v7 = 10;
                            ++v7;
                        } else {
                            SeekDescriptor(t, fd, -1, 1);
                            if (v24 != 10) {
                                *v7 = 13;
                                ++v7;
                            } else {
                                *v7 = 10;
                                ++v7;
                            }
                        }
                    }
                    v17end = dst + v23;                  // v17 = v23 (loop bound refresh)
                }
            } else {                                          // ordinary byte
                *v7++ = v19;
                ++v25;
            }
            v17end = dst + v23;
        } while (v25 < dst + v23);
    }
    return static_cast<unsigned>(v7 - dst);
}

// gilde.exe 0x14296b3 — VIBE_File_ChangeSize
int ChangeSize(FdTable& t, int fd, int newSize) {
    FdEntry* e = (static_cast<unsigned>(fd) < static_cast<unsigned>(t.count))
                     ? t.Entry(fd) : nullptr;
    int v3 = 0;
    if (!e || (e->flags & 1) == 0) {
        Errno() = 9;                                          // EBADF
        return -1;
    }
    int cur = SeekDescriptor(t, fd, 0, 1);                    // save position
    if (cur == -1)
        return -1;
    int curEnd = SeekDescriptor(t, fd, 0, 2);                 // size
    if (curEnd == -1)
        return -1;
    int v5 = newSize - curEnd;
    if (v5 <= 0) {
        if (v5 < 0) {                                         // shrink
            SeekDescriptor(t, fd, newSize, 0);
            int osHandle = GetOsHandle(t, fd);
            int ok = Ops4Hooks().osChsize(osHandle);
            v3 = (ok != 0) - 1;                               // 0 ok, -1 fail
            if (!ok) {
                Errno() = 13;                                 // EACCES
                DosErrno() = static_cast<int>(Ops4Hooks().lastError());
            }
        }
    } else {                                                  // grow with zeros
        char zeros[4096];
        std::memset(zeros, 0, sizeof(zeros));
        int saveFlags = LockHandle(t, static_cast<unsigned>(fd), 0x8000);  // binary mode
        bool broke = false;
        while (true) {
            unsigned chunk = 4096;
            if (v5 < 4096)
                chunk = static_cast<unsigned>(v5);
            int w = WriteFd(t, fd, zeros, chunk);
            if (w == -1) { broke = true; break; }
            v5 -= w;
            if (v5 <= 0)
                break;
        }
        if (broke) {
            if (DosErrno() == 5)
                Errno() = 13;                                 // EACCES
            v3 = -1;
        }
        LockHandle(t, static_cast<unsigned>(fd), saveFlags);  // restore mode
    }
    SeekDescriptor(t, fd, cur, 0);                            // restore position
    return v3;
}

// gilde.exe 0x1427cc8 — VIBE_File_OpenWithMode
int OpenWithMode(FdTable& t, const char* path, guild::u16 oflag, int shflag, char pmode) {
    guild::u8 v21;       // initial flags byte
    int v17;             // unused commit flag (kept 1:1)
    if ((oflag & 0x80u) == 0) {                               // not O_BINARY
        v21 = 0;
        v17 = 1;
    } else {
        v17 = 0;
        v21 = 16;                                             // 0x10
    }
    (void)v17;
    // default-text decision (dword_145A4B0 == 0x8000 => default binary)
    if ((oflag & 0x8000) == 0
        && ((oflag & 0x4000) != 0 || g_defaultFmode != 0x8000))
        v21 |= 0x80u;                                         // text

    unsigned access;
    if ((oflag & 3) != 0) {
        if ((oflag & 3) == 1) {
            access = 0x40000000u;                             // GENERIC_WRITE
        } else if ((oflag & 3) == 2) {
            access = 0xC0000000u;                             // GENERIC_READ|WRITE
        } else {
            Errno() = 22; DosErrno() = 0;                     // EINVAL
            return -1;
        }
    } else {
        access = 0x80000000u;                                 // GENERIC_READ
    }

    int share;
    switch (shflag) {                                         // share mode
        case 16: share = 0; break;
        case 32: share = 1; break;
        case 48: share = 2; break;
        case 64: share = 3; break;
        default: Errno() = 22; DosErrno() = 0; return -1;
    }

    int disp;
    unsigned v4 = oflag & 0x700u;                             // create disposition bits
    if (v4 > 0x400) {
        if (v4 != 1280) {
            if (v4 == 1536) { disp = 5; }
            else if (v4 != 1792) { Errno() = 22; DosErrno() = 0; return -1; }
            else { disp = 1; }
        } else { disp = 1; }
    } else if (v4 == 1024 || (oflag & 0x700) == 0) {
        disp = 3;                                             // OPEN_EXISTING
    } else if (v4 == 256) {
        disp = 4;                                             // OPEN_ALWAYS
    } else if (v4 == 512) {
        disp = 5;                                             // TRUNCATE_EXISTING
    } else if (v4 != 768) {
        Errno() = 22; DosErrno() = 0; return -1;
    } else {
        disp = 2;                                             // CREATE_ALWAYS
    }

    unsigned attrs = 128;                                     // FILE_ATTRIBUTE_NORMAL
    if ((oflag & 0x100) != 0 && (pmode & 0x80) == 0)          // O_CREAT & writable pmode
        attrs = 1;                                            // FILE_ATTRIBUTE_READONLY
    if ((oflag & 0x40) != 0) {                                // _O_TEMPORARY
        attrs |= 0x4000000u;
        access |= 0x10000u;                                   // BYTE2 |= 1 -> DELETE
    }
    if ((oflag & 0x1000) != 0)                                // _O_SEQUENTIAL
        attrs |= 0x100u;
    if ((oflag & 0x20) != 0)                                  // _O_TEMPORARY no-inherit
        attrs |= 0x8000000u;
    else if ((oflag & 0x10) != 0)
        attrs |= 0x10000000u;

    int fd = AllocHandle(t);
    if (fd == -1) {
        DosErrno() = 0;
        Errno() = 24;                                         // EMFILE
        return -1;
    }

    int osHandle = Ops4Hooks().osOpen(path, access, share, disp, attrs);
    if (osHandle == -1) {
        unsigned err = Ops4Hooks().lastError();
        MapOsErrorToErrno(err);
        CloseDescriptor(t, static_cast<unsigned>(fd));        // release the slot
        return -1;
    }
    int ft = Ops4Hooks().fileType(osHandle);                  // GetFileType
    switch (ft) {
        case 0:                                               // unknown -> error
            MapOsErrorToErrno(Ops4Hooks().lastError());
            CloseDescriptor(t, static_cast<unsigned>(fd));
            return -1;
        case 2: v21 |= 0x40u; break;                          // char device
        case 3: v21 |= 8u;    break;                          // pipe
        default: break;
    }

    SetOsHandle(t, static_cast<unsigned>(fd), osHandle);
    guild::u8 finalFlags = static_cast<guild::u8>(v21 | 1);
    FdEntry* e = t.Entry(fd);
    e->flags = finalFlags;

    // O_APPEND: seek to end, strip a trailing ^Z (text), reset to start.
    if ((v21 & 0x48) == 0 && (finalFlags & 0x80) && (oflag & 2) != 0) {
        int endPos = SeekDescriptor(t, fd, -1, 2);
        if (endPos == -1) {
            if (DosErrno() != 131) {                          // not ERROR_NEGATIVE_SEEK
                CloseDescriptor(t, static_cast<unsigned>(fd));
                return -1;
            }
        } else {
            char last = 0;
            if ((ReadTranslate(t, fd, &last, 1) == 0 && last == 26
                 && ChangeSize(t, fd, endPos) == -1)
                || SeekDescriptor(t, fd, 0, 0) == -1) {
                CloseDescriptor(t, static_cast<unsigned>(fd));
                return -1;
            }
        }
    }
    if ((finalFlags & 0x48) == 0 && (oflag & 8) != 0)         // O_APPEND
        e->flags |= 0x20u;
    return fd;
}

// gilde.exe 0x142431b — VIBE_File_ParseModeAndOpen
CrtFile* ParseModeAndOpen(FdTable& t, const char* path, const char* mode, int shflag,
                          CrtFile* f) {
    const char* v4 = mode;
    int v18 = 0;      // seen +/- duplicate guard (binary/text exclusivity)
    int v19 = 0;      // seen text/binary guard
    char v5 = *mode;
    unsigned v6;      // oflag accumulator
    unsigned v7;      // FILE flags accumulator (dword_145A3B8 | base)
    const unsigned base = 0;   // dword_145A3B8 (0 in a clean runtime)

    if (v5 == 'a') {
        v6 = 265;                                             // O_WRONLY|O_CREAT|O_APPEND
        v7 = base | 2;
    } else if (v5 == 'r') {
        v6 = 0;                                               // O_RDONLY
        v7 = base | 1;
    } else if (v5 == 'w') {
        v6 = 769;                                             // O_WRONLY|O_CREAT|O_TRUNC
        v7 = base | 2;
    } else {
        return nullptr;
    }

    int v8 = 1;                                               // keep-scanning flag
    while (true) {
        char v9 = *++v4;
        if (!v9 || !v8)
            break;
        if (static_cast<guild::u8>(v9) > 84) {                // 'b','t','c','n','S','R','D' ...
            int v13 = v9 - 98;                                // 'b'
            if (v13 == 0) {
                if ((v6 & 0xC000) != 0) { v8 = 0; }
                else v6 |= 0x8000u;                           // O_BINARY (BYTE1 |= 0x80)
            } else {
                int v14 = v13 - 1;                            // 'c' (99)
                if (v14 == 0) {
                    if (v18) { v8 = 0; }
                    else { v18 = 1; v7 |= 0x4000u; }          // commit-on-flush
                } else {
                    int v15 = v14 - 11;                       // 'n' (110)
                    if (v15 == 0) {
                        if (v18) { v8 = 0; }
                        else { v18 = 1; v7 &= ~0x4000u; }     // no-commit
                    } else if (v15 == 6 && (v6 & 0xC000) == 0) {
                        v6 |= 0x4000u;                        // 't' text (BYTE1 |= 0x40)
                    } else {
                        v8 = 0;
                    }
                }
            }
        } else if (v9 == 84) {                                // 'T' temporary
            if ((v6 & 0x1000) != 0) v8 = 0;
            else v6 |= 0x1000u;
        } else {
            int v10 = v9 - 43;                                // '+'
            if (v10 == 0) {
                if ((v6 & 2) != 0) { v8 = 0; }
                else { v6 = (v6 & 0xFFFFFFFCu) | 2; v7 = (v7 & 0xFFFFFF7Cu) | 0x80; }
            } else {
                int v11 = v10 - 25;                           // 'D' (68)
                if (v11 == 0) {
                    if ((v6 & 0x40) != 0) v8 = 0;
                    else v6 |= 0x40u;                         // _O_TEMPORARY (delete)
                } else {
                    int v12 = v11 - 14;                       // 'R' (82)
                    if (v12 == 0) {
                        if (v19) { v8 = 0; }
                        else { v19 = 1; v6 |= 0x10u; }        // _O_RANDOM
                    } else if (v12 == 1) {                    // 'S' (83)
                        if (v19) { v8 = 0; }
                        else { v19 = 1; v6 |= 0x20u; }        // _O_SEQUENTIAL
                    } else {
                        v8 = 0;
                    }
                }
            }
        }
    }

    int v16 = OpenWithMode(t, path, static_cast<guild::u16>(v6),
                           shflag ? shflag : 0, static_cast<char>(0xA4));
    if (v16 < 0)
        return nullptr;
    ++g_streamOpenCount;                                      // dword_145A25C
    f->flags = v7;                                            // a4[3]
    f->cnt   = 0;                                             // a4[1]
    f->ptr   = nullptr;                                       // a4[0]
    f->base  = nullptr;                                       // a4[2]
    f->hold  = 0;                                             // a4[7]
    f->fd    = v16;                                           // a4[4]
    return f;
}

// gilde.exe 0x5eb830 — VIBE_File_FindFirstEntry
void* FindFirstEntry(const char* pattern, FindData* out) {
    guild::shim::DirEntry first;
    void* handle = Ops4Hooks().findFirst(pattern, &first);
    if (handle == nullptr) {                                  // FindFirstFileA == -1
        Errno() = kEBADF;                                     // VIBE_File_MapLastError leg
        return reinterpret_cast<void*>(static_cast<intptr_t>(-1));
    }
    // VIBE_File_FindNextMatching(handle, 0x37, &v7) — apply default 0x37 mask.
    // A directory carries 0x10; a loose file always carries the ARCHIVE bit
    // (0x20) on a real volume, which is what makes it match the 0x37 mask.
    guild::u32 attrs = first.isDir ? 0x10u : 0x20u;
    if (!FindEntryMatches(0x37, &attrs)) {
        Ops4Hooks().findClose(handle);
        Errno() = kEINVAL;                                    // SetErrnoReturnError
        return reinterpret_cast<void*>(static_cast<intptr_t>(-1));
    }
    CopyFindDataAttributes(first, out);
    return handle;
}

// gilde.exe 0x5eb940 — VIBE_File_FindClose
int FindClose(void* handle) {
    return Ops4Hooks().findClose(handle) - 1;
}

// gilde.exe 0x5d90c0 — VIBE_Vfs_CloseAndFreeEntry
int CloseAndFreeEntry(CrtFile* f) {
    Ops4Hooks().lockEnter();                                  // off_64A920
    TempFileNode* v2 = g_tempHead;
    if (g_tempHead) {
        while (f != v2->file) {                               // a1 != v2[1]
            v2 = v2->next;                                    // *v2
            if (!v2) {
                Ops4Hooks().lockLeave();                      // off_64A924
                return -1;
            }
        }
        Ops4Hooks().lockLeave();
        return Ops4Hooks().flushAndFree(f);                   // VIBE_Resource_FlushAndFree
    }
    Ops4Hooks().lockLeave();
    return -1;
}

} // namespace guild::io
