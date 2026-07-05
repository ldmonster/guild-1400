#include "gui/text/textdb.h"

#include "crt/rand.h"

namespace guild::gui::text {

// VIBE_Math_RandomModulo @0x58b89c — (n ? (int)RandNext() % n : 0).
int RandomModulo(unsigned short n) {
    if (!n)
        return 0;
    return guild::crt::RandNext() % n;
}

namespace {
// VIBE_Util_StrCmpNoCase @0x5cb8f0 — ASCII A-Z case-insensitive compare,
// returns (loweredA - loweredB) at the first difference / NUL.
int StrCmpNoCase(const char* a, const char* b) {
    for (;;) {
        unsigned char va = static_cast<unsigned char>(*a);
        unsigned char vb = static_cast<unsigned char>(*b);
        if (va >= 0x41u && va <= 0x5Au)
            va += 32;
        if (vb >= 0x41u && vb <= 0x5Au)
            vb += 32;
        if (va != vb || !vb)
            return static_cast<int>(va) - static_cast<int>(vb);
        ++a;
        ++b;
    }
}
} // namespace

int TextDb::Add(const std::string& text, const std::string& name, u8 tag) {
    int idx = static_cast<int>(entries_.size());
    entries_.push_back(Entry{text, name, tag});
    return idx;
}

void TextDb::SetAt(int index, const std::string& text, const std::string& name,
                   u8 tag) {
    if (index < 0)
        return;
    if (index >= static_cast<int>(entries_.size()))
        entries_.resize(static_cast<std::size_t>(index) + 1);
    entries_[static_cast<std::size_t>(index)] = Entry{text, name, tag};
}

const char* TextDb::Text(int id) const {
    if (id < 0 || id >= Count())
        return nullptr;
    return entries_[id].text.c_str();
}

u8 TextDb::Tag(int id) const {
    if (id < 0 || id >= Count())
        return kTagPlain;
    return entries_[id].tag;
}

const char* TextDb::Name(int id) const {
    if (id < 0 || id >= Count())
        return "";
    return entries_[id].name.c_str();
}

// VIBE_Text_FindTextArrayIndex @0x44e0d8:
//   if (!dword_8C36B0[0]) return -1;
//   i = 0;
//   while (StrCmpNoCase(name, byte_8D36B0[80*i])) {
//       ++i;
//       if (i >= 0x4000 || !dword_8C36B0[i]) return -1;
//   }
//   return i;
int TextDb::FindIndex(const char* name) const {
    if (Count() == 0)
        return -1;
    int i = 0;
    while (StrCmpNoCase(name, entries_[i].name.c_str()) != 0) {
        ++i;
        if (i >= kMaxEntries || i >= Count())
            return -1;
    }
    return i;
}

// VIBE_Text_ParseRandomTextToken @0x44b8a0:
//   if (!StrncmpN(token, "{r", 2)) {
//       v2 = token[2] - '1';
//       if (v2 > 9) ReportMessage("txt_ParseRandomText(): Syntax error...");
//       else tag = v2 + 9;
//   }
//   byte_767EB0[dword_62EB24] = tag;   // stamp the current cursor slot
//   return dword_62EB24;
//
// The original writes the tag onto the slot at the append cursor (the entry
// being built). In this model the cursor is the last appended entry.
int TextDb::ParseRandomTextToken(const char* token, bool* ok) {
    bool valid = false;
    u8 tag = kTagNone;
    if (token[0] == '{' && token[1] == 'r') {
        unsigned v2 = static_cast<unsigned char>(token[2]) - '1';
        if (v2 <= 9) {
            tag = static_cast<u8>(v2 + kRandomTagBase);
            valid = true;
        }
        // else: syntax error logged in the original; tag left unchanged (0).
    }
    int cursor = Count() - 1;
    if (cursor >= 0)
        entries_[cursor].tag = tag;
    if (ok)
        *ok = valid;
    return cursor < 0 ? 0 : cursor;
}

// RenderRichString random path:
//   v211 = dword_8C36B0[baseIndex + (u16)VIBE_Math_RandomModulo(variants)]
// Advances the CRT RNG (seed it via crt::Srand for determinism) and returns the
// chosen variant string. Returns nullptr if the picked index is out of range.
const char* TextDb::PickRandomVariant(int baseIndex, int variants) const {
    int offset = static_cast<unsigned short>(RandomModulo(static_cast<unsigned short>(variants)));
    return Text(baseIndex + offset);
}

} // namespace guild::gui::text
