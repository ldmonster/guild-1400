// tests/unit/interactive_test.cpp — unit coverage for the OUTER INTERACTIVE LOOP
// adapter (src/play/interactive.cpp), specifically PlatformClickSource: the
// shim::IPlatform-backed MenuClickSource.
//
// Asserts the OS-boundary -> menu-input mapping in isolation (headless via the
// scriptable MockInputPlatform): hover resolved through an injected hit-test,
// left-button DOWN-edge click detection (one click per press, not per held frame),
// and the ESC (VK_ESCAPE == 27) quit gate.
#include "test.h"
#include "play/interactive.h"
#include "shim_impl/mock_input_platform.h"

using namespace guild;

// Hover comes from the injected hit-test; with no hit-test wired, nothing is hovered.
TEST(InteractiveClickSrc, HoverDefaultsToNoneWithoutHitTest) {
    shim::MockInputPlatform plat;
    plat.setMouse(100, 200, false);
    play::PlatformClickSource src(plat); // no hit-test
    CHECK_EQ(src.hoverThisFrame(0), -1);
}

// Hover resolves the cursor through the injected hit-test fn.
TEST(InteractiveClickSrc, HoverResolvedViaInjectedHitTest) {
    shim::MockInputPlatform plat;
    // A hit-test that returns radio slot 0 only when the cursor is over the New Game
    // button band (x in [32..96], y in [10..40]); -1 elsewhere.
    auto hit = [](int x, int y) -> int {
        if (x >= 32 && x <= 96 && y >= 10 && y <= 40) return 0;
        return -1;
    };
    play::PlatformClickSource src(plat, hit);

    plat.setMouse(36, 14, false);          // over New Game
    CHECK_EQ(src.hoverThisFrame(0), 0);

    plat.setMouse(500, 500, false);        // nowhere
    CHECK_EQ(src.hoverThisFrame(1), -1);
}

// clickThisFrame fires exactly once on the left-button DOWN edge, not while held.
TEST(InteractiveClickSrc, ClickIsDownEdgeOnly) {
    shim::MockInputPlatform plat;
    play::PlatformClickSource src(plat);

    plat.setMouse(10, 10, false);
    CHECK(src.clickThisFrame(0) == false); // button up

    plat.setMouse(10, 10, true);
    CHECK(src.clickThisFrame(1) == true);  // false -> true: the edge
    CHECK(src.clickThisFrame(2) == false); // still held: no new edge

    plat.setMouse(10, 10, false);
    CHECK(src.clickThisFrame(3) == false); // released
    plat.setMouse(10, 10, true);
    CHECK(src.clickThisFrame(4) == true);  // pressed again: a new edge
}

// escThisFrame mirrors keyDown(VK_ESCAPE == 27).
TEST(InteractiveClickSrc, EscFromVkEscapeKey) {
    shim::MockInputPlatform plat;
    play::PlatformClickSource src(plat);

    CHECK(src.escThisFrame(0) == false);   // unset
    plat.setKey(play::kVkEscape, true);
    CHECK_EQ(play::kVkEscape, 27);
    CHECK(src.escThisFrame(1) == true);
    plat.setKey(play::kVkEscape, false);
    CHECK(src.escThisFrame(2) == false);
}
