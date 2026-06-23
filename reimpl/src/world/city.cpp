#include "world/city.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>

namespace guild::world {

// ===========================================================================
// Global record arrays (modeled as real arrays; original bases in comments).
// ===========================================================================
CityRecord g_cities[kCityMaxCount];      // gilde.exe byte_13CD6A0
GoodSlot   g_goods[kGoodCategoryCount];  // gilde.exe 0x1234750
float      g_capDivisor    = 0.0f;       // gilde.exe flt_641DA8
float      g_cityTotalMoney = 0.0f;      // gilde.exe flt_641FD4
float      g_cityTotalGoods = 0.0f;      // gilde.exe flt_641FD8

// gilde.exe 0x5dc070 — VIBE_Util_ParseInt.
// The original tests a 256-entry char-class table byte_64A208 (index = ch+1):
//   bit 0x02 == "blank/whitespace", bit 0x20 == "decimal digit".
// We reproduce that classification with the C locale (the .ini files are ASCII).
i32 UtilParseInt(const char* s) {
    auto isBlank = [](unsigned char c) {
        return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
    };
    while (*s && isBlank(static_cast<unsigned char>(*s)))
        ++s;
    char sign = *s;
    if (*s == '+' || *s == '-')
        ++s;
    // Accumulate in u32: the original's `lea`/`imul`-based  v = (u8)*s + 10*v - 48
    // wraps in 2's complement on overflow (e.g. an over-long digit run). Doing the
    // arithmetic unsigned reproduces that bit-pattern EXACTLY while avoiding C++
    // signed-overflow UB; the final reinterpretation back to i32 is identical to
    // the binary's register value.
    std::uint32_t v = 0;
    while (*s >= '0' && *s <= '9') {
        // v3 = (u8)*s + 10*v;  v = v3 - 48;  (exact wrapping arithmetic)
        v = static_cast<unsigned char>(*s) + 10u * v;
        v -= 48u;
        ++s;
    }
    if (sign == '-')
        v = static_cast<std::uint32_t>(0) - v;   // x86 `neg`, wraps for INT_MIN
    return static_cast<i32>(v);
}

// gilde.exe 0x50704c — VIBE_City_ParseCsvFieldList.
// Faithful behaviour (verified 1:1 against the disasm, wave-16): the original
// scans the input by tokens. It runs a comma-scan BEFORE each parse:
//   * the pre-loop finds the end (a ',' or, on end-of-string, sets esi=0) of the
//     FIRST token;
//   * each iteration copies [start, end) into a 256-byte scratch (StrNCopyPad,
//     which stops at the source NUL and zero-pads), ParseInt's it, stores the
//     dword, then — if the "last field" flag v16 is already set — STOPS;
//   * otherwise it advances past the ',' and scans the NEXT token. If THAT scan
//     hits end-of-string, it restores `end` to the real end of that token and
//     sets v16=1, so the NEXT iteration parses that final NUL-terminated token
//     and then breaks.
// Net effect: the final token (terminated by NUL rather than ',') is parsed
// exactly once and the loop stops — there is NO extra trailing empty token. With
// fewer commas than `maxFields`, the returned count == (#commas + 1) at most, and
// the loop also stops when the written byte-offset reaches 4*maxFields.
//
// (Wave-16 fix: the previous reconstruction parsed one extra empty token, e.g.
// "10,20"/maxFields=4 -> count 3 with out[2]==0; the binary returns count 2.)
int CityParseCsvFieldList(const char* text, i32* out, int maxFields) {
    if (maxFields <= 0)
        return 0;

    // Scan a token starting at `p`: advance to the next ',' or the end. Mirrors
    // the binary's scan loops (the `sub esi,esi` end sentinel == returning null).
    // Returns the ',' position, or nullptr if end-of-string was reached.
    auto scan = [](const char* p) -> const char* {
        while (*p != ',') {
            if (*p == '\0')
                return nullptr;            // sub esi,esi  (end sentinel)
            char c = *++p;
            if (*p == ',')
                break;
            ++p;
            if (c == '\0')
                return nullptr;            // sub esi,esi
        }
        return p;
    };

    const char* start = text;              // ecx — token start (never lost)
    const char* end   = scan(text);        // esi — token end (',' or null)
    int count = 0;                         // ebp
    int byteOff = 0;                        // edi
    const int limit = 4 * maxFields;        // var_18
    // If the FIRST token has no comma the original's pre-scan yields esi==0 and
    // the iteration parses that single token, then `lea ecx,[esi+1]` walks into a
    // near-null pointer (only reachable with malformed comma-less input that the
    // city loader never passes). We treat a comma-less first token as the final
    // (and only) field — parse it once and stop — staying in-bounds. Real .ini
    // data and every default string here is comma-delimited, so this never
    // affects observable output.
    bool lastField = (end == nullptr);     // v16

    while (true) {
        // Copy [start, end) — StrNCopyPad stops at the source NUL, so an `end`
        // of nullptr (end-of-string) simply copies to the NUL and pads. We cap
        // at the 256-byte scratch exactly like the original's fixed buffer.
        char scratch[256];
        std::size_t n;
        if (end == nullptr) {
            n = std::strlen(start);
        } else {
            n = static_cast<std::size_t>(end - start);
        }
        if (n >= sizeof(scratch))
            n = sizeof(scratch) - 1;
        std::memcpy(scratch, start, n);
        scratch[n] = '\0';
        out[count++] = UtilParseInt(scratch);
        byteOff += 4;

        if (lastField)                      // if (v16) break;
            break;

        // Advance past the comma and scan the next token.
        start = end + 1;                    // lea ecx,[esi+1]
        end = scan(start);
        if (end == nullptr) {
            // End-of-string while scanning the next token: this token is the
            // final one. Restore `end` to its real end and flag last-field.
            end = start + std::strlen(start);
            lastField = true;
        }
        if (byteOff >= limit)               // wrote maxFields dwords
            return count;
    }
    return count;
}

// gilde.exe 0x577a9c — VIBE_City_InitParameterTable.
// Zeroes the accumulator/delta span and seeds the per-good drift weight (+0),
// contribution weight (+2) and cap (+4). The original leaves flt_641DA8 holding
// the incoming FP register value; we take it as an argument.
void CityInitParameterTable(float capDivisor) {
    g_capDivisor = capDivisor;

    // Per-good drift weight (word_1234750[2*g]).  index 15 == -1 -> 0xFFFF.
    static const u16 kDriftWeight[kGoodCategoryCount] = {
        0, 0, 0, 300, 100, 350, 800, 600, 600, 800, 65535, 1000, 1000, 1000,
        500, 1500, 200, 800, 250, 1500, 250, 250, 250, 1200, 1200, 1200, 1200, 0
    };
    // Per-good contribution weight (word_1234752[2*g]).
    static const u16 kContribWeight[kGoodCategoryCount] = {
        0, 100, 10, 5, 25, 10, 10, 20, 20, 20, 30, 30, 30, 30, 30, 15, 30, 5,
        30, 20, 30, 30, 20, 10, 10, 10, 10, 0
    };
    // Per-good cap (word_1234754[2*g]).
    static const u16 kCap[kGoodCategoryCount] = {
        0, 0, 0, 150, 500, 500, 500, 250, 400, 600, 0, 2000, 2000, 2000, 1000,
        0, 500, 1800, 250, 300, 250, 250, 400, 1000, 1000, 1000, 1000, 0
    };

    for (int g = 0; g < kGoodCategoryCount; ++g) {
        g_goods[g].driftWeight   = kDriftWeight[g];
        g_goods[g].contribWeight = kContribWeight[g];
        g_goods[g].cap           = kCap[g];
        g_goods[g].pad6          = 0;
        g_goods[g].accum         = 0.0f;  // flt_1234758 span zeroed by the loop
        g_goods[g].priceDelta    = 0.0f;  // flt_123475C
    }
}

// ===========================================================================
// Self-contained INI mapper (non-original convenience; mirrors the loader's
// field placement so a synthetic city can be built without the Win32 INI API).
// ===========================================================================
struct IniDocument {
    // section -> (key -> value)
    std::unordered_map<std::string, std::unordered_map<std::string, std::string>> sections;
};

IniDocument* IniParse(const char* text) {
    auto* doc = new IniDocument();
    std::string section;
    const char* p = text;
    std::string line;
    auto flushLine = [&]() {
        // trim leading whitespace
        size_t a = line.find_first_not_of(" \t\r\n");
        if (a == std::string::npos) { line.clear(); return; }
        std::string s = line.substr(a);
        // trim trailing
        size_t b = s.find_last_not_of(" \t\r\n");
        s = s.substr(0, b + 1);
        line.clear();
        if (s.empty() || s[0] == ';' || s[0] == '#')
            return;
        if (s.front() == '[') {
            size_t e = s.find(']');
            if (e != std::string::npos)
                section = s.substr(1, e - 1);
            return;
        }
        size_t eq = s.find('=');
        if (eq == std::string::npos)
            return;
        std::string key = s.substr(0, eq);
        std::string val = s.substr(eq + 1);
        // trim key trailing / val leading
        size_t kb = key.find_last_not_of(" \t");
        if (kb != std::string::npos) key = key.substr(0, kb + 1);
        size_t va = val.find_first_not_of(" \t");
        val = (va == std::string::npos) ? std::string() : val.substr(va);
        doc->sections[section][key] = val;
    };
    for (; *p; ++p) {
        if (*p == '\n') { flushLine(); }
        else            { line.push_back(*p); }
    }
    flushLine();
    return doc;
}

void IniFree(IniDocument* doc) { delete doc; }

const char* IniGet(const IniDocument* doc, const char* section,
                   const char* key, const char* def) {
    auto si = doc->sections.find(section);
    if (si == doc->sections.end())
        return def;
    auto ki = si->second.find(key);
    if (ki == si->second.end())
        return def;
    return ki->second.c_str();
}

void CityLoadFromIni(const IniDocument* doc, int index) {
    CityRecord& c = g_cities[index];
    std::memset(&c, 0, sizeof(c));

    // Parse CSV into an aligned local buffer, then memcpy into the record at a
    // byte offset (avoids taking the address of a packed member directly).
    char* base = reinterpret_cast<char*>(&c);
    auto csv = [&](const char* section, const char* key, const char* def,
                   size_t fieldOffset, int n) {
        i32 tmp[16] = {0};
        if (n > 16) n = 16;
        CityParseCsvFieldList(IniGet(doc, section, key, def), tmp, n);
        std::memcpy(base + fieldOffset, tmp, sizeof(i32) * static_cast<size_t>(n));
    };

    // [A - ALLGEMEIN]
    const char* nameStr = IniGet(doc, "A - ALLGEMEIN", "Stadtname", "Unknown");
    for (int i = 0; i < 31 && nameStr[i]; ++i)
        c.name[i] = static_cast<u16>(static_cast<unsigned char>(nameStr[i]));

    csv("A - ALLGEMEIN", "KartenPosition", "0,0", offsetof(CityRecord, mapPos), 2);
    c.faith        = static_cast<u8>(UtilParseInt(IniGet(doc, "A - ALLGEMEIN", "Glaube", "0")));
    c.historyStart = UtilParseInt(IniGet(doc, "A - ALLGEMEIN", "HistorieStart", "1400"));
    c.historyEnd   = UtilParseInt(IniGet(doc, "A - ALLGEMEIN", "HistorieEnde", "1400"));
    c.land         = static_cast<u8>(UtilParseInt(IniGet(doc, "A - ALLGEMEIN", "Land", "0")));
    c.language     = static_cast<u8>(UtilParseInt(IniGet(doc, "A - ALLGEMEIN", "Sprache", "0")));
    csv("A - ALLGEMEIN", "KartenOffset", "0, 0", offsetof(CityRecord, mapOffset), 2);
    c.maxPlayer    = static_cast<u8>(UtilParseInt(IniGet(doc, "A - ALLGEMEIN", "MaxPlayer", "4")));

    // [A - EINWOHNER] / [A - PRUNK]
    for (int i = 0; i < 10; ++i) {
        char key[32];
        std::snprintf(key, sizeof(key), "Einwohner_%d", i);
        i32 pair[2] = {0, 0};
        CityParseCsvFieldList(IniGet(doc, "A - EINWOHNER", key, "1400,0"), pair, 2);
        c.einwohner[i].year = pair[0];
        c.einwohner[i].value = pair[1];
    }
    for (int i = 0; i < 10; ++i) {
        char key[32];
        std::snprintf(key, sizeof(key), "Prunk_%d", i);
        i32 pair[2] = {0, 0};
        CityParseCsvFieldList(IniGet(doc, "A - PRUNK", key, "1400,0"), pair, 2);
        c.prunk[i].year = pair[0];
        c.prunk[i].value = pair[1];
    }

    // [B - WETTER]
    csv("B - WETTER", "Regenwahrscheinlichkeit", "0,0,0,0", offsetof(CityRecord, rainProb), 4);
    csv("B - WETTER", "Schneewahrscheinlichkeit", "0,0,0,0", offsetof(CityRecord, snowProb), 4);
    c.freezeProb = UtilParseInt(IniGet(doc, "B - WETTER", "Zufrierenwahrscheinlichkeit", "0"));

    // [C - PRIVILEGIEN] — 11 ints parsed, only the low byte of each kept.
    {
        i32 priv[11] = {0};
        CityParseCsvFieldList(IniGet(doc, "C - PRIVILEGIEN", "Privilegien",
                                      "0,0,0,0,0,0,0,0,0,0,0"), priv, 11);
        for (int i = 0; i < 11; ++i)
            c.privileges[i] = static_cast<u8>(priv[i]);
    }

    // [F - GESETZE] — six 12-id law books, parsed as ints then stored as bytes.
    auto lawBook = [&](const char* key, u8* dst) {
        i32 ids[12] = {0};
        CityParseCsvFieldList(IniGet(doc, "F - GESETZE", key, "0,0,0,0"), ids, 12);
        for (int i = 0; i < 12; ++i)
            dst[i] = static_cast<u8>(ids[i]);
    };
    lawBook("Verfassungsgesetze", c.verfassung);
    lawBook("Finanzgesetze",      c.finanz);
    lawBook("Strafgesetze",       c.straf);
    lawBook("Gildengesetze",      c.gilde);
    lawBook("Kirchengesetze",     c.kirche);
    lawBook("DiebeRaeubergesetze", c.diebeRaeuber);
    // Import/Export goods + Umland counts require live world state (object/person
    // id resolution); left zero here, populated at runtime by the engine.
}

} // namespace guild::world
