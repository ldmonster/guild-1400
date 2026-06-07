#include "io/file_buffered.h"

#include <cstdio>
#include <cstring>
#include <new>

namespace guild::io {

// Verify the fixed-offset prefix matches the recovered FILE record (the part
// the original's pointer arithmetic touches at +0..+0x18).
static_assert(offsetof(BufferedFile, cursorOff)  == 0x00, "cursor@+0x00");
static_assert(offsetof(BufferedFile, remaining)  == 0x04, "remaining@+0x04");
static_assert(offsetof(BufferedFile, descUnused) == 0x08, "desc@+0x08");
static_assert(offsetof(BufferedFile, flags)      == 0x0C, "flags@+0x0C");

namespace {
constexpr guild::u32 kDefaultBufSize = 4096;   // VIBE_File_AllocReadBuffer default

// VIBE_File_ParseOpenMode @0x5d4220 — distil a C-stdio mode string.
struct ParsedMode {
    bool read = false;
    bool write = false;
    bool append = false;
    bool binary = false;   // 'b'
    bool plus = false;     // '+'
    bool valid = false;
};
ParsedMode ParseMode(const char* mode) {
    ParsedMode m;
    if (!mode || !*mode)
        return m;
    switch (mode[0]) {
        case 'r': m.read = true;  m.valid = true; break;   // base flag 1
        case 'w': m.write = true; m.valid = true; break;   // base flag 2
        case 'a': m.write = true; m.append = true; m.valid = true; break; // 0x82
        default: return m;                                 // EINVAL
    }
    for (const char* p = mode + 1; *p; ++p) {
        if (*p == '+') { m.plus = true; m.read = true; m.write = true; }
        else if (*p == 'b') m.binary = true;
        else if (*p == 't') m.binary = false;
        // other chars ignored, matching the original's tolerant scan
    }
    return m;
}

// Map a parsed mode to the IFile open string the host backend expects.
const char* HostMode(const ParsedMode& m) {
    if (m.append) return m.binary ? "ab" : "a";
    if (m.read && m.write) return m.binary ? "rb+" : "r+";
    if (m.write) return m.binary ? "wb" : "w";
    return m.binary ? "rb" : "r";
}

// VIBE_Crt_FillReadBuffer @0x5fba68 — refill the read buffer from the file.
// Returns the number of bytes now available (0 at EOF, sets kFileEof).
guild::u32 FillReadBuffer(BufferedFile* f) {
    if (!f->buffer) {
        f->buffer = new (std::nothrow) guild::u8[f->bufSize ? f->bufSize : kDefaultBufSize];
        if (!f->bufSize) f->bufSize = kDefaultBufSize;
    }
    f->cursorOff = 0;
    std::size_t got = f->file ? f->file->read(f->buffer, f->bufSize) : 0;
    f->remaining = static_cast<guild::u32>(got);
    if (got == 0)
        f->flags |= kFileEof;   // 0x10 marks EOF on the read side post-fill
    return f->remaining;
}
} // namespace

// gilde.exe 0x5d4488/0x5d4444/0x5d435c — VIBE_File_OpenStream chain.
BufferedFile* FileOpenBuffered(guild::shim::IFileSystem* fs, const char* path,
                               const char* mode) {
    if (!fs || !path)
        return nullptr;
    ParsedMode m = ParseMode(mode);
    if (!m.valid)
        return nullptr;
    guild::shim::IFile* file = fs->open(path, HostMode(m));
    if (!file)
        return nullptr;

    BufferedFile* f = new (std::nothrow) BufferedFile();
    if (!f) { fs->close(file); return nullptr; }
    std::memset(f, 0, sizeof(*f));
    f->fs = fs;
    f->file = file;
    f->binary = m.binary;
    f->readMode = m.read;
    f->writeMode = m.write;
    f->bufSize = kDefaultBufSize;
    f->cursorOff = 0;
    f->remaining = 0;
    f->flags = 0;
    if (m.read)  f->flags |= kFileRead;
    if (m.write) f->flags |= kFileWrite;
    if (m.binary) f->flags |= kFileBinary;
    return f;
}

// gilde.exe 0x5d4770 — VIBE_File_Read.  Pulls bytes from the buffer, refilling
// as needed; in binary mode bulk reads bypass the buffer for large requests.
// Text mode performs CR removal / Ctrl-Z (0x1A) EOF, exactly as the original.
std::size_t FileRead(void* dst, std::size_t size, std::size_t count,
                     BufferedFile* f) {
    if (!f || !dst || (f->flags & kFileRead) == 0)
        return 0;
    std::size_t want = size * count;
    if (want == 0)
        return 0;

    guild::u8* out = static_cast<guild::u8*>(dst);
    std::size_t produced = 0;

    if (f->binary) {
        std::size_t left = want;
        while (left) {
            if (f->remaining) {
                guild::u32 take = f->remaining;
                if (take > left) take = static_cast<guild::u32>(left);
                std::memcpy(out, f->buffer + f->cursorOff, take);
                out += take;
                produced += take;
                f->cursorOff += take;
                f->remaining -= take;
                left -= take;
                continue;
            }
            // buffer empty: for large reads go straight to the file
            if (left >= f->bufSize) {
                std::size_t got = f->file ? f->file->read(out, left) : 0;
                if (got == 0) { f->flags |= kFileEof; break; }
                out += got;
                produced += got;
                left -= got;
                if (got < f->bufSize) { /* short read may be eof */ }
            } else {
                if (FillReadBuffer(f) == 0)
                    break;
            }
        }
    } else {
        // text mode: byte-by-byte with CR swallow and Ctrl-Z EOF
        std::size_t left = want;
        while (left) {
            int c = FileGetc(f);
            if (c < 0)
                break;
            *out++ = static_cast<guild::u8>(c);
            ++produced;
            --left;
        }
    }
    return produced / size;
}

// gilde.exe 0x5d4770 (text path) — read one translated byte.
int FileGetc(BufferedFile* f) {
    if (!f || (f->flags & kFileRead) == 0)
        return -1;
    for (;;) {
        if (f->remaining == 0) {
            if (FillReadBuffer(f) == 0)
                return -1;
        }
        guild::u8 b = f->buffer[f->cursorOff++];
        --f->remaining;
        if (f->binary)
            return b;
        if (b == '\r')      // swallow CR; loop to next byte
            continue;
        if (b == 0x1A) {    // Ctrl-Z -> EOF in text mode
            f->flags |= kFileEof;
            return -1;
        }
        return b;
    }
}

// Flush a dirty write buffer (VIBE_File_FlushBuffer @0x5fb620, simplified to
// the IFile boundary). Returns 0 / -1.
int FileFlush(BufferedFile* f) {
    if (!f)
        return -1;
    if ((f->flags & kFileWrite) == 0)
        return 0;
    if ((f->flags & kFileDirty) && f->remaining && f->buffer && f->file) {
        std::size_t n = f->file->write(f->buffer, f->remaining);
        if (n != f->remaining) {
            f->flags |= kFileError;
            return -1;
        }
    }
    f->remaining = 0;
    f->cursorOff = 0;
    f->flags &= ~static_cast<guild::u32>(kFileDirty);
    return 0;
}

// Write through the buffer (mirrors the FlushBuffer/WriteHandle pairing).
std::size_t FileWrite(const void* src, std::size_t size, std::size_t count,
                      BufferedFile* f) {
    if (!f || !src || (f->flags & kFileWrite) == 0)
        return 0;
    std::size_t want = size * count;
    if (want == 0)
        return 0;
    if (!f->buffer) {
        f->buffer = new (std::nothrow) guild::u8[f->bufSize ? f->bufSize : kDefaultBufSize];
        if (!f->bufSize) f->bufSize = kDefaultBufSize;
        if (!f->buffer) return 0;
    }
    const guild::u8* in = static_cast<const guild::u8*>(src);
    std::size_t left = want;
    while (left) {
        guild::u32 room = f->bufSize - f->remaining;
        if (room == 0) {
            if (FileFlush(f) != 0)
                break;
            room = f->bufSize;
        }
        guild::u32 take = room < left ? room : static_cast<guild::u32>(left);
        std::memcpy(f->buffer + f->remaining, in, take);
        f->remaining += take;
        f->flags |= kFileDirty;
        in += take;
        left -= take;
    }
    return (want - left) / size;
}

// gilde.exe 0x5d45f8 — VIBE_File_Seek. Flush/resync the buffer then move the
// OS file pointer. SEEK_CUR accounts for unconsumed read-buffer bytes.
int FileSeek(BufferedFile* f, long offset, int whence) {
    if (!f || !f->file)
        return -1;
    if (f->flags & kFileWrite)
        FileFlush(f);
    long target = offset;
    if (whence == SEEK_CUR) {
        // current OS position minus the bytes still sitting in the read buffer
        long osPos = static_cast<long>(f->file->tell());
        target = osPos - static_cast<long>(f->remaining) + offset;
        whence = SEEK_SET;
    }
    // dump the read buffer; subsequent reads refill from the new position
    f->remaining = 0;
    f->cursorOff = 0;
    f->flags &= ~static_cast<guild::u32>(kFileEof);
    std::int64_t r = f->file->seek(target, whence);
    return r < 0 ? -1 : 0;
}

long FileTell(BufferedFile* f) {
    if (!f || !f->file)
        return -1;
    long osPos = static_cast<long>(f->file->tell());
    if (f->flags & kFileWrite)
        return osPos + static_cast<long>(f->remaining);  // pending write bytes
    return osPos - static_cast<long>(f->remaining);       // unconsumed read bytes
}

namespace {
// Raw, untranslated byte read (the original VIBE_Vfs_ReadLine reads through
// VIBE_Vfs_ReadStream which, for a loose file, returns raw bytes; the CR/LF
// handling is done inside ReadLine itself, not by the stream layer). Returns -1
// at EOF.
int RawGetc(BufferedFile* f) {
    if (!f || (f->flags & kFileRead) == 0)
        return -1;
    if (f->remaining == 0) {
        if (FillReadBuffer(f) == 0)
            return -1;
    }
    guild::u8 b = f->buffer[f->cursorOff++];
    --f->remaining;
    return b;
}
} // namespace

// gilde.exe 0x4516cc — VIBE_Vfs_ReadLine.  Read one raw byte; if it is CR the
// line is empty. Otherwise append until LF / EOF / limit, breaking on CR. Then
// NUL-terminate and consume the trailing CR/LF that terminated the line.
//
// The original swallows a *run* of CR/LF after the line via a peek-less getc
// loop, which (with its terminating non-EOL read) advances one byte into the
// next line. We instead seek that one over-read byte back so consecutive
// ReadLine calls round-trip; this is the behaviour every caller relies on (a
// config/script line iterator) and is observationally identical except for the
// off-by-one consumption the original's loop exhibits on the FINAL line.
char* FileReadLine(char* dst, int maxLen, BufferedFile* f) {
    int c = RawGetc(f);
    if (c < 0)
        return nullptr;
    char* p = dst;
    int n = 0;
    if (c != 13) {
        while (c != 10 && c != -1 && n < maxLen) {
            *p++ = static_cast<char>(c);
            ++n;
            c = RawGetc(f);
            if (c == 13)
                break;
        }
    }
    *p = '\0';
    // consume the CR/LF run that terminated this line, pushing back any first
    // non-EOL byte (the start of the next line) via a 1-byte rewind.
    while (c == 13 || c == 10) {
        c = RawGetc(f);
        if (c != 13 && c != 10 && c != -1) {
            // rewind one byte: cursor back if it came from the buffer, else seek
            if (f->cursorOff > 0) { --f->cursorOff; ++f->remaining; }
            else FileSeek(f, -1, SEEK_CUR);
            break;
        }
    }
    return dst;
}

// gilde.exe 0x5fc9a0 / 0x5d4494 — close + free.
int FileClose(BufferedFile* f) {
    if (!f)
        return -1;
    int rc = 0;
    if (f->flags & kFileWrite)
        rc = FileFlush(f);
    if (f->fs && f->file)
        f->fs->close(f->file);
    delete[] f->buffer;
    delete f;
    return rc;
}

// gilde.exe 0x5eb920 — VIBE_File_CreateDirectory.
int FileCreateDirectory(guild::shim::IFileSystem* fs, const char* path) {
    if (!fs || !path)
        return -1;
    return fs->makeDir(path) ? 0 : -1;
}

} // namespace guild::io
