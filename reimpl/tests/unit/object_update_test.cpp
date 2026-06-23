// Golden vectors for sim::object_update — the pure logic of VIBE_Object_Update /
// VIBE_Animation_Apply / VIBE_EntityChild_Process / VIBE_Entity_InteractionLogic.
// gilde.exe 0x40eea0 / 0x415b78 / 0x418f34 / 0x41078c.
#include "sim/object_update.h"
#include "tests/framework/test.h"

#include <cmath>
#include <string>

using namespace guild::sim;

namespace {
bool NearF(float a, float b, float e = 1e-3f) { return std::fabs(a - b) <= e; }
// Fixed-width 8px glyph advance for layout tests.
int Adv8(int) { return 8; }
}  // namespace

// --- ConvertX truncation (0x5c6b08) -----------------------------------------
TEST(ObjectUpdate, ConvertXTruncates) {
    CHECK_EQ((int)ConvertXTrunc(3.9), 3);
    CHECK_EQ((int)ConvertXTrunc(-3.9), -3);   // toward zero, NOT floor
    CHECK_EQ((int)ConvertXTrunc(99.999), 99);
}

// --- CountDigits (0x40f044) -------------------------------------------------
TEST(ObjectUpdate, CountDigits) {
    CHECK_EQ(CountDigits(0), 0);
    CHECK_EQ(CountDigits(-5), 0);
    CHECK_EQ(CountDigits(7), 1);
    CHECK_EQ(CountDigits(99), 2);
    CHECK_EQ(CountDigits(1000), 4);
    // cap at 10 digits.
    CHECK_EQ(CountDigits(2000000000), 10);
}

// --- FormatAmount (0x40f06d) g / kg / t + zero-pad + plain -------------------
TEST(ObjectUpdate, FormatAmountMass) {
    // mass bit (0x04): total = unit*count.
    CHECK_EQ(FormatAmount(0x04, 5, 100, 0), std::string("500 g"));       // <1000
    CHECK_EQ(FormatAmount(0x04, 5, 1000, 0), std::string("5.0 kg"));     // 5000 g
    CHECK_EQ(FormatAmount(0x04, 3, 1000000, 0), std::string("3.0 t"));   // 3,000,000 g
}
TEST(ObjectUpdate, FormatAmountPlainAndPad) {
    CHECK_EQ(FormatAmount(0x00, 42, 7, 0), std::string("42"));           // "%i" of count
    // zero-padded "%%0%ii " with digits=3 of count*unit (12).
    CHECK_EQ(FormatAmount(0x08, 4, 3, 3), std::string("012 "));
}

// --- FormatBuildPercent (0x40f6b0): 100*fill, ConvertX truncate, clamp 1.0 --
TEST(ObjectUpdate, FormatBuildPercent) {
    int pct = -1;
    CHECK_EQ(FormatBuildPercent(0.5f, &pct), std::string("50%"));
    CHECK_EQ(pct, 50);
    CHECK_EQ(FormatBuildPercent(0.999f, &pct), std::string("99%"));   // 99.9 -> 99 (trunc)
    CHECK_EQ(pct, 99);
    CHECK_EQ(FormatBuildPercent(1.7f, &pct), std::string("100%"));    // clamp >1.0
    CHECK_EQ(pct, 100);
}

// --- ComputeBarFraction (0x41089e): fraction + <1px snap-to-1 ---------------
TEST(ObjectUpdate, ComputeBarFraction) {
    // maxLen=100 over [0..200]; cur=100 -> 50px, target=150 -> 75px.
    BarFraction bf = ComputeBarFraction(100, 200, 0, 100, 150);
    CHECK(NearF(bf.filled, 50.0f));
    CHECK(NearF(bf.target, 75.0f));
    // tiny positive value (<1.0) snaps to 1.0.
    BarFraction tiny = ComputeBarFraction(1, 1000, 0, 1, 0);  // 0.001 -> snap 1.0
    CHECK(NearF(tiny.filled, 1.0f));
    CHECK(NearF(tiny.target, 0.0f));
}

// --- FormatBarPercent (0x41186d): cur*100/full + 0.5, ConvertX trunc --------
TEST(ObjectUpdate, FormatBarPercent) {
    int p = -1;
    CHECK_EQ(FormatBarPercent(50, 100, &p), std::string("50%"));   // 50.0+0.5=50.5 -> 50
    CHECK_EQ(p, 50);
    CHECK_EQ(FormatBarPercent(1, 3, &p), std::string("33%"));      // 33.33+0.5=33.8 -> 33
    CHECK_EQ(FormatBarPercent(2, 3, &p), std::string("67%"));      // 66.66+0.5=67.1 -> 67
}

// --- ChildPriorityClass (0x418fe4): 8/6/4/2 divisor order -------------------
TEST(ObjectUpdate, ChildPriorityClass) {
    CHECK_EQ(ChildPriorityClass(0), 0);
    CHECK_EQ(ChildPriorityClass(24), 8);   // 24%8==0 -> 8 (first)
    CHECK_EQ(ChildPriorityClass(6), 6);    // 6%8!=0, 6%6==0 -> 6
    CHECK_EQ(ChildPriorityClass(12), 6);   // 12%8!=0, 12%6==0 -> 6
    CHECK_EQ(ChildPriorityClass(4), 4);    // 4%8!=0,4%6!=0,4%4==0 -> 4
    CHECK_EQ(ChildPriorityClass(2), 2);    // -> 2
    CHECK_EQ(ChildPriorityClass(7), 0);    // odd, none
}

// --- WrapText (0x415e11): greedy line packing on spaces ---------------------
TEST(ObjectUpdate, WrapTextBasic) {
    // 8px glyphs, no gaps; width 40 fits 5 chars per line.
    WrapResult r = WrapText("aaa bbb ccc", 40, &Adv8, 0, 0, 12);
    // "aaa bbb" = 7 chars*8 = 56 > 40, so "bbb" wraps; each word is 24px.
    // "aaa" (24) + space-gap(0) + "bbb"(24) = 48 > 40 -> wrap.
    CHECK(r.lineCount >= 2);
    CHECK_EQ(r.contentH, r.lineCount * 12);
}
TEST(ObjectUpdate, WrapTextHardBreak) {
    // '~' forces a line break and is not emitted.
    WrapResult r = WrapText("ab~cd", 1000, &Adv8, 0, 0, 10);
    CHECK_EQ(r.lineCount, 2);
    CHECK(r.lines.find('~') == std::string::npos);  // '~' dropped
    CHECK(r.lines.find("ab") != std::string::npos);
    CHECK(r.lines.find("cd") != std::string::npos);
}
TEST(ObjectUpdate, WrapTextSingleLine) {
    WrapResult r = WrapText("short", 1000, &Adv8, 0, 0, 16);
    CHECK_EQ(r.lineCount, 1);
    CHECK_EQ(r.lines, std::string("short"));
    CHECK_EQ(r.contentH, 16);
}

// ===========================================================================
// Wave-21 — additional fully-recovered renderer-body fragments.
// ===========================================================================

// 0x40eef4..0x40ef12 — Object_Update label width clamp (min 48, optional cap).
TEST(ObjectUpdate, ClampWidthMinimum) {
    CHECK_EQ(ObjectUpdateClampWidth(10, 0), 48);   // below min -> 48
    CHECK_EQ(ObjectUpdateClampWidth(60, 0), 60);   // above min, no cap
}
TEST(ObjectUpdate, ClampWidthCap) {
    CHECK_EQ(ObjectUpdateClampWidth(200, 100), 100); // >= cap -> cap
    CHECK_EQ(ObjectUpdateClampWidth(80, 100), 80);   // below cap unchanged
    CHECK_EQ(ObjectUpdateClampWidth(100, 100), 100); // == cap -> cap (>=)
    CHECK_EQ(ObjectUpdateClampWidth(30, 100), 48);   // min applies before cap
}

// 0x419076..0x4190c9 — EntityChild_Process scroll-thumb enable flags.
TEST(EntityChildProcess, ScrollArrowsEnable) {
    // node235 = (page+step+pos16 >= rangeTop) ? 1 : 0
    ScrollArrowFlags f = EntityChildScrollArrows(/*thumbPos*/0, /*pos16*/10,
                                                 /*rangeTop*/20, /*page*/5, /*step*/6);
    CHECK_EQ(f.node235, 1);   // 5+6+10=21 >= 20
    CHECK_EQ(f.node234, 0);   // step != 0 -> OFF

    ScrollArrowFlags g = EntityChildScrollArrows(0, 0, 20, 5, 0);
    CHECK_EQ(g.node235, 0);   // 5+0+0=5 < 20
    CHECK_EQ(g.node234, 1);   // step == 0 -> ON
}

// 0x4190fd — EntityChild_Process scroll percent (rangeTop/thumbPos + 0.5, trunc).
TEST(EntityChildProcess, ScrollPercent) {
    CHECK_EQ(EntityChildScrollPercent(50, 100), 1);   // 0.5 + 0.5 = 1.0 -> 1
    CHECK_EQ(EntityChildScrollPercent(99, 100), 1);   // 0.99 + 0.5 = 1.49 -> 1
    CHECK_EQ(EntityChildScrollPercent(100, 100), 1);  // 1.0 + 0.5 = 1.5 -> 1
    CHECK_EQ(EntityChildScrollPercent(150, 100), 2);  // 1.5 + 0.5 = 2.0 -> 2
}

// 0x41924a / 0x4192c5 — EntityChild_Process 9-slice tile counts.
TEST(EntityChildProcess, BorderTileCounts) {
    BorderTileCounts c = EntityChildBorderTiles(80, 48);
    CHECK_EQ(c.horiz, (80 - 16) >> 3);   // 8
    CHECK_EQ(c.vert,  (48 - 16) >> 3);   // 4
    BorderTileCounts tiny = EntityChildBorderTiles(10, 10);
    CHECK(tiny.horiz <= 0);              // tiny widget -> no tiles
}
