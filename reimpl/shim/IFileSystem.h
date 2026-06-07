#pragma once
// Host filesystem boundary. The reconstructed VFS (guild::io) layers archive
// (PKZIP .BIN/.BIN0-5), gzip, and loose-file handling on top of this interface;
// only this interface touches the real OS.
#include <cstddef>
#include <cstdint>

namespace guild::shim {

// One directory entry, as surfaced by IFileSystem::listDir. Mirrors the subset
// of WIN32_FIND_DATAA the VFS scanner (VIBE_Vfs_ScanDirectory, originally driven
// by FindFirstFileA/FindNextFileA) consumes: the leaf name, whether it is a
// directory, and the DOS-packed last-write time the scanner stores per file.
struct DirEntry {
    const char* name = nullptr;   // leaf file/dir name (no path)
    bool        isDir = false;    // FILE_ATTRIBUTE_DIRECTORY (0x10)
    // DOS-packed mtime: (year-1980)<<25 | month<<21 | day<<16 | hour<<11 |
    //                   minute<<5 | second>>1   (0 if unknown)
    std::uint32_t dosTime = 0;
};

class IFile {
public:
    virtual ~IFile() = default;
    virtual std::size_t read(void* dst, std::size_t n) = 0;
    virtual std::size_t write(const void* src, std::size_t n) = 0;
    virtual std::int64_t seek(std::int64_t off, int whence) = 0; // whence: SEEK_SET/CUR/END
    virtual std::int64_t tell() = 0;
    virtual std::int64_t size() = 0;
};

// Result of an IFileSystem::listDir call. Owns the backing storage for the
// returned DirEntry names so the caller can hold the entries past the call.
class IDirListing {
public:
    virtual ~IDirListing() = default;
    virtual std::size_t count() const = 0;
    virtual const DirEntry& at(std::size_t i) const = 0;
};

class IFileSystem {
public:
    virtual ~IFileSystem() = default;
    virtual IFile* open(const char* path, const char* mode) = 0; // nullptr on failure
    virtual void close(IFile* f) = 0;
    virtual bool exists(const char* path) = 0;
    // List the immediate entries of directory `path` (the original used a
    // FindFirstFileA("<dir>/*") + FindNextFileA loop). Returns nullptr if the
    // directory does not exist. Caller owns the result and must `delete` it.
    // Default returns nullptr so existing backends keep compiling; the VFS
    // scanner requires a backend that overrides it.
    virtual IDirListing* listDir(const char* /*path*/) { return nullptr; }
    // Create a directory (VIBE_File_CreateDirectory -> CreateDirectoryA).
    // Returns true on success (or if it already exists). Default: no-op false.
    virtual bool makeDir(const char* /*path*/) { return false; }
};

} // namespace guild::shim
