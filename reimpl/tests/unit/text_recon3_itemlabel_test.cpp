#include "tests/framework/test.h"
#include "play/text_recon3_itemlabel.h"

#include <cstring>

using namespace guild;
using namespace guild::play;

namespace {

// Build a synthetic 536-byte record. Name fields are stored as narrow ASCII
// (the original wide fields with zero high bytes behave identically under the
// byte-string sprintf / strlen paths exercised here).
struct Rec {
    u8 b[536];
    Rec() { std::memset(b, 0, sizeof b); }
    void name48(const char* s) { std::strcpy(reinterpret_cast<char*>(b) + 48, s); }
    void name64(const char* s) { std::strcpy(reinterpret_cast<char*>(b) + 64, s); }
    void set(int off, u8 v) { b[off] = v; }
};

// Resolver that returns a fixed string and records the last id it saw.
int g_last_id = -1;
const char* StubResolve(int id, char* scratch) {
    g_last_id = id;
    std::strcpy(scratch, "TTL");
    return scratch;
}

ItemLabelHooks WithResolver() {
    ItemLabelHooks h;
    h.resolveStringField = StubResolve;
    return h;
}

} // namespace

TEST(TextRecon3, WideCopyNarrowBytes) {
    // Narrow byte string "Ab" = {'A','b',0}. The original loop copies the low
    // byte, and (when non-zero) the next byte as the "high byte", advancing by 2
    // and continuing while that byte is non-zero. So 'A'(lo),'b'(hi),then 0 stops.
    char dst[16];
    std::memset(dst, 0x7f, sizeof dst);
    char* r = WideCopy(dst, "Ab");
    CHECK_EQ(r, dst);
    CHECK_EQ(dst[0], 'A');
    CHECK_EQ(dst[1], 'b');
    CHECK_EQ(dst[2], 0);
}

TEST(TextRecon3, WideCopyTrueWideStopsAtZeroHighByte) {
    // True UTF-16LE 'A' = {'A',0,...}. low='A' copied; high byte 0 copied to
    // dst[1]; while(0) stops. Only one code unit (the ASCII char + its 0 high
    // byte) is emitted -- this is the faithful original behavior.
    const char src[] = {'A', 0, 'b', 0, 0, 0};
    char dst[16];
    std::memset(dst, 0x7f, sizeof dst);
    WideCopy(dst, src);
    CHECK_EQ(dst[0], 'A');
    CHECK_EQ(dst[1], 0);
    // dst[2] was never written (loop stopped after one code unit).
    CHECK_EQ(dst[2], 0x7f);
}

TEST(TextRecon3, OutOfRangeReturnsZero) {
    char out[64];
    std::memset(out, 0x7f, sizeof out);
    int r = FormatItemLabelWithIcon(0x300, 0, out, 1, nullptr, false);
    CHECK_EQ(r, 0);
    CHECK_EQ(out[0], 0);  // inert out-of-range label is empty wide string
}

TEST(TextRecon3, DefaultRecordPathReturnsOne) {
    char out[64];
    std::memset(out, 0x7f, sizeof out);
    // record==nullptr -> default-record branch
    int r = FormatItemLabelWithIcon(5, 0, out, 1, nullptr, false);
    CHECK_EQ(r, 1);
    CHECK_EQ(out[0], 0);  // inert default-lang label empty
}

TEST(TextRecon3, Kind1CopiesName48) {
    Rec rec;
    rec.name48("Hammer");
    char out[64];
    int r = FormatItemLabelWithIcon(5, 0 /*mode!=2*/, out, 1, rec.b, false);
    CHECK_EQ(r, 1);
    CHECK(std::strcmp(out, "Hammer") == 0);
}

TEST(TextRecon3, Kind2NoTitlePlainName) {
    Rec rec;
    rec.name48("Brot");
    rec.set(13, 0);   // no title id
    char out[64];
    int r = FormatItemLabelWithIcon(5, 0, out, 2, rec.b, false);
    CHECK_EQ(r, 1);
    CHECK(std::strcmp(out, "Brot") == 0);
}

TEST(TextRecon3, Kind2WithTitleInsertsIconSeparator) {
    Rec rec;
    rec.name48("Klaus");
    rec.set(13, 4);   // title id present
    rec.set(9, 0);    // base 272
    char out[64];
    g_last_id = -1;
    auto prev = SetItemLabelHooks(WithResolver());
    int r = FormatItemLabelWithIcon(5, 0, out, 2, rec.b, false);
    SetItemLabelHooks(prev);
    CHECK_EQ(r, 1);
    CHECK_EQ(g_last_id, 4 + 272);            // base 272 (byte9==0)
    CHECK(std::strcmp(out, "TTL$AKlaus") == 0);
}

TEST(TextRecon3, Kind2TitleBaseSwitchesOnByte9) {
    Rec rec;
    rec.name48("X");
    rec.set(13, 7);
    rec.set(9, 1);    // base 279
    char out[64];
    g_last_id = -1;
    auto prev = SetItemLabelHooks(WithResolver());
    FormatItemLabelWithIcon(5, 0, out, 2, rec.b, false);
    SetItemLabelHooks(prev);
    CHECK_EQ(g_last_id, 7 + 279);
}

TEST(TextRecon3, Kind3TitleWithTwoNames) {
    Rec rec;
    rec.name48("von");
    rec.name64("Berg");
    rec.set(358, 3);
    rec.set(64, 'B');   // name64 non-empty (first byte)
    rec.set(9, 0);      // base 525
    char out[64];
    g_last_id = -1;
    auto prev = SetItemLabelHooks(WithResolver());
    int r = FormatItemLabelWithIcon(5, 0 /*mode!=2*/, out, 3, rec.b, false);
    SetItemLabelHooks(prev);
    CHECK_EQ(r, 1);
    CHECK_EQ(g_last_id, 3 + 525);
    CHECK(std::strcmp(out, "TTL von Berg") == 0);
}

TEST(TextRecon3, Kind3NoTitleTwoNames) {
    Rec rec;
    rec.name48("Hans");
    rec.name64("Meier");
    rec.set(358, 0);
    rec.set(64, 'M');
    char out[64];
    int r = FormatItemLabelWithIcon(5, 0, out, 3, rec.b, false);
    CHECK_EQ(r, 1);
    CHECK(std::strcmp(out, "Hans Meier") == 0);
}

TEST(TextRecon3, Kind4PluralAppendsSWhenNotBlocker) {
    Rec rec;
    rec.name48("Brot");   // ends 't' -> not a blocker -> append 's'
    rec.set(356, 0);
    rec.set(357, 0);
    char out[64];
    int r = FormatItemLabelWithIcon(5, 2 /*mode==2*/, out, 4, rec.b, false);
    CHECK_EQ(r, 1);
    CHECK(std::strcmp(out, "Brots") == 0);
}

TEST(TextRecon3, Kind4PluralNoSWhenBlocker) {
    Rec rec;
    rec.name48("Glas");   // ends 's' -> blocker -> no 's'
    rec.set(356, 0);
    rec.set(357, 0);
    char out[64];
    int r = FormatItemLabelWithIcon(5, 2, out, 4, rec.b, false);
    CHECK_EQ(r, 1);
    CHECK(std::strcmp(out, "Glas") == 0);
}

// --- kind-4 title-base golden pins (0x59d14a). The four bases are recovered from
// the decompile: B(356) branch -> 370 (byte9!=0) / 294 (byte9==0); B(357) branch ->
// 498 (byte9!=0) / 471 (byte9==0). The resolved id is byte (record+356)/(record+357)
// plus the base. Values traced to text_recon3_itemlabel.cpp case 4. ---
TEST(TextRecon3, Kind4Title356Base294Byte9Zero) {
    Rec rec;
    rec.name48("Brot");
    rec.set(356, 11);   // declension id byte
    rec.set(9, 0);      // base 294
    char out[64];
    g_last_id = -1;
    auto prev = SetItemLabelHooks(WithResolver());
    int r = FormatItemLabelWithIcon(5, 0, out, 4, rec.b, false);
    SetItemLabelHooks(prev);
    CHECK_EQ(r, 1);
    CHECK_EQ(g_last_id, 11 + 294);
    CHECK(std::strcmp(out, "TTL$ABrot") == 0);
}

TEST(TextRecon3, Kind4Title356Base370Byte9NonZero) {
    Rec rec;
    rec.name48("Brot");
    rec.set(356, 4);
    rec.set(9, 1);      // base 370
    char out[64];
    g_last_id = -1;
    auto prev = SetItemLabelHooks(WithResolver());
    FormatItemLabelWithIcon(5, 0, out, 4, rec.b, false);
    SetItemLabelHooks(prev);
    CHECK_EQ(g_last_id, 4 + 370);
}

TEST(TextRecon3, Kind4Title357Base471Byte9Zero) {
    Rec rec;
    rec.name48("Brot");
    rec.set(356, 0);    // first branch off
    rec.set(357, 7);    // second branch declension id
    rec.set(9, 0);      // base 471
    char out[64];
    g_last_id = -1;
    auto prev = SetItemLabelHooks(WithResolver());
    FormatItemLabelWithIcon(5, 0, out, 4, rec.b, false);
    SetItemLabelHooks(prev);
    CHECK_EQ(g_last_id, 7 + 471);
}

TEST(TextRecon3, Kind4Title357Base498Byte9NonZero) {
    Rec rec;
    rec.name48("Brot");
    rec.set(356, 0);
    rec.set(357, 3);
    rec.set(9, 5);      // byte9 != 0 -> base 498
    char out[64];
    g_last_id = -1;
    auto prev = SetItemLabelHooks(WithResolver());
    FormatItemLabelWithIcon(5, 0, out, 4, rec.b, false);
    SetItemLabelHooks(prev);
    CHECK_EQ(g_last_id, 3 + 498);
}

// kind-3 title base 560 (byte9 != 0). Complements the existing 525 pin (line ~142).
TEST(TextRecon3, Kind3Title560Byte9NonZero) {
    Rec rec;
    rec.name48("First");
    rec.set(64, 0);
    rec.set(358, 6);    // declension title present
    rec.set(9, 1);      // base 560
    char out[64];
    g_last_id = -1;
    auto prev = SetItemLabelHooks(WithResolver());
    FormatItemLabelWithIcon(5, 0, out, 3, rec.b, false);
    SetItemLabelHooks(prev);
    CHECK_EQ(g_last_id, 6 + 560);
}

// kind-8 profession-title base 279 (byte9 != 0, byte13 present). Complements the
// existing kind-8 declension 525 pin (line ~255).
TEST(TextRecon3, Kind8ProfessionTitleBase279Byte9NonZero) {
    Rec rec;
    rec.name48("Korn");
    rec.set(13, 5);      // profession title present
    rec.set(358, 0);     // no declension title -> uses v62 (profession) branch
    rec.set(9, 1);       // base 279
    char out[64];
    g_last_id = -1;
    auto prev = SetItemLabelHooks(WithResolver());
    FormatItemLabelWithIcon(5, 0, out, 8, rec.b, false);
    SetItemLabelHooks(prev);
    CHECK_EQ(g_last_id, 5 + 279);
}

TEST(TextRecon3, Kind6PrefersName64) {
    Rec rec;
    rec.name48("First");
    rec.name64("Second");
    rec.set(64, 'S');
    char out[64];
    FormatItemLabelWithIcon(5, 0, out, 6, rec.b, false);
    CHECK(std::strcmp(out, "Second") == 0);
}

TEST(TextRecon3, Kind6FallsBackToName48) {
    Rec rec;
    rec.name48("Only48");
    rec.set(64, 0);   // name64 empty
    char out[64];
    FormatItemLabelWithIcon(5, 0, out, 6, rec.b, false);
    CHECK(std::strcmp(out, "Only48") == 0);
}

TEST(TextRecon3, Kind7WithBothNamesIcon) {
    Rec rec;
    rec.name48("Title");
    rec.name64("Name");
    rec.set(64, 'N');
    char out[64];
    FormatItemLabelWithIcon(5, 0 /*mode!=2*/, out, 7, rec.b, false);
    CHECK(std::strcmp(out, "Title$AName") == 0);
}

TEST(TextRecon3, Kind7PluralOnName64) {
    Rec rec;
    rec.name48("Apfel");
    rec.name64("Birne");   // ends 'e' -> not blocker -> append 's'
    rec.set(64, 'B');
    char out[64];
    FormatItemLabelWithIcon(5, 2, out, 7, rec.b, false);
    CHECK(std::strcmp(out, "Apfel$ABirnes") == 0);
}

TEST(TextRecon3, Kind7PluralBlockerNoS) {
    Rec rec;
    rec.name48("Apfel");
    rec.name64("Glas");    // ends 's' -> blocker -> no 's'
    rec.set(64, 'G');
    char out[64];
    FormatItemLabelWithIcon(5, 2, out, 7, rec.b, false);
    CHECK(std::strcmp(out, "Apfel$AGlas") == 0);
}

TEST(TextRecon3, Kind5ResolvesTitleOnly) {
    Rec rec;
    rec.set(358, 9);
    rec.set(9, 0);   // base 525
    char out[64];
    g_last_id = -1;
    auto prev = SetItemLabelHooks(WithResolver());
    int r = FormatItemLabelWithIcon(5, 0, out, 5, rec.b, false);
    SetItemLabelHooks(prev);
    CHECK_EQ(r, 1);
    CHECK_EQ(g_last_id, 9 + 525);
    CHECK(std::strcmp(out, "TTL") == 0);
}

TEST(TextRecon3, Kind8TitleAndName) {
    Rec rec;
    rec.name48("Korn");      // ends 'n' -> not blocker -> name24 gets 's'
    rec.set(13, 0);          // no profession title
    rec.set(358, 2);         // declension title present
    rec.set(9, 0);           // base 525
    char out[64];
    g_last_id = -1;
    auto prev = SetItemLabelHooks(WithResolver());
    int r = FormatItemLabelWithIcon(5, 2 /*mode==2*/, out, 8, rec.b, false);
    SetItemLabelHooks(prev);
    CHECK_EQ(r, 1);
    CHECK_EQ(g_last_id, 2 + 525);
    // v63 = "TTL"; v61 = "Korns" (mode==2, 'n' not blocker); output "v63$Av61".
    CHECK(std::strcmp(out, "TTL$AKorns") == 0);
}

TEST(TextRecon3, DefaultKindReturnsOne) {
    Rec rec;
    char out[64];
    std::memset(out, 0x7f, sizeof out);
    int r = FormatItemLabelWithIcon(5, 0, out, 99 /*unhandled*/, rec.b, false);
    CHECK_EQ(r, 1);   // default switch arm just returns 1, leaves out untouched
}

// ---------------------------------------------------------------------------
// HARDENING (wave-12): a malformed record whose name field has NO NUL terminator
// inside the 536-byte entry must NOT cause strlen / WideCopy / sprintf to read
// past the record (the original VIBE_Text_FormatItemLabelWithIcon assumed an
// in-record NUL). Field reads are now bounded to the 536-byte record extent.
// On well-formed input the output is byte-identical (the goldens above), so these
// only assert the malformed paths complete without an OOB read (caught by ASAN).
// ---------------------------------------------------------------------------
namespace {
// Fill the whole record from `from` to the 536-byte end with non-NUL bytes.
void FillNoNul(Rec& rec, int from) {
    for (int i = from; i < 536; ++i) rec.b[i] = 'A';
}
} // namespace

TEST(TextRecon3, Kind1NulLessName48NoOOB) {
    Rec rec;
    FillNoNul(rec, 48);              // name48 runs to the record end, no NUL
    char out[1024];
    std::memset(out, 0, sizeof out);
    int r = FormatItemLabelWithIcon(5, 0 /*mode!=2*/, out, 1, rec.b, false);
    CHECK_EQ(r, 1);
    // The bounded field caps at the record end: at most (536-48)=488 'A' chars.
    CHECK((int)std::strlen(out) <= 488);
}

TEST(TextRecon3, Kind2NulLessName48SprintfNoOOB) {
    Rec rec;
    FillNoNul(rec, 48);
    rec.set(13, 0);                 // no title -> sprintf(out,"%s",name48)
    char out[1024];
    std::memset(out, 0, sizeof out);
    int r = FormatItemLabelWithIcon(5, 0, out, 2, rec.b, false);
    CHECK_EQ(r, 1);
    CHECK((int)std::strlen(out) <= 488);
}

TEST(TextRecon3, Kind4PluralNulLessFieldEndCharNoOOB) {
    // mode==2 kind4 reads FieldEndChar(record,48), which does
    // record[47 + strlen(record+48)] — bounded so it cannot read past the record.
    Rec rec;
    FillNoNul(rec, 48);
    rec.set(356, 0);
    rec.set(357, 0);
    char out[2048];
    std::memset(out, 0, sizeof out);
    int r = FormatItemLabelWithIcon(5, 2, out, 4, rec.b, false);
    CHECK_EQ(r, 1);
}

TEST(TextRecon3, Kind3NulLessName64NoOOB) {
    Rec rec;
    rec.name48("Hans");
    rec.set(358, 0);
    rec.set(64, 'M');
    FillNoNul(rec, 64);             // name64 runs to the end, no NUL (overwrites 'M')
    char out[2048];
    std::memset(out, 0, sizeof out);
    int r = FormatItemLabelWithIcon(5, 0, out, 3, rec.b, false);
    CHECK_EQ(r, 1);
}

TEST(TextRecon3, ManyKindsNulLessRecordNoOOB) {
    // Sweep every label flavour over a fully NUL-less record (all fields run to
    // the record end). None may over-read.
    for (char kind = 1; kind <= 8; ++kind) {
        Rec rec;
        FillNoNul(rec, 9);          // even the title/flag bytes are non-zero
        char out[4096];
        std::memset(out, 0, sizeof out);
        auto prev = SetItemLabelHooks(WithResolver());
        int r = FormatItemLabelWithIcon(5, 2 /*plural on*/, out, kind, rec.b, false);
        SetItemLabelHooks(prev);
        CHECK_EQ(r, 1);
    }
}
