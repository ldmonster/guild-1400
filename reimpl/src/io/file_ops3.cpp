// gilde.exe — guild::io  (file_ops3: VFS byte/line readers, chunk-table scanning,
// config-line reader, working-dir / process-id helpers). See file_ops3.h.
#include "io/file_ops3.h"
#include "io/vfs.h"
#include "io/vfs_tree.h"
#include "io/worldio.h"   // reuse the existing BioReadByte / BioReadDword (0x5dc850/0x5dc8b0)
#include "util/string_ops.h"

#include <cstring>

namespace guild::io {

// --- hooks (inert defaults; tests install their own) ------------------------
namespace {

guild::u32 DefaultGetProcessId() { return 0; }
guild::u32 DefaultGetCurrentDir(guild::u32 /*size*/, char* buf) {
    if (buf) buf[0] = 0;
    return 0;
}
void  DefaultMapLastError() {}
void  DefaultSetErrnoEinval(int /*code*/) {}
void* DefaultAllocMem(guild::u32 /*size*/) { return nullptr; }
void  DefaultFreeMem(void* /*ptr*/) {}

FileOps3Hooks g_hooks = {
    &DefaultGetProcessId,
    &DefaultGetCurrentDir,
    &DefaultMapLastError,
    &DefaultSetErrnoEinval,
    &DefaultAllocMem,
    &DefaultFreeMem,
};

// Module-static 256-byte line scratch buffer (mirrors byte_1407BB0).
char g_configLine[256];

} // namespace

FileOps3Hooks SetFileOps3Hooks(const FileOps3Hooks* hooks) {
    FileOps3Hooks prev = g_hooks;
    if (hooks) {
        g_hooks = *hooks;
        if (!g_hooks.getProcessId)   g_hooks.getProcessId   = &DefaultGetProcessId;
        if (!g_hooks.getCurrentDir)  g_hooks.getCurrentDir  = &DefaultGetCurrentDir;
        if (!g_hooks.mapLastError)   g_hooks.mapLastError    = &DefaultMapLastError;
        if (!g_hooks.setErrnoEinval) g_hooks.setErrnoEinval   = &DefaultSetErrnoEinval;
        if (!g_hooks.allocMem)       g_hooks.allocMem         = &DefaultAllocMem;
        if (!g_hooks.freeMem)        g_hooks.freeMem          = &DefaultFreeMem;
    } else {
        g_hooks = FileOps3Hooks{
            &DefaultGetProcessId, &DefaultGetCurrentDir, &DefaultMapLastError,
            &DefaultSetErrnoEinval, &DefaultAllocMem, &DefaultFreeMem,
        };
    }
    return prev;
}

void VfsFreeMem(void* ptr) {
    if (ptr) g_hooks.freeMem(ptr);
}

// ----------------------------------------------------------------------------
// VIBE_Bio_ReadByte @0x5dc850 and VIBE_Bio_ReadDwordSwapArgs @0x5dc8b0 are
// defined in worldio.cpp (guild::io::BioReadByte / BioReadDword) and reused here.

// gilde.exe 0x5e3bd0 — VIBE_Script_ReadToken  (__usercall, al = (a1@eax))
//   if (!VfsReadStream(buf,1,a1,1)) return 43;   // '+'  (EOF)
//   if (buf[0] > 0x3A)              return 39;    // '\''
//   return buf[0];
guild::u8 ScriptReadToken(VfsHandle* h) {
    guild::u8 b[1];
    if (!VfsReadStream(b, 1, h, 1))
        return static_cast<guild::u8>(kVfsTokenEof);
    if (b[0] > 0x3Au)
        return static_cast<guild::u8>(kVfsTokenOther);
    return b[0];
}

// gilde.exe 0x451698 — VIBE_Vfs_ReadByte  (__usercall, eax = (a1@eax))
//   if (VfsReadStream(buf,1,a1,1) == 1) return buf[0]; else return -1;
int VfsReadByte(VfsHandle* h) {
    guild::u8 b[1];
    if (VfsReadStream(b, 1, h, 1) == 1)
        return b[0];
    return -1;
}

// gilde.exe 0x4516cc — VIBE_Vfs_ReadLine  (__usercall, eax=(a1@eax), edx=a2, ebx=a3)
//   Read one byte; -1 at EOF. If first byte is CR, emit empty line. Otherwise
//   copy bytes until LF / EOF / maxLen, NUL-terminate, then swallow a trailing
//   run of CR/LF. Returns a1, or null if the first read failed.
char* VfsReadLine(char* dst, int maxLen, VfsHandle* h) {
    guild::u8 tmp[1];
    int c = (VfsReadStream(tmp, 1, h, 1) == 1) ? tmp[0] : -1;
    if (c == -1)
        return nullptr;

    char* p = dst;
    int count = 0;
    if (c != 13) {
        for (;;) {
            if (c == 10 || c == -1 || count >= maxLen)
                break;
            *p++ = static_cast<char>(c);
            ++count;
            c = (VfsReadStream(tmp, 1, h, 1) == 1) ? tmp[0] : -1;
            if (c == 13)
                break;
        }
    }
    *p = 0;
    if (c != -1) {
        while (c == 13 || c == 10) {
            c = (VfsReadStream(tmp, 1, h, 1) == 1) ? tmp[0] : -1;
        }
    }
    return dst;
}

// gilde.exe 0x5a75f4 — VIBE_Vfs_ReadStreamBool
//   return VIBE_Vfs_ReadStream(a1,a2,a3,a4) != 0;
bool VfsReadStreamBool(void* dst, guild::u32 size, VfsHandle* h, guild::u32 count) {
    return VfsReadStream(dst, size, h, count) != 0;
}

// ----------------------------------------------------------------------------
// gilde.exe 0x5f86fc — VIBE_Vfs_FindChunkStart  (__usercall, eax = (a1@eax))
//
// Open "rb", read+discard the 4-byte header, then walk the chunk table:
//   tok = ReadToken
//     '-' (45): candidate section start -> read len, read magic;
//                if magic == 0xFAB50005 return the stream (positioned past it);
//                else seek back (len-4, CUR) and keep scanning.
//     '+' (43): premature end -> close, return null.
//     other:    read len, seek (len, CUR) to skip the chunk body, keep scanning.
VfsHandle* VfsFindChunkStart(const char* path) {
    VfsHandle* s = VfsOpenFile(path, "rb");
    if (!s)
        return nullptr;

    guild::u8 header[4];
    VfsReadStream(header, 4, s, 1);

    for (;;) {
        guild::u8 tok = ScriptReadToken(s);
        if (tok == kVfsTokenEof) {            // '+'
            VfsCloseStream(s);
            return nullptr;
        }
        if (tok != kVfsTokenSection) {        // not '-': skip the chunk body
            guild::u32 len = 0;
            BioReadDword(s, &len);
            VfsSeek(s, static_cast<long>(static_cast<guild::i32>(len)), 1 /*SEEK_CUR*/);
            continue;
        }
        // '-' : potential sentinel record
        guild::u32 len = 0, magic = 0;
        BioReadDword(s, &len);
        BioReadDword(s, &magic);
        if (magic == kVfsChunkMagic)
            return s;
        VfsSeek(s, static_cast<long>(static_cast<guild::i32>(len)) - 4, 1 /*SEEK_CUR*/);
        // fall through to next iteration (continue scanning)
    }
}

// gilde.exe 0x5f8570 — VIBE_Vfs_ScanChunkLength  (__usercall, eax=(a1@eax), ecx=a2)
//
// Same walk as VfsFindChunkStart, but records the file offset (VfsTell) at the
// start of each record and, on the sentinel match, closes the stream and returns
// that offset. '+' -> close, return -1.
long VfsScanChunkLength(const char* path) {
    VfsHandle* s = VfsOpenFile(path, "rb");
    if (!s)
        return -1;

    guild::u8 header[4];
    VfsReadStream(header, 4, s, 1);

    for (;;) {
        long pos = VfsTell(s);
        guild::u8 tok = ScriptReadToken(s);
        if (tok == kVfsTokenEof) {            // '+'
            VfsCloseStream(s);
            return -1;
        }
        if (tok != kVfsTokenSection) {        // not '-'
            guild::u32 len = 0;
            BioReadDword(s, &len);
            VfsSeek(s, static_cast<long>(static_cast<guild::i32>(len)), 1);
            continue;
        }
        guild::u32 len = 0, magic = 0;
        BioReadDword(s, &len);
        BioReadDword(s, &magic);
        if (magic == kVfsChunkMagic) {
            VfsCloseStream(s);
            return pos;
        }
        VfsSeek(s, static_cast<long>(static_cast<guild::i32>(len)) - 4, 1);
    }
}

// gilde.exe 0x5f8640 — VIBE_Vfs_CheckChunkFlag  (__usercall, al=(a1@eax), cx=a2)
//
//   flag = 1
//   if (!open) return 1
//   read 4-byte header
//   readByte; readDword; readByte; readDword; readByte (into tagByte)
//   if (tagByte == 2) { readByte; if ((tagByte>>1)&1) flag = 0; }
//   close; return flag
bool VfsCheckChunkFlag(const char* path) {
    bool flag = true;
    VfsHandle* s = VfsOpenFile(path, "rb");
    if (!s)
        return flag;

    guild::u8 header[4];
    VfsReadStream(header, 4, s, 1);

    guild::u32 dword = 0;
    guild::u8 tag[1];
    BioReadByte(s, tag);
    BioReadDword(s, &dword);
    BioReadByte(s, tag);
    BioReadDword(s, &dword);
    BioReadByte(s, tag);
    if (tag[0] == 2) {
        BioReadByte(s, tag);
        if (((tag[0] >> 1) & 1) == 1)
            flag = false;
    }
    VfsCloseStream(s);
    return flag;
}

// ----------------------------------------------------------------------------
// gilde.exe 0x5dc790 — VIBE_Vfs_ReadConfigLine  (__usercall, eax = (a1@eax))
//
// Loop reading lines (256-byte scratch). For each line:
//   - right-trim trailing LF/CR/TAB/SPACE (scan from end down to first non-ws);
//   - if the whole line was whitespace (i < 0), read the next line;
//   - terminate at that position, skip leading LF/CR/TAB/SPACE;
//   - if the first surviving char is NUL, read the next line;
//   - if it is ';' (comment), read the next line;
//   - otherwise upper-case the survivor in place and return it.
char* VfsReadConfigLine(VfsHandle* h) {
    for (;;) {
        char* result = VfsReadLine(g_configLine, 256, h);
        if (!result)
            return nullptr;

        // Right-trim: find last index whose char is not LF/CR/TAB/SPACE.
        int i = static_cast<int>(std::strlen(g_configLine)) - 1;
        for (; i > -1; --i) {
            char ch = g_configLine[i];
            if (ch != 10 && ch != 13 && ch != 9 && ch != 32)
                break;
        }
        if (i < 0)
            continue;                       // line was all whitespace

        // The original writes the NUL one past the last kept char (byte_1407BB1[i]
        // = g_configLine[i+1]); since i is the last non-ws index, that trims the
        // trailing whitespace run.
        g_configLine[i + 1] = 0;

        // Skip leading whitespace.
        char* p = g_configLine;
        while (*p == 10 || *p == 13 || *p == 9 || *p == 32) {
            if (*++p == 0)
                break;
        }
        if (*p == 0)
            continue;                       // nothing but whitespace remained
        if (*p == ';')
            continue;                       // comment line

        guild::util::StrToUpper(p);
        return p;
    }
}

// gilde.exe 0x5dc770 — VIBE_Vfs_FileExists  (__usercall, al = (a1@eax))
//   return VIBE_Vfs_ResolvePath(a1, dword_62EB78, &dummy) != 0;
bool VfsFileExists(const char* path) {
    VfsNode* dummyDir = nullptr;
    return ResolvePath(path, VfsRoot(), &dummyDir) != nullptr;
}

// gilde.exe 0x5fc7b0 — VIBE_Vfs_GetProcessId
//   return GetCurrentProcessId();
guild::u32 VfsGetProcessId() {
    return g_hooks.getProcessId();
}

// gilde.exe 0x5eeeb0 — VIBE_Vfs_GetWorkingDir  (__usercall, eax=a1(dst), edx=a2(size))
//   len = GetCurrentDirectoryA(0x104, scratch)
//   if (!len)            { MapLastError(); return 0; }
//   if (dst) {
//     if (len > size)    { SetErrnoEinval(); return 0; }
//   } else {
//     need = len + 1; if (size > need) need = size;
//     dst = AllocFromFreeList(need); if (!dst) { SetErrnoEinval(); return 0; }
//   }
//   memcpy(dst, scratch, len + 1); return dst;
char* VfsGetWorkingDir(char* dst, guild::u32 size) {
    char scratch[0x104];
    guild::u32 len = g_hooks.getCurrentDir(0x104u, scratch);
    if (!len) {
        g_hooks.mapLastError();
        return nullptr;
    }
    if (dst) {
        if (len > size) {
            g_hooks.setErrnoEinval(14);   // 0x5eef0d: mov eax, 0Eh (EINVAL)
            return nullptr;
        }
    } else {
        guild::u32 need = len + 1;
        if (size > need)
            need = size;
        dst = static_cast<char*>(g_hooks.allocMem(need));
        if (!dst) {
            g_hooks.setErrnoEinval(5);    // 0x5eeef2: mov eax, 5 (EIO)
            return nullptr;
        }
    }
    std::memcpy(dst, scratch, len + 1);
    return dst;
}

} // namespace guild::io
