// Boundary / malformed hardening tests for the city-definition loader + helpers:
//   world/city.{h,cpp}       — UtilParseInt, CityParseCsvFieldList, CityLoadFromIni,
//                              IniParse/IniGet/IniFree
//   world/city_load.{h,cpp}  — CityGetDistrictCoord, CityLoadDefinitionIni (mock src)
//
// Focus (wave-12): truncated / empty / malformed INI text, oversized CSV token &
// field counts, out-of-range city slot + district index, NUL-less / overlong
// strings. Runs clean under ASan+UBSan; valid-input behaviour is unchanged.
#include "test.h"

#include "world/city.h"
#include "world/city_load.h"
#include "world/types.h"

#include <cstring>
#include <string>
#include <vector>

using namespace guild;
using namespace guild::world;

// --- UtilParseInt: blanks, signs, non-digits, empty ---------------------------
TEST(CityLoadHarden, ParseIntEdges) {
    CHECK_EQ(UtilParseInt(""), 0);
    CHECK_EQ(UtilParseInt("   "), 0);
    CHECK_EQ(UtilParseInt("  -42xyz"), -42);   // stops at first non-digit
    CHECK_EQ(UtilParseInt("+7"), 7);
    CHECK_EQ(UtilParseInt("abc"), 0);
    CHECK_EQ(UtilParseInt("-"), 0);            // sign with no digits
    CHECK_EQ(UtilParseInt("000123"), 123);
}

// --- CityParseCsvFieldList: empty text, oversized token, fewer/more fields -----
TEST(CityLoadHarden, CsvFieldListBounds) {
    i32 out[4];

    // maxFields <= 0 -> nothing written.
    out[0] = 0x5A5A;
    CHECK_EQ(CityParseCsvFieldList("1,2,3", out, 0), 0);
    CHECK_EQ(out[0], 0x5A5A);

    // Fewer commas than fields: VERIFIED 1:1 against the disasm @0x50704c
    // (wave-16). The original parses the final NUL-terminated token exactly once
    // and stops (the v16 "last field" flag is set during the scan of that token,
    // then `if(v16) break` fires after it is parsed) — it does NOT emit an extra
    // empty trailing token. So "10,20" with maxFields 4 yields n==2, out[0]=10,
    // out[1]=20, and out[2..3] are left untouched. (The previous reconstruction
    // wrongly returned n==3 with out[2]==0; fixed in wave-16 to match the binary.)
    out[0] = out[1] = out[2] = out[3] = -1;
    int n = CityParseCsvFieldList("10,20", out, 4);
    CHECK_EQ(n, 2);
    CHECK_EQ(out[0], 10);
    CHECK_EQ(out[1], 20);
    CHECK_EQ(out[2], -1);   // not written: no extra empty token
    CHECK_EQ(out[3], -1);   // beyond the parsed range -> untouched

    // More fields available than maxFields: only maxFields parsed.
    int n2 = CityParseCsvFieldList("1,2,3,4,5,6", out, 4);
    CHECK_EQ(n2, 4);
    CHECK_EQ(out[3], 4);

    // Oversized token (> the 256-byte scratch) must not overflow the scratch.
    std::string big(1000, '7');             // 1000 '7' digits, no comma
    i32 one[1];
    int n3 = CityParseCsvFieldList(big.c_str(), one, 1);
    CHECK_EQ(n3, 1);                          // parsed (truncated token) without OOB
}

// --- IniParse / IniGet on empty + malformed text ------------------------------
TEST(CityLoadHarden, IniMalformed) {
    // Empty text.
    IniDocument* d0 = IniParse("");
    CHECK(std::strcmp(IniGet(d0, "X", "k", "def"), "def") == 0);
    IniFree(d0);

    // Lines with no '=', a section with no ']', comments, blank lines.
    const char* txt =
        ";comment\n"
        "[A - ALLGEMEIN\n"      // missing ']'
        "noequalshere\n"
        "\n"
        "Stadtname=Trier\n";    // before any well-formed section -> empty section
    IniDocument* d = IniParse(txt);
    // Key landed under the empty (default) section because '[A - ALLGEMEIN' had no ']'.
    CHECK(std::strcmp(IniGet(d, "", "Stadtname", "def"), "Trier") == 0);
    CHECK(std::strcmp(IniGet(d, "missing", "k", "def"), "def") == 0);
    IniFree(d);
}

// --- CityLoadFromIni on a near-empty doc: defaults applied, no OOB -------------
TEST(CityLoadHarden, LoadFromTruncatedIni) {
    // Only a bare section header; every key falls back to its default.
    IniDocument* doc = IniParse("[A - ALLGEMEIN]\n");
    std::memset(&g_cities[1], 0xAB, sizeof(CityRecord));
    CityLoadFromIni(doc, 1);
    const CityRecord& c = g_cities[1];
    // Defaults: name "Unknown", historyStart/End 1400, maxPlayer 4.
    CHECK_EQ((int)c.name[0], (int)'U');
    CHECK_EQ(c.historyStart, 1400);
    CHECK_EQ(c.historyEnd, 1400);
    CHECK_EQ((int)c.maxPlayer, 4);
    IniFree(doc);
}

// --- Overlong / NUL-less Stadtname is capped at 31 chars (name[32]) -----------
TEST(CityLoadHarden, OverlongCityName) {
    std::string longName(200, 'Z');
    std::string ini = "[A - ALLGEMEIN]\nStadtname=" + longName + "\n";
    IniDocument* doc = IniParse(ini.c_str());
    CityLoadFromIni(doc, 2);
    const CityRecord& c = g_cities[2];
    // 31 chars copied, slot 31 left as the terminator region (not written past 31).
    CHECK_EQ((int)c.name[0], (int)'Z');
    CHECK_EQ((int)c.name[30], (int)'Z');
    IniFree(doc);
}

// --- District coord accessor: in-range, boundary, out-of-range -----------------
TEST(CityLoadHarden, DistrictCoordBounds) {
    g_districtCoords[0].a = 11; g_districtCoords[0].b = 22;
    g_districtCoords[kDistrictCount - 1].a = 33;
    g_districtCoords[kDistrictCount - 1].b = 44;

    i32 out[2];
    out[0] = out[1] = -1;
    CHECK_EQ(CityGetDistrictCoord(0, out), 1);
    CHECK_EQ(out[0], 11); CHECK_EQ(out[1], 22);

    CHECK_EQ(CityGetDistrictCoord(kDistrictCount - 1, out), 1);
    CHECK_EQ(out[0], 33); CHECK_EQ(out[1], 44);

    // Out of range (== count, > count, negative): returns 0, leaves out untouched.
    out[0] = 0x7E; out[1] = 0x7F;
    CHECK_EQ(CityGetDistrictCoord(kDistrictCount, out), 0);
    CHECK_EQ(out[0], 0x7E);
    CHECK_EQ(CityGetDistrictCoord(100000, out), 0);
    CHECK_EQ(CityGetDistrictCoord(-1, out), 0);
}

// --- CityLoadDefinitionIni: out-of-range slot rejected, missing file fails -----
namespace {
struct FailSource : ICityFileSource {
    char* Load(const char*) override { return nullptr; }  // file not found
    void  Free(char*) override {}
};
struct EmptySource : ICityFileSource {
    char* Load(const char*) override {
        char* b = new char[1]; b[0] = '\0'; return b;     // empty .ini text
    }
    void Free(char* b) override { delete[] b; }
};
}  // namespace

TEST(CityLoadHarden, LoadDefinitionSlotAndFileBounds) {
    FailSource fail;
    // Out-of-range slot -> 0, never touches the file source.
    CHECK_EQ(CityLoadDefinitionIni(fail, "x", kCityMaxCount), 0);
    CHECK_EQ(CityLoadDefinitionIni(fail, "x", -1), 0);
    // Valid slot but the file can't be loaded -> 0.
    CHECK_EQ(CityLoadDefinitionIni(fail, "missing", 1), 0);

    // Empty .ini text parses to all-defaults and succeeds (slot != 0, no recursion).
    EmptySource empty;
    CHECK_EQ(CityLoadDefinitionIni(empty, "empty", 1), 1);
}
