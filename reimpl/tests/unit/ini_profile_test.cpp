// Golden tests for guild::io::IniProfile — the Win32 GetPrivateProfileStringA /
// GetPrivateProfileIntA / WritePrivateProfileStringA semantics the game relies
// on (callers: VIBE_AiMethod_RegisterFromIni @0x468f6c,
// VIBE_Config_ReadGfxAndSoundSettings @0x56b834, etc.). Headless: exercises the
// in-memory parsing core, no filesystem.

#include "tests/framework/test.h"
#include "src/io/ini_profile.h"

#include <cstring>
#include <string>

using guild::io::IniProfile;

namespace {

// A representative .ini mirroring the shapes the game uses (Config-style numeric
// knobs, the AI method [section] with ShortDesire/ShortChange keys, quoted and
// whitespace-padded values, comments, case variation).
const char* kSample =
    "; leading comment\r\n"
    "[Gfx]\r\n"
    "texture_scale = 2\r\n"
    "details=1\r\n"
    "  character_detail   =   0  \r\n"
    "stadt = \"Augsburg\"\r\n"
    "neg = -5\r\n"
    "trailing_int = 42abc\r\n"
    "\r\n"
    "# another comment\r\n"
    "[Sound]\r\n"
    "master_vol=100\r\n"
    "empty=\r\n"
    "[Method1]\r\n"
    "ShortDesire1 = Hunger\r\n"
    "ShortChange1 = 1.5\r\n";

// Helper: run getString and return result string + reported count.
struct StrRes { std::string s; guild::u32 n; bool nulTerminated; };

StrRes getStr(const IniProfile& ini, const char* sec, const char* key,
              const char* def, guild::u32 cap) {
    char buf[256];
    std::memset(buf, '\xAA', sizeof(buf));  // poison to catch missing NUL
    guild::u32 n = ini.getString(sec, key, def, buf, cap);
    StrRes r;
    r.n = n;
    r.nulTerminated = (cap == 0) ? true : (buf[n] == '\0');
    r.s = (cap == 0) ? std::string() : std::string(buf, n);
    return r;
}

} // namespace

TEST(IniProfile, BasicSectionKeyLookup) {
    IniProfile ini;
    ini.parse(kSample);
    StrRes r = getStr(ini, "Gfx", "texture_scale", "X", 64);
    CHECK_EQ(r.s, std::string("2"));
    CHECK_EQ(r.n, 1u);
    CHECK(r.nulTerminated);
    StrRes r2 = getStr(ini, "Sound", "master_vol", "X", 64);
    CHECK_EQ(r2.s, std::string("100"));
}

TEST(IniProfile, DefaultFallbackMissingKeyAndSection) {
    IniProfile ini;
    ini.parse(kSample);
    // Missing key in an existing section -> default.
    StrRes r = getStr(ini, "Gfx", "does_not_exist", "fallback", 64);
    CHECK_EQ(r.s, std::string("fallback"));
    CHECK_EQ(r.n, 8u);
    // Missing section entirely -> default.
    StrRes r2 = getStr(ini, "NoSuchSection", "k", "def2", 64);
    CHECK_EQ(r2.s, std::string("def2"));
    // NULL default -> empty string, count 0.
    StrRes r3 = getStr(ini, "Gfx", "nope", nullptr, 64);
    CHECK_EQ(r3.s, std::string());
    CHECK_EQ(r3.n, 0u);
    CHECK(r3.nulTerminated);
}

TEST(IniProfile, WhitespaceAndQuoteTrimming) {
    IniProfile ini;
    ini.parse(kSample);
    // Key & value both padded with spaces -> trimmed.
    StrRes r = getStr(ini, "Gfx", "character_detail", "X", 64);
    CHECK_EQ(r.s, std::string("0"));
    // Surrounding double-quotes stripped.
    StrRes r2 = getStr(ini, "Gfx", "stadt", "X", 64);
    CHECK_EQ(r2.s, std::string("Augsburg"));
    CHECK_EQ(r2.n, 8u);
    // Empty value (key= with nothing) -> empty string, count 0.
    StrRes r3 = getStr(ini, "Sound", "empty", "DEF", 64);
    CHECK_EQ(r3.s, std::string());
    CHECK_EQ(r3.n, 0u);
}

TEST(IniProfile, CaseInsensitiveSectionAndKey) {
    IniProfile ini;
    ini.parse(kSample);
    StrRes r = getStr(ini, "gfx", "TEXTURE_SCALE", "X", 64);
    CHECK_EQ(r.s, std::string("2"));
    StrRes r2 = getStr(ini, "SOUND", "Master_Vol", "X", 64);
    CHECK_EQ(r2.s, std::string("100"));
}

TEST(IniProfile, BufferTruncationAndReturnCount) {
    IniProfile ini;
    ini.parse(kSample);
    // "Augsburg" is 8 chars. cap=5 -> 4 chars + NUL, return 4.
    StrRes r = getStr(ini, "Gfx", "stadt", "X", 5);
    CHECK_EQ(r.n, 4u);
    CHECK_EQ(r.s, std::string("Augs"));
    CHECK(r.nulTerminated);
    // cap=1 -> 0 chars + NUL, return 0.
    StrRes r1 = getStr(ini, "Gfx", "stadt", "X", 1);
    CHECK_EQ(r1.n, 0u);
    CHECK(r1.nulTerminated);
    // cap=0 -> nothing written, return 0.
    char dummy[4] = {1, 1, 1, 1};
    guild::u32 n0 = ini.getString("Gfx", "stadt", "X", dummy, 0);
    CHECK_EQ(n0, 0u);
    CHECK_EQ(dummy[0], (char)1);  // untouched
    // Default also truncates: cap=4, default "fallback" (8) -> 3 + NUL.
    StrRes rd = getStr(ini, "Gfx", "missing", "fallback", 4);
    CHECK_EQ(rd.n, 3u);
    CHECK_EQ(rd.s, std::string("fal"));
}

TEST(IniProfile, GetIntParsing) {
    IniProfile ini;
    ini.parse(kSample);
    CHECK_EQ(ini.getInt("Gfx", "texture_scale", -1), 2);
    CHECK_EQ(ini.getInt("Gfx", "details", -1), 1);
    CHECK_EQ(ini.getInt("Sound", "master_vol", -1), 100);
    // Negative value.
    CHECK_EQ(ini.getInt("Gfx", "neg", 999), -5);
    // Leading digits then garbage -> parse leading int (kernel32 behavior).
    CHECK_EQ(ini.getInt("Gfx", "trailing_int", -1), 42);
    // Missing key -> default.
    CHECK_EQ(ini.getInt("Gfx", "nope", 7), 7);
    // Missing section -> default.
    CHECK_EQ(ini.getInt("Nope", "x", 13), 13);
    // Empty value -> default (GetPrivateProfileString returns 0 for empty value,
    // so GetPrivateProfileInt returns the default). kernel32 GetPrivateProfileIntW.
    CHECK_EQ(ini.getInt("Sound", "empty", 55), 55);
    // Present, non-empty, non-numeric value "Augsburg" -> RtlUnicodeStringToInteger
    // sees 'A' (>= base 10) immediately -> 0. kernel32 returns 0, NOT the default,
    // because GetPrivateProfileString returned nonzero (key present, value non-empty).
    CHECK_EQ(ini.getInt("Gfx", "stadt", 88), 0);
    // Case-insensitive.
    CHECK_EQ(ini.getInt("GFX", "DETAILS", -1), 1);
}

TEST(IniProfile, GetIntBaseAutoDetect) {
    // kernel32 GetPrivateProfileIntW -> RtlUnicodeStringToInteger(base 0).
    // 0x prefix -> hex; 0o -> octal; 0b -> binary; a bare leading 0 stays base 10
    // (NOT C-style octal). Stops at first invalid/over-base digit.
    IniProfile ini;
    ini.parse("[N]\n"
              "h=0x1F\n"
              "h2=0xff\n"
              "oct=0o17\n"
              "bin=0b101\n"
              "leadzero=010\n"      // base 10 -> 10, not octal 8
              "hexstop=0x1Gz\n"     // 0x1, then 'G' (>=16) stops -> 1
              "plus=+7\n"
              "spaced=   12\n");    // leading whitespace skipped
    CHECK_EQ(ini.getInt("N", "h", -1), 31);     // 0x1F
    CHECK_EQ(ini.getInt("N", "h2", -1), 255);   // 0xff (lowercase)
    CHECK_EQ(ini.getInt("N", "oct", -1), 15);   // 0o17 = 15
    CHECK_EQ(ini.getInt("N", "bin", -1), 5);    // 0b101 = 5
    CHECK_EQ(ini.getInt("N", "leadzero", -1), 10);  // base 10, not 8
    CHECK_EQ(ini.getInt("N", "hexstop", -1), 1);    // 0x1 then stop
    CHECK_EQ(ini.getInt("N", "plus", -1), 7);
    CHECK_EQ(ini.getInt("N", "spaced", -1), 12);
}

TEST(IniProfile, GetIntNegativeWrapAndJustZeroX) {
    IniProfile ini;
    // "-5" -> 0u-5 == 0xFFFFFFFB -> as int -5.
    ini.parse("[N]\nneg=-5\njustx=0x\nlone=-\n");
    CHECK_EQ(ini.getInt("N", "neg", 999), -5);
    // "0x" with no hex digits -> total stays 0 -> 0 (present non-empty).
    CHECK_EQ(ini.getInt("N", "justx", 77), 0);
    // "-" alone -> minus, no digits -> 0u-0 == 0.
    CHECK_EQ(ini.getInt("N", "lone", 77), 0);
}

TEST(IniProfile, HashIsNotACommentChar) {
    // kernel32: only ';' starts a comment. A '#'-led line is a normal line and is
    // dropped only because it lacks '='. A key whose value contains '#' keeps it.
    IniProfile ini;
    ini.parse("[S]\n"
              "#notacomment=should_be_ignored_no_eq? no it has eq\n"
              "real=value#withhash\n"
              ";realcomment=skipme\n");
    // The '#notacomment=...' line HAS an '=' so it is parsed as a key named
    // "#notacomment" (kernel32 does not special-case '#').
    StrRes r = getStr(ini, "S", "#notacomment", "MISS", 64);
    CHECK_EQ(r.s, std::string("should_be_ignored_no_eq? no it has eq"));
    // '#' inside a value is preserved.
    CHECK_EQ(getStr(ini, "S", "real", "X", 64).s, std::string("value#withhash"));
    // ';' IS a comment -> that key does not exist.
    StrRes rc = getStr(ini, "S", ";realcomment", "MISS", 64);
    CHECK_EQ(rc.s, std::string("MISS"));
}

TEST(IniProfile, SingleQuoteStripping) {
    // kernel32 PROFILE_CopyEntry strips a single matching pair of EITHER ' or ".
    IniProfile ini;
    ini.parse("[S]\n"
              "sq='hello'\n"
              "dq=\"world\"\n"
              "mixed='mismatch\"\n"   // first ' , last " -> no strip
              "sqempty=''\n");
    CHECK_EQ(getStr(ini, "S", "sq", "X", 32).s, std::string("hello"));
    CHECK_EQ(getStr(ini, "S", "dq", "X", 32).s, std::string("world"));
    CHECK_EQ(getStr(ini, "S", "mixed", "X", 32).s, std::string("'mismatch\""));
    CHECK_EQ(getStr(ini, "S", "sqempty", "X", 32).s, std::string());
}

TEST(IniProfile, EnumerateKeyNamesNullKey) {
    IniProfile ini;
    ini.parse(kSample);
    char buf[256];
    std::memset(buf, '\xAA', sizeof(buf));
    guild::u32 n = ini.getString("Method1", nullptr, "", buf, sizeof(buf));
    // Two keys: "ShortDesire1\0" "ShortChange1\0" then final "\0".
    const char* expect = "ShortDesire1\0ShortChange1";
    // total length of the two names + the two embedded NULs:
    // 12 + 1 + 12 + 1 = 26, return excludes the FINAL terminating NUL.
    CHECK_EQ(n, 26u);
    CHECK_EQ(std::memcmp(buf, "ShortDesire1\0ShortChange1\0", 26), 0);
    (void)expect;
    // double-NUL terminated: buf[n] is the final NUL.
    CHECK_EQ(buf[n], '\0');
    CHECK_EQ(buf[25], '\0');  // NUL after the second key
}

TEST(IniProfile, EnumerateSectionNamesNullSection) {
    IniProfile ini;
    ini.parse(kSample);
    char buf[256];
    std::memset(buf, '\xAA', sizeof(buf));
    guild::u32 n = ini.getString(nullptr, nullptr, "", buf, sizeof(buf));
    // Sections in file order: Gfx, Sound, Method1 (implicit unnamed excluded).
    CHECK_EQ(std::memcmp(buf, "Gfx\0Sound\0Method1\0", 18), 0);
    // 3+1 + 5+1 + 7+1 = 18, return excludes final NUL.
    CHECK_EQ(n, 18u);
    CHECK_EQ(buf[n], '\0');
}

TEST(IniProfile, EnumerationTruncation) {
    IniProfile ini;
    ini.parse(kSample);
    char buf[8];
    std::memset(buf, '\xAA', sizeof(buf));
    // Section names "Gfx\0Sound..." truncated into 8 bytes; must stay
    // double-NUL terminated within capacity.
    guild::u32 n = ini.getString(nullptr, nullptr, "", buf, 8);
    CHECK(n < 8u);
    CHECK_EQ(buf[n], '\0');         // final NUL present
    CHECK_EQ(std::memcmp(buf, "Gfx\0", 4), 0);  // first name intact
}

TEST(IniProfile, KeysBeforeAnySectionNotReachable) {
    // kernel32: keys with no [section] are not returned by a named query.
    IniProfile ini;
    ini.parse("orphan=1\r\n[Real]\r\nk=v\r\n");
    StrRes r = getStr(ini, "Real", "k", "X", 64);
    CHECK_EQ(r.s, std::string("v"));
    // The unnamed section is not enumerable as a section name.
    char buf[64];
    ini.getString(nullptr, nullptr, "", buf, sizeof(buf));
    CHECK_EQ(std::memcmp(buf, "Real\0", 5), 0);
}

TEST(IniProfile, WriteSetInsertDelete) {
    IniProfile ini;
    ini.parse(kSample);
    // Overwrite existing.
    CHECK(ini.setString("Gfx", "texture_scale", "9"));
    CHECK_EQ(ini.getInt("Gfx", "texture_scale", -1), 9);
    // Insert new key into existing section.
    CHECK(ini.setString("Gfx", "new_key", "hello"));
    StrRes r = getStr(ini, "Gfx", "new_key", "X", 64);
    CHECK_EQ(r.s, std::string("hello"));
    // Insert into a brand-new section.
    CHECK(ini.setString("Brand", "k", "v"));
    StrRes r2 = getStr(ini, "Brand", "k", "X", 64);
    CHECK_EQ(r2.s, std::string("v"));
    // Delete a key (value == nullptr).
    CHECK(ini.setString("Gfx", "details", nullptr));
    StrRes r3 = getStr(ini, "Gfx", "details", "GONE", 64);
    CHECK_EQ(r3.s, std::string("GONE"));
    // Delete a whole section (key == nullptr).
    CHECK(ini.setString("Sound", nullptr, nullptr));
    StrRes r4 = getStr(ini, "Sound", "master_vol", "GONE", 64);
    CHECK_EQ(r4.s, std::string("GONE"));
}

TEST(IniProfile, SerializeRoundTrip) {
    IniProfile ini;
    ini.parse("[A]\r\nx=1\r\ny=\"q\"\r\n[B]\r\nz=hi\r\n");
    std::string out = ini.serialize();
    // Re-parse the serialized output; values must survive (quotes were stripped
    // on first parse, so y is round-tripped as bare q).
    IniProfile ini2;
    ini2.parse(out);
    StrRes rx = getStr(ini2, "A", "x", "?", 32);
    CHECK_EQ(rx.s, std::string("1"));
    StrRes ry = getStr(ini2, "A", "y", "?", 32);
    CHECK_EQ(ry.s, std::string("q"));
    StrRes rz = getStr(ini2, "B", "z", "?", 32);
    CHECK_EQ(rz.s, std::string("hi"));
}

TEST(IniProfile, NewlineVariantsAndCommentForms) {
    // Unix \n, lone \r, and ';'/'#' comments all handled.
    IniProfile ini;
    ini.parse("[S]\nk1=a\n; comment\nk2=b\r# hash\rk3=c\r\n");
    CHECK_EQ(getStr(ini, "S", "k1", "X", 16).s, std::string("a"));
    CHECK_EQ(getStr(ini, "S", "k2", "X", 16).s, std::string("b"));
    CHECK_EQ(getStr(ini, "S", "k3", "X", 16).s, std::string("c"));
}

TEST(IniProfile, ValueWithEmbeddedEquals) {
    // kernel32 splits only on the FIRST '='; the rest is part of the value.
    IniProfile ini;
    ini.parse("[S]\nexpr = a=b=c\nurl=http://x?q=1\n");
    CHECK_EQ(getStr(ini, "S", "expr", "X", 32).s, std::string("a=b=c"));
    CHECK_EQ(getStr(ini, "S", "url", "X", 32).s, std::string("http://x?q=1"));
}

TEST(IniProfile, UnbalancedQuotesNotStripped) {
    // Quote strip only when BOTH a leading and trailing '"' are present.
    IniProfile ini;
    ini.parse("[S]\na=\"only_left\nb=only_right\"\nc=\"\"\nd=\"x\"\n");
    // Single leading quote -> kept verbatim.
    CHECK_EQ(getStr(ini, "S", "a", "X", 32).s, std::string("\"only_left"));
    // Single trailing quote -> kept verbatim.
    CHECK_EQ(getStr(ini, "S", "b", "X", 32).s, std::string("only_right\""));
    // Exactly two quotes (empty quoted) -> stripped to empty, count 0.
    StrRes rc = getStr(ini, "S", "c", "X", 32);
    CHECK_EQ(rc.s, std::string());
    CHECK_EQ(rc.n, 0u);
    // Normal quoted single char -> stripped.
    CHECK_EQ(getStr(ini, "S", "d", "X", 32).s, std::string("x"));
}

TEST(IniProfile, AiMethodPresenceContract) {
    // VIBE_AiMethod_RegisterFromIni @0x468f6c relies on "nonzero return == key
    // present" with default "" and nSize 0x80: a present non-empty value returns
    // its length; an absent key returns 0 (the default "" length).
    IniProfile ini;
    ini.parse("[Method7]\nShortDesire1 = Hunger\n");
    StrRes present = getStr(ini, "Method7", "ShortDesire1", "", 0x80);
    CHECK(present.n != 0u);
    CHECK_EQ(present.s, std::string("Hunger"));
    StrRes absent = getStr(ini, "Method7", "LongDesire1", "", 0x80);
    CHECK_EQ(absent.n, 0u);
}
