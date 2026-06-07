#include "gui/text/markup.h"

#include "crt/printf.h"

#include <cstring>

namespace guild::gui::text {

namespace {

// Read the optional leading decimal digit after the introducer.
//   if (isdigit(*p)) { arg = *p-'0'; ++p; } else arg = -1;
int ReadDigit(const char*& p) {
    unsigned char ch = static_cast<unsigned char>(*p);
    if (ch >= '0' && ch <= '9') {
        ++p;
        return ch - '0';
    }
    return -1;
}

// Classify the letter that follows the '$' introducer into a MarkupKind. Mirrors
// the byte-exact `v15` switch in VIBE_Window_ParseMarkupAndBuild @0x416720.
MarkupKind ClassifyDollar(unsigned char v15) {
    switch (v15) {
        case 0x3C: return MarkupKind::BoundRight;    // '<'
        case 0x3D: return MarkupKind::BoundLeft;     // '='
        case 0x41: return MarkupKind::LineFeed;      // 'A'
        case 0x42: return MarkupKind::ColumnCenter;  // 'B'  (v208=128)
        case 0x43: return MarkupKind::Clear;         // 'C'
        case 0x46: return MarkupKind::FontColor;     // 'F'
        case 0x4C: return MarkupKind::ColumnReset;   // 'L'  (v208=0)
        case 0x4D: return MarkupKind::Embed;         // 'M'  ($M)
        case 0x52: return MarkupKind::ColumnRight;   // 'R'  (v208=64)
        case 0x54: return MarkupKind::Tab;           // 'T'  ($T)
        case 0x59: return MarkupKind::ColumnFull;    // 'Y'  (v208=256)
        case 0x5B: return MarkupKind::BracketOpen;   // '['  ($[)
        case 0x5D: return MarkupKind::BracketClose;  // ']'  ($])
        case 0x69: return MarkupKind::Inline;        // 'i'  ($i)
        case 0x74: return MarkupKind::EditField;     // 't'  ($t)
        default:   return MarkupKind::Unknown;       // -> "Unknown textparameter"
    }
}

// Scan an inline-button label "[...]": the original walks from after '[' to the
// matching ']'; if none is found (NUL first) it logs aMissing_0 ("Missing ']'").
// Returns pointer to ']' or nullptr. Mirrors the v40/v73 scan loops.
const char* FindBracketEnd(const char* p) {
    while (*p != ']') {
        if (!*p)
            return nullptr;
        ++p;
    }
    return p;
}

} // namespace

// gilde.exe 0x416720 — VIBE_Window_ParseMarkupAndBuild (tokenizer half).
std::vector<MarkupToken> TokenizeMarkup(const char* str, std::string* error) {
    std::vector<MarkupToken> toks;
    if (error)
        error->clear();

    const char* p = str;
    std::string run;  // accumulating literal text

    auto flushRun = [&]() {
        if (!run.empty()) {
            MarkupToken t;
            t.kind = MarkupKind::Text;
            t.text = run;
            toks.push_back(t);
            run.clear();
        }
    };

    while (*p) {
        char c = *p;

        if (c == '$') {
            flushRun();
            const char* q = p + 1;
            int arg = ReadDigit(q);
            unsigned char letter = static_cast<unsigned char>(*q);
            MarkupKind kind = ClassifyDollar(letter);

            MarkupToken t;
            t.kind = kind;
            t.arg = arg;
            t.letter = static_cast<char>(letter);

            if (kind == MarkupKind::Unknown) {
                // The original logs the unknown markup parameter.
                if (error && error->empty()) {
                    char buf[64];
                    guild::crt::Sprintf(buf, "Unknown textparameter: %%%c", letter);
                    *error = buf;
                }
                t.text.assign(2, '\0');
                t.text[0] = '$';
                t.text[1] = static_cast<char>(letter);
                toks.push_back(t);
                p = q + 1;
                continue;
            }

            // $i / $t inline widgets may carry a selector letter and a "[label]".
            if (kind == MarkupKind::Inline || kind == MarkupKind::EditField) {
                const char* r = q + 1;
                unsigned char sel = static_cast<unsigned char>(*r);
                if (sel == 'a' || sel == 'n' || sel == 'i' || sel == 'b' ||
                    sel == 'c' || sel == 's' || sel == 't') {
                    t.letter = static_cast<char>(sel);
                    ++r;
                }
                if (*r == '[') {
                    const char* end = FindBracketEnd(r + 1);
                    if (!end) {
                        if (error && error->empty())
                            *error = "Missing ']'";  // aMissing_0
                        t.text.assign(r + 1);         // label runs to end of string
                        toks.push_back(t);
                        break;
                    }
                    t.text.assign(r + 1, end);
                    r = end + 1;
                }
                toks.push_back(t);
                p = r;
                continue;
            }

            // $FF font-color absorbs the trailing 'F'.
            if (kind == MarkupKind::FontColor && static_cast<unsigned char>(*(q + 1)) == 'F') {
                ++q;
            }

            t.text.assign(1, '$');
            t.text.push_back(static_cast<char>(letter));
            toks.push_back(t);
            p = q + 1;
            continue;
        }

        if (c == '%') {
            flushRun();
            const char* q = p + 1;
            int arg = ReadDigit(q);
            unsigned char letter = static_cast<unsigned char>(*q);

            MarkupToken t;
            t.kind = MarkupKind::PercentCode;
            t.arg = arg;
            t.letter = static_cast<char>(letter);
            t.text.assign(1, '%');
            t.text.push_back(static_cast<char>(letter));
            toks.push_back(t);
            p = q + 1;
            continue;
        }

        run.push_back(c);
        ++p;
    }
    flushRun();
    return toks;
}

} // namespace guild::gui::text
