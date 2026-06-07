#include "config/cmdline.h"

#include <cctype>
#include <cstdlib>

namespace guild::config {

namespace {

// gilde.exe byte_1464E01 — the runtime ctype table the cmdline tokenizer
// consults via `(table[c] & 4)` to detect DBCS lead bytes. In the default
// (C / non-DBCS) locale shipped in the image, bit 2 (0x04) is never set, so
// this predicate is always false here. Reproduced as a function so the parse
// logic stays 1:1 with the original; swapping in a real DBCS table would make
// it active. (Recovered bytes: only the 0x10/0x20 print/space bits are set.)
inline bool IsLeadByte(unsigned char) {
    return false; // table[c] & 4 == 0 for every c in the recovered C-locale table
}

} // namespace

// gilde.exe 0x14263cb — VIBE_CmdLine_Parse
// Faithful translation of the MSVC CRT parse_cmdline. Pointers a4 (argc) and a5
// (char count) of the original are folded into the returned argv vector. The
// two-pass scheme (count, then fill) collapses into one pass here.
std::vector<std::string> ParseCmdLine(const std::string& cmdline) {
    std::vector<std::string> argv;
    const unsigned char* p = reinterpret_cast<const unsigned char*>(cmdline.c_str());

    // --- argv[0]: the program name -----------------------------------------
    std::string arg0;
    if (*p == '"') {
        // Quoted: copy until the matching quote (lead bytes copied as pairs).
        ++p;
        while (*p && *p != '"') {
            if (IsLeadByte(*p))
                arg0.push_back(static_cast<char>(*p++));
            arg0.push_back(static_cast<char>(*p));
            ++p;
        }
        if (*p == '"')
            ++p;
    } else {
        // Unquoted: copy until a space/tab/NUL.
        while (*p) {
            unsigned char c = *p;
            if (IsLeadByte(c)) {
                arg0.push_back(static_cast<char>(c));
                ++p;
                arg0.push_back(static_cast<char>(*p));
                ++p;
                continue;
            }
            if (c == ' ' || c == '\t')
                break;
            arg0.push_back(static_cast<char>(c));
            ++p;
        }
    }
    argv.push_back(arg0);

    // --- remaining arguments ----------------------------------------------
    bool inQuotes = false;
    while (*p) {
        // Skip inter-argument whitespace.
        while (*p == ' ' || *p == '\t')
            ++p;
        if (!*p)
            break;

        std::string cur;
        for (;;) {
            // Count a run of backslashes.
            unsigned slashes = 0;
            while (*p == '\\') {
                ++p;
                ++slashes;
            }
            bool emitChar = true;
            if (*p == '"') {
                if ((slashes & 1) == 0) {
                    // Even backslashes: the quote is a delimiter, unless it is a
                    // doubled "" inside a quoted region (which emits one ").
                    if (inQuotes && p[1] == '"') {
                        ++p; // consume one of the doubled quotes, emit the other
                    } else {
                        emitChar = false; // quote toggles state, not emitted
                    }
                    inQuotes = !inQuotes;
                }
                slashes >>= 1; // each pair of backslashes -> one literal '\'
            }
            // Emit slashes/2 literal backslashes.
            for (unsigned i = 0; i < slashes; ++i)
                cur.push_back('\\');

            unsigned char c = *p;
            if (!c || (!inQuotes && (c == ' ' || c == '\t')))
                break;
            if (emitChar) {
                if (IsLeadByte(c)) {
                    cur.push_back(static_cast<char>(c));
                    ++p;
                }
                cur.push_back(static_cast<char>(*p));
            }
            ++p;
        }
        argv.push_back(cur);
    }

    return argv;
}

// gilde.exe 0x534bbc (option-scan) — find `prefix` (e.g. "STADT=\""), then copy
// the text up to the next double-quote.
//
//   match = strstr(cmdline, prefix);   // prefix ends with the opening quote
//   start = match + strlen(prefix);    // first char of the value
//   scan for the closing '"' starting at `start`
//   copy [start, closing) into out
bool ExtractQuotedOption(const std::string& cmdline, const std::string& prefix,
                         std::string* out) {
    std::size_t at = cmdline.find(prefix);
    if (at == std::string::npos)
        return false;
    std::size_t start = at + prefix.size(); // points just past the opening quote
    // Scan for the closing quote (DBCS-aware skip mirrors the original loop).
    std::size_t i = start;
    std::size_t close = std::string::npos;
    while (i < cmdline.size()) {
        unsigned char c = static_cast<unsigned char>(cmdline[i]);
        if (c == '"') {
            close = i;
            break;
        }
        if (IsLeadByte(c)) {
            // Lead byte: skip the trail byte too; a trail of '"' still closes.
            if (i + 1 < cmdline.size() && cmdline[i + 1] == '"') {
                close = i + 1;
                break;
            }
            i += 2;
            continue;
        }
        ++i;
    }
    if (close == std::string::npos)
        return false; // no closing quote -> treated as not present
    *out = cmdline.substr(start, close - start);
    return true;
}

// gilde.exe 0x534bbc — STADT/BERUF/IP/PORT extraction.
LaunchOptions ParseLaunchOptions(const std::string& cmdline) {
    // The original uppercases the whole command line before scanning.
    std::string up = cmdline;
    for (char& c : up)
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));

    LaunchOptions opt;
    std::string v;
    if (ExtractQuotedOption(up, "STADT=\"", &v)) {
        opt.hasStadt = true;
        opt.stadt = v;
    }
    if (ExtractQuotedOption(up, "BERUF=\"", &v)) {
        opt.hasBeruf = true;
        opt.beruf = v;
    }
    if (ExtractQuotedOption(up, "IP=\"", &v)) {
        opt.hasIp = true;
        opt.ip = v;
    }
    if (ExtractQuotedOption(up, "PORT=\"", &v)) {
        opt.hasPort = true;
        // gilde.exe: dword_122F490 = VIBE_Util_ParseInt(buf)  (atoi-style).
        opt.port = std::atoi(v.c_str());
    }
    return opt;
}

} // namespace guild::config
