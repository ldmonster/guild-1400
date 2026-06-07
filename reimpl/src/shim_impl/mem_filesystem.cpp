#include "shim_impl/mem_filesystem.h"
#include <cstring>
#include <cstdio>

namespace guild::shim {

// A handle over a Blob owned by MemFileSystem::files_. Reads/writes operate
// directly on the backing vector (so changes are visible via get()).
class MemFile : public IFile {
public:
    MemFile(MemFileSystem::Blob* blob, bool readable, bool writable, bool append)
        : blob_(blob), readable_(readable), writable_(writable) {
        pos_ = append ? static_cast<std::int64_t>(blob_->size()) : 0;
    }

    std::size_t read(void* dst, std::size_t n) override {
        if (!readable_ || n == 0)
            return 0;
        std::int64_t sz = static_cast<std::int64_t>(blob_->size());
        if (pos_ < 0 || pos_ >= sz)
            return 0;
        std::size_t avail = static_cast<std::size_t>(sz - pos_);
        std::size_t take = n < avail ? n : avail;
        std::memcpy(dst, blob_->data() + pos_, take);
        pos_ += static_cast<std::int64_t>(take);
        return take;
    }

    std::size_t write(const void* src, std::size_t n) override {
        if (!writable_ || n == 0)
            return 0;
        std::int64_t end = pos_ + static_cast<std::int64_t>(n);
        if (end > static_cast<std::int64_t>(blob_->size()))
            blob_->resize(static_cast<std::size_t>(end)); // grow (zero-fill gaps)
        std::memcpy(blob_->data() + pos_, src, n);
        pos_ = end;
        return n;
    }

    std::int64_t seek(std::int64_t off, int whence) override {
        std::int64_t base = 0;
        switch (whence) {
            case SEEK_SET: base = 0; break;
            case SEEK_CUR: base = pos_; break;
            case SEEK_END: base = static_cast<std::int64_t>(blob_->size()); break;
            default: return -1;
        }
        std::int64_t np = base + off;
        if (np < 0)
            return -1;
        pos_ = np;
        return pos_;
    }

    std::int64_t tell() override { return pos_; }
    std::int64_t size() override { return static_cast<std::int64_t>(blob_->size()); }

private:
    MemFileSystem::Blob* blob_;
    std::int64_t pos_ = 0;
    bool readable_;
    bool writable_;
};

IFile* MemFileSystem::open(const char* path, const char* mode) {
    if (!path || !mode)
        return nullptr;

    bool read = false, write = false, append = false, truncate = false,
         must_exist = false, plus = false;
    switch (mode[0]) {
        case 'r': read = true; must_exist = true; break;
        case 'w': write = true; truncate = true; break;
        case 'a': write = true; append = true; break;
        default: return nullptr;
    }
    for (const char* m = mode + 1; *m; ++m)
        if (*m == '+') plus = true;
    if (plus) { read = true; write = true; }

    std::string key(path);
    auto it = files_.find(key);
    if (it == files_.end()) {
        if (must_exist)
            return nullptr; // "r" on a missing file fails, like fopen
        it = files_.emplace(key, Blob{}).first;
    }
    if (truncate)
        it->second.clear();

    return new MemFile(&it->second, read, write, append);
}

void MemFileSystem::close(IFile* f) {
    delete f;
}

bool MemFileSystem::exists(const char* path) {
    return path && files_.find(path) != files_.end();
}

MemFileSystem::Blob MemFileSystem::get(const std::string& path) const {
    auto it = files_.find(path);
    return it == files_.end() ? Blob{} : it->second;
}

} // namespace guild::shim
