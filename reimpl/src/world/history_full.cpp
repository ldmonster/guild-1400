#include "world/history_full.h"

#include <cstring>

// Faithful port of the chronicle text-parser cores (gilde.exe 0x4fd220 / 0x4fd44c
// / 0x4fe694) and the recovered substitution tables. The engine label/slot tables,
// He broadcast and .esc replacement dispatch are leaves; recovered here are the
// pure syntax rules + the byte-exact role/prefix tables.

namespace guild::world {

// gilde.exe dword_6343B8 (5-byte stride): "_NEW","_USE","_REL","_SET","_USE".
const char* const kHistoryPrefixTable[5] = { "_NEW", "_USE", "_REL", "_SET", "_USE" };

// gilde.exe aBuergermeister_3 @0x633FF8 (64-byte stride, 15 entries) — recovered
// byte-for-byte via get_bytes.
const char* const kHistoryRoleNames[15] = {
    "BUERGERMEISTER",        //  0
    "BISCHOF",               //  1
    "RND_GILDENMEISTER",     //  2
    "RND_AMTSTRAEGERIN",     //  3
    "RND_AMTSTRAEGER",       //  4
    "RND_AMTSPERSON",        //  5
    "REICHSTER_EINWOHNER",   //  6
    "BESTES_WIRTSHAUS",      //  7
    "GELD",                  //  8
    "STADTKASSE",            //  9
    "RND_KIRCHENBERUF",      // 10
    "RND_REICH",             // 11
    "RND_NPC_EINWOHNER",     // 12
    "RND_HANDELSHERR",       // 13
    "RND_SPIELER",           // 14
};

int HistoryRoleNameIndex(const char* token) {
    if (!token)
        return -1;
    // The original: while (memcmp(v3, name, strlen(name))) advance; index >= 15 -> not found.
    for (int i = 0; i < kHistoryRoleCount; ++i) {
        const char* name = kHistoryRoleNames[i];
        if (std::strncmp(token, name, std::strlen(name)) == 0)
            return i;
    }
    return -1;
}

// gilde.exe 0x4fd220 — VIBE_History_ParseTextFirstPass (syntax-validation core).
// Mirrors the [ / # / ] bracket-region grammar: regionStart ('[') begins a region;
// a '#' inside it sets the escape marker once; a ']' closes a started+marked region;
// trailing unbalanced state is a syntax error.
bool HistoryParseTextFirstPassValid(const char* label) {
    if (!label)
        return true;                 // null -> nothing to validate (matches early-out)

    bool regionStarted = false;      // v2  (the '[' position, non-null when open)
    bool markerSet     = false;      // v20 (the '#' marker, set once per region)
    bool stray         = false;      // v19 (residual error flag)

    for (const char* p = label; *p; ) {
        char c = *p;
        if (c == '[') {              // *v9 == 91 -> start region
            if (regionStarted)       // already in a region -> syntax error
                return false;
            regionStarted = true;
            ++p;
            continue;
        }
        if (c == '#') {              // v10 == 35 -> escape marker
            if (!regionStarted || markerSet)  // '#' outside a region or duplicated
                return false;
            markerSet = true;
            ++p;
            continue;
        }
        if (c == ']') {              // v10 == 93 -> close region
            if (!regionStarted || !markerSet) // ']' with no open/marked region
                return false;
            // The original collapses the marked span here; for validation we just
            // close the region and clear the markers.
            regionStarted = false;
            markerSet     = false;
            stray         = false;
            ++p;
            continue;
        }
        ++p;                         // ordinary char
    }
    // if ( v2 || v20 || v19 ) -> Syntax Error.
    return !(regionStarted || markerSet || stray);
}

// gilde.exe 0x4fd44c — prefix-mode classification.
HistoryTokenMode HistoryClassifyTokenMode(const char* token) {
    if (!token)
        return HistoryTokenMode::kLiteral;
    // The original compares the token's leading dword against prefix[0..2]; a match
    // at index i sets v16 = i. No match -> v16 stays 4 (literal role-name path).
    for (int i = 0; i < 3; ++i) {
        const char* pre = kHistoryPrefixTable[i];
        if (std::strncmp(token, pre, std::strlen(pre)) == 0)
            return static_cast<HistoryTokenMode>(i);
    }
    return HistoryTokenMode::kLiteral;
}

int HistoryTokenSlot(const char* token, HistoryTokenMode mode) {
    if (!token || mode == HistoryTokenMode::kLiteral)
        return -1;                   // literal tokens carry no slot reference
    // The slot digit follows the 4-char prefix: token[4] is the slot char in the
    // original (v12[0] = *(a1 + 5) then ParseInt). A single decimal digit 0..7.
    char d = token[4];
    if (d < '0' || d > '9')
        return -1;
    int slot = d - '0';
    return slot < 8 ? slot : -1;     // v13 < 8 gate
}

// gilde.exe 0x4fe694 — one forward-scan step classification.
HistoryScanCode HistoryScanStep(bool parseOk, i32 dayDiff, bool textEmpty) {
    if (!parseOk)
        return HistoryScanCode::kParseError;      // v5 == 0 -> result 4
    if (dayDiff == 1)                              // prior-day window
        return textEmpty ? HistoryScanCode::kEmitEmpty   // v6 = 6 when !*a2
                         : HistoryScanCode::kEmit;        // v6 = 1
    if (dayDiff <= 1)                              // on/after current day -> stop
        return HistoryScanCode::kStop;            // result 2
    return HistoryScanCode::kEndOfFile;           // older -> continue (caller loops)
}

} // namespace guild::world
