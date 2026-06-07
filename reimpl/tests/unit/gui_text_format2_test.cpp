#include "test.h"

#include "gui/text/text_format2.h"

#include <cstring>
#include <string>

using namespace guild::gui::text;

namespace {

std::string callDP(unsigned v) {
    char buf[8] = {0};
    char* p = buf;
    FormatDigitPair(static_cast<guild::u16>(v), p);
    return std::string(buf, p - buf);
}
std::string callDP2(unsigned v) {
    char buf[8] = {0};
    char* p = buf;
    FormatDigitPair2(v, p);
    return std::string(buf, p - buf);
}
std::string callDP3(unsigned v) {
    char buf[16] = {0};
    char* p = buf;
    FormatDigitPair3(v, p);
    return std::string(buf, p - buf);
}

} // namespace

// --- FormatDigitPair: fixed 2-digit -----------------------------------------
TEST(GuiTextFormat2, DigitPairGolden) {
    CHECK_EQ(callDP(0), std::string("00"));
    CHECK_EQ(callDP(5), std::string("05"));
    CHECK_EQ(callDP(9), std::string("09"));
    CHECK_EQ(callDP(10), std::string("10"));
    CHECK_EQ(callDP(42), std::string("42"));
    CHECK_EQ(callDP(99), std::string("99"));
}

// --- FormatDigitPair2: fixed 4-digit ----------------------------------------
TEST(GuiTextFormat2, DigitPair2Golden) {
    CHECK_EQ(callDP2(0), std::string("0000"));
    CHECK_EQ(callDP2(7), std::string("0007"));
    CHECK_EQ(callDP2(99), std::string("0099"));
    CHECK_EQ(callDP2(100), std::string("0100"));
    CHECK_EQ(callDP2(1234), std::string("1234"));
    CHECK_EQ(callDP2(9999), std::string("9999"));
}

// --- FormatDigitPair3: fixed 8-digit ----------------------------------------
TEST(GuiTextFormat2, DigitPair3Golden) {
    CHECK_EQ(callDP3(0), std::string("00000000"));
    CHECK_EQ(callDP3(5), std::string("00000005"));
    CHECK_EQ(callDP3(9999), std::string("00009999"));
    CHECK_EQ(callDP3(10000), std::string("00010000"));
    CHECK_EQ(callDP3(12345678u), std::string("12345678"));
    CHECK_EQ(callDP3(99999999u), std::string("99999999"));
}

// --- TrimTrailingSpace: split off trailing digit-class run ------------------
TEST(GuiTextFormat2, TrimTrailingSplit) {
    auto run = [](const char* s) {
        char out[64] = {0};
        int len = static_cast<int>(std::strlen(s)) - 1;
        int split = TrimTrailingSpace(s, len, out);
        return std::pair<int, std::string>(split, std::string(out));
    };
    auto a = run("abc123");
    CHECK_EQ(a.first, 3);
    CHECK_EQ(a.second, std::string("123"));

    auto b = run("12345");
    CHECK_EQ(b.first, 0);
    CHECK_EQ(b.second, std::string("12345"));

    auto c = run("hello");
    CHECK_EQ(c.first, 5);
    CHECK_EQ(c.second, std::string(""));

    auto d = run("a1");
    CHECK_EQ(d.first, 1);
    CHECK_EQ(d.second, std::string("1"));
}

// --- StripNameTokens: dash / percent folding --------------------------------
TEST(GuiTextFormat2, StripNameTokens) {
    auto run = [](const char* s) {
        char out[128] = {0};
        guild::i32 consumed = 0, tail = 0;
        StripNameTokens(s, out, consumed, tail);
        return std::make_tuple(std::string(out), consumed, tail);
    };

    auto a = run("foo-bar-baz-qux");
    CHECK_EQ(std::get<0>(a), std::string("foobar"));
    CHECK_EQ(std::get<1>(a), 8);
    CHECK_EQ(std::get<2>(a), 7);

    auto b = run("a%b");
    CHECK_EQ(std::get<0>(b), std::string("a %b"));
    CHECK_EQ(std::get<1>(b), 3);
    CHECK_EQ(std::get<2>(b), 0);

    auto c = run("a%%b");
    CHECK_EQ(std::get<0>(c), std::string("a % %b"));
    CHECK_EQ(std::get<1>(c), 4);

    auto d = run("%abc");
    CHECK_EQ(std::get<0>(d), std::string("%abc"));
    CHECK_EQ(std::get<1>(d), 4);

    auto e = run("ab-cd");
    CHECK_EQ(std::get<0>(e), std::string("abcd"));
    CHECK_EQ(std::get<1>(e), 5);
}

// --- GetCurrentIconWord -----------------------------------------------------
TEST(GuiTextFormat2, CurrentIconWord) {
    guild::i16 saved = g_currentIconWord;
    CHECK_EQ((int)(guild::u16)g_currentIconWord, 0xFFFF);  // BSS init
    g_currentIconWord = 0x0011;
    CHECK_EQ((int)GetCurrentIconWord(), 0x0011);
    g_currentIconWord = saved;
}

// --- AppendWideLines: clear + N*(line + "|") --------------------------------
TEST(GuiTextFormat2, AppendWideLines) {
    char caption[256];
    std::memset(caption, 0xAA, sizeof(caption));  // dirty buffer

    AppendWideLines(caption, 3, "ab");
    CHECK_EQ(std::string(caption), std::string("ab|ab|ab|"));

    AppendWideLines(caption, 1, "X");
    CHECK_EQ(std::string(caption), std::string("X|"));

    AppendWideLines(caption, 0, "ignored");
    CHECK_EQ(std::string(caption), std::string(""));  // count<=0 => just cleared
}
