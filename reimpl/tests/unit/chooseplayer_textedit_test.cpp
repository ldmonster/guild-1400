// ApplyTextEdit — the native player wizard's text-field edit step (SDL text-input +
// Backspace). Pure + deterministic.
#include "tests/framework/test.h"
#include "play/sdl_chooseplayer_screen.h"
#include <string>
using namespace guild;
using namespace guild::play;
TEST(ChoosePlayerTextEdit, AppendsPrintable) {
    std::string b; ApplyTextEdit(b, "Han", false); CHECK_EQ(b, std::string("Han"));
    ApplyTextEdit(b, "s", false); CHECK_EQ(b, std::string("Hans"));
}
TEST(ChoosePlayerTextEdit, BackspacePops) {
    std::string b = "Hans"; ApplyTextEdit(b, "", true); CHECK_EQ(b, std::string("Han"));
    ApplyTextEdit(b, "", true); ApplyTextEdit(b, "", true); ApplyTextEdit(b, "", true);
    CHECK(b.empty()); ApplyTextEdit(b, "", true); CHECK(b.empty());   // no underflow
}
TEST(ChoosePlayerTextEdit, FiltersControlAndCaps) {
    std::string b; ApplyTextEdit(b, "\n\tA\x01""B", false); CHECK_EQ(b, std::string("AB"));
    std::string c; for (int i=0;i<40;++i) ApplyTextEdit(c, "x", false, 8); CHECK_EQ((int)c.size(), 8);
}

// ---- HARDENING (wave-12): the text-entry buffer-bounds envelope ----

// An overlong paste in a single call must clamp at maxLen (never grow past it).
TEST(ChoosePlayerTextEdit, OverlongSinglePasteClamps) {
    std::string b;
    std::size_t n = ApplyTextEdit(b, std::string(1000, 'A'), false, 31);
    CHECK_EQ((int)b.size(), 31);
    CHECK_EQ((int)n, 31);
    // A further paste on a full buffer adds nothing.
    n = ApplyTextEdit(b, "BBBB", false, 31);
    CHECK_EQ((int)b.size(), 31);
    CHECK_EQ((int)n, 31);
}

// maxLen == 0 (degenerate cap) accepts nothing and never underflows on backspace.
TEST(ChoosePlayerTextEdit, ZeroMaxLenAcceptsNothing) {
    std::string b;
    CHECK_EQ((int)ApplyTextEdit(b, "abc", false, 0), 0);
    CHECK(b.empty());
    CHECK_EQ((int)ApplyTextEdit(b, "", true, 0), 0);   // backspace on empty
    CHECK(b.empty());
}

// Embedded NUL bytes (NUL-less-string guard): a NUL is a control byte (< 0x20)
// and must be filtered, so the resulting std::string is never NUL-injected.
TEST(ChoosePlayerTextEdit, EmbeddedNulFiltered) {
    std::string typed; typed.push_back('A'); typed.push_back('\0'); typed.push_back('B');
    std::string b;
    ApplyTextEdit(b, typed, false);
    CHECK_EQ(b, std::string("AB"));
    CHECK_EQ((int)b.size(), 2);
}

// High Latin-1 bytes (>= 0x80) are printable and accepted (the original keeps the
// full byte range above space); they must still respect the cap.
TEST(ChoosePlayerTextEdit, HighBytesAcceptedAndCapped) {
    std::string typed(50, (char)0xC4);   // 'Ä' in Latin-1
    std::string b;
    ApplyTextEdit(b, typed, false, 16);
    CHECK_EQ((int)b.size(), 16);
}

// Append-then-overlong-backspace storm: many backspaces past empty never underflow.
TEST(ChoosePlayerTextEdit, BackspaceStormNoUnderflow) {
    std::string b = "Hi";
    for (int i = 0; i < 100; ++i) ApplyTextEdit(b, "", true);
    CHECK(b.empty());
}
