// Unit tests for the GUI text formatting/parsing engine (guild::gui::text).
// Golden vectors computed from the recovered logic of:
//   VIBE_Money_FormatWithSeparators @0x58f798
//   VIBE_GameTime_PackToRecord/GetSeasonFromYear @0x583304/0x583384
//   VIBE_Text_RenderRichString format-code core @0x59d6e8
//   VIBE_Window_ParseMarkupAndBuild tokenizer @0x416720
//   VIBE_Text_FindTextArrayIndex/ParseRandomTextToken @0x44e0d8/0x44b8a0
#include "gui/text/format.h"
#include "gui/text/markup.h"
#include "gui/text/richtext.h"
#include "gui/text/textdb.h"

#include "crt/rand.h"

#include "tests/framework/test.h"

#include <cstring>
#include <string>

using namespace guild::gui::text;

namespace {
const char* kSeasons[4] = {"Spring", "Summer", "Autumn", "Winter"};
}

// ---------------------------------------------------------------------------
// FormatGroupedInt — '.' thousands separators (bare %i code).
// ---------------------------------------------------------------------------
TEST(GuiTextFmt, GroupedIntPositive) {
    char buf[64];
    FormatGroupedInt(0, buf);        CHECK(std::string(buf) == "0");
    FormatGroupedInt(42, buf);       CHECK(std::string(buf) == "42");
    FormatGroupedInt(999, buf);      CHECK(std::string(buf) == "999");
    FormatGroupedInt(1000, buf);     CHECK(std::string(buf) == "1.000");
    FormatGroupedInt(1234, buf);     CHECK(std::string(buf) == "1.234");
    FormatGroupedInt(12345, buf);    CHECK(std::string(buf) == "12.345");
    FormatGroupedInt(123456, buf);   CHECK(std::string(buf) == "123.456");
    FormatGroupedInt(1234567, buf);  CHECK(std::string(buf) == "1.234.567");
}

TEST(GuiTextFmt, GroupedIntNegativeQuirk) {
    // Faithful: the original groups the sign-included "%i" string, so the '-'
    // counts as position 0 and a separator lands right after it for 6+ digits.
    char buf[64];
    FormatGroupedInt(-1, buf);       CHECK(std::string(buf) == "-1");
    FormatGroupedInt(-1234, buf);    CHECK(std::string(buf) == "-1.234");
    FormatGroupedInt(-123456, buf);  CHECK(std::string(buf) == "-.123.456");
}

// ---------------------------------------------------------------------------
// FormatMoney — coin icon 0x11, rounding, '.' grouping.
// ---------------------------------------------------------------------------
TEST(GuiTextFmt, Money) {
    char buf[128];
    auto money = [&](int a) { FormatMoney(a, 1, buf); return std::string(buf); };
    CHECK(money(0)       == std::string("0\x11", 2));
    CHECK(money(5)       == std::string("5\x11", 2));
    CHECK(money(999)     == std::string("999\x11", 4));
    CHECK(money(1000)    == std::string("1.000\x11", 6));
    CHECK(money(1234)    == std::string("1.234\x11", 6));
    CHECK(money(123456)  == std::string("123.456\x11", 8));
    CHECK(money(-1)      == std::string("-1\x11", 3));
    CHECK(money(-1234)   == std::string("-1.234\x11", 7));
    CHECK(money(-123456) == std::string("-123.456\x11", 9));
}

TEST(GuiTextFmt, MoneyRoundingAndDivisor) {
    char buf[128];
    // (int)(mag/divisor + 0.5) — round-half-up then truncate toward zero.
    FormatMoney(2, 4, buf);  CHECK(std::string(buf) == std::string("1\x11", 2));  // 0.5 +0.5 =1.0 ->1
    FormatMoney(1, 4, buf);  CHECK(std::string(buf) == std::string("0\x11", 2));  // 0.25+0.5 =0.75->0
    FormatMoney(10, 4, buf); CHECK(std::string(buf) == std::string("3\x11", 2));  // 2.5 +0.5 =3.0 ->3
}

// ---------------------------------------------------------------------------
// Date / season.
// ---------------------------------------------------------------------------
TEST(GuiTextFmt, DateRecordAndSeason) {
    GameTimeSource src{};
    src.yearQuarter = 2;   // year base 2, quarter 2 (autumn)
    src.hour = 13;
    src.minute = 45;
    src.extra = 0xCAFE;
    DateRecord rec{};
    PackDateRecord(src, rec);
    CHECK_EQ((int)rec.dayMarker, 1);
    CHECK_EQ((int)rec.month, 7);      // 3*2+1
    CHECK_EQ((int)rec.year, 1402);    // 2+1400
    CHECK_EQ((int)rec.hour, 13);
    CHECK_EQ((int)rec.minute, 45);
    CHECK_EQ((int)rec.extra, 0xCAFE);
    // GetSeasonFromYear takes the packed year in the high word.
    CHECK_EQ(SeasonFromYear(2 << 16), 2);
    CHECK_EQ(SeasonFromYear(7 << 16), 3);
}

// ---------------------------------------------------------------------------
// RenderRichString — format-code production.
// ---------------------------------------------------------------------------
TEST(GuiTextRich, GroupedIntCode) {
    auto r = RenderRichString("Score: %i!", {Arg::MakeInt(1234567)});
    CHECK(r == "Score: 1.234.567!");
}

TEST(GuiTextRich, LiteralPercent) {
    auto r = RenderRichString("100%% done", {});
    CHECK(r == std::string("100\x16 done"));  // %% -> 0x16
}

TEST(GuiTextRich, CountCode) {
    auto r = RenderRichString("x%a", {Arg::MakeInt(7)});
    CHECK(r == std::string("x7\x14", 3));  // %a -> "%i%c" icon 0x14
}

TEST(GuiTextRich, MoneyCode) {
    auto r = RenderRichString("Cost: %m.", {Arg::MakeMoney(1234, 1)});
    CHECK(r == std::string("Cost: 1.234\x11.", 13));
}

TEST(GuiTextRich, DateCode) {
    GameTimeSource src{};
    src.yearQuarter = 1;  // summer 1401
    auto r = RenderRichString("Date: %T", {Arg::MakeDate(src)}, nullptr, kSeasons);
    CHECK(r == "Date: Summer 1401");
}

TEST(GuiTextRich, StringSubstitution) {
    auto r = RenderRichString("Hello, %s!", {Arg::MakeStr("World")});
    CHECK(r == "Hello, World!");
}

TEST(GuiTextRich, DollarMarkupPassThrough) {
    // '$' markup is left verbatim for the markup builder.
    auto r = RenderRichString("a$Mb", {});
    CHECK(r == "a$Mb");
}

// ---------------------------------------------------------------------------
// TextDb — array lookup, name search, random tokens.
// ---------------------------------------------------------------------------
TEST(GuiTextDb, AddAndLookup) {
    TextDb db;
    int a = db.Add("Alpha", "first");
    int b = db.Add("Beta", "second");
    CHECK_EQ(a, 0);
    CHECK_EQ(b, 1);
    CHECK_EQ(db.Count(), 2);
    CHECK(std::string(db.Text(0)) == "Alpha");
    CHECK(std::string(db.Text(1)) == "Beta");
    CHECK(db.Text(2) == nullptr);   // out of range -> null
    CHECK(db.Text(-1) == nullptr);
}

TEST(GuiTextDb, FindIndexCaseInsensitive) {
    TextDb db;
    db.Add("Alpha", "Greeting");
    db.Add("Beta", "Farewell");
    db.Add("Gamma", "Question");
    CHECK_EQ(db.FindIndex("greeting"), 0);
    CHECK_EQ(db.FindIndex("FAREWELL"), 1);
    CHECK_EQ(db.FindIndex("Question"), 2);
    CHECK_EQ(db.FindIndex("missing"), -1);
}

TEST(GuiTextDb, ParseRandomTextToken) {
    TextDb db;
    db.Add("v0", "name");
    bool ok = false;
    int idx = db.ParseRandomTextToken("{r3}", &ok);
    CHECK(ok);
    CHECK_EQ(idx, 0);
    CHECK_EQ((int)db.Tag(0), 3 + 8);   // N+8 => '3'-'1'+9 = 11
    // malformed token -> not ok, tag reset to 0
    db.ParseRandomTextToken("{rZ}", &ok);
    CHECK(!ok);
    CHECK_EQ((int)db.Tag(0), 0);
}

TEST(GuiTextDb, RandomVariantDeterministic) {
    TextDb db;
    int base = db.Add("variant-A", "g");
    db.Add("variant-B", "g");
    db.Add("variant-C", "g");
    // Seed the CRT RNG for determinism; same seed -> same pick.
    guild::crt::Srand(12345);
    const char* first = db.PickRandomVariant(base, 3);
    guild::crt::Srand(12345);
    const char* second = db.PickRandomVariant(base, 3);
    CHECK(first != nullptr);
    CHECK(std::strcmp(first, second) == 0);
    // The pick must be one of the three variants.
    std::string s(first);
    CHECK(s == "variant-A" || s == "variant-B" || s == "variant-C");
}

// ---------------------------------------------------------------------------
// Markup tokenizer.
// ---------------------------------------------------------------------------
TEST(GuiTextMarkup, BasicTokens) {
    std::string err;
    auto t = TokenizeMarkup("Hi$Mworld$T", &err);
    CHECK(err.empty());
    // Text("Hi"), Embed, Text("world"), Tab
    CHECK_EQ((int)t.size(), 4);
    CHECK(t[0].kind == MarkupKind::Text && t[0].text == "Hi");
    CHECK(t[1].kind == MarkupKind::Embed);
    CHECK(t[2].kind == MarkupKind::Text && t[2].text == "world");
    CHECK(t[3].kind == MarkupKind::Tab);
}

TEST(GuiTextMarkup, ColumnAndLineCodes) {
    std::string err;
    auto t = TokenizeMarkup("$L$R$B$Y$3A", &err);
    CHECK(err.empty());
    CHECK_EQ((int)t.size(), 5);
    CHECK(t[0].kind == MarkupKind::ColumnReset);
    CHECK(t[1].kind == MarkupKind::ColumnRight);
    CHECK(t[2].kind == MarkupKind::ColumnCenter);
    CHECK(t[3].kind == MarkupKind::ColumnFull);
    CHECK(t[4].kind == MarkupKind::LineFeed);
    CHECK_EQ(t[4].arg, 3);  // "$3A" -> 3 line feeds
}

TEST(GuiTextMarkup, InlineButtonLabel) {
    std::string err;
    auto t = TokenizeMarkup("$ia[Click]", &err);
    CHECK(err.empty());
    CHECK_EQ((int)t.size(), 1);
    CHECK(t[0].kind == MarkupKind::Inline);
    CHECK_EQ(t[0].letter, 'a');     // red-button selector
    CHECK(t[0].text == "Click");
}

TEST(GuiTextMarkup, MissingBracketError) {
    std::string err;
    auto t = TokenizeMarkup("$ia[Unterminated", &err);
    CHECK(err == "Missing ']'");
}

TEST(GuiTextMarkup, UnknownCodeError) {
    std::string err;
    auto t = TokenizeMarkup("$Z", &err);
    CHECK(err == std::string("Unknown textparameter: %Z"));
    CHECK(t[0].kind == MarkupKind::Unknown);
}

TEST(GuiTextMarkup, PercentCode) {
    std::string err;
    auto t = TokenizeMarkup("val=%3i", &err);
    CHECK(err.empty());
    CHECK_EQ((int)t.size(), 2);
    CHECK(t[0].kind == MarkupKind::Text && t[0].text == "val=");
    CHECK(t[1].kind == MarkupKind::PercentCode);
    CHECK_EQ(t[1].arg, 3);
    CHECK_EQ(t[1].letter, 'i');
}

TEST(GuiTextMarkup, BracketRegion) {
    std::string err;
    auto t = TokenizeMarkup("$[region$]", &err);
    CHECK(err.empty());
    CHECK(t[0].kind == MarkupKind::BracketOpen);
    CHECK(t[1].kind == MarkupKind::Text && t[1].text == "region");
    CHECK(t[2].kind == MarkupKind::BracketClose);
}
