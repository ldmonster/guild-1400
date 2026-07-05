#pragma once
// Guild text module — the localized text / label database.
//
// The engine keeps all UI strings in one big indexed array, loaded from the
// localized .dat/text resources. Three parallel arrays form the database:
//
//   dword_8C36B0[i]      -> char*  : the i-th text string (the "text array")
//   byte_8D36B0[80 * i]  -> char[] : the i-th entry's name/key (80-byte stride)
//   byte_767EB0[i]       -> u8     : a per-entry tag byte (gender/category/random
//                                    selector; the random-text builder writes a
//                                    9..18 marker here, see ParseRandomTextToken)
//   dword_62EB24         -> i32    : number of entries currently loaded.
//
// Recovered originals modeled here:
//   VIBE_Text_FindTextArrayIndex @0x44e0d8 — case-insensitive name -> index
//   VIBE_Text_ParseRandomTextToken @0x44b8a0 — "{rN}" -> tag byte (N+8) on a slot
//   the `dword_8C36B0[id]` indexed lookup used pervasively by the renderers
//   the title/season sub-tables (dword_8C379C/8C3790/8C37A8/8C37E0) which are
//   fixed-index slices of the same text array.
//
// This is a faithful in-memory model of the database; loading the real .dat
// files (VIBE_Text_BuildTextArray @0x44bb5c, 7.5 KB) is deferred (see report).

#include "guild/common/types.h"

#include <cstring>
#include <string>
#include <vector>

namespace guild::gui::text {

using guild::i32;
using guild::u8;

inline constexpr int kNameStride = 80;     // byte_8D36B0 stride
inline constexpr int kMaxEntries = 0x4000; // FindTextArrayIndex/RenderRichString bound

// Tag-byte sentinel meaning "no special tag" (the random builder leaves
// non-random entries at 0; RenderRichString treats 255 as "plain string").
inline constexpr u8 kTagNone   = 0;
inline constexpr u8 kTagPlain  = 255;

// VIBE_Math_RandomModulo @0x58b89c — (n ? (int)crt::RandNext() % n : 0).
int RandomModulo(unsigned short n);

// {rN} maps to a tag byte of (N-1)+9 == N+8, for N in 1..10 (digit '1'..':').
// The original: v2 = ch - '1';  if (v2 <= 9) tag = v2 + 9.
inline constexpr u8 kRandomTagBase = 9;

// In-memory localized text database. Mirrors the three parallel globals.
class TextDb {
public:
    // Append an entry (string + name + tag). Returns its index. Mirrors how
    // BuildTextArray fills dword_8C36B0 / byte_8D36B0 / byte_767EB0 in order.
    int Add(const std::string& text, const std::string& name, u8 tag = kTagNone);

    // Place an entry at an ABSOLUTE index (the engine's dword_8C36B0[baseIndex+i]
    // slotting), growing the table with empty entries as needed. Order-independent
    // — unlike Add(), it does not assume members load in increasing baseIndex order.
    void SetAt(int index, const std::string& text, const std::string& name,
               u8 tag = kTagNone);

    int Count() const { return static_cast<int>(entries_.size()); }  // dword_62EB24

    // dword_8C36B0[id] — the i-th string. Returns nullptr when out of range
    // (the renderers test `dword_8C36B0[id]` for null before use).
    const char* Text(int id) const;

    // byte_767EB0[id] — the per-entry tag byte (255 when out of range, matching
    // the renderer's `(unsigned __int8)byte_767EB0[v94]` reads of valid slots).
    u8 Tag(int id) const;

    // byte_8D36B0[80*id] — the entry name/key.
    const char* Name(int id) const;

    // VIBE_Text_FindTextArrayIndex @0x44e0d8 — case-insensitive search over the
    // name array; returns the matching index or -1. Stops at the first empty
    // string slot or kMaxEntries, exactly like the original.
    int FindIndex(const char* name) const;

    // VIBE_Text_ParseRandomTextToken @0x44b8a0 — given a "{rN}" token, decode N
    // and stamp the tag byte (N+8) onto the entry at the current append cursor.
    // Returns the cursor index (dword_62EB24). On a malformed token (N not 1..10)
    // the original logs "txt_ParseRandomText(): Syntax error..." and leaves the
    // tag at its previous value; we surface that via `ok`.
    //   token points at the '{' of "{rN}".
    int ParseRandomTextToken(const char* token, bool* ok = nullptr);

    // VIBE_Math_RandomModulo-based random-variant pick: given a base index whose
    // tag marks the start of a {rN} group of `variants`, advance by a seeded
    // RandNext()%variants. Mirrors the RenderRichString random path:
    //   v211 = dword_8C36B0[baseIndex + (u16)VIBE_Math_RandomModulo(variants)]
    // The caller supplies `variants` decoded from the original {rN} text (N).
    const char* PickRandomVariant(int baseIndex, int variants) const;

private:
    struct Entry {
        std::string text;
        std::string name;
        u8 tag;
    };
    std::vector<Entry> entries_;
};

} // namespace guild::gui::text
