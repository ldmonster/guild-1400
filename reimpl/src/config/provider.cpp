#include "config/provider.h"

#include <cctype>

namespace guild::config {

namespace {
std::string lower(const std::string& s) {
    std::string r = s;
    for (char& c : r)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return r;
}
} // namespace

// --- MemRegistry -----------------------------------------------------------
// Win32 registry keys and value names are case-insensitive; we canonicalize
// both to lowercase so the in-memory store matches that behavior.

MemRegistry::Key* MemRegistry::keyFor(int handle) {
    if (handle < 0 || static_cast<std::size_t>(handle) >= handles_.size())
        return nullptr;
    const std::string& path = handles_[handle];
    if (path.empty())
        return nullptr;
    auto it = keys_.find(path);
    if (it == keys_.end())
        return nullptr;
    return &it->second;
}

int MemRegistry::openKey(const std::string& subkey, bool create) {
    const std::string path = lower(subkey);
    auto it = keys_.find(path);
    if (it == keys_.end()) {
        if (!create)
            return -1;
        keys_.emplace(path, Key{});
    }
    // Allocate a handle slot (reuse a freed one if available).
    for (std::size_t i = 0; i < handles_.size(); ++i) {
        if (handles_[i].empty()) {
            handles_[i] = path;
            return static_cast<int>(i);
        }
    }
    handles_.push_back(path);
    return static_cast<int>(handles_.size() - 1);
}

int MemRegistry::closeKey(int handle) {
    if (handle >= 0 && static_cast<std::size_t>(handle) < handles_.size())
        handles_[handle].clear();
    return 0;
}

bool MemRegistry::queryDword(int handle, const std::string& name, std::uint32_t* out) {
    Key* k = keyFor(handle);
    if (!k)
        return false;
    auto it = k->find(lower(name));
    if (it == k->end() || it->second.type != RegType::Dword)
        return false;
    *out = it->second.dword;
    return true;
}

bool MemRegistry::queryString(int handle, const std::string& name, std::string* out) {
    Key* k = keyFor(handle);
    if (!k)
        return false;
    auto it = k->find(lower(name));
    if (it == k->end() || it->second.type != RegType::Sz)
        return false;
    *out = it->second.sz;
    return true;
}

int MemRegistry::setDword(int handle, const std::string& name, std::uint32_t value) {
    Key* k = keyFor(handle);
    if (!k)
        return 1; // not ERROR_SUCCESS
    RegValue v;
    v.type = RegType::Dword;
    v.dword = value;
    (*k)[lower(name)] = v;
    return 0;
}

int MemRegistry::setString(int handle, const std::string& name, const std::string& value) {
    Key* k = keyFor(handle);
    if (!k)
        return 1;
    RegValue v;
    v.type = RegType::Sz;
    v.sz = value;
    (*k)[lower(name)] = v;
    return 0;
}

// --- MemEnv ----------------------------------------------------------------
bool MemEnv::get(const std::string& name, std::string* out) const {
    auto it = vars_.find(name);
    if (it == vars_.end())
        return false;
    *out = it->second;
    return true;
}

void MemEnv::set(const std::string& name, const std::string& value) {
    vars_[name] = value;
}

} // namespace guild::config
