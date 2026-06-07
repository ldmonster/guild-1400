#include "io/file.h"

#include <cstddef>
#include <cstdio>

namespace guild::io {

// Verify the 320-byte handle layout matches the original byte offsets exactly.
static_assert(offsetof(VfsHandle, name)           == 0x000, "name@+0x000");
static_assert(offsetof(VfsHandle, backendStream)  == 0x100, "stream@+0x100 (#64)");
static_assert(offsetof(VfsHandle, memRemaining)   == 0x104, "rem@+0x104 (#65)");
static_assert(offsetof(VfsHandle, memReadTotal)   == 0x108, "pos@+0x108 (#66)");
static_assert(offsetof(VfsHandle, writeDst)       == 0x10C, "writeDst@+0x10C (#67)");
static_assert(offsetof(VfsHandle, writeRemaining) == 0x110, "writeRem@+0x110 (#68)");
static_assert(offsetof(VfsHandle, writeTotal)     == 0x114, "writeTotal@+0x114 (#69)");
static_assert(offsetof(VfsHandle, crc)            == 0x138, "crc@+0x138 (#78)");
static_assert(offsetof(VfsHandle, flags)          == 0x13C, "flags@+0x13C");
static_assert(sizeof(VfsHandle)                   == 320,   "handle is 320 bytes");

// gilde.exe 0x5d4488/0x5fb27c — VIBE_File_OpenStream / VIBE_File_OpenCreate.
// The original drops to the buffered C-stdio layer (VIBE_File_OpenCreate maps the
// mode flags and CreateFile/WriteFile). Here the bytes are served by IFileSystem
// so the dispatch logic stays platform-neutral.
LooseFile* FileOpen(guild::shim::IFileSystem* fs, const char* path, const char* mode) {
    if (!fs || !path)
        return nullptr;
    guild::shim::IFile* f = fs->open(path, mode);
    if (!f)
        return nullptr;
    LooseFile* lf = new LooseFile{fs, f};
    return lf;
}

// gilde.exe 0x5d4770 — VIBE_File_Read  (__usercall: eax=dst, edx=size, ebx=count).
// Returns the number of WHOLE records read times size, i.e. total bytes, matching
// the fread-style contract the VFS read path relies on.
std::size_t FileRead(LooseFile* lf, void* dst, std::size_t size, std::size_t count) {
    if (!lf || !lf->file || !dst)
        return 0;
    std::size_t want = size * count;
    if (want == 0)
        return 0;
    return lf->file->read(dst, want);
}

// gilde.exe 0x5d45f8 — VIBE_File_Seek.
int FileSeek(LooseFile* lf, long offset, int whence) {
    if (!lf || !lf->file)
        return -1;
    std::int64_t r = lf->file->seek(offset, whence);
    return r < 0 ? -1 : 0;
}

// VIBE_File_Tell.
long FileTell(LooseFile* lf) {
    if (!lf || !lf->file)
        return -1;
    return static_cast<long>(lf->file->tell());
}

// gilde.exe 0x5fc9a0 — VIBE_File_CloseHandle.
void FileClose(LooseFile* lf) {
    if (!lf)
        return;
    if (lf->fs && lf->file)
        lf->fs->close(lf->file);
    delete lf;
}

} // namespace guild::io
