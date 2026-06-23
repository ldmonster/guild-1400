// Golden-vector unit tests for the locale reconstruction (src/util/locale_recon.*).
#include "tests/framework/test.h"
#include "util/locale_recon.h"

#include <cstring>

using namespace guild::util;

// -------------------------------------------------------------------------------------
// VIBE_Locale_CopyLanguageString @0x5a33a0
// -------------------------------------------------------------------------------------
TEST(LocaleReconLang, MatchesEachLanguageAndSetsGlobal) {
    CHECK_EQ(LocaleCopyLanguageString("GERMAN"),  0u);
    CHECK_EQ(LanguageIdGlobal(), 0);
    CHECK_EQ(LocaleCopyLanguageString("ENGLISH"), 1u);
    CHECK_EQ(LanguageIdGlobal(), 1);
    CHECK_EQ(LocaleCopyLanguageString("FRENCH"),  2u);
    CHECK_EQ(LocaleCopyLanguageString("ITALIAN"), 3u);
    CHECK_EQ(LocaleCopyLanguageString("SPANISH"), 4u);
    CHECK_EQ(LanguageIdGlobal(), 4);
}

TEST(LocaleReconLang, CaseInsensitiveViaUpperCasing) {
    CHECK_EQ(LocaleCopyLanguageString("german"),  0u);
    CHECK_EQ(LocaleCopyLanguageString("English"), 1u);
    CHECK_EQ(LocaleCopyLanguageString("FrEnCh"),  2u);
}

TEST(LocaleReconLang, UnknownReturns0xFFFFAndLeavesGlobalSentinel) {
    CHECK_EQ(LocaleCopyLanguageString("KLINGON"), 0xFFFFu);
    CHECK_EQ(LanguageIdGlobal(), static_cast<unsigned short>(0xFFFF));
    CHECK_EQ(LocaleCopyLanguageString(""), 0xFFFFu);
}

// -------------------------------------------------------------------------------------
// VIBE_Locale_SetupMbcsCodePage @0x609f90
// -------------------------------------------------------------------------------------
TEST(LocaleReconMbcs, SbcsResetClearsState) {
    // First arrange a dirty state, then reset.
    MbcsStateGlobal().leadByte[0x90] = 1;
    MbcsStateGlobal().dbcs = 1;
    MbcsStateGlobal().codePage = 932;
    int r = LocaleSetupMbcsCodePage(0xFFFFFFFDu);
    CHECK_EQ(r, 0);
    CHECK_EQ(MbcsStateGlobal().dbcs, 0);
    CHECK_EQ(MbcsStateGlobal().codePage, 0);
    CHECK_EQ(MbcsStateGlobal().leadByte[0x90], 0);
}

TEST(LocaleReconMbcs, Cp932HardCodesLeadRanges) {
    int r = LocaleSetupMbcsCodePage(0xFFFFFFFCu);
    CHECK_EQ(r, 0);
    CHECK_EQ(MbcsStateGlobal().dbcs, 1);
    CHECK_EQ(MbcsStateGlobal().codePage, 932);
    // 0x81..0x9F (129..159) and 0xE0..0xFC (224..252) are lead bytes.
    CHECK_EQ(MbcsStateGlobal().leadByte[129], 1);
    CHECK_EQ(MbcsStateGlobal().leadByte[159], 1);
    CHECK_EQ(MbcsStateGlobal().leadByte[224], 1);
    CHECK_EQ(MbcsStateGlobal().leadByte[252], 1);
    // Boundaries just outside the ranges are NOT lead bytes.
    CHECK_EQ(MbcsStateGlobal().leadByte[128], 0);
    CHECK_EQ(MbcsStateGlobal().leadByte[160], 0);
    CHECK_EQ(MbcsStateGlobal().leadByte[223], 0);
}

// Hook fixture providing a DBCS code page with a single lead-byte range 0x81..0x84.
static int g_cpinfo_calls = 0;
static unsigned int FakeACP()   { return 99; }
static unsigned int FakeOEMCP() { return 437; }
static int FakeGetCPInfo(unsigned int cp, unsigned char* out) {
    ++g_cpinfo_calls;
    std::memset(out, 0, 12);
    if (cp == 99) {            // DBCS page with one lead-byte range
        out[0] = 0x81;
        out[1] = 0x84;
        return 1;
    }
    if (cp == 1) {             // plain SBCS page (no lead bytes)
        return 1;
    }
    if (cp == 0) {
        return 0;              // failure
    }
    return 1;
}

TEST(LocaleReconMbcs, GetCPInfoLeadRangeMarked) {
    MbcsHooks saved = MbcsHookTable();
    MbcsHookTable().getACP = FakeACP;
    MbcsHookTable().getOEMCP = FakeOEMCP;
    MbcsHookTable().getCPInfo = FakeGetCPInfo;

    g_cpinfo_calls = 0;
    int r = LocaleSetupMbcsCodePage(0xFFFFFFFFu);  // -> ACP == 99
    CHECK_EQ(r, 0);
    CHECK_EQ(g_cpinfo_calls, 1);
    CHECK_EQ(MbcsStateGlobal().dbcs, 1);            // LeadByte[0] != 0
    CHECK_EQ(MbcsStateGlobal().codePage, 99);
    CHECK_EQ(MbcsStateGlobal().leadByte[0x81], 1);
    CHECK_EQ(MbcsStateGlobal().leadByte[0x84], 1);
    CHECK_EQ(MbcsStateGlobal().leadByte[0x85], 0);

    MbcsHookTable() = saved;
}

TEST(LocaleReconMbcs, AcpOneFallsBackToOemForCodePage) {
    MbcsHooks saved = MbcsHookTable();
    MbcsHookTable().getACP = FakeACP;
    MbcsHookTable().getOEMCP = FakeOEMCP;
    MbcsHookTable().getCPInfo = FakeGetCPInfo;

    // cp==0 resolves to acp=1 (the !acp -> 1 path), GetCPInfo(1) succeeds (SBCS),
    // and CodePage is taken from GetOEMCP == 437.
    int r = LocaleSetupMbcsCodePage(0);
    CHECK_EQ(r, 0);
    CHECK_EQ(MbcsStateGlobal().dbcs, 0);
    CHECK_EQ(MbcsStateGlobal().codePage, 437);

    MbcsHookTable() = saved;
}

TEST(LocaleReconMbcs, GetCPInfoFailureReturns1) {
    MbcsHooks saved = MbcsHookTable();
    MbcsHookTable().getACP = FakeACP;
    MbcsHookTable().getOEMCP = FakeOEMCP;
    MbcsHookTable().getCPInfo = FakeGetCPInfo;

    // Force the GetCPInfo failure branch: pass a cp whose !acp check keeps it, and make
    // GetCPInfo(cp) return 0. cp must be non-zero so it isn't remapped to 1; use a cp
    // that FakeGetCPInfo treats as the failure case by routing through ACP==0... instead
    // we directly request a code page our fake fails on by temporarily failing cp 5.
    MbcsHookTable().getCPInfo = [](unsigned int cp, unsigned char* out) -> int {
        std::memset(out, 0, 12);
        return cp == 5 ? 0 : 1;
    };
    int r = LocaleSetupMbcsCodePage(5);
    CHECK_EQ(r, 1);

    MbcsHookTable() = saved;
}
