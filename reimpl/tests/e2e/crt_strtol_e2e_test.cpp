#include "test.h"
#include "crt/strtol.h"
#include "crt/ctype.h"

#include <cstring>
#include <string>

using namespace guild;

// Parse a config-like line "key=123, hex=0xFF, name=Foo" using only the recovered
// CRT parsing/case routines, exercising Atoi, StrToLong (base 0 auto-detect),
// StringToUpper and StringCompareNoCaseN together.
TEST(CrtStrtolE2E, ParseConfigLine) {
    char line[] = "  Count = 42 , Mask = 0xFF , Octal = 0755 , Name = guild ";

    // Tokenize on ',' and split each "key = value" pair.
    int count = -1;
    unsigned mask = 0;
    long octal = 0;
    std::string name;

    char buf[256];
    std::strcpy(buf, line);
    char* save = buf;
    for (char* tok = std::strtok(save, ","); tok; tok = std::strtok(nullptr, ",")) {
        char* eq = std::strchr(tok, '=');
        if (!eq) continue;
        *eq = '\0';
        char* key = tok;
        char* val = eq + 1;

        // Trim leading whitespace from the key using the ctype space bit.
        while (*key && (crt::kPctype[static_cast<unsigned char>(*key)] & 0x08))
            ++key;
        // Trim trailing whitespace from the key.
        char* kend = key + std::strlen(key);
        while (kend > key && (crt::kPctype[static_cast<unsigned char>(kend[-1])] & 0x08))
            --kend;
        *kend = '\0';

        if (crt::StringCompareNoCaseN("count", key, 6) == 0) {
            count = crt::Atoi(val); // skips its own leading ws, stops at trailing ws
        } else if (crt::StringCompareNoCaseN("mask", key, 5) == 0) {
            const char* end = nullptr;
            mask = static_cast<unsigned>(crt::Strtoul(val, &end, 0)); // 0x.. -> hex
        } else if (crt::StringCompareNoCaseN("octal", key, 6) == 0) {
            const char* end = nullptr;
            octal = crt::Strtol(val, &end, 0); // leading 0 -> octal
        } else if (crt::StringCompareNoCaseN("name", key, 5) == 0) {
            // Skip whitespace, copy the bare token, uppercase it.
            while (*val && (crt::kPctype[static_cast<unsigned char>(*val)] & 0x08))
                ++val;
            char nbuf[64];
            std::strcpy(nbuf, val);
            char* ne = nbuf + std::strlen(nbuf);
            while (ne > nbuf && (crt::kPctype[static_cast<unsigned char>(ne[-1])] & 0x08))
                --ne;
            *ne = '\0';
            crt::StringToUpper(nbuf);
            name = nbuf;
        }
    }

    CHECK_EQ(count, 42);
    CHECK_EQ(mask, 0xFFu);
    CHECK_EQ(octal, 0755L);   // 493 decimal
    CHECK(name == "GUILD");
}

// Round-trip-ish flow: format-free numeric parse of several bases through StrToLong.
TEST(CrtStrtolE2E, MixedBaseSequence) {
    struct { const char* s; int base; long v; } seq[] = {
        {"0b not used", 10, 0}, {"  -17", 10, -17}, {"0x2A", 0, 42},
        {"0100", 0, 64}, {"ZZ", 36, 35 * 36 + 35},
    };
    for (auto& e : seq) {
        const char* end = nullptr;
        long got = static_cast<long>(static_cast<i32>(
            crt::StrToLong(e.s, &end, /*signed*/1, e.base)));
        CHECK_EQ(got, e.v);
    }
}
