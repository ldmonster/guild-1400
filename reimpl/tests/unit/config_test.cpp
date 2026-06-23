#include "test.h"

#include "config/cmdline.h"
#include "config/cpu.h"
#include "config/errorlog.h"
#include "config/ini.h"
#include "config/registry.h"

#include <string>
#include <vector>

using namespace guild;
using namespace guild::config;

// ===========================================================================
// INI parsing  (GetPrivateProfileString / GetPrivateProfileInt semantics)
// ===========================================================================

static const char* kSampleIni =
    "[Gfx]\n"
    "texture_scale=2\n"
    "details=1\n"
    "character_detail=0\n"
    "cur_res=1\n"
    "brightness_a=80\n"
    "gamma_r=120\n"
    "[Sound]\n"
    "master_vol=10\n"
    "sfx_vol=7\n"
    "[Game]\n"
    "speed=3\n"
    "stadt=Koeln\n"
    "mission=5\n"
    "; a comment line\n"
    "difficulty=2\n";

TEST(ConfigIni, SectionAndKeyLookup) {
    IniFile ini(kSampleIni);
    CHECK_EQ(ini.getInt("Gfx", "texture_scale", 99), 2);
    CHECK_EQ(ini.getInt("Sound", "sfx_vol", 99), 7);
    CHECK_EQ(ini.getString("Game", "stadt", "X"), std::string("Koeln"));
}

TEST(ConfigIni, CaseInsensitiveSectionsAndKeys) {
    IniFile ini(kSampleIni);
    CHECK_EQ(ini.getInt("GFX", "Texture_Scale", 0), 2);
    CHECK_EQ(ini.getInt("game", "MISSION", 0), 5);
}

TEST(ConfigIni, MissingKeyReturnsDefault) {
    IniFile ini(kSampleIni);
    CHECK_EQ(ini.getInt("Gfx", "nope", 42), 42);
    CHECK_EQ(ini.getInt("NoSection", "x", -7), -7);
    CHECK_EQ(ini.getString("Game", "nope", "fallback"), std::string("fallback"));
}

TEST(ConfigIni, CommentsAndWhitespaceIgnored) {
    IniFile ini("[s]\n  key  =  value  \n;comment=bad\n");
    CHECK_EQ(ini.getString("s", "key", "X"), std::string("value"));
    CHECK_EQ(ini.getString("s", "comment", "default"), std::string("default"));
}

TEST(ConfigIni, ParseProfileIntSemantics) {
    CHECK_EQ(ParseProfileInt("123"), 123);
    CHECK_EQ(ParseProfileInt("  -45"), -45);
    CHECK_EQ(ParseProfileInt("12abc"), 12);   // stop at first non-digit
    CHECK_EQ(ParseProfileInt("abc"), 0);      // non-numeric -> 0
    CHECK_EQ(ParseProfileInt(""), 0);
    CHECK_EQ(ParseProfileInt("+9"), 9);
}

TEST(ConfigIni, ReadGfxSoundGameDefaultsAndScaling) {
    IniFile ini(kSampleIni);
    GfxSettings gfx;
    SoundSettings snd;
    GameSettings game;
    ReadGfxAndSoundSettings(ini, gfx, snd, game);

    // Present keys.
    CHECK_EQ((int)gfx.textureScale, 2);
    CHECK_EQ((int)gfx.details, 1);
    CHECK_EQ((int)gfx.characterDetail, 0); // overrides the default-of-1
    CHECK_EQ((int)snd.masterVol, 10);
    CHECK_EQ(game.speed, 3);
    CHECK_EQ(game.mission, 5);
    CHECK_EQ((int)game.difficulty, 2);

    // Defaults for absent keys.
    CHECK_EQ((int)gfx.fogPlane, 0);
    CHECK_EQ((int)game.nachtwaechter, 1); // default 1
    CHECK_EQ((int)game.hints, 1);
    CHECK_EQ(game.stadt, std::string("Koeln"));

    // Float scaling (*0.01). brightness_a=80 -> 0.80; gamma_r=120 -> 1.20.
    CHECK(gfx.brightness[3] > 0.799f && gfx.brightness[3] < 0.801f);
    CHECK(gfx.gamma[0] > 1.199f && gfx.gamma[0] < 1.201f);
    // Default-scaled: contrast_r default 100 -> 1.0.
    CHECK(gfx.contrast[0] > 0.999f && gfx.contrast[0] < 1.001f);
    // brightness_a default would be 50 -> 0.5, but here it is 80.

    // cur_res=1 -> table entry (1024,768): resHeight=1024, resWidth=768
    // (mirrors dword_63D728/dword_63D72C assignment order).
    CHECK_EQ(gfx.resHeight, 1024);
    CHECK_EQ(gfx.resWidth, 768);
}

TEST(ConfigIni, ResolutionTable) {
    int w = 0, h = 0;
    ResolutionForIndex(0, &w, &h);
    CHECK_EQ(h, 800);
    CHECK_EQ(w, 600);
    ResolutionForIndex(2, &w, &h);
    CHECK_EQ(h, 1152);
    CHECK_EQ(w, 864);
    ResolutionForIndex(99, &w, &h); // out of range -> zeros
    CHECK_EQ(w, 0);
    CHECK_EQ(h, 0);
}

// ---------------------------------------------------------------------------
// Wave-11 hardening: malformed / truncated / oversized INI inputs. The tokenizer
// is std::string-based; these drive it with adversarial text so ASAN+UBSAN
// confirms the line/section/key/value slicing never indexes out of bounds and
// degenerate input fails soft (default returned, no crash).
// ---------------------------------------------------------------------------
TEST(ConfigIniMalformed, EmptyAndWhitespaceOnly) {
    IniFile e1("");
    CHECK_EQ(e1.getInt("Gfx", "x", 7), 7);
    CHECK_EQ(e1.getString("Gfx", "x", "def"), std::string("def"));
    IniFile e2("   \t  ");          // whitespace, no newline
    CHECK_EQ(e2.getInt("S", "k", 3), 3);
    IniFile e3("\r\n\r\n\n");       // only line breaks
    CHECK_EQ(e3.getString("S", "k", "d"), std::string("d"));
}

TEST(ConfigIniMalformed, NoTrailingNewline) {
    // Last line has no '\n' — getline must still yield it.
    IniFile ini("[S]\nk=v");        // no newline after the value
    CHECK_EQ(ini.getString("S", "k", "X"), std::string("v"));
    IniFile h("[S]");               // section header with no newline, no keys
    CHECK_EQ(h.getString("S", "k", "X"), std::string("X"));
}

TEST(ConfigIniMalformed, UnterminatedSection) {
    // '[' with no closing ']' — the line is skipped, the prior section stays.
    IniFile ini("[Good]\na=1\n[Unterminated\nb=2\n");
    CHECK_EQ(ini.getInt("Good", "a", -1), 1);
    // 'b' was parsed while the section was still [Good] (the broken header was
    // skipped and did NOT change the current section).
    CHECK_EQ(ini.getInt("Good", "b", -1), 2);
    // The mangled name is not a real section.
    CHECK_EQ(ini.getInt("Unterminated", "b", 55), 55);
    // A lone '[' as the entire file must not index past the end.
    IniFile lone("[");
    CHECK_EQ(lone.getString("x", "y", "z"), std::string("z"));
}

TEST(ConfigIniMalformed, EmptySectionBrackets) {
    IniFile ini("[]\nk=v\n");       // empty section name -> "" current section
    // Stored under the empty section; not reachable as a named section.
    CHECK_EQ(ini.getString("", "k", "X"), std::string("v"));
    CHECK_EQ(ini.getString("S", "k", "X"), std::string("X"));
}

TEST(ConfigIniMalformed, KeyWithNoEquals) {
    // A line with no '=' is ignored (not a key).
    IniFile ini("[S]\njust_a_word\nreal=ok\n");
    CHECK_EQ(ini.getString("S", "just_a_word", "MISS"), std::string("MISS"));
    CHECK_EQ(ini.getString("S", "real", "MISS"), std::string("ok"));
    // '=' as the very first char -> empty key.
    IniFile eq("[S]\n=value\n");
    CHECK_EQ(eq.getString("S", "", "X"), std::string("value"));
}

TEST(ConfigIniMalformed, DuplicateKeysLastWins) {
    IniFile ini("[S]\nk=1\nk=2\nk=3\n");
    CHECK_EQ(ini.getInt("S", "k", -1), 3);   // last assignment wins (Win32)
    // Duplicate sections merge; last value of each key wins.
    IniFile two("[S]\nk=1\n[S]\nk=9\nj=4\n");
    CHECK_EQ(two.getInt("S", "k", -1), 9);
    CHECK_EQ(two.getInt("S", "j", -1), 4);
}

TEST(ConfigIniMalformed, HugeValueAndKey) {
    // A very long value and key must parse without any fixed-buffer overflow.
    std::string big(200000, 'Z');
    std::string text = "[S]\nhuge=" + big + "\n";
    IniFile ini(text);
    CHECK_EQ(ini.getString("S", "huge", "X").size(), big.size());
    std::string bigkey(100000, 'k');
    IniFile ki("[S]\n" + bigkey + "=v\n");
    CHECK_EQ(ki.getString("S", bigkey, "X"), std::string("v"));
    // ParseProfileInt on a digit run far past the int range must NOT invoke
    // signed-overflow UB (caught by UBSAN). Unsigned accumulation makes the
    // value well-defined garbage; the contract is only "no UB, no crash".
    int over = ParseProfileInt(std::string(40, '9'));
    (void)over;
    // Values that DO fit are still exact (regression: the UB fix must not move
    // in-range results).
    CHECK_EQ(ParseProfileInt("2147483647"), 2147483647); // INT_MAX
    CHECK_EQ(ParseProfileInt("-2147483648"), (-2147483647 - 1)); // INT_MIN
    CHECK_EQ(ParseProfileInt("000123"), 123);            // leading zeros
    CHECK_EQ(ParseProfileInt("  -0  "), 0);
}

TEST(ConfigIniMalformed, NonAsciiBytes) {
    // High-bit bytes in section/key/value must be handled without UB in the
    // ctype calls (we cast to unsigned char before tolower/isspace).
    std::string text;
    text += "[\xC3\xA9]\n";                       // 'é' section
    text += "key\xC2\xA0=v\xE2\x82\xAC\n";        // NBSP in key, euro in value
    IniFile ini(text);
    // No crash; the value round-trips (lookups use the same lowering).
    std::string sec = "\xC3\xA9";
    std::string key = "key\xC2\xA0";
    std::string got = ini.getString(sec, key, "MISS");
    CHECK(got == std::string("v\xE2\x82\xAC") || got == std::string("MISS"));
    // Embedded NUL byte mid-line (stringstream stops the line at it? cast-safe).
    std::string nul = std::string("[S]\nk=a\x00z\n", 11);
    IniFile ni(nul);
    CHECK_EQ(ni.getInt("Nope", "x", 4), 4); // smoke: parse completed, no UB
}

TEST(ConfigIniMalformed, MissingSectionAndKeyLookups) {
    IniFile ini("[Only]\na=1\n");
    CHECK_EQ(ini.getInt("Absent", "a", 11), 11);     // section missing
    CHECK_EQ(ini.getInt("Only", "absent", 22), 22);  // key missing
    CHECK_EQ(ini.getString("Absent", "absent", "d"), std::string("d"));
    // ReadGfxAndSoundSettings against an empty provider yields all defaults
    // (exercises the bulk getInt path with every key absent).
    IniFile empty("");
    GfxSettings gfx; SoundSettings snd; GameSettings game;
    ReadGfxAndSoundSettings(empty, gfx, snd, game);
    CHECK_EQ((int)gfx.characterDetail, 1);
    CHECK_EQ((int)game.nachtwaechter, 1);
    CHECK_EQ(game.stadt, std::string("Augsburg"));
}

TEST(ConfigIniMalformed, ResolutionIndexBounds) {
    int w = 0, h = 0;
    // Every u8 index, including past the table, must stay in-bounds.
    for (int idx = 0; idx <= 255; ++idx) {
        ResolutionForIndex((guild::u8)idx, &w, &h);
        if (idx >= 6) { CHECK_EQ(w, 0); CHECK_EQ(h, 0); }
    }
}

// ===========================================================================
// Command-line parsing
// ===========================================================================

TEST(ConfigCmdLine, LaunchOptionsAllFields) {
    LaunchOptions o = ParseLaunchOptions("STADT=\"Foo\" BERUF=\"3\" IP=\"1.2.3.4\" PORT=\"9000\"");
    CHECK(o.hasStadt);
    CHECK(o.hasBeruf);
    CHECK(o.hasIp);
    CHECK(o.hasPort);
    CHECK_EQ(o.stadt, std::string("FOO"));     // command line is uppercased first
    CHECK_EQ(o.beruf, std::string("3"));
    CHECK_EQ(o.ip, std::string("1.2.3.4"));
    CHECK_EQ(o.port, 9000);
}

TEST(ConfigCmdLine, LaunchOptionsPartial) {
    LaunchOptions o = ParseLaunchOptions("IP=\"10.0.0.1\" PORT=\"7777\"");
    CHECK(!o.hasStadt);
    CHECK(!o.hasBeruf);
    CHECK(o.hasIp);
    CHECK(o.hasPort);
    CHECK_EQ(o.ip, std::string("10.0.0.1"));
    CHECK_EQ(o.port, 7777);
}

TEST(ConfigCmdLine, ExtractQuotedOptionMissingPrefix) {
    std::string v;
    CHECK(!ExtractQuotedOption("PORT=\"1\"", "STADT=\"", &v));
}

TEST(ConfigCmdLine, ExtractQuotedOptionMissingClose) {
    std::string v;
    CHECK(!ExtractQuotedOption("STADT=\"unterminated", "STADT=\"", &v));
}

TEST(ConfigCmdLine, ExtractQuotedOptionEmpty) {
    std::string v("untouched");
    CHECK(ExtractQuotedOption("STADT=\"\"", "STADT=\"", &v));
    CHECK_EQ(v, std::string(""));
}

TEST(ConfigCmdLine, ArgvSimple) {
    std::vector<std::string> a = ParseCmdLine("gilde.exe foo bar");
    CHECK_EQ((int)a.size(), 3);
    CHECK_EQ(a[0], std::string("gilde.exe"));
    CHECK_EQ(a[1], std::string("foo"));
    CHECK_EQ(a[2], std::string("bar"));
}

TEST(ConfigCmdLine, ArgvQuotedProgramAndArgs) {
    std::vector<std::string> a =
        ParseCmdLine("\"C:\\Spiele\\Die Gilde\\gilde.exe\" STADT=\"Foo Bar\"");
    CHECK_EQ((int)a.size(), 2);
    CHECK_EQ(a[0], std::string("C:\\Spiele\\Die Gilde\\gilde.exe"));
    CHECK_EQ(a[1], std::string("STADT=Foo Bar"));
}

TEST(ConfigCmdLine, ArgvBackslashQuoteRules) {
    // MSVC rules: \" -> literal ", 2N backslashes -> N backslashes.
    std::vector<std::string> a = ParseCmdLine("prog a\\\\\\\"b c");
    CHECK_EQ((int)a.size(), 3);
    CHECK_EQ(a[0], std::string("prog"));
    // a\\\"b : three backslashes + quote -> one backslash + literal quote
    CHECK_EQ(a[1], std::string("a\\\"b"));
    CHECK_EQ(a[2], std::string("c"));
}

TEST(ConfigCmdLine, ArgvDoubledQuoteInsideQuotes) {
    // Inside quotes, "" emits one literal quote and stays quoted.
    std::vector<std::string> a = ParseCmdLine("prog \"he said \"\"hi\"\"\"");
    CHECK_EQ((int)a.size(), 2);
    CHECK_EQ(a[1], std::string("he said \"hi\""));
}

// ===========================================================================
// Registry get/set roundtrip (in-memory backend)
// ===========================================================================

TEST(ConfigRegistry, DwordRoundtrip) {
    MemRegistry reg;
    int h = OpenKey(reg, "Gilde", OpenMode::Create);
    CHECK(h >= 0);
    CHECK_EQ(SetDwordValue(reg, h, "score", 12345), 0);
    CHECK_EQ(QueryDwordValue(reg, h, "score"), 12345u);
    u32 out = 0;
    CHECK_EQ(QueryDwordOut(reg, h, "score", &out), 1);
    CHECK_EQ(out, 12345u);
    CloseKey(reg, h);
}

TEST(ConfigRegistry, StringRoundtrip) {
    MemRegistry reg;
    int h = OpenKey(reg, "Gilde", OpenMode::Create);
    CHECK_EQ(SetStringValue(reg, h, "name", "Severin"), 0);
    std::string s;
    CHECK_EQ(QueryStringValue(reg, h, "name", &s), 1);
    CHECK_EQ(s, std::string("Severin"));
    CloseKey(reg, h);
}

TEST(ConfigRegistry, FloatRoundtrip) {
    MemRegistry reg;
    int h = OpenKey(reg, "Gilde", OpenMode::Create);
    CHECK_EQ(SetFloatValue(reg, h, "gamma", 1.5f), 0);
    float f = QueryFloatValue(reg, h, "gamma");
    CHECK(f > 1.49f && f < 1.51f);
    float out = 0;
    CHECK_EQ(QueryFloatOut(reg, h, "gamma", &out), 1);
    CHECK(out > 1.49f && out < 1.51f);
    CloseKey(reg, h);
}

TEST(ConfigRegistry, MissingValueAndKey) {
    MemRegistry reg;
    // Open of a non-existent key in Open mode fails.
    CHECK_EQ(OpenKey(reg, "DoesNotExist", OpenMode::Open), -1);
    // Mode 0 never touches the registry.
    CHECK_EQ(OpenKey(reg, "Whatever", OpenMode::None), -1);
    int h = OpenKey(reg, "Gilde", OpenMode::Create);
    CHECK_EQ(QueryDwordValue(reg, h, "absent"), 0u); // missing dword -> 0
    u32 out = 7;
    CHECK_EQ(QueryDwordOut(reg, h, "absent", &out), 0); // returns 0, out unchanged
    CHECK_EQ(out, 7u);
    std::string s("keep");
    CHECK_EQ(QueryStringValue(reg, h, "absent", &s), 0);
    CHECK_EQ(s, std::string("keep"));
    CloseKey(reg, h);
}

TEST(ConfigRegistry, ReopenSeesPriorWrites) {
    MemRegistry reg;
    int h1 = OpenKey(reg, "Gilde", OpenMode::Create);
    SetDwordValue(reg, h1, "v", 99);
    CloseKey(reg, h1);
    int h2 = OpenKey(reg, "Gilde", OpenMode::Open); // now exists
    CHECK(h2 >= 0);
    CHECK_EQ(QueryDwordValue(reg, h2, "v"), 99u);
    CloseKey(reg, h2);
}

// ===========================================================================
// ErrorLog formatting
// ===========================================================================

namespace {
struct CaptureSink : ILogSink {
    std::string fileOut, msgOut, dbgOut, conOut;
    void file(const std::string& t) override { fileOut += t; }
    void msgBox(const std::string& t) override { msgOut += t; }
    void debug(const std::string& t) override { dbgOut += t; }
    void console(const std::string& t) override { conOut += t; }
};
} // namespace

TEST(ConfigErrorLog, WriteRecordRespectsFlags) {
    CaptureSink s;
    WriteRecord(s, "hello", kLogFile | kLogDebug);
    CHECK_EQ(s.fileOut, std::string("hello\n"));
    CHECK_EQ(s.dbgOut, std::string("hello\n"));
    CHECK_EQ(s.conOut, std::string("")); // console bit not set
}

TEST(ConfigErrorLog, ReportMessageFormat) {
    CaptureSink s;
    std::string body = FormatMessage(s, kLogFile | kLogMsgBox, "disk full");
    CHECK_EQ(body, std::string("MESSAGE: disk full\n"));
    CHECK_EQ(s.fileOut, std::string("MESSAGE: disk full\n\n"));
    CHECK_EQ(s.msgOut, std::string("disk full")); // msgbox gets raw msg
}

TEST(ConfigErrorLog, ModuleLineWithAndWithoutTime) {
    std::string withT = FormatModuleLine("render.cpp", 42, "bad state", true, 1234);
    CHECK_EQ(withT, std::string("Time:    1234\tModule 'render.cpp':\tLine:42:\tbad state"));
    std::string noT = FormatModuleLine("render.cpp", 42, "bad state", false, 0);
    CHECK_EQ(noT, std::string("tModule 'render.cpp':\tLine:42:\tbad state"));
}

// ===========================================================================
// CPUID feature decode
// ===========================================================================

TEST(ConfigCpu, DecodeBasicFeatures) {
    // EDX with FPU(0), TSC(4), MMX(23), SSE(25), SSE2(26) set.
    u32 edx = kFpu | kTsc | kMmx | kSse | kSse2;
    CpuFeatures f = DecodeFeatures(/*eax=*/0, /*ecx=*/0, edx);
    CHECK(f.has(kFpu));
    CHECK(f.has(kTsc));
    CHECK(f.has(kMmx));
    CHECK(f.has(kSse));
    CHECK(f.has(kSse2));
    CHECK(!f.has(kHtt));
    CHECK(!f.has(kFxsr));
}

TEST(ConfigCpu, DecodeEcxFeatures) {
    u32 ecx = kSse3 | kSsse3 | kSse41;
    CpuFeatures f = DecodeFeatures(0, ecx, 0);
    CHECK(f.has(kSse3));
    CHECK(f.has(kSsse3));
    CHECK(f.has(kSse41));
    CHECK(!f.has(kSse42));
    CHECK(!f.has(kAvx));
}

TEST(ConfigCpu, DecodeFamilyModelStepping) {
    // Pentium III (family 6, model 8, stepping 6): EAX = 0x00000686.
    CpuFeatures f = DecodeFeatures(0x00000686, 0, 0);
    CHECK_EQ(f.family, 6u);
    CHECK_EQ(f.model, 8u);    // family 6 folds extended model (here 0) << 4
    CHECK_EQ(f.stepping, 6u);
}

TEST(ConfigCpu, DecodeExtendedFamilyModel) {
    // family 0x0F with ext family 0x01, ext model 0x2, base model 0x4:
    // EAX = (extFam<<20)|(extModel<<16)|(baseFamily<<8)|(baseModel<<4)|stepping
    u32 eax = (0x01u << 20) | (0x2u << 16) | (0x0Fu << 8) | (0x4u << 4) | 0x3u;
    CpuFeatures f = DecodeFeatures(eax, 0, 0);
    CHECK_EQ(f.family, 0x0Fu + 0x01u); // 16
    CHECK_EQ(f.model, (0x2u << 4) | 0x4u); // 0x24
    CHECK_EQ(f.stepping, 3u);
}

namespace {
struct StubCpuid : CpuidShim {
    CpuidRegs r;
    CpuidRegs query(u32, u32) override { return r; }
};
} // namespace

TEST(ConfigCpu, DetectThroughShim) {
    StubCpuid stub;
    stub.r.eax = 0x00000686;
    stub.r.edx = kFpu | kMmx | kSse;
    CpuFeatures f = DetectFeatures(stub);
    CHECK(f.has(kSse));
    CHECK_EQ(f.family, 6u);
}

// ===========================================================================
// Trace call-instruction decode
// ===========================================================================

TEST(ConfigTrace, DecodeDirectCall) {
    // E8 rel32 call. Bytes: E8 10 00 00 00 (rel=0x10), return addr just past it.
    u8 code[5] = {0xE8, 0x10, 0x00, 0x00, 0x00};
    u32 target = 0;
    int kind = DecodeCallInstruction(code + 5, 5, /*imageBase=*/0x400000, &target);
    CHECK_EQ(kind, 1);
    CHECK_EQ(target, 0x400010u);
}

TEST(ConfigTrace, DecodeIndirectCall) {
    // FF 15 disp32 : call [disp32] (6 bytes).
    u8 code[6] = {0xFF, 0x15, 0x00, 0x10, 0x40, 0x00};
    u32 target = 0;
    int kind = DecodeCallInstruction(code + 6, 6, 0, &target);
    CHECK_EQ(kind, 2);
}

TEST(ConfigTrace, DecodeNonCall) {
    u8 code[5] = {0x90, 0x90, 0x90, 0x90, 0x90}; // NOPs
    u32 target = 0;
    int kind = DecodeCallInstruction(code + 5, 5, 0, &target);
    CHECK_EQ(kind, 0);
}
