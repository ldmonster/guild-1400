#pragma once
// gilde.exe command-line handling.
//
// Two distinct pieces, both reconstructed here:
//
//  1. ParseCmdLine — the MSVC CRT command-line tokenizer
//     (VIBE_CmdLine_Parse @0x14263cb / VIBE_CmdLine_BuildArgv @0x1426332):
//     splits a raw command line into argv[], honoring "quoted strings",
//     backslash-escaped quotes (\" and 2N backslashes), and DBCS lead bytes.
//
//  2. The getopt-style option extraction the game does at startup
//     (VIBE_GameLogic_MainEntryAndShutdown @0x534bbc): the launcher passes
//        STADT="..."  BERUF="..."  IP="..."  PORT="..."
//     The original strstr()s each prefix, then copies the text between the
//     opening and closing double-quote into the matching field.
#include "guild/common/types.h"
#include <string>
#include <vector>

namespace guild::config {

// gilde.exe 0x14263cb — VIBE_CmdLine_Parse (MSVC parse_cmdline).
// Tokenizes `cmdline` into argv. argv[0] is the program path: the first token,
// taken verbatim up to the first whitespace (or, if it starts with '"', up to
// the matching '"'). Subsequent tokens follow the MSVC quoting rules.
std::vector<std::string> ParseCmdLine(const std::string& cmdline);

// The four launcher options the game extracts at startup.
struct LaunchOptions {
    bool hasStadt = false;
    bool hasBeruf = false;
    bool hasIp = false;
    bool hasPort = false;
    std::string stadt; // STADT="..."  -> ::ReturnedString (city override)
    std::string beruf; // BERUF="..."  -> byte_63C7DC      (profession override)
    std::string ip;    // IP="..."     -> byte_122EE90     (host/ip override)
    int port = 0;      // PORT="..."   -> dword_122F490 (via VIBE_Util_ParseInt)
};

// gilde.exe 0x534bbc (option-scan section) — extract STADT/BERUF/IP/PORT from a
// raw command line. The command line is uppercased before scanning (the
// original calls VIBE_Util_StrToUpper(lpCmdLine) first), so the option names
// match case-insensitively. PORT's quoted text is parsed as a decimal int.
LaunchOptions ParseLaunchOptions(const std::string& cmdline);

// gilde.exe (option-scan helper) — for a prefix like `STADT="`, find the
// opening quote, scan to the matching closing quote, and return the enclosed
// text. Returns false if the prefix is absent or the closing quote is missing.
// Exposed for testing the quote-scan in isolation.
bool ExtractQuotedOption(const std::string& cmdline, const std::string& prefix,
                         std::string* out);

} // namespace guild::config
