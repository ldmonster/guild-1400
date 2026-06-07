#pragma once
// In-memory IFileSystem variant for tests — files live in a std::map of byte
// vectors, no real OS access. NOT a translation of gilde.exe; a clean
// implementation of the interface contract. Mode semantics mirror fopen:
//   "r"  read existing (fail if absent)   "w" truncate/create   "a" append/create
//   "rb"/"wb"/"r+"/"w+" etc. — the 'b' is ignored (always binary), '+' grants R/W.
#include "shim/IFileSystem.h"
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace guild::shim {

class MemFileSystem : public IFileSystem {
public:
    using Blob = std::vector<std::uint8_t>;

    IFile* open(const char* path, const char* mode) override;
    void close(IFile* f) override;
    bool exists(const char* path) override;

    // --- test helpers ---
    // Inject/replace a file's contents directly.
    void put(const std::string& path, const Blob& data) { files_[path] = data; }
    // Read a file's current contents (empty if absent).
    Blob get(const std::string& path) const;

private:
    friend class MemFile;
    std::map<std::string, Blob> files_;
};

} // namespace guild::shim
