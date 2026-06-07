#include "crt/printf.h"
#include "test.h"

#include <cstdio>
#include <string>

// End-to-end: a complex multi-arg template compared byte-for-byte to snprintf,
// exercising sign flags, zero/space padding, width, precision, hex, char, and
// string conversions in one pass (the integer/string surface the original
// VIBE_Crt_FormatStringCore @0x6051f0 implements).
TEST(CrtPrintfE2E, ComplexTemplate) {
    const char* fmt =
        "id=%05d name=%-10s hp=%+d/%d mana=[% d] gold=0x%08X pct=%3d%% tag=%c";

    char ours[256];
    char gold[256];
    int rn = ::guild::crt::Sprintf(
        ours, fmt, 7, "knight", 120, 200, -5, 0xCAFEu, 87, '!');
    int gn = std::snprintf(
        gold, sizeof(gold), fmt, 7, "knight", 120, 200, -5, 0xCAFEu, 87, '!');

    CHECK_EQ(std::string(ours), std::string(gold));
    CHECK_EQ(rn, gn);
}

TEST(CrtPrintfE2E, MixedRadixAndWidths) {
    const char* fmt = "[%6.4d|%-8s|%#x|%o|%+.3d|%*u]";
    char ours[256];
    char gold[256];
    int rn = ::guild::crt::Sprintf(ours, fmt, 42, "x", 0xffu, 64u, 7, 5, 99u);
    int gn = std::snprintf(gold, sizeof(gold), fmt, 42, "x", 0xffu, 64u, 7, 5, 99u);
    CHECK_EQ(std::string(ours), std::string(gold));
    CHECK_EQ(rn, gn);
}

TEST(CrtPrintfE2E, RepeatedFormatStability) {
    // Format the same template many times to confirm no state leaks across
    // conversions (the spec struct is reset per directive in the original).
    for (int i = -3; i <= 3; ++i) {
        char ours[128];
        char gold[128];
        int rn = ::guild::crt::Sprintf(ours, "v=%+04d h=%#06x s=%-4s|", i, (unsigned)(i & 0xff), "ok");
        int gn = std::snprintf(gold, sizeof(gold), "v=%+04d h=%#06x s=%-4s|", i, (unsigned)(i & 0xff), "ok");
        CHECK_EQ(std::string(ours), std::string(gold));
        CHECK_EQ(rn, gn);
    }
}
