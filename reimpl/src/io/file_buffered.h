#pragma once
// gilde.exe — guild::io  (MODULE: buffered stdio-style FILE layer, VIBE_File_*)
//
// The engine ships its own buffered C-stdio clone (Watcom/Borland-style FILE)
// that the loose-file VFS path opens through (VIBE_File_OpenStream @0x5d4488).
// Reads and writes go through a fixed-size buffer; the buffer is refilled /
// flushed against the OS handle. Here the OS handle is replaced by a
// shim::IFile, but the buffering logic and the FILE struct layout are recovered
// 1:1 from the originals.
//
// Recovered FILE struct (offsets confirmed from VIBE_File_Read / Seek /
// FillReadBuffer / FlushBuffer / AllocReadBuffer / AdjustBufferOffset):
//   +0x00  ptr   : current cursor inside the buffer
//   +0x04  u32   : bytes remaining in buffer (read) / bytes pending (write)
//   +0x08  ptr   : -> stream descriptor   { +8 buffer base, +16 deviceFlags }
//   +0x0C  u32   : primary flags  (bit0/1 = read/write '&6'; 0x10 dirty;
//                                   0x20 error; 0x40 binary)
//   +0x0D  u8    : secondary flags (0x04 unbuffered; 0x20 device-detected)
//   +0x10  i32   : OS file descriptor (here: index of the bound IFile)
//   +0x14  u32   : buffer size
//
// Mode parse (VIBE_File_ParseOpenMode @0x5d4220): first char r/w/a sets
// base flags 1/2/0x82; '+' -> read+write (|3); 'b' -> binary (|0x40);
// 't' text; the parse also records text-mode into a side dword.
#include "guild/common/types.h"
#include "shim/IFileSystem.h"
#include <cstddef>

namespace guild::io {

// FILE primary-flag bits (+0x0C).
enum FileFlags : guild::u32 {
    kFileRead   = 0x01,   // opened for read
    kFileWrite  = 0x02,   // opened for write
    kFileDirty  = 0x10,   // buffer holds unflushed write data
    kFileError  = 0x20,   // error/eof sticky
    kFileBinary = 0x40,   // binary (no CR translation)
    kFileEof    = 0x10,   // (read side reuses 0x10 as eof marker post-fill)
};

// The buffered FILE handle. Byte layout mirrors the original 32-bit record up
// to +0x18; the descriptor/IFile pointers are wider on a 64-bit host so they
// follow the fixed-offset prefix.
struct BufferedFile {
    guild::u32  cursorOff;   // +0x00  byte offset of cursor within `buffer`
    guild::u32  remaining;   // +0x04  bytes left to read / bytes buffered to write
    guild::u32  descUnused;  // +0x08  (original: stream-descriptor ptr)
    guild::u32  flags;       // +0x0C  primary flags
    // (the original packs the secondary-flags byte at +0x0D inside the dword;
    //  we keep it separate for clarity)
    guild::u8   flags2;      // secondary flags (text/device)
    guild::u32  bufSize;     // +0x14  buffer capacity

    // host-side state (replaces the OS fd table + global buffer pool)
    guild::shim::IFileSystem* fs;
    guild::shim::IFile*       file;
    guild::u8*  buffer;       // the I/O buffer (lazily allocated)
    bool        binary;       // cached binary mode
    bool        readMode;     // opened for read
    bool        writeMode;    // opened for write
};

// VIBE_File_OpenStream @0x5d4488 -> Open -> ParseOpenMode + AllocStream +
// OpenWithFlags. Open a loose file via `fs`. `mode` is a C-stdio mode string
// ("rb","wb","r","w","rt",...). Returns nullptr on failure.
// (Named FileOpenBuffered to coexist with io/file.h's LooseFile* FileOpen,
//  which has the same parameter list but a different return type.)
BufferedFile* FileOpenBuffered(guild::shim::IFileSystem* fs, const char* path,
                               const char* mode);

// VIBE_File_Read @0x5d4770 — read `size*count` bytes through the buffer.
// Returns the number of whole records read (bytes/size), matching fread.
std::size_t FileRead(void* dst, std::size_t size, std::size_t count,
                     BufferedFile* f);

// VIBE_File_WriteHandle path (via VIBE_Vfs_WriteStream) — write `size*count`
// bytes through the buffer. Returns whole records written.
std::size_t FileWrite(const void* src, std::size_t size, std::size_t count,
                      BufferedFile* f);

// VIBE_File_Seek @0x5d45f8 — whence SEEK_SET/CUR/END. Returns 0 / -1.
int FileSeek(BufferedFile* f, long offset, int whence);

// Current absolute position (ftell-equivalent), or -1.
long FileTell(BufferedFile* f);

// Read one byte (getc), or -1 at EOF.
int FileGetc(BufferedFile* f);

// VIBE_Vfs_ReadLine-style line read against a buffered file: read up to
// `maxLen` chars into `dst` stopping at LF, swallowing CR; NUL-terminate.
// Returns `dst`, or nullptr at immediate EOF.
char* FileReadLine(char* dst, int maxLen, BufferedFile* f);

// Flush a write buffer to the backing file. Returns 0 / -1.
int FileFlush(BufferedFile* f);

// VIBE_File_CloseHandle @0x5fc9a0 (+ ReleaseStream) — flush, close, free.
int FileClose(BufferedFile* f);

// VIBE_File_CreateDirectory @0x5eb920 — make a directory through `fs`.
// Returns 0 on success, -1 on failure (matching the original's mapped errno).
int FileCreateDirectory(guild::shim::IFileSystem* fs, const char* path);

} // namespace guild::io
