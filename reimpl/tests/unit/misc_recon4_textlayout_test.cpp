// Golden tests for the VIBE_Property_Set font text layout loop.
//   gilde.exe 0x4159dc
#include "test.h"
#include "sim/misc_recon4_textlayout.h"

#include <string>
#include <vector>

using namespace guild::sim;
using guild::u16;

namespace {
// Fixed-metric "font": every glyph has leftBearing 0 and advance 10 (except space
// which is handled specially by the layout via env.spaceWidth).
GlyphMetrics fixedGlyph(void*, guild::u8) {
    GlyphMetrics g; g.leftBearing = 0; g.advance = 10; return g;
}

struct DrawLog { std::vector<char> chars; std::vector<int> xs; };
DrawLog* g_log = nullptr;
void logBasic(void*, int x, int y, void*, guild::u8 ch) {
    (void)y; g_log->chars.push_back((char)ch); g_log->xs.push_back(x);
}
} // namespace

static TextLayoutEnv makeEnv() {
    TextLayoutEnv e;
    e.glyphMetrics = &fixedGlyph;
    e.lineHeight = 16;
    e.spaceWidth = 5;
    e.tracking   = 1;
    e.clipRight  = 100000;   // effectively no clip
    e.drawBasic  = &logBasic;
    return e;
}

TEST(MiscRecon4, TextLayoutAdvancesPerGlyph) {
    DrawLog log; g_log = &log;
    TextLayoutEnv e = makeEnv();
    TextLayoutResult out;
    u16 lh = PropertySetDrawText(e, /*startX*/0, "AB", /*y*/0, nullptr,
                                 /*mode*/0, &out);
    CHECK_EQ((int)lh, 16);
    CHECK_EQ((int)log.chars.size(), 2);
    CHECK_EQ(log.chars[0], 'A');
    CHECK_EQ(log.xs[0], 0);            // first glyph at pen 0 (leftBearing 0)
    // after 'A': pen += tracking(1) + advance(10) = 11.
    CHECK_EQ(log.chars[1], 'B');
    CHECK_EQ(log.xs[1], 11);
    // total width = pen - startX = 22.
    CHECK_EQ(out.width, 22);
    CHECK_EQ((int)out.lineHeight, 16);
}

TEST(MiscRecon4, TextLayoutSpaceUsesSpaceWidth) {
    DrawLog log; g_log = &log;
    TextLayoutEnv e = makeEnv();
    TextLayoutResult out;
    // "A B": 'A' (advance 11) then ' ' (spaceWidth 5, no draw) then 'B'.
    PropertySetDrawText(e, 0, "A B", 0, nullptr, 0, &out);
    CHECK_EQ((int)log.chars.size(), 2);     // space not drawn
    CHECK_EQ(log.xs[0], 0);                 // 'A'
    CHECK_EQ(log.xs[1], 16);                // 0 +11 (A) +5 (space) = 16
    CHECK_EQ(out.width, 27);                // +11 advance after B
}

TEST(MiscRecon4, TextLayoutTildeSkipped) {
    DrawLog log; g_log = &log;
    TextLayoutEnv e = makeEnv();
    TextLayoutResult out;
    // '~' is skipped entirely (no draw, no post-advance).
    PropertySetDrawText(e, 0, "~A", 0, nullptr, 0, &out);
    CHECK_EQ((int)log.chars.size(), 1);
    CHECK_EQ(log.chars[0], 'A');
    CHECK_EQ(log.xs[0], 0);                 // '~' didn't advance the pen
}

TEST(MiscRecon4, TextLayoutClipBreak) {
    DrawLog log; g_log = &log;
    TextLayoutEnv e = makeEnv();
    e.clipRight = 12;                       // breaks once pen >= 12
    TextLayoutResult out;
    // 'A' drawn at 0, pen->11 (<12, continue). 'B' drawn at 11, pen->22 (>=12 break).
    PropertySetDrawText(e, 0, "ABC", 0, nullptr, 0, &out);
    CHECK_EQ((int)log.chars.size(), 2);     // 'C' never reached
    CHECK_EQ(log.chars[1], 'B');
}

TEST(MiscRecon4, TextLayoutModeFlagsDriveExtraPasses) {
    // mode bit2 -> shadow (velocity at +6), bit1 -> advanced, bit0 -> velocity.
    int basic = 0, adv = 0, vel = 0;
    static int* pb; static int* pa; static int* pv;
    pb = &basic; pa = &adv; pv = &vel;
    TextLayoutEnv e = makeEnv();
    e.drawBasic    = [](void*, int, int, void*, guild::u8){ (*pb)++; };
    e.drawAdvanced = [](void*, int, int, void*, guild::u8){ (*pa)++; };
    e.drawVelocity = [](void*, int, int, void*, guild::u8){ (*pv)++; };
    TextLayoutResult out;
    // mode = 0b111 = 7: shadow velocity + basic + advanced + velocity for 1 glyph.
    PropertySetDrawText(e, 0, "X", 0, nullptr, 7, &out);
    CHECK_EQ(basic, 1);
    CHECK_EQ(adv, 1);
    CHECK_EQ(vel, 2);   // bit2 shadow pass + bit0 pass
}
