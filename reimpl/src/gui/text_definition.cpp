// gilde.exe — localized-text .def driver + .dat per-label compiler.
// See text_definition.h for the full provenance and the recovered grammars.

#include "gui/text_definition.h"

#include "gui/text/text_strtok.h"   // StrtokWhitespace @0x5e9cd0 (REUSED)

#include <cstdio>
#include <cstring>

namespace guild::gui::text {

namespace {

// loc_5CB930 == strstr: first occurrence of `needle` in `hay`, or nullptr.
// (The original is a hand-rolled substring search; std::strstr is byte-identical
// in result, which is all the caller observes.)
const char* StrStr(const char* hay, const char* needle) {
    return std::strstr(hay, needle);
}

// VIBE_Util_StrChr @0x5d3ef0 — returns a pointer to the LAST occurrence of byte
// `c` in `s`, or nullptr. The original walks the whole string, overwriting its
// result every time it sees `c` (it is NOT std::strchr, which returns the first).
// Searching for '\0' returns a pointer to the terminator (the loop's `c==*s`
// check fires on the NUL when c==0), matching the original exactly.
char* StrChr(char* s, int c) {
    char* result = nullptr;
    char target = static_cast<char>(c);   // a2 @<dl> — compared as a byte
    do {
        if (*s == target)
            result = s;
    } while (*s++);
    return result;
}

// VIBE_String_SkipLeadingSpaces @0x44b268 — return a pointer to the first
// non-space (0x20) character, or nullptr if the string is all spaces / empty.
const char* SkipLeadingSpaces(const char* a1) {
    std::size_t len = std::strlen(a1);
    std::size_t v2 = 0;
    char v3 = 0;
    do {
        if (v2 >= len)
            break;
        v3 = a1[v2++];
    } while (v3 == ' ');
    if (v2 == len)
        return nullptr;
    return &a1[v2 - 1];
}

// VIBE_String_TrimTrailingSpaces @0x44b2ac — if the last char is a space, walk
// back to the last non-space and NUL-terminate after it (in place). Returns a
// pointer into the string (unused by our callers, kept for fidelity).
void TrimTrailingSpaces(char* a1) {
    int v2 = static_cast<int>(std::strlen(a1)) - 1;
    if (v2 < 0)
        return;
    char* result = &a1[v2];
    if (*result == ' ') {
        while (v2 > 0) {
            if (*result != ' ') {
                *(result + 1) = 0;
                return;
            }
            --result;
            --v2;
        }
    }
}

// VIBE_Util_StrCmpNoCase @0x5cb8f0 — ASCII case-insensitive strcmp (only A-Z is
// folded, exactly as the original). Returns folded(*a) - folded(*b) at the first
// difference / terminator.
int StrCmpNoCase(const char* a, const char* b) {
    const unsigned char* p = reinterpret_cast<const unsigned char*>(a);
    const unsigned char* q = reinterpret_cast<const unsigned char*>(b);
    for (;;) {
        unsigned char v3 = *p;
        unsigned char v4 = *q;
        if (v3 >= 0x41 && v3 <= 0x5A) v3 += 32;
        if (v4 >= 0x41 && v4 <= 0x5A) v4 += 32;
        if (v3 != v4 || !v4)
            return static_cast<int>(v3) - static_cast<int>(v4);
        ++p;
        ++q;
    }
}

// VIBE_Util_StrNCopyPad @0x5d9360 — copy up to `n` bytes from `src` into `dst`,
// stopping early at a NUL, then NUL-pad the remainder of the `n`-byte field.
// (Used to lift a sub-span out of the token between bracket markers.)
void StrNCopyPad(char* dst, const char* src, int n) {
    int i = 0;
    while (n && *src) {
        dst[i++] = *src++;
        --n;
    }
    while (n) {
        dst[i++] = 0;
        --n;
    }
}

} // namespace

// ---------------------------------------------------------------------------
// VIBE_Text_LoadDefinitionFile @0x44b8f4 — the .def parse half.
// ---------------------------------------------------------------------------
std::vector<std::string> ParseDefinitionIncludes(const char* text, std::size_t len) {
    std::vector<std::string> out;
    if (!text)
        return out;

    // Walk the buffer line by line. VIBE_Text_ReadLine reads up to a '\n' (and
    // leaves the trailing '\n' in the buffer); the shipped file is CRLF, so a
    // line may carry a trailing '\r'. The original's strstr / quote scans don't
    // care about the '\r' because it never appears inside a quoted name.
    std::size_t i = 0;
    while (i < len) {
        // Extract one line [i, j) up to and excluding '\n'.
        std::size_t j = i;
        while (j < len && text[j] != '\n')
            ++j;
        std::string line(text + i, j - i);
        i = (j < len) ? j + 1 : j;

        const char* l = line.c_str();

        // A line that CONTAINS "//" is a comment -> skip. (strstr(line,"//"))
        if (StrStr(l, "//"))
            continue;

        // Must contain "#include" -> else skip (#outputpath/#headerfile/blank).
        const char* inc = StrStr(l, "#include");
        if (!inc)
            continue;

        // First '"' at or after the #include opens the name.
        const char* open = std::strchr(inc, '"');
        if (!open)
            continue;
        const char* nameStart = open + 1;
        // Next '"' closes it.
        const char* close = std::strchr(nameStart, '"');
        if (!close)
            continue;

        // Copy the quoted name and strip the extension. The original NUL-
        // terminates at the close quote, then VIBE_Util_StrChr(name, '.') returns
        // the LAST '.' (it scans the whole string), and writes a NUL there. So
        // the cut is at the LAST dot, not the first (rfind, not find).
        std::string name(nameStart, static_cast<std::size_t>(close - nameStart));
        char* dot = StrChr(&name[0], '.');
        if (dot)
            name.resize(static_cast<std::size_t>(dot - name.c_str()));

        out.push_back(name);
        if (static_cast<int>(out.size()) >= kDefMaxIncludes)
            break;
    }
    return out;
}

bool LoadDefinitionFile(const char* text, std::size_t len,
                        const std::function<bool(const std::string&)>& loadOne,
                        bool* loadedFlag) {
    std::vector<std::string> names = ParseDefinitionIncludes(text, len);

    // Load each in order; bail at the first failure (the original's load loop
    // breaks the moment VIBE_Text_LoadTextFile returns 0, returning 0).
    for (const std::string& n : names) {
        if (loadOne && !loadOne(n))
            return false;
    }

    if (loadedFlag)
        *loadedFlag = true;   // dword_B537B4 = 1
    return true;
}

// ---------------------------------------------------------------------------
// VIBE_Text_ParseLabelDefinition @0x44b2e0 — the per-line .dat compiler.
// ---------------------------------------------------------------------------
void LabelCompilerState::BeginFile(int fileIndex, const std::string& prefix) {
    if (static_cast<int>(fileBase.size()) <= fileIndex) {
        fileBase.resize(fileIndex + 1, 0);
        filePrefix.resize(fileIndex + 1);
    }
    fileBase[fileIndex]   = count;          // dword_76BEAC[fileIndex] = count
    filePrefix[fileIndex] = prefix;         // byte_A136B0[80*fileIndex]
}

bool ParseLabelDefinition(const char* line, int fileIndex,
                          LabelCompilerState& st, TextDb& db) {
    // Per-language singular / plural value slots (v60 / v59 in the original).
    // The original zeroes 640 bytes (== kLabelMaxLangs * kLabelLangStride) for
    // each; we model them as fixed-size arrays of strings, default-empty.
    std::string sing[kLabelMaxLangs];   // v60 — "%s%s%s" prefix+singular+suffix
    std::string plur[kLabelMaxLangs];   // v59 — "%s%s%s" prefix+plural+suffix

    u8   tag      = 0;     // v70 — gender selector tag byte (byte_767EB0[id])
    bool haveAny  = false; // whether any value token produced an entry string

    // Tokenize the line on ",\";\n" (comma, quote, semicolon, newline).
    StrtokContext ctx;
    // StrtokWhitespace mutates the buffer in place; copy the line so callers can
    // keep `line` const (the original tokenizes the caller's mutable line buffer).
    std::string buf(line ? line : "");
    char* tok = StrtokWhitespace(ctx, &buf[0], ",\";\n");

    int langIndex = 0;  // v69 — 0-based token counter (token 0 == gender)

    while (tok) {
        if (langIndex == 0) {
            // token[0]: gender selector — trim and match (m)/(w)/(s).
            char field[256];
            // copy tok into the scratch field (the original's char-pair copy).
            std::size_t k = 0;
            for (; tok[k] && k + 1 < sizeof(field); ++k)
                field[k] = tok[k];
            field[k] = 0;

            const char* s = SkipLeadingSpaces(field);
            char* cmp = field;
            if (s) {
                // TrimTrailingSpaces operates on the SkipLeadingSpaces result.
                cmp = const_cast<char*>(s);
            }
            TrimTrailingSpaces(cmp);

            if (StrCmpNoCase(cmp, "(m)") == 0)       tag = 0;
            else if (StrCmpNoCase(cmp, "(w)") == 0)  tag = 1;
            else if (StrCmpNoCase(cmp, "(s)") == 0)  tag = 2;
            else                                     tag = 0;
        } else {
            // token[1..]: a value spec for language column (langIndex-1).
            int col = langIndex - 1;
            if (col >= kLabelMaxLangs) {
                // beyond the 4-language capacity: the original would overrun; we
                // stop consuming columns (the shipped data never exceeds 4).
                tok = StrtokWhitespace(ctx, nullptr, ",\";\n");
                ++langIndex;
                continue;
            }

            const char* v37 = SkipLeadingSpaces(tok);
            if (v37) {
                char prefix[128]  = {0};   // v65 / var_A8
                char single[128]  = {0};   // v63 / var_1A8
                char plural[128]  = {0};   // v64 / var_128
                char suffix[128]  = {0};   // v62 / var_228 (after ']')

                // Trim trailing spaces on the trimmed token (in a mutable copy).
                std::string vt(v37);
                TrimTrailingSpaces(&vt[0]);
                const char* p = vt.c_str();

                // Scan for '[' (ecx in the original); 0 when absent.
                const char* lbrack = std::strchr(p, '[');
                bool haveBracket = (lbrack != nullptr);  // var_24 (v67) = 1 here

                if (haveBracket) {
                    // Prefix = text before '[' (only copied when '[' is not at the
                    // very start: original tests `[` != token).
                    if (lbrack != p)
                        StrNCopyPad(prefix, p, static_cast<int>(lbrack - p));

                    const char* core = lbrack + 1;             // var_20 = '['+1
                    const char* slash = std::strchr(lbrack, '/');  // scan from '['
                    if (slash) {
                        // [single/plural]
                        // single = core .. slash       (len = slash - '[' - 1)
                        StrNCopyPad(single, core, static_cast<int>(slash - lbrack - 1));
                        // rbrack scanned from slash
                        const char* rbrack = std::strchr(slash, ']');
                        if (!rbrack || !haveBracket)
                            return false;   // missing ']' -> malformed (return 0)
                        // plural = slash+1 .. rbrack    (len = rbrack - slash - 1)
                        StrNCopyPad(plural, slash + 1,
                                    static_cast<int>(rbrack - slash - 1));
                        // suffix = rbrack+1 .. end (only if non-empty)
                        const char* after = rbrack + 1;
                        if (std::strlen(after) + 1 != 1)
                            StrNCopyPad(suffix, after, static_cast<int>(std::strlen(after)));
                    } else {
                        // [..] with no '/'. rbrack scanned from '[' (ecx).
                        const char* rbrack = std::strchr(lbrack, ']');
                        if (!rbrack || !haveBracket)
                            return false;   // missing ']' -> malformed (return 0)
                        // single = core .. rbrack       (len = rbrack - '[' - 1)
                        StrNCopyPad(single, core, static_cast<int>(rbrack - lbrack - 1));
                        // plural = core+1 .. rbrack     (len = rbrack - core - 1)
                        //   (the original quirk: plural drops the first core char)
                        StrNCopyPad(plural, core + 1,
                                    static_cast<int>(rbrack - core - 1));
                        const char* after = rbrack + 1;
                        if (std::strlen(after) + 1 != 1)
                            StrNCopyPad(suffix, after, static_cast<int>(std::strlen(after)));
                    }
                } else {
                    // No '[': the whole (trimmed) token is the value -> goes into
                    // `prefix` (v65); single/plural/suffix stay empty.
                    StrNCopyPad(prefix, p, static_cast<int>(std::strlen(p)));
                }

                // Build the singular and plural forms: "%s%s%s".
                //   singular = prefix + single + suffix
                //   plural   = prefix + plural + suffix
                char one[512];
                char two[512];
                std::snprintf(one, sizeof(one), "%s%s%s", prefix, single, suffix);
                std::snprintf(two, sizeof(two), "%s%s%s", prefix, plural, suffix);
                sing[col] = one;
                plur[col] = two;
                haveAny = true;
            }
        }

        // advance to the next token (strtok(NULL, ...)), bumping the counter.
        tok = StrtokWhitespace(ctx, nullptr, ",\";\n");
        ++langIndex;
    }

    (void)haveAny;

    // Assemble the entry blob: each singular slot then each plural slot, each
    // with a trailing '|' (asc_61899C), concatenated in order:
    //   s0| s1| s2| s3| p0| p1| p2| p3|
    std::string blob;
    int blobLen = 0;
    for (int i = 0; i < kLabelMaxLangs; ++i) {
        std::string seg = sing[i];
        seg.push_back('|');
        blob += seg;
        blobLen += static_cast<int>(seg.size());   // dword_62EB30 += strlen(seg)
    }
    for (int i = 0; i < kLabelMaxLangs; ++i) {
        std::string seg = plur[i];
        seg.push_back('|');
        blob += seg;
        blobLen += static_cast<int>(seg.size());
    }

    // The per-file base and prefix (dword_76BEAC[fileIndex] / byte_A136B0).
    int base = 0;
    std::string prefixName;
    if (fileIndex >= 0 && fileIndex < static_cast<int>(st.fileBase.size())) {
        base       = st.fileBase[fileIndex];
        prefixName = st.filePrefix[fileIndex];
    }

    // Synthesize the key: "<filePrefix>+<localIndex>".
    int localIndex = st.count - base;
    char key[256];
    std::snprintf(key, sizeof(key), "%s+%i", prefixName.c_str(), localIndex);

    // Append the entry (string, key, tag) — byte_767EB0[id]=tag,
    // byte_8D36B0[80*id]=key, dword_8C36B0/blob = string. Advance the cursors.
    db.Add(blob, key, tag);
    st.blobLen += blobLen + 1;   // dword_62EB30 += (sum) + 1 (the ++ at 0x44b587)
    st.count   += 1;            // dword_62EB24 += 1
    return true;
}

} // namespace guild::gui::text
