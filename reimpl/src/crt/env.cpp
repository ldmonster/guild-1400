#include "crt/env.h"
#include "crt/string.h" // StringLength / StringCompare (the C-locale comparator)

#include <cstring>

namespace guild::crt {

Environment& Env() {
    static Environment g;
    return g;
}

// Compare two keys up to `n` bytes (VIBE_String_CompareLocale in the C locale
// is byte-wise strcmp); returns 0 when equal over the span.
namespace {
bool keyMatches(const char* entry, const char* name, std::size_t nameLen) {
    // entry must read name[0..nameLen) then a '=' (or NUL).
    if (std::strncmp(entry, name, nameLen) != 0)
        return false;
    char term = entry[nameLen];
    return term == '=' || term == '\0';
}
} // namespace

// VIBE_Env_FindEntry @0x14297f9 (getenv).
const char* Environment::GetEnv(const char* name) const {
    if (!name)
        return nullptr;
    std::size_t len = StringLength(name);
    for (const std::string& e : entries_) {
        // The original requires strlen(entry) > len, entry[len]=='=', and the
        // KEY span to match. That means an entry "KEY=..." with a value.
        if (e.size() > len && e[len] == '=' &&
            std::strncmp(e.c_str(), name, len) == 0) {
            return e.c_str() + len + 1; // value just past '='
        }
    }
    return nullptr;
}

// VIBE_Env_FindIndex @0x142a039.
int Environment::FindIndex(const char* name, int nameLen) const {
    int i = 0;
    for (; i < static_cast<int>(entries_.size()); ++i) {
        if (keyMatches(entries_[i].c_str(), name,
                       static_cast<std::size_t>(nameLen))) {
            return i; // positive: found
        }
    }
    // Not found: the original returns -(slotsScanned) (one's-complement-ish
    // negative insertion point). We return the negated count past the end.
    return -i;
}

// putenv — VIBE_Env_PutEnvAnsi @0x60b520 logic, modelled over the vector.
int Environment::PutEnv(const char* assignment) {
    if (!assignment)
        return -1;
    const char* eq = std::strchr(assignment, '=');
    if (eq == assignment) // empty key
        return -1;

    std::size_t keyLen =
        eq ? static_cast<std::size_t>(eq - assignment) : std::strlen(assignment);
    if (keyLen == 0)
        return -1;

    // Locate an existing entry with this KEY.
    int idx = -1;
    for (int i = 0; i < static_cast<int>(entries_.size()); ++i) {
        if (keyMatches(entries_[i].c_str(), assignment, keyLen)) {
            idx = i;
            break;
        }
    }

    // "KEY" with no '=', or "KEY=" with an empty value, removes the entry.
    bool remove = (eq == nullptr) || (eq[1] == '\0');

    if (remove) {
        if (idx >= 0)
            entries_.erase(entries_.begin() + idx);
        return 0;
    }

    std::string line(assignment);
    if (idx >= 0)
        entries_[idx] = line; // replace value in place
    else
        entries_.push_back(line); // append new entry
    return 0;
}

} // namespace guild::crt
