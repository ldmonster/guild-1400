// Unit tier — HUD overlay LAYOUT MATH golden vectors (no surface).  Asserts the
// reconstructed leaves the renderer composes produce the exact expected values:
//   * bar fill ratio -> pixels (VIBE_Hud_BuildScaledTiledBar scaling via the real
//     PlayerBar_OutputRatioPercent),
//   * money string (the real VIBE_Money_FormatWithSeparators),
//   * map marker projected coords (the real VIBE_MapView_ComputeMarkerScreenPos),
//   * the clock/date caption (the real VIBE_Clock_ComputeGameTimeOfDay).
#include "test.h"

#include "play/hud_render.h"
#include "gui/playerbar.h"
#include "gui/mapview.h"
#include "gui/hud.h"
#include "world/money_format.h"

#include <string>
#include <cstdio>

using namespace guild;

// Bar fill pixels: ratio in [0,1] -> fraction of the 80px sub-window.
TEST(HudRenderUnit, BarFillRatioGolden) {
    // pct = (int)(ratio*100); px = pct*subWinW/100.
    CHECK_EQ(play::HudBarFillPixels(0.0,  80), 0);
    CHECK_EQ(play::HudBarFillPixels(1.0,  80), 80);   // 100% -> full width
    CHECK_EQ(play::HudBarFillPixels(0.5,  80), 40);   // 50%  -> half
    CHECK_EQ(play::HudBarFillPixels(0.25, 80), 20);   // 25%  -> quarter
    // Clamp above 1.0 and below 0.0.
    CHECK_EQ(play::HudBarFillPixels(1.5,  80), 80);
    CHECK_EQ(play::HudBarFillPixels(-0.3, 80), 0);
    // The percent matches the slot's "%i%%" label (real PlayerBar leaf).
    CHECK_EQ(gui::PlayerBar_OutputRatioPercent(0.5), 50);
}

// Money string: real thousands grouping + the (substituted) currency glyph.
TEST(HudRenderUnit, MoneyStringGolden) {
    // Raw money formatter (golden from world::MoneyFormatWithSeparators).
    CHECK(world::MoneyFormatWithSeparators(0, 1)    == std::string("0") + world::kCurrencyGlyph);
    CHECK(world::MoneyFormatWithSeparators(999, 1)  == std::string("999") + world::kCurrencyGlyph);
    CHECK(world::MoneyFormatWithSeparators(1000, 1) == std::string("1.000") + world::kCurrencyGlyph);
    CHECK(world::MoneyFormatWithSeparators(-1234567, 1)
          == std::string("-1.234.567") + world::kCurrencyGlyph);

    // HUD string substitutes the glyph for a printable '$' (the 5x7 font draws it).
    CHECK(play::HudMoneyString(1000, 1) == "1.000$");
    CHECK(play::HudMoneyString(0, 1)    == "0$");
    CHECK(play::HudMoneyString(-1234567, 1) == "-1.234.567$");
}

// Date caption: the real Clock_ComputeTimeOfDay over the tick accumulator.
TEST(HudRenderUnit, DateCaptionGolden) {
    // tick 0 -> (0*scale + 0.5)*100 = 50 sec -> 00:00 (50s -> h=0,m=0).
    gui::ClockTime t0 = gui::Clock_ComputeTimeOfDay(0);
    CHECK_EQ(t0.totalSeconds, 50);
    CHECK_EQ(t0.h, 0); CHECK_EQ(t0.m, 0);
    CHECK(play::HudDateString(3, 0) == "DAY 3  00:00");

    // A larger tick advances the clock; assert the string is well-formed + matches.
    gui::ClockTime t = gui::Clock_ComputeTimeOfDay(20000);
    char buf[64];
    std::snprintf(buf, sizeof(buf), "DAY 7  %02d:%02d", t.h, t.m);
    CHECK(play::HudDateString(7, 20000) == std::string(buf));
}

// Map marker projection: the real MapView_ComputeMarkerScreenPos, exposed coords.
TEST(HudRenderUnit, MarkerCoordsGolden) {
    play::HudMarker mk{};
    mk.worldX = 100.0f;
    mk.worldZ = 50.0f;

    // Golden: project the same marker directly through the gui leaf and compare.
    gui::MapMarker ref{};
    ref.worldX = 100.0f; ref.worldZ = 50.0f;
    gui::MapView_ComputeMarkerScreenPos(ref, /*panX*/ 10, /*panY*/ 20, /*camOrig*/ 0);

    play::MarkerXY xy = play::HudMarkerScreenXY(mk, 10, 20, 0);
    CHECK_EQ(xy.x, (int)ref.screenX);
    CHECK_EQ(xy.y, (int)ref.screenY);
    // The projection is deterministic: same inputs -> same output.
    play::MarkerXY xy2 = play::HudMarkerScreenXY(mk, 10, 20, 0);
    CHECK_EQ(xy.x, xy2.x);
    CHECK_EQ(xy.y, xy2.y);
}
