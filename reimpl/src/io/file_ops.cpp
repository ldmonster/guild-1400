#include "io/file_ops.h"

#include <cstring>

namespace guild::io {

// ---------------------------------------------------------------------------
// open()/CreateFile flag mapping
// ---------------------------------------------------------------------------

// gilde.exe 0x6062c0 — VIBE_File_MapCreateDisposition
int MapCreateDisposition(int disp, guild::u32* access, guild::u32* creation) {
    if (disp == 2) {
        *access = 0xC0000000u;   // -1073741824 (GENERIC_READ|GENERIC_WRITE)
        *creation = 128;
    } else if (disp == 1) {
        *access = 0x40000000u;   // GENERIC_WRITE
        *creation = 128;
    } else {
        *access = 0x80000000u;   // GENERIC_READ
        *creation = 1;
    }
    return disp;
}

// gilde.exe 0x6062f4 — VIBE_File_MapAccessFlags
guild::u32 MapAccessFlags(unsigned char modeByte, guild::u32* outAccess) {
    guild::u32 group = static_cast<guild::u32>(modeByte) & 0x70u;  // eax = a1 & 0x70
    int sub = modeByte & 7;                                        // v4 = a1 & 7
    if (group < 0x20u) {
        if (group != 0) {
            if (group == 0x10u)
                *outAccess = 0;
        } else {
            *outAccess = 1;
            if (sub == 0)
                *reinterpret_cast<unsigned char*>(outAccess) |= 2u;
        }
    } else if (group <= 0x20u) {            // group == 0x20
        *outAccess = 1;
    } else if (group >= 0x30u) {
        if (group <= 0x30u) {               // group == 0x30
            *outAccess = 2;
        } else if (group == 0x40u) {
            *outAccess = 3;
        }
    }
    return group;
}

// ---------------------------------------------------------------------------
// Buffered-stream offset math
// ---------------------------------------------------------------------------

// gilde.exe 0x5d45a0 — VIBE_File_AdjustBufferOffset
//   if ( delta > s->cnt || delta < (base->start + 8?) ... ) return 1
// The original test: a1 > *(a2+4)  ||  a1 < *(*(a2+8)+8) - *a2
//   *(a2+4)        = s->cnt
//   *(a2+8)        = s->base (the buffer descriptor)
//   *(*(a2+8)+8)   = base->start
//   *a2            = s->ptr
// i.e. delta must lie in [start-ptr, cnt] to stay inside the buffer.
int AdjustBufferOffset(int delta, StreamBuf* s) {
    BufBase* base = reinterpret_cast<BufBase*>(s->base);
    int lowBound = static_cast<int>(reinterpret_cast<char*>(base->start) - s->ptr);
    if (delta > s->cnt || delta < lowBound)
        return 1;
    int newCnt = s->cnt - delta;          // v4 = s->cnt - delta
    char* newPtr = s->ptr + delta;        // v3 = delta + s->ptr
    s->flags &= ~0x10u;                   // clear lookahead flag
    s->ptr = newPtr;
    s->cnt = newCnt;
    return 0;
}

// gilde.exe 0x5d45e0 — VIBE_File_ResetBuffer
StreamBuf* ResetBuffer(StreamBuf* s) {
    s->flags &= ~0x10u;
    BufBase* base = reinterpret_cast<BufBase*>(s->base);
    s->cnt = 0;
    s->ptr = base->start;
    return s;
}

// ---------------------------------------------------------------------------
// DOS date/time conversion (documented FileTimeToDosDateTime math)
// ---------------------------------------------------------------------------

// gilde.exe 0x5fe760 — VIBE_File_ConvertFileTimeToDos (+ FileTimeToDosDateTime)
bool ConvertFileTimeToDos(const DosTime& t, guild::u16* outDate, guild::u16* outTime) {
    if (t.year < 1980 || t.year > 1980 + 127)
        return false;
    *outDate = static_cast<guild::u16>(((t.year - 1980) << 9) |
                                       (t.month << 5) | t.day);
    *outTime = static_cast<guild::u16>((t.hour << 11) | (t.minute << 5) |
                                       (t.second >> 1));
    return true;
}

// gilde.exe 0x5fe788 — VIBE_File_ConvertDosToFileTime (+ DosDateTimeToFileTime)
bool ConvertDosToFileTime(guild::u16 date, guild::u16 time, DosTime* out) {
    out->year   = ((date >> 9) & 0x7F) + 1980;
    out->month  = (date >> 5) & 0x0F;
    out->day    = date & 0x1F;
    out->hour   = (time >> 11) & 0x1F;
    out->minute = (time >> 5) & 0x3F;
    out->second = (time & 0x1F) * 2;
    return true;
}

// ---------------------------------------------------------------------------
// Directory enumeration / attribute mapping
// ---------------------------------------------------------------------------

// gilde.exe 0x5fe7f4 — VIBE_File_FindNextMatching (single-entry test)
bool FindEntryMatches(int mask, guild::u32* attrs) {
    if (*attrs == 0)
        *attrs = 128;                 // FILE_ATTRIBUTE_NORMAL
    return (mask & *attrs) != 0;
}

// gilde.exe 0x5eb894 — VIBE_File_CopyFindDataAttributes
void CopyFindDataAttributes(const guild::shim::DirEntry& e, FindData* out) {
    // The original copied the Win32 attribute bits the game cares about. The
    // host DirEntry only distinguishes directories; map that to the 0x10 bit
    // and default everything else to "archive" (0x20) for regular files, which
    // is what FindFirstFileA reports for an ordinary file.
    out->attributes = 0;
    if (e.isDir)
        out->attributes |= 0x10;      // FILE_ATTRIBUTE_DIRECTORY
    else
        out->attributes |= 0x20;      // FILE_ATTRIBUTE_ARCHIVE
    // Original computed three FILETIME->unix slots; the host supplies one
    // DOS-packed mtime which we store into all three time fields.
    out->ctime = e.dosTime;
    out->atime = e.dosTime;
    out->mtime = e.dosTime;
    out->size  = 0;                   // host listing does not surface size here
    // Copy the leaf name (the original copied byte pairs until NUL).
    const char* src = e.name ? e.name : "";
    std::size_t i = 0;
    for (; i + 1 < sizeof(out->name) && src[i]; ++i)
        out->name[i] = src[i];
    out->name[i] = '\0';
}

// ---------------------------------------------------------------------------
// OS operations routed through IFileSystem
// ---------------------------------------------------------------------------

// gilde.exe 0x5eb920 — VIBE_File_CreateDirectory is translated as
// FileCreateDirectory in io/file_buffered.cpp (reused, not redefined here).

// gilde.exe 0x606520 — VIBE_File_CheckAccess (GetFileAttributesA)
int CheckAccess(guild::shim::IFileSystem* fs, const char* path, char /*mode*/) {
    if (!fs || !fs->exists(path))
        return -1;                    // GetFileAttributesA == -1 path
    // The original then failed write requests on a read-only file; the host
    // does not surface the read-only bit, so a present file is accessible.
    return 0;
}

// ---------------------------------------------------------------------------
// Vfs path helpers
// ---------------------------------------------------------------------------

// gilde.exe 0x5d911c — VIBE_Util_NibbleToHexChar
char NibbleToHexChar(int nibble) {
    int r = nibble + 48;              // '0'
    if (r > 57)                       // > '9'
        r += 39;                      // -> 'a'..'f'
    return static_cast<char>(r);
}

// gilde.exe 0x5d9128 — VIBE_Vfs_BuildTempFileName
//   ebx = pid | (pid >> 16)               (fold the high word into the low word)
//   copy tempDir into out (byte-pair loop), then from p = out+strlen(out):
//     p[0]='t'; loop edx=p+4..p+1 writing [edx+1] = hex(ebx & 0xF), ebx>>=4
//       => p[1..4] hold the 4 hex nibbles, p[4]=low nibble, p[1]=nibble>>12
//     p[5]='_'; p[6]=hex((idx>>4)&0xF); p[7]=hex(idx&0xF);
//     p[8]='.'; p[9..11]="tmp"; p[12]=0
char* BuildTempFileName(char* out, const char* tempDir, guild::u32 pid, int idx) {
    guild::u32 ebx = pid | (pid >> 16);     // shr ebx,10h ; or ebx,eax
    // copy tempDir into out (the original copied byte pairs until NUL)
    std::size_t n = 0;
    while ((out[n] = tempDir[n]) != '\0')
        ++n;
    char* p = out + std::strlen(out);        // esi = ecx+ebp (strlen via scasb)
    p[0] = 't';                              // 0x74
    // edx starts at p+4 and is pre-decremented; [edx+1] is written each pass,
    // ending when edx == p. That fills p[1]..p[4], low nibble at p[4].
    for (int e = 4; e >= 1; --e) {
        p[e] = NibbleToHexChar(static_cast<int>(ebx & 0xF));
        ebx >>= 4;
    }
    p[5] = '_';                              // 0x5F
    p[6] = NibbleToHexChar((idx >> 4) & 0xF);
    p[7] = NibbleToHexChar(idx & 0xF);
    p[8]  = '.';                             // 0x2E
    p[9]  = 't';                             // 0x74
    p[10] = 'm';                             // 0x6D
    p[11] = 'p';                             // 0x70
    p[12] = '\0';
    return out;
}

} // namespace guild::io
