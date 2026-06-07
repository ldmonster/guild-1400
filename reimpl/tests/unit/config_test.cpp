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
