// End-to-end test for the GUI text engine: resolve a localized message by id,
// expand mixed %-codes against a text DB, and tokenize a marked-up string into
// the full token stream — exercising format.cpp + textdb.cpp + richtext.cpp +
// markup.cpp together (the RenderRichString / RenderFormattedMessage flow).
#include "gui/text/format.h"
#include "gui/text/markup.h"
#include "gui/text/richtext.h"
#include "gui/text/textdb.h"

#include "crt/rand.h"

#include "tests/framework/test.h"

#include <string>
#include <vector>

using namespace guild::gui::text;

namespace {
const char* kSeasons[4] = {"Spring", "Summer", "Autumn", "Winter"};
}

// Full mixed rich string: grouped int + money + count + date + DB string + a
// literal '%%' + a pass-through '$' markup token.
TEST(GuiTextE2E, MixedRichString) {
    TextDb db;
    int town = db.Add("Cologne", "town");  // id 0

    GameTimeSource when{};
    when.yearQuarter = 1;  // year 1401, season 1401%4=1 -> Summer

    // Codes per gilde.exe 0x59d6e8: %i grouped int, %T money, %a count, %D date.
    // (%s is not a value code -- it copies through verbatim.)
    std::vector<Arg> args = {
        Arg::MakeInt(12345),        // %i
        Arg::MakeMoney(1234, 1),    // %T (money)
        Arg::MakeInt(3),            // %a
        Arg::MakeDate(when),        // %D
    };
    (void)town;

    auto r = RenderRichString(
        "Town pop %i, tax %T for %a guards, $Mfounded %D (100%%).",
        args, &db, kSeasons);

    std::string expected =
        "Town pop 12.345, tax 1.234\x11 for 3\x14 guards, "
        "$Mfounded Summer 1401 (100\x16).";
    CHECK(r == expected);
}

// RenderFormattedMessage: resolve the format string from the DB by id, then
// expand it. Mirrors VIBE_Text_RenderFormattedMessage @0x59f99c.
TEST(GuiTextE2E, FormattedMessageById) {
    TextDb db;
    db.Add("Plain entry", "intro");                       // id 0
    int msg = db.Add("You earned %T today!", "earn_msg"); // id 1 (%T = money)

    auto r = RenderFormattedMessage(db, msg, {Arg::MakeMoney(5678, 1)}, kSeasons);
    CHECK(r == std::string("You earned 5.678\x11 today!", 24));

    // Out-of-range id -> empty (the original writes nothing).
    auto empty = RenderFormattedMessage(db, 99, {});
    CHECK(empty.empty());
}

// Random-text selection through the DB: a {rN} group resolved with a seeded RNG.
TEST(GuiTextE2E, RandomTextGroup) {
    TextDb db;
    int base = db.Add("greeting one", "greet");  // id 0
    db.Add("greeting two", "greet");              // id 1

    // {r2} with base id => pick among 2 variants, deterministic under a seed.
    guild::crt::Srand(777);
    auto a = RenderRichString("Bard says: {r2}", {Arg::MakeInt(base)}, &db);
    guild::crt::Srand(777);
    auto b = RenderRichString("Bard says: {r2}", {Arg::MakeInt(base)}, &db);
    CHECK(a == b);
    CHECK(a == "Bard says: greeting one" || a == "Bard says: greeting two");
}

// Full markup tokenization feeding a builder: $[ region ]$ with an inline button
// and a percent code interleaved.
TEST(GuiTextE2E, MarkupStream) {
    std::string err;
    auto t = TokenizeMarkup("$[Header$T$ib[OK] count %i$]", &err);
    CHECK(err.empty());

    // Expect: BracketOpen, Text("Header"), Tab, Inline(letter 'b', text "OK"),
    //         Text(" count "), PercentCode('i'), BracketClose
    CHECK_EQ((int)t.size(), 7);
    CHECK(t[0].kind == MarkupKind::BracketOpen);
    CHECK(t[1].kind == MarkupKind::Text && t[1].text == "Header");
    CHECK(t[2].kind == MarkupKind::Tab);
    CHECK(t[3].kind == MarkupKind::Inline && t[3].letter == 'b' && t[3].text == "OK");
    CHECK(t[4].kind == MarkupKind::Text && t[4].text == " count ");
    CHECK(t[5].kind == MarkupKind::PercentCode && t[5].letter == 'i');
    CHECK(t[6].kind == MarkupKind::BracketClose);
}
