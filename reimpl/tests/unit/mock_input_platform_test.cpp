// Unit tests for guild::shim::MockInputPlatform — the scriptable headless
// IPlatform used to drive the interactive loop deterministically.
#include "test.h"
#include "shim_impl/mock_input_platform.h"

using guild::shim::MockInputPlatform;
using guild::shim::MouseState;

TEST(MockInputPlatform, WindowAndFrameAdvance) {
    MockInputPlatform p;
    CHECK(p.createMainWindow("t", 640, 480, false) == true);
    p.destroyMainWindow(); // no-op, must not throw

    CHECK_EQ(p.frame(), 0);
    CHECK_EQ((int)p.timeMs(), 0);
    // Each pumpMessages advances the frame counter by 1.
    CHECK(p.pumpMessages() == true);
    CHECK_EQ(p.frame(), 1);
    CHECK_EQ((int)p.timeMs(), 16);
    CHECK(p.pumpMessages() == true);
    CHECK_EQ(p.frame(), 2);
    CHECK_EQ((int)p.timeMs(), 32);
}

TEST(MockInputPlatform, ScriptedMouse) {
    MockInputPlatform p;
    MouseState m;
    p.getMouse(m);
    CHECK_EQ(m.x, 0);
    CHECK_EQ(m.y, 0);
    CHECK(m.left == false);

    p.setMouse(120, 45, true);
    MouseState m2;
    p.getMouse(m2);
    CHECK_EQ(m2.x, 120);
    CHECK_EQ(m2.y, 45);
    CHECK(m2.left == true);

    p.setMouse(7, 8, false);
    MouseState m3;
    p.getMouse(m3);
    CHECK_EQ(m3.x, 7);
    CHECK_EQ(m3.y, 8);
    CHECK(m3.left == false);
}

TEST(MockInputPlatform, ScriptedKeys) {
    MockInputPlatform p;
    CHECK(p.keyDown(0x41) == false); // unset key reads false
    p.setKey(0x41, true);
    CHECK(p.keyDown(0x41) == true);
    CHECK(p.keyDown(0x42) == false); // other keys unaffected
    p.setKey(0x41, false);
    CHECK(p.keyDown(0x41) == false);
}

TEST(MockInputPlatform, QueuedClickFiresOnRightFrame) {
    MockInputPlatform p;
    p.queueClickAt(3, 200, 150);

    // Frames 0..2: no click.
    for (int f = 0; f < 3; ++f) {
        MouseState m;
        p.getMouse(m);
        CHECK(m.left == false);
        p.pumpMessages();
    }
    CHECK_EQ(p.frame(), 3);

    // Frame 3: click reported at the queued coords.
    MouseState onClick;
    p.getMouse(onClick);
    CHECK(onClick.left == true);
    CHECK_EQ(onClick.x, 200);
    CHECK_EQ(onClick.y, 150);

    // Advance to frame 4: auto-up.
    p.pumpMessages();
    CHECK_EQ(p.frame(), 4);
    MouseState afterClick;
    p.getMouse(afterClick);
    CHECK(afterClick.left == false);
}

TEST(MockInputPlatform, QuitAfterTriggersPumpFalse) {
    MockInputPlatform p;
    p.quitAfter(2);

    // Frames 0 and 1 keep running.
    CHECK(p.pumpMessages() == true);  // frame 0 -> 1
    CHECK_EQ(p.frame(), 1);
    CHECK(p.pumpMessages() == true);  // frame 1 -> 2
    CHECK_EQ(p.frame(), 2);
    // At frame 2 (== quit frame) pumpMessages reports false.
    CHECK(p.pumpMessages() == false); // frame 2 -> 3
    CHECK_EQ(p.frame(), 3);
    // And stays false for subsequent frames.
    CHECK(p.pumpMessages() == false);
}

TEST(MockInputPlatform, NoQuitByDefault) {
    MockInputPlatform p;
    for (int i = 0; i < 100; ++i) {
        CHECK(p.pumpMessages() == true);
    }
    CHECK_EQ(p.frame(), 100);
}
