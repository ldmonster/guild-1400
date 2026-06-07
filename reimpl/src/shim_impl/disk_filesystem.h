#pragma once
// Portable default backend for IFileSystem — backed by real std::filesystem / FILE*.
// NOT a translation of gilde.exe; a clean implementation of the interface contract.
// Only this layer touches the real OS; the reconstructed VFS (guild::io) layers
// archive/gzip/loose-file handling on top.
#include "shim/IFileSystem.h"
#include <cstdio>
#include <string>

namespace guild::shim {

class DiskFile : public IFile {
public:
    explicit DiskFile(std::FILE* fp) : fp_(fp) {}
    ~DiskFile() override;

    std::size_t read(void* dst, std::size_t n) override;
    std::size_t write(const void* src, std::size_t n) override;
    std::int64_t seek(std::int64_t off, int whence) override;
    std::int64_t tell() override;
    std::int64_t size() override;

private:
    std::FILE* fp_ = nullptr;
};

class DiskFileSystem : public IFileSystem {
public:
    DiskFileSystem() = default;
    // Paths passed to open()/exists() are resolved relative to `root` (if set).
    explicit DiskFileSystem(std::string root) : root_(std::move(root)) {}

    IFile* open(const char* path, const char* mode) override;
    void close(IFile* f) override;
    bool exists(const char* path) override;
    IDirListing* listDir(const char* path) override;
    bool makeDir(const char* path) override;

private:
    std::string resolve(const char* path) const;
    std::string root_;
};

} // namespace guild::shim
