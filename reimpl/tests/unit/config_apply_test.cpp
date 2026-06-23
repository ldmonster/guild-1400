// Golden-vector tests for VIBE_Config_ApplyCameraAndScrollSettings (0x56c0cc).
#include "play/config_apply.h"

#include "gui/input_state.h"

#include "../framework/test.h"

#include <cmath>

using namespace guild;
using namespace guild::play;

TEST(ConfigApply, LinearScrollBranch) {
    CameraScrollOptions opt;
    opt.field0 = 80;             // dword_1233558
    opt.sensitivityByte = 30;    // byte at +4
    opt.wheel = 200;             // dword_1233560
    CameraScrollGlobals g;

    // advancedScroll = false -> (double)80 * 0.00625 + 0.25 = 0.5 + 0.25 = 0.75
    i32 result = ConfigApplyCameraAndScrollSettings(opt, /*advanced*/false, /*raw*/0, g);

    CHECK_EQ(result, 30);
    CHECK_EQ(g.focusReset, 0);
    CHECK_EQ(g.edgeMargin, 70);                 // 100 - 30
    CHECK(std::fabs(g.scrollSpeed - 0.75f) < 1e-6f);
    // SetWheelBase(200 - 64) = 136 + 256 = 392 -> g_wheelBase.
    CHECK_EQ(g.wheelBase, 392);
    CHECK_EQ(guild::gui::g_wheelBase, 392);
}

TEST(ConfigApply, AdvancedScrollBranch) {
    CameraScrollOptions opt;
    opt.field0 = 0;
    opt.sensitivityByte = 100;
    opt.wheel = 64;
    CameraScrollGlobals g;

    // advanced raw = 4 -> ((double)4 * 0.25 + 0.5) * 0.8 = (1.0 + 0.5)*0.8 = 1.2
    i32 result = ConfigApplyCameraAndScrollSettings(opt, /*advanced*/true, /*raw*/4, g);

    CHECK_EQ(result, 100);
    CHECK_EQ(g.edgeMargin, 0);                  // 100 - 100
    CHECK(std::fabs(g.scrollSpeed - 1.2f) < 1e-6f);
    // SetWheelBase(64 - 64) = 0 + 256 = 256.
    CHECK_EQ(g.wheelBase, 256);
}

TEST(ConfigApply, AdvancedRawUnsigned) {
    // dword_631284 is read UNSIGNED; a value with the high bit set must NOT go
    // negative. raw = 0x80000000 -> (double)2147483648 * 0.25 + 0.5) * 0.8.
    CameraScrollOptions opt;
    CameraScrollGlobals g;
    ConfigApplyCameraAndScrollSettings(opt, true, 0x80000000u, g);
    double expected = (static_cast<double>(0x80000000u) * 0.25 + 0.5) * 0.8;
    CHECK(g.scrollSpeed > 0.0f);                 // positive (unsigned interpretation)
    CHECK(std::fabs(static_cast<double>(g.scrollSpeed) - expected) <
          expected * 1e-6);
}
