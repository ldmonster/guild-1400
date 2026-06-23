// gilde.exe — guild::io  (MODULE: Win32 .ini profile parser)
//
// 1:1 reimplementation of the documented kernel32 private-profile semantics.
// See ini_profile.h for the provenance / caller list. No third-party INI lib
// (user-approved hand-roll); file bytes flow through shim::IFileSystem.

#include "ini_profile.h"

#include <cstring>

namespace guild::io {

namespace {

// ASCII case-insensitive equality (kernel32 compares section/key names without
// regard to case; it is the OS-codepage ASCII fold, which for the keys/sections
// the game uses is plain A-Z<->a-z).
inline char asciiLower(char c) {
    if (c >= 'A' && c <= 'Z') return static_cast<char>(c - 'A' + 'a');
    return c;
}

bool ieq(const char* a, const char* b) {
    if (a == b) return true;
    if (!a || !b) return false;
    for (;; ++a, ++b) {
        char ca = asciiLower(*a);
        char cb = asciiLower(*b);
        if (ca != cb) return false;
        if (ca == '\0') return true;
    }
}

bool ieq(const std::string& a, const char* b) {
    return ieq(a.c_str(), b);
}

inline bool isWs(char c) {
    // kernel32 (Wine/ReactOS PROFILE_trim_spaces -> PROFILE_isspaceW) treats as
    // whitespace: 0x09..0x0d (TAB,LF,VT,FF,CR), 0x1a (SUB), and 0x20 (space).
    // We trim with this exact set around keys/section names/values.
    unsigned char u = static_cast<unsigned char>(c);
    return (u >= 0x09 && u <= 0x0d) || u == 0x1a || u == 0x20;
}

// Trim leading/trailing spaces+tabs from [first,last). Returns the trimmed view
// as a std::string.
std::string trimSpan(const char* first, const char* last) {
    while (first < last && isWs(*first)) ++first;
    while (last > first && isWs(last[-1])) --last;
    return std::string(first, last);
}

// kernel32 PROFILE_CopyEntry: a single matching pair of surrounding quotes is
// removed. The quote char may be either a single quote (') OR a double quote
// (") — whichever the first char is — and is stripped only when:
//   - the first char is ' or "
//   - there is at least one more char after it (value[1] is non-NUL)
//   - the LAST char equals that same quote char
// (Wine: if (*value=='\'' || *value=='\"') if (value[1] && value[len-1]==*value)).
void stripSurroundingQuotes(std::string& v) {
    if (v.size() >= 2 && (v.front() == '"' || v.front() == '\'') &&
        v.back() == v.front()) {
        v = v.substr(1, v.size() - 2);
    }
}

// Copy `src` into a NUL-terminated `out` of capacity `nSize`, MSDN-style:
// copies at most nSize-1 chars then a NUL; returns chars copied (excl. NUL).
// If nSize==0 or out==NULL, writes nothing and returns 0.
guild::u32 copyOut(const std::string& src, char* out, guild::u32 nSize) {
    if (!out || nSize == 0) return 0;
    guild::u32 n = static_cast<guild::u32>(src.size());
    if (n > nSize - 1) n = nSize - 1;
    std::memcpy(out, src.data(), n);
    out[n] = '\0';
    return n;
}

} // namespace

// ---------------------------------------------------------------------------
// Parsing core
// ---------------------------------------------------------------------------
void IniProfile::parse(const char* data, std::size_t len) {
    sections_.clear();
    if (!data) return;

    // All keys before the first [section] live in an implicit unnamed section.
    sections_.push_back(IniSection{std::string(), {}});
    IniSection* cur = &sections_.back();

    std::size_t i = 0;
    while (i < len) {
        // Find end of this physical line (handle \n, \r\n, lone \r).
        std::size_t lineStart = i;
        while (i < len && data[i] != '\n' && data[i] != '\r') ++i;
        const char* lb = data + lineStart;
        const char* le = data + i;
        // Advance past the line terminator (\r, \n, or \r\n).
        if (i < len && data[i] == '\r') {
            ++i;
            if (i < len && data[i] == '\n') ++i;
        } else if (i < len && data[i] == '\n') {
            ++i;
        }

        // Skip leading whitespace on the line.
        const char* p = lb;
        while (p < le && isWs(*p)) ++p;
        if (p == le) continue;     // blank line
        // kernel32 IS_ENTRY_COMMENT: ONLY ';' starts a comment. '#' is NOT a
        // comment char in kernel32 — a line beginning with '#' is parsed as a
        // normal (key=value) line (and ignored only if it has no '='). The real
        // game .ini files use only ';' for comments (see Gilde.INI [Network]).
        if (*p == ';') continue;   // comment line

        if (*p == '[') {
            // Section header: text up to the matching ']' (kernel32 takes up to
            // the first ']'). Trailing content after ']' is ignored.
            const char* nameStart = p + 1;
            const char* q = nameStart;
            while (q < le && *q != ']') ++q;
            std::string name = trimSpan(nameStart, q);
            sections_.push_back(IniSection{name, {}});
            cur = &sections_.back();
            continue;
        }

        // key = value. kernel32 splits on the first '='. A line with no '=' is
        // ignored.
        const char* eq = p;
        while (eq < le && *eq != '=') ++eq;
        if (eq == le) continue;  // no '=' -> not a key line

        std::string key = trimSpan(p, eq);
        if (key.empty()) continue;  // "= value" with empty key: ignore
        std::string value = trimSpan(eq + 1, le);
        stripSurroundingQuotes(value);

        cur->keys.push_back(IniKeyValue{std::move(key), std::move(value)});
    }
}

const IniSection* IniProfile::findSection(const char* name) const {
    for (const auto& s : sections_) {
        if (ieq(s.name, name)) return &s;
    }
    return nullptr;
}

IniSection* IniProfile::findSectionMut(const char* name) {
    for (auto& s : sections_) {
        if (ieq(s.name, name)) return &s;
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// GetPrivateProfileStringA semantics
// ---------------------------------------------------------------------------
guild::u32 IniProfile::getString(const char* section, const char* key,
                                 const char* defValue, char* out,
                                 guild::u32 nSize) const {
    // Enumeration forms.
    if (section == nullptr) return getSectionNames(out, nSize);
    if (key == nullptr) return getSectionKeyNames(section, out, nSize);

    const std::string def = defValue ? std::string(defValue) : std::string();

    const IniSection* sec = findSection(section);
    if (sec) {
        for (const auto& kv : sec->keys) {
            if (ieq(kv.key, key)) {
                return copyOut(kv.value, out, nSize);
            }
        }
    }
    // Not found -> copy default (MSDN). The default itself is NOT quote-stripped
    // and IS truncated to fit, then returned by length.
    return copyOut(def, out, nSize);
}

guild::u32 IniProfile::getSectionKeyNames(const char* section, char* out,
                                          guild::u32 nSize) const {
    if (!out || nSize == 0) return 0;
    if (nSize == 1) { out[0] = '\0'; return 0; }

    const IniSection* sec = findSection(section);
    guild::u32 pos = 0;  // write cursor; we always leave room for the final NUL.
    if (sec) {
        for (const auto& kv : sec->keys) {
            // Each entry: key chars + NUL. Stop if we cannot fit at least the
            // NUL for this entry plus the final terminating NUL.
            for (std::size_t c = 0; c < kv.key.size(); ++c) {
                if (pos + 1 >= nSize) goto done;  // keep 1 byte for final NUL
                out[pos++] = kv.key[c];
            }
            if (pos + 1 >= nSize) goto done;
            out[pos++] = '\0';
        }
    }
done:
    out[pos] = '\0';  // final terminating NUL (double-NUL overall)
    return pos;
}

guild::u32 IniProfile::getSectionNames(char* out, guild::u32 nSize) const {
    if (!out || nSize == 0) return 0;
    if (nSize == 1) { out[0] = '\0'; return 0; }

    guild::u32 pos = 0;
    for (const auto& s : sections_) {
        // The implicit unnamed pre-section (name == "") is not enumerable as a
        // section name (kernel32 only reports real [section] headers).
        if (s.name.empty()) continue;
        for (std::size_t c = 0; c < s.name.size(); ++c) {
            if (pos + 1 >= nSize) goto done;
            out[pos++] = s.name[c];
        }
        if (pos + 1 >= nSize) goto done;
        out[pos++] = '\0';
    }
done:
    out[pos] = '\0';
    return pos;
}

// ---------------------------------------------------------------------------
// GetPrivateProfileIntA semantics
// ---------------------------------------------------------------------------
// kernel32 RtlUnicodeStringToInteger with base==0, applied to up to
// `len` chars at `s`. Skips leading control chars (<= ' '), an optional single
// '+'/'-' sign, then auto-detects a 0b/0o/0x prefix (binary/octal/hex), else
// base 10. Accumulates digits (0-9, A-Z, a-z) in a u32 (mod 2^32, no overflow
// check), stopping at the first char whose value is invalid or >= base. The
// minus sign negates the u32 (two's-complement wrap). Returns the raw 32-bit
// pattern as u32. This is the exact algorithm kernel32 uses for GetPrivateProfileInt.
static guild::u32 rtlStrToIntBase0(const char* s, std::size_t len) {
    std::size_t i = 0;
    while (i < len && static_cast<unsigned char>(s[i]) <= ' ') ++i;
    bool minus = false;
    if (i < len) {
        if (s[i] == '+') { ++i; }
        else if (s[i] == '-') { minus = true; ++i; }
    }
    guild::u32 base = 10;
    if (i + 1 < len && s[i] == '0') {
        char c1 = s[i + 1];
        if (c1 == 'b') { i += 2; base = 2; }
        else if (c1 == 'o') { i += 2; base = 8; }
        else if (c1 == 'x') { i += 2; base = 16; }
    }
    guild::u32 total = 0;
    for (; i < len; ++i) {
        char c = s[i];
        guild::u32 digit;
        if (c >= '0' && c <= '9') digit = static_cast<guild::u32>(c - '0');
        else if (c >= 'A' && c <= 'Z') digit = static_cast<guild::u32>(c - 'A' + 10);
        else if (c >= 'a' && c <= 'z') digit = static_cast<guild::u32>(c - 'a' + 10);
        else break;
        if (digit >= base) break;
        total = total * base + digit;
    }
    return minus ? static_cast<guild::u32>(0u - total) : total;
}

int IniProfile::getInt(const char* section, const char* key,
                       int defValue) const {
    // kernel32 GetPrivateProfileIntW: fetch the value via GetPrivateProfileString
    // into a 30-WCHAR buffer (default ""). If GetPrivateProfileString returns 0
    // (key/section absent OR value empty) -> return def_val. Otherwise convert
    // with RtlUnicodeStringToInteger(base 0). A present, non-empty, non-numeric
    // value therefore yields 0 (NOT the default).
    char buffer[30];
    guild::u32 n = getString(section, key, "", buffer, 30u);
    if (n == 0) return defValue;        // absent or empty value
    if (buffer[0] == '\0') return defValue;  // (n==0 already covers this; kept 1:1)
    return static_cast<int>(rtlStrToIntBase0(buffer, n));
}

// ---------------------------------------------------------------------------
// WritePrivateProfileStringA semantics (in-memory mutation)
// ---------------------------------------------------------------------------
bool IniProfile::setString(const char* section, const char* key,
                           const char* value) {
    if (!section) return false;

    // section!=NULL, key==NULL -> delete the whole section.
    if (!key) {
        for (auto it = sections_.begin(); it != sections_.end(); ++it) {
            if (ieq(it->name, section)) { sections_.erase(it); break; }
        }
        return true;
    }

    IniSection* sec = findSectionMut(section);

    // key!=NULL, value==NULL -> delete the key.
    if (!value) {
        if (sec) {
            for (auto it = sec->keys.begin(); it != sec->keys.end(); ++it) {
                if (ieq(it->key, key)) { sec->keys.erase(it); break; }
            }
        }
        return true;
    }

    // Set / insert. Create the section if needed.
    if (!sec) {
        sections_.push_back(IniSection{std::string(section), {}});
        sec = &sections_.back();
    }
    for (auto& kv : sec->keys) {
        if (ieq(kv.key, key)) { kv.value = value; return true; }
    }
    sec->keys.push_back(IniKeyValue{std::string(key), std::string(value)});
    return true;
}

std::string IniProfile::serialize() const {
    std::string out;
    for (const auto& s : sections_) {
        if (s.name.empty() && s.keys.empty()) continue;
        if (!s.name.empty()) {
            out += '[';
            out += s.name;
            out += "]\r\n";
        }
        for (const auto& kv : s.keys) {
            out += kv.key;
            out += '=';
            out += kv.value;
            out += "\r\n";
        }
    }
    return out;
}

// ---------------------------------------------------------------------------
// Win32-shaped entry points
// ---------------------------------------------------------------------------
namespace {

// Read an entire file through the shim into a std::string. Returns false if the
// file is absent / unreadable (-> caller falls back to defaults, like kernel32
// when the .ini does not exist).
bool readWholeFile(guild::shim::IFileSystem* fs, const char* path,
                   std::string& out) {
    out.clear();
    if (!fs || !path) return false;
    guild::shim::IFile* f = fs->open(path, "rb");
    if (!f) return false;
    std::int64_t sz = f->size();
    if (sz > 0) {
        out.resize(static_cast<std::size_t>(sz));
        std::size_t got = f->read(&out[0], static_cast<std::size_t>(sz));
        out.resize(got);
    }
    fs->close(f);
    return true;
}

} // namespace

guild::u32 GetPrivateProfileStringA(guild::shim::IFileSystem* fs,
                                    const char* section, const char* key,
                                    const char* defValue, char* out,
                                    guild::u32 nSize, const char* fileName) {
    IniProfile ini;
    std::string text;
    if (readWholeFile(fs, fileName, text)) {
        ini.parse(text);
    }
    // If the file was absent, `ini` is empty -> lookups fall through to default
    // (string form) / produce empty enumerations, matching kernel32.
    return ini.getString(section, key, defValue, out, nSize);
}

int GetPrivateProfileIntA(guild::shim::IFileSystem* fs, const char* section,
                          const char* key, int defValue, const char* fileName) {
    IniProfile ini;
    std::string text;
    if (readWholeFile(fs, fileName, text)) {
        ini.parse(text);
    }
    return ini.getInt(section, key, defValue);
}

bool WritePrivateProfileStringA(guild::shim::IFileSystem* fs,
                                const char* section, const char* key,
                                const char* value, const char* fileName) {
    if (!fs || !fileName) return false;
    IniProfile ini;
    std::string text;
    if (readWholeFile(fs, fileName, text)) {
        ini.parse(text);
    }
    ini.setString(section, key, value);
    std::string outText = ini.serialize();

    guild::shim::IFile* f = fs->open(fileName, "wb");
    if (!f) return false;
    bool ok = true;
    if (!outText.empty()) {
        std::size_t wrote = f->write(outText.data(), outText.size());
        ok = (wrote == outText.size());
    }
    fs->close(f);
    return ok;
}

} // namespace guild::io
