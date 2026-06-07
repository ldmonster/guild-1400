#pragma once
#include "guild/common/types.h"
#include <string>
#include <vector>

// CRT environment (getenv / putenv) from gilde.exe, namespace guild::crt.
//
// The original keeps the process environment as a NULL-terminated array of
// "KEY=VALUE" C strings (the classic _environ / dword_145A204 block). getenv
// scans it for a matching KEY and returns the pointer just past '='; putenv
// replaces / removes / appends an entry in place. The live block management is
// Mem-coupled (alloc/free/realloc of dword_145A204) and the wide<->ansi sync is
// a large deferred sub-system; we model the array as a vector<string> so the
// search / replace / remove / append SEMANTICS are reproduced and testable.
//
//   VIBE_Env_FindEntry  @0x14297f9 — getenv: return value-of(KEY) or nullptr.
//   VIBE_Env_FindIndex  @0x142a039 — locate KEY's slot (or the negative
//                                    insertion point), used by putenv.
//   VIBE_Env_SetEntry / putenv     — the replace/remove/append logic
//                                    (VIBE_Env_PutEnvAnsi @0x60b520 family).
//
// Key comparison is VIBE_String_CompareLocale, which in the C locale is plain
// case-sensitive strcmp up to the '=' / NUL — reproduced here.
namespace guild::crt {

class Environment {
public:
    Environment() = default;

    // Seed from "KEY=VALUE" strings (the initial _environ block).
    explicit Environment(std::vector<std::string> entries)
        : entries_(std::move(entries)) {}

    // VIBE_Env_FindEntry @0x14297f9 (getenv). Returns a pointer to the VALUE
    // portion (just past '=') of the entry whose KEY matches `name`, or nullptr.
    // Matching is case-sensitive and requires the byte at strlen(name) to be '='.
    const char* GetEnv(const char* name) const;

    // VIBE_Env_FindIndex @0x142a039. Returns the index of the slot whose KEY
    // matches `name` (compared up to nameLen, the next byte being '=' or NUL),
    // or the negative one's-complement insertion index when not found, matching
    // the original's signed return.
    int FindIndex(const char* name, int nameLen) const;

    // putenv("KEY=VALUE") — the VIBE_Env_PutEnvAnsi @0x60b520 logic:
    //   * split at the first '='. No '=' (or "KEY=" with empty value) removes
    //     the entry; otherwise replace an existing KEY's line or append a new
    //     one. Returns 0 on success, -1 on a malformed argument (no key).
    int PutEnv(const char* assignment);

    // Observable state.
    const std::vector<std::string>& Entries() const { return entries_; }
    std::size_t Count() const { return entries_.size(); }

private:
    std::vector<std::string> entries_; // dword_145A204 block (KEY=VALUE lines)
};

// The process-wide environment (mirrors the original's global block).
Environment& Env();

} // namespace guild::crt
