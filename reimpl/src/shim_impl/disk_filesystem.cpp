#include "shim_impl/disk_filesystem.h"
#include <chrono>
#include <ctime>
#include <filesystem>
#include <vector>

namespace guild::shim {

DiskFile::~DiskFile() {
    if (fp_)
        std::fclose(fp_);
}

std::size_t DiskFile::read(void* dst, std::size_t n) {
    if (!fp_ || n == 0)
        return 0;
    return std::fread(dst, 1, n, fp_);
}

std::size_t DiskFile::write(const void* src, std::size_t n) {
    if (!fp_ || n == 0)
        return 0;
    return std::fwrite(src, 1, n, fp_);
}

std::int64_t DiskFile::seek(std::int64_t off, int whence) {
    if (!fp_)
        return -1;
#if defined(_WIN32)
    if (_fseeki64(fp_, off, whence) != 0)
        return -1;
    return _ftelli64(fp_);
#else
    if (std::fseek(fp_, static_cast<long>(off), whence) != 0)
        return -1;
    return static_cast<std::int64_t>(std::ftell(fp_));
#endif
}

std::int64_t DiskFile::tell() {
    if (!fp_)
        return -1;
#if defined(_WIN32)
    return _ftelli64(fp_);
#else
    return static_cast<std::int64_t>(std::ftell(fp_));
#endif
}

std::int64_t DiskFile::size() {
    if (!fp_)
        return -1;
    std::int64_t cur = tell();
    if (cur < 0 || seek(0, SEEK_END) < 0)
        return -1;
    std::int64_t end = tell();
    seek(cur, SEEK_SET);
    return end;
}

std::string DiskFileSystem::resolve(const char* path) const {
    if (root_.empty())
        return path ? path : "";
    return (std::filesystem::path(root_) / (path ? path : "")).string();
}

IFile* DiskFileSystem::open(const char* path, const char* mode) {
    if (!path || !mode)
        return nullptr;
    std::string full = resolve(path);
    std::FILE* fp = std::fopen(full.c_str(), mode);
    if (!fp)
        return nullptr;
    return new DiskFile(fp);
}

void DiskFileSystem::close(IFile* f) {
    delete f; // ~DiskFile closes the FILE*
}

bool DiskFileSystem::exists(const char* path) {
    if (!path)
        return false;
    std::error_code ec;
    return std::filesystem::exists(resolve(path), ec);
}

namespace {
// A directory listing that owns its entry names. Mirrors the data the VFS
// scanner (originally a FindFirstFileA/FindNextFileA loop) consumes.
class DiskDirListing : public IDirListing {
public:
    void add(std::string name, bool isDir, std::uint32_t dosTime) {
        names_.push_back(std::move(name));
        entries_.push_back(DirEntry{}); // fix up name ptr below (stable storage)
        entries_.back().isDir = isDir;
        entries_.back().dosTime = dosTime;
    }
    void finalize() {
        for (std::size_t i = 0; i < entries_.size(); ++i)
            entries_[i].name = names_[i].c_str();
    }
    std::size_t count() const override { return entries_.size(); }
    const DirEntry& at(std::size_t i) const override { return entries_[i]; }
private:
    std::vector<std::string> names_;
    std::vector<DirEntry>    entries_;
};

// Pack a std::filesystem write time into the DOS format the scanner stores.
std::uint32_t PackDosTime(const std::filesystem::path& p) {
    std::error_code ec;
    auto ft = std::filesystem::last_write_time(p, ec);
    if (ec) return 0;
    // Convert file_clock -> system_clock -> time_t -> tm (portable best-effort).
    auto sctp = std::chrono::time_point_cast<std::chrono::system_clock::duration>(
        ft - std::filesystem::file_time_type::clock::now() +
        std::chrono::system_clock::now());
    std::time_t tt = std::chrono::system_clock::to_time_t(sctp);
    std::tm tmv{};
#if defined(_WIN32)
    localtime_s(&tmv, &tt);
#else
    localtime_r(&tt, &tmv);
#endif
    int year = tmv.tm_year + 1900;
    if (year < 1980) year = 1980;
    return (static_cast<std::uint32_t>(year - 1980) << 25) |
           (static_cast<std::uint32_t>(tmv.tm_mon + 1) << 21) |
           (static_cast<std::uint32_t>(tmv.tm_mday) << 16) |
           (static_cast<std::uint32_t>(tmv.tm_hour) << 11) |
           (static_cast<std::uint32_t>(tmv.tm_min) << 5) |
           (static_cast<std::uint32_t>(tmv.tm_sec) >> 1);
}
} // namespace

IDirListing* DiskFileSystem::listDir(const char* path) {
    if (!path)
        return nullptr;
    std::filesystem::path dir = resolve(path);
    std::error_code ec;
    if (!std::filesystem::is_directory(dir, ec))
        return nullptr;
    DiskDirListing* l = new DiskDirListing();
    for (auto& de : std::filesystem::directory_iterator(dir, ec)) {
        if (ec) break;
        bool isDir = de.is_directory(ec);
        l->add(de.path().filename().string(), isDir,
               isDir ? 0u : PackDosTime(de.path()));
    }
    l->finalize();
    return l;
}

bool DiskFileSystem::makeDir(const char* path) {
    if (!path)
        return false;
    std::error_code ec;
    std::filesystem::path p = resolve(path);
    if (std::filesystem::exists(p, ec))
        return true;
    return std::filesystem::create_directories(p, ec);
}

} // namespace guild::shim
