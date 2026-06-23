#include "tests/framework/test.h"
#include "render/texlight_recon3_animset.h"

#include <cstring>

using namespace guild;
using namespace guild::render;

// =============================================================================
// Golden tests for gilde.exe 0x5da4b4 VIBE_Texture_LoadAnimatedSet and its
// pure analysis half AnimSet_Analyze. Vectors derived directly from the
// disassembled control flow (string copies, StrToUpper, trailing digit-class
// scan, the "_A" gate, ParseInt) and the inert-default boundary hooks.
// =============================================================================

// --- ctype digit-class predicate (byte_64A208 bit 0x20) ---
TEST(TexLightRecon3, DigitClassMatchesAsciiDigits) {
    for (int c = 0; c < 256; ++c) {
        bool expect = (c >= '0' && c <= '9');
        CHECK_EQ(AnimSet_IsDigitClass((u8)c), expect);
    }
}

// --- analysis: a well-formed animated-set name "<base>_A<sep><digits>" ---
// Pattern recovered from the gate: upper[edx-1]=='A', upper[edx-2]=='_',
// trailing digit run present, edx is the index of the non-digit separator.
TEST(TexLightRecon3, AnalyzeMatchesUnderscoreAPattern) {
    // "WALL_AX12": indices W0 A1 L2 L3 _4 A5 X6 1->7 2->8 (len=9).
    // Scan from idx8('2') -> edx=6 ('X' non-digit). Gate: edx(6) < len-2(7),
    // edx>1, upper[5]='A', upper[4]='_' -> matched, frame=ParseInt("12")=12.
    // (NOTE: the gate edx < len-2 requires at least TWO trailing digits when the
    //  separator is one char after "_A"; this is the exact asm comparison.)
    AnimSetParse p = AnimSet_Analyze("WALL_AX12");
    CHECK(p.matched);
    CHECK(p.sawDigit);
    CHECK_EQ(p.frame, 12);
    CHECK_EQ(p.sepIndex, 6);
    CHECK(std::strcmp(p.working, "WALL_AX12") == 0);
    CHECK(std::strcmp(p.upper, "WALL_AX12") == 0);
}

// Exactly one trailing digit with the separator at len-2 is REJECTED by the
// `edx >= len-2` (jnb) guard at 0x5da596 — a precise behavioral edge.
TEST(TexLightRecon3, AnalyzeRejectsSingleTrailingDigitAtBoundary) {
    // "WALL_AX3": edx=6, len=8, len-2=6 -> edx >= len-2 -> rejected.
    AnimSetParse p = AnimSet_Analyze("WALL_AX3");
    CHECK(p.sawDigit);     // a digit WAS seen
    CHECK(!p.matched);     // but the boundary guard rejects it
}

// Lowercase input is uppercased into `upper` before the 'A'/'_' compare, but
// the digit scan and ParseInt run on the un-cased `working` copy.
TEST(TexLightRecon3, AnalyzeUppercasesBeforeGate) {
    // "tile_ay12": w0 t,..., _4 a5 y6 1->7 2->8. edx scans down past '2','1'
    // to 'y'(idx6). upper="TILE_AY12": upper[5]='A', upper[4]='_' -> match.
    AnimSetParse p = AnimSet_Analyze("tile_ay12");
    CHECK(p.matched);
    CHECK_EQ(p.frame, 12);
    CHECK_EQ(p.sepIndex, 6);
    CHECK(std::strcmp(p.working, "tile_ay12") == 0);
    CHECK(std::strcmp(p.upper, "TILE_AY12") == 0);
}

// --- analysis: names that must NOT match ---
TEST(TexLightRecon3, AnalyzeRejectsNoDigits) {
    AnimSetParse p = AnimSet_Analyze("WALL_AX");  // no trailing digit
    CHECK(!p.matched);
}

TEST(TexLightRecon3, AnalyzeRejectsMissingUnderscoreA) {
    AnimSetParse p = AnimSet_Analyze("WALLXY3");  // no "_A" before separator
    CHECK(!p.matched);
}

TEST(TexLightRecon3, AnalyzeRejectsAllDigitsTail) {
    // "12345": digit scan walks down to edx=0 (edx<=0 stops). Gate edx<=1 fails.
    AnimSetParse p = AnimSet_Analyze("12345");
    CHECK(!p.matched);
}

TEST(TexLightRecon3, AnalyzeRejectsShortName) {
    AnimSetParse p = AnimSet_Analyze("_A5");  // edx of separator <= 1 -> reject
    CHECK(!p.matched);
}

// --- full function: gate failure returns 0 and stamps nothing ---
TEST(TexLightRecon3, LoadReturnsZeroOnNoMatch) {
    AnimSetCallerRecord rec{};
    rec.curAnimId = 77;
    rec.frameStamp = 9;
    AnimSetHooks hooks;  // all inert
    u8 r = VIBE_Texture_LoadAnimatedSet("plain", 0, 1, 2, rec, hooks);
    CHECK_EQ((int)r, 0);
    CHECK_EQ(rec.curAnimId, 77);   // untouched
    CHECK_EQ((int)rec.frameStamp, 9);
}

// --- full function: matched but frame!=0 and bit 0x40000 clear -> abort ---
TEST(TexLightRecon3, LoadAbortsWhenFrameNonzeroAndFlagClear) {
    AnimSetCallerRecord rec{};
    rec.curAnimId = 5;
    AnimSetHooks hooks;
    int animId = 100;
    hooks.nextAnimId = &animId;
    // "WALL_AX12" matches, frame=12 != 0, flags has no 0x40000 -> return 0,
    // record NOT stamped, anim id NOT incremented.
    u8 r = VIBE_Texture_LoadAnimatedSet("WALL_AX12", 0, 0, 0, rec, hooks);
    CHECK_EQ((int)r, 0);
    CHECK_EQ(rec.curAnimId, 5);    // untouched (returned before stamping)
    CHECK_EQ(animId, 100);
}

// --- full function: frame==0 proceeds; no BuildBmpPath -> returns 0 after stamp ---
TEST(TexLightRecon3, LoadFrameZeroProceedsStampsRecord) {
    AnimSetCallerRecord rec{};
    AnimSetHooks hooks;
    int animId = 42;
    hooks.nextAnimId = &animId;
    // buildBmpPath inert (null) -> aborts at 0x5da631 AFTER stamping +0x50/+0x71.
    // "WALL_AX00": frame=ParseInt("00")=0. Stamp curAnimId=42, frameStamp=0.
    u8 r = VIBE_Texture_LoadAnimatedSet("WALL_AX00", 0, 0, 0, rec, hooks);
    CHECK_EQ((int)r, 0);
    CHECK_EQ(rec.curAnimId, 42);          // stamped from nextAnimId
    CHECK_EQ((int)rec.frameStamp, 0);     // (u8)frame
}

// frame!=0 but bit 0x40000 set -> proceeds (does NOT abort).
TEST(TexLightRecon3, LoadFrameNonzeroWithFlagProceeds) {
    AnimSetCallerRecord rec{};
    AnimSetHooks hooks;
    int animId = 7;
    hooks.nextAnimId = &animId;
    u8 r = VIBE_Texture_LoadAnimatedSet("WALL_AX12", 0x40000, 0, 0, rec, hooks);
    CHECK_EQ((int)r, 0);                  // buildBmpPath null -> 0
    CHECK_EQ(rec.curAnimId, 7);           // stamped (proceeded past abort)
    CHECK_EQ((int)rec.frameStamp, 12);
}

// --- full function: next-frame file EXISTS -> LoadByName path ---
static const char* g_buildPath(const char* /*name*/) { return "tex/dir/x.bmp"; }
static bool g_exists_true(const char* /*p*/) { return true; }
static char g_loadName[280];
static int  g_loadFlags;
static u8   g_loadPal;
static u8   g_loadFlag;
static u8 g_loadByName(const char* n, int flags, u8 pal, u8 lf) {
    std::snprintf(g_loadName, sizeof(g_loadName), "%s", n);
    g_loadFlags = flags; g_loadPal = pal; g_loadFlag = lf;
    return 0xAB;
}

TEST(TexLightRecon3, LoadExistingNextFrameCallsLoadByName) {
    AnimSetCallerRecord rec{};
    AnimSetHooks hooks;
    int animId = 3;
    hooks.nextAnimId = &animId;
    hooks.buildBmpPath = &g_buildPath;
    hooks.fileExists = &g_exists_true;
    hooks.loadByName = &g_loadByName;
    g_loadName[0] = 0;
    // "WALL_AX00": sepIndex=6, member truncated at sepIndex+1=7 -> "WALL_AX".
    // palIdx=5. loadName = "%s%i" (member, palIdx) = "WALL_AX5". flags |= 0x40000.
    u8 r = VIBE_Texture_LoadAnimatedSet("WALL_AX00", 0, 9, 5, rec, hooks);
    CHECK_EQ((int)r, 0xAB);
    CHECK(std::strcmp(g_loadName, "WALL_AX5") == 0);
    CHECK_EQ(g_loadFlags & 0x40000, 0x40000);
    CHECK_EQ((int)g_loadPal, 5);
    CHECK_EQ((int)g_loadFlag, 9);
}

// --- full function: next frame ABSENT -> bank walk stamps siblings, anim id++ ---
static bool g_exists_false(const char* /*p*/) { return false; }

TEST(TexLightRecon3, LoadMissingNextFrameWalksBankAndIncrements) {
    AnimSetCallerRecord rec{};
    AnimSetHooks hooks;
    int animId = 50;
    hooks.nextAnimId = &animId;
    hooks.buildBmpPath = &g_buildPath;
    hooks.fileExists = &g_exists_false;

    AnimSetRecord bank[4]{};
    // r0: active, same anim id, name contains member "WALL_AX" -> stamped.
    bank[0] = AnimSetRecord{ /*ref*/1, /*anim*/50, /*frame*/0, "WALL_AX_inst" };
    // r1: active, same anim id, name does NOT contain member -> not stamped.
    bank[1] = AnimSetRecord{ 1, 50, 0, "OTHER" };
    // r2: inactive (ref 0) -> skipped even though id matches & name contains.
    bank[2] = AnimSetRecord{ 0, 50, 0, "WALL_AX_dead" };
    // r3: active but different anim id -> skipped.
    bank[3] = AnimSetRecord{ 1, 99, 0, "WALL_AX_other" };
    hooks.bank = bank;
    hooks.bankCount = 4;

    // "WALL_AX78": frame=78, member->"WALL_AX". Next frame absent -> walk.
    u8 r = VIBE_Texture_LoadAnimatedSet("WALL_AX78", 0x40000, 0, 0, rec, hooks);
    CHECK_EQ((int)r, 0);
    CHECK_EQ((int)bank[0].frame, 79);  // (u8)(frame+1)
    CHECK_EQ((int)bank[1].frame, 0);
    CHECK_EQ((int)bank[2].frame, 0);
    CHECK_EQ((int)bank[3].frame, 0);
    CHECK_EQ(animId, 51);              // incremented once at the end
    CHECK_EQ(rec.curAnimId, 50);      // stamped from the pre-increment value
    CHECK_EQ((int)rec.frameStamp, 78);
}
