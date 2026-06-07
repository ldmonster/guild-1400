#include "test.h"
#include "crt/string.h"

#include <cstring>
#include <string>

using namespace guild;

// End-to-end: build a buffer, format numbers into it, concatenate fields, then
// tokenize on a delimiter using StringFindChar + StringCompare, exercising several
// of the reconstructed routines together.
TEST(CrtStringE2E, BuildAndTokenize) {
    char line[128];
    std::memset(line, 0, sizeof(line));

    // Build "id=42;hp=255;name=guild" using IntToAscii + Concat.
    char num[16];
    crt::StringConcat(line, "id=");
    crt::StringIntToAscii(42, num, 10);
    crt::StringConcat(line, num);

    crt::StringConcat(line, ";hp=");
    crt::StringUIntToString(255u, num, 10);
    crt::StringConcat(line, num);

    crt::StringConcat(line, ";name=guild");

    CHECK_EQ(crt::StringLength(line), static_cast<u32>(std::strlen("id=42;hp=255;name=guild")));
    CHECK(std::string(line) == "id=42;hp=255;name=guild");

    // Tokenize on ';' using StringFindChar, copying each token via memcpy + memset.
    char tokens[4][32];
    int count = 0;
    char* p = line;
    while (p && *p) {
        char* semi = crt::StringFindChar(p, ';');
        size_t len = semi ? static_cast<size_t>(semi - p) : crt::StringLength(p);
        std::memset(tokens[count], 0, sizeof(tokens[count]));
        std::memcpy(tokens[count], p, len);
        tokens[count][len] = 0;
        ++count;
        p = semi ? semi + 1 : nullptr;
    }

    CHECK_EQ(count, 3);
    CHECK_EQ(crt::StringCompare(tokens[0], "id=42"), 0);
    CHECK_EQ(crt::StringCompare(tokens[1], "hp=255"), 0);
    CHECK_EQ(crt::StringCompare(tokens[2], "name=guild"), 0);

    // Prefix matching: each token's "key" prefix.
    CHECK_EQ(crt::StringMatchPrefix(tokens[0], "id="), 3);
    CHECK_EQ(crt::StringMatchPrefix(tokens[2], "name="), 5);

    // Trim a padded copy and re-compare.
    char padded[32];
    std::memset(padded, 0, sizeof(padded));
    std::memcpy(padded, "name=guild   ", std::strlen("name=guild   "));
    crt::StringTrimTrailingSpaces(padded);
    CHECK_EQ(crt::StringCompare(padded, tokens[2]), 0);
}

// E2E: round-trip an unsigned value through hex formatting and confirm the digits,
// then locate a digit with StringFindChar.
TEST(CrtStringE2E, HexRoundTrip) {
    char buf[16];
    crt::StringUIntToString(0xCAFEu, buf, 16);
    CHECK(std::string(buf) == "cafe");
    CHECK(crt::StringFindChar(buf, 'f') == buf + 2);
    CHECK_EQ(crt::StringLength(buf), 4u);
}
