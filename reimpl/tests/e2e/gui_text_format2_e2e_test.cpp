#include "test.h"

#include "gui/text/text_format2.h"

#include <cstring>
#include <string>

using namespace guild::gui::text;

// End-to-end: simulate the text-engine pipeline these helpers participate in.
// 1) StripNameTokens folds a dash/percent-delimited raw label into a display name.
// 2) The fixed-width digit emitters produce a zero-padded count suffix.
// 3) TrimTrailingSpace splits a value's trailing digit run for column alignment.
// 4) AppendWideLines builds the multi-line caption a list widget renders.
TEST(GuiTextFormat2E2E, LabelPipeline) {
    // (1) raw "merchant-name-extra%c" -> stop after two dashes.
    char display[128] = {0};
    guild::i32 consumed = 0, tail = 0;
    StripNameTokens("merchant-name-extra", display, consumed, tail);
    CHECK_EQ(std::string(display), std::string("merchantname"));
    CHECK_EQ(consumed, 14);  // up to and including the 2nd dash

    // (2) format an item count as a 4-digit field, then an 8-digit gold value.
    char numbuf[32] = {0};
    char* p = numbuf;
    FormatDigitPair2(42, p);          // "0042"
    *p++ = ':';
    FormatDigitPair3(12345678u, p);   // "12345678"
    *p = 0;
    CHECK_EQ(std::string(numbuf), std::string("0042:12345678"));

    // (3) split "stock128" into prefix length + trailing digit run.
    char digits[16] = {0};
    int split = TrimTrailingSpace("stock128", 7, digits);
    CHECK_EQ(split, 5);
    CHECK_EQ(std::string(digits), std::string("128"));

    // (4) compose the caption a 3-row list widget displays.
    char caption[256];
    std::memset(caption, 0x7F, sizeof(caption));
    AppendWideLines(caption, 3, display);  // "merchantname|" x3
    CHECK_EQ(std::string(caption),
             std::string("merchantname|merchantname|merchantname|"));
}

// End-to-end determinism: the digit emitters cover their full ranges with no
// off-by-one at the decade/century/myriad split points.
TEST(GuiTextFormat2E2E, DigitRangeBoundaries) {
    auto dp2 = [](unsigned v) {
        char b[8] = {0};
        char* p = b;
        FormatDigitPair2(v, p);
        return std::string(b, p - b);
    };
    auto dp3 = [](unsigned v) {
        char b[16] = {0};
        char* p = b;
        FormatDigitPair3(v, p);
        return std::string(b, p - b);
    };
    CHECK_EQ(dp2(99), std::string("0099"));
    CHECK_EQ(dp2(100), std::string("0100"));
    CHECK_EQ(dp3(9999), std::string("00009999"));
    CHECK_EQ(dp3(10000), std::string("00010000"));

    // Every value formats to exactly the expected fixed width.
    for (unsigned v = 0; v < 10000u; ++v) {
        CHECK(dp2(v).size() == 4);
    }
}
