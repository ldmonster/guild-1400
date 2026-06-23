// Parse the real `_M0_DIFFICULTY` rich-text markup (VIBE_Text_RenderRichString 0x16CC)
// into the difficulty screen's heading / prompt / option rows. Deterministic (the exact
// shipped CP1251 string, byte-for-byte from Resources/textbin_deutsch.BIN).
#include "tests/framework/test.h"
#include "play/sdl_charintro_screen.h"
#include <string>
using namespace guild;
using namespace guild::play;

// The exact shipped markup at entry `_M0_DIFFICULTY+0` (CP1251 bytes).
static const char* kDifficultyMarkup =
    "$Z$[\xD3\xF0\xEE\xE2\xE5\xED\xFC \xF1\xEB\xEE\xE6\xED\xEE\xF1\xF2\xE8$]$N$Z"
    "\xCF\xEE\xE6\xE0\xEB\xF3\xE9\xF1\xF2\xE0, \xE2\xFB\xE1\xE5\xF0\xE8\xF2\xE5 \xF3\xF0\xEE\xE2\xE5\xED\xFC \xF1\xEB\xEE\xE6\xED\xEE\xF1\xF2\xE8.$N"
    "%ia[\xEE\xF7\xE5\xED\xFC \xEB\xE5\xE3\xEA\xE8\xE9]$N"
    "%ia[\xEB\xE5\xE3\xEA\xE8\xE9]$N"
    "%ia[\xED\xEE\xF0\xEC\xE0\xEB\xFC\xED\xFB\xE9]$N"
    "%ia[\xF2\xFF\xE6\xE5\xEB\xFB\xE9]$N"
    "%ia[\xEE\xF7\xE5\xED\xFC \xF2\xFF\xE6\xE5\xEB\xFB\xE9]$N"
    "%in[\xED\xE0\xE7\xE0\xE4]$N";

TEST(CharIntroMarkup, ExtractsDifficultyScreen) {
    CharIntroContent c = ParseDifficultyMarkup(kDifficultyMarkup);
    // Heading + prompt are recovered (exact CP1251 byte strings).
    CHECK_EQ(c.heading, std::string("\xD3\xF0\xEE\xE2\xE5\xED\xFC \xF1\xEB\xEE\xE6\xED\xEE\xF1\xF2\xE8"));
    CHECK(!c.prompt.empty());
    CHECK_EQ(c.prompt, std::string("\xCF\xEE\xE6\xE0\xEB\xF3\xE9\xF1\xF2\xE0, \xE2\xFB\xE1\xE5\xF0\xE8\xF2\xE5 \xF3\xF0\xEE\xE2\xE5\xED\xFC \xF1\xEB\xEE\xE6\xED\xEE\xF1\xF2\xE8."));
    // Six options: five selectable difficulty rows + a trailing non-selecting "back".
    CHECK_EQ((int)c.options.size(), 6);
    CHECK_EQ((int)c.selectable.size(), 6);
    for (int i = 0; i < 5; ++i) CHECK(c.selectable[i]);   // %ia
    CHECK(!c.selectable[5]);                              // %in (back)
    CHECK_EQ(c.options[0], std::string("\xEE\xF7\xE5\xED\xFC \xEB\xE5\xE3\xEA\xE8\xE9"));  // очень легкий
    CHECK_EQ(c.options[2], std::string("\xED\xEE\xF0\xEC\xE0\xEB\xFC\xED\xFB\xE9"));        // нормальный
    CHECK_EQ(c.options[5], std::string("\xED\xE0\xE7\xE0\xE4"));                            // назад
}

// A minimal ASCII markup parses too (parser is content-agnostic).
TEST(CharIntroMarkup, AsciiSmoke) {
    CharIntroContent c = ParseDifficultyMarkup("$[Title$]$NPick:$N%ia[A]$N%ia[B]$N%in[back]$N");
    CHECK_EQ(c.heading, std::string("Title"));
    CHECK_EQ(c.prompt, std::string("Pick:"));
    CHECK_EQ((int)c.options.size(), 3);
    CHECK(c.selectable[0]); CHECK(c.selectable[1]); CHECK(!c.selectable[2]);
    CHECK_EQ(c.options[2], std::string("back"));
}
