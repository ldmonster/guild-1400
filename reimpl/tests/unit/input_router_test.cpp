// Unit: InputRouter coordinate->slot mapping + key-latch golden cases.
//
// Drives the router against the reconstructed gui Window/Widget geometry with a
// scripted platform: a click at a known coordinate must hit-test to the right widget,
// resolve the right child slot, and route through the REAL gui click core (observable:
// the routed widget id + the radio-group selection it drives). Keys latch with edges.
#include "test.h"

#include "play/input_router.h"
#include "shim_impl/scripted_platform.h"

#include "gui/input.h"
#include "gui/hud.h"
#include "gui/window.h"
#include "gui/object.h"
#include "gui/radiogroup.h"
#include "gui/types.h"

using namespace guild;
using namespace guild::gui;

namespace {

// Reset all gui input/window/widget/radio state to a clean slate.
void resetGui() {
    ResetWindows();
    ResetWidgets();
    ResetRadioGroups();
    ResetHudSlots();
    ResetInputState();
}

// Build a window with `n` clickable child buttons laid out as a horizontal row of
// `cellW`-wide, `cellH`-tall cells starting at (winX, winY). Returns the window slot;
// fills `outWidgets` with the child widget indices and registers them as a radio group.
int buildButtonRow(int winX, int winY, int cellW, int cellH, int n,
                   int* outWidgets, int* outRadioGroup) {
    int slot = Window_Create((i16)winX, (i16)winY, (i16)(cellW * n), (i16)cellH, 0);
    for (int i = 0; i < n; ++i) {
        int widx = Object_AddToWindow(slot, /*y=*/0, /*x=*/(i16)(i * cellW), /*gfx=*/0);
        outWidgets[i] = widx;
        // Object_AddToWindow sets the child's absolute x/y but not w/h; set the
        // clickable bounds explicitly (the original's widget geometry init).
        Widget& w = g_widgets[widx];
        w.w() = (i16)cellW;
        w.h() = (i16)cellH;
        w.id() = 500 + i; // distinct, non-zero routed id per button
    }
    *outRadioGroup = RadioGroup_Create(n, outWidgets); // sets btnFlagA on each -> clickable
    return slot;
}

} // namespace

TEST(InputRouterUnit, ClickMapsToMiddleButtonSlot) {
    resetGui();
    int widgets[3], grp = -1;
    // Window at (100,50); three 40x20 cells. Cell 1 spans x in [140,180).
    int win = buildButtonRow(100, 50, 40, 20, 3, widgets, &grp);
    CHECK(win >= 0);
    CHECK(grp >= 0);

    shim::ScriptedPlatform plat;
    play::InputRouter router(plat);

    // Click the MIDDLE button: x=160 (inside cell 1), y=58 (inside [50,70)).
    plat.setMouse(160, 58, /*left=*/true);
    play::RouteResult r = router.pump();

    CHECK(r.clickEdge);
    CHECK_EQ(r.hoverWindow, win);
    CHECK_EQ(r.hoverObject, widgets[1]);
    // Child slot within the window's id list: button 1 is the second child -> slot 1.
    CHECK_EQ(r.clickedSlot, 1);
    // RouteClick returns the routed widget id (= its widget +8 id), not -1 (clickable).
    CHECK_EQ(r.routedId, g_widgets[widgets[1]].id());
    // Observable side effect: the radio group selected button 1.
    CHECK_EQ(g_radioGroups[grp].selected, 1);
    // And gui::g_lastClickedId records the routed widget id.
    CHECK_EQ(g_lastClickedId, g_widgets[widgets[1]].id());
}

TEST(InputRouterUnit, ClickMapsToEachColumnDeterministically) {
    resetGui();
    int widgets[4], grp = -1;
    int win = buildButtonRow(0, 0, 50, 30, 4, widgets, &grp);
    CHECK(win >= 0);

    shim::ScriptedPlatform plat;
    play::InputRouter router(plat);

    // Each column center -> its own slot. Release between clicks so each is an edge.
    const int centers[4] = {25, 75, 125, 175};
    for (int i = 0; i < 4; ++i) {
        plat.setMouse(centers[i], 15, /*left=*/false); // release
        router.pump();
        plat.setMouse(centers[i], 15, /*left=*/true);  // press edge
        play::RouteResult r = router.pump();
        CHECK_EQ(r.hoverObject, widgets[i]);
        CHECK_EQ(r.clickedSlot, i);
        CHECK_EQ(g_radioGroups[grp].selected, i);
    }
}

TEST(InputRouterUnit, ClickOutsideAnyWidgetRoutesNothing) {
    resetGui();
    int widgets[2], grp = -1;
    buildButtonRow(100, 100, 40, 20, 2, widgets, &grp);

    shim::ScriptedPlatform plat;
    play::InputRouter router(plat);

    // Click far away from the window.
    plat.setMouse(10, 10, /*left=*/true);
    play::RouteResult r = router.pump();
    CHECK(r.clickEdge);
    CHECK_EQ(r.hoverWindow, -1);
    CHECK_EQ(r.hoverObject, -1);
    CHECK_EQ(r.clickedSlot, -1);
    CHECK_EQ(r.routedId, -1);
    CHECK_EQ(g_radioGroups[grp].selected, -1); // never selected -> stays -1
}

TEST(InputRouterUnit, ClickEdgeOnlyOnPressTransition) {
    resetGui();
    int widgets[1], grp = -1;
    buildButtonRow(0, 0, 60, 60, 1, widgets, &grp);

    shim::ScriptedPlatform plat;
    play::InputRouter router(plat);

    // Frame 1: button up over the widget -> no edge.
    plat.setMouse(30, 30, false);
    play::RouteResult r0 = router.pump();
    CHECK(!r0.clickEdge);

    // Frame 2: button DOWN -> click edge.
    plat.setMouse(30, 30, true);
    play::RouteResult r1 = router.pump();
    CHECK(r1.clickEdge);
    CHECK_EQ(r1.hoverObject, widgets[0]);

    // Frame 3: still held -> NOT an edge (held, not a new press).
    plat.setMouse(30, 30, true);
    play::RouteResult r2 = router.pump();
    CHECK(!r2.clickEdge);

    // Frame 4: released -> no edge.
    plat.setMouse(30, 30, false);
    play::RouteResult r3 = router.pump();
    CHECK(!r3.clickEdge);
}

TEST(InputRouterUnit, HudDispatchClassifiesActionFromMode) {
    resetGui();
    int widgets[1], grp = -1;
    buildButtonRow(0, 0, 40, 40, 1, widgets, &grp);
    // Register the widget into a HUD action slot so the hot-spot scan resolves it.
    g_hudSlots[0].widget = widgets[0];
    g_hudSlots[0].owner  = 1;

    shim::ScriptedPlatform plat;
    play::InputRouter router(plat);

    plat.setMouse(20, 20, true);
    // Selection click-mode (0x800) -> kSelection (HudClickAction enum value 1).
    play::RouteResult r = router.pump(gui::kClickFlagSelection);
    CHECK_EQ(r.hoverObject, widgets[0]);
    CHECK_EQ(r.hudAction, static_cast<int>(HudClickAction::kSelection));

    // Status-banner mode (0x8) -> kStatusBanner.
    plat.setMouse(20, 20, false); router.pump();
    plat.setMouse(20, 20, true);
    play::RouteResult r2 = router.pump(gui::kClickFlagStatusBanner);
    CHECK_EQ(r2.hudAction, static_cast<int>(HudClickAction::kStatusBanner));
}

TEST(InputRouterUnit, KeyLatchGoldenEdges) {
    resetGui();
    shim::ScriptedPlatform plat;
    play::InputRouter router(plat);

    // Nothing pressed.
    router.latch();
    CHECK(!router.keyDown(play::kVkEscape));
    CHECK(!router.keyPressed(play::kVkEscape));

    // Press ESC: down + press edge.
    plat.pressKey(play::kVkEscape);
    router.latch();
    CHECK(router.keyDown(play::kVkEscape));
    CHECK(router.keyPressed(play::kVkEscape));

    // Hold ESC: still down, NOT a fresh press edge.
    router.latch();
    CHECK(router.keyDown(play::kVkEscape));
    CHECK(!router.keyPressed(play::kVkEscape));

    // Release ESC: up.
    plat.releaseKey(play::kVkEscape);
    router.latch();
    CHECK(!router.keyDown(play::kVkEscape));
    CHECK(!router.keyPressed(play::kVkEscape));

    // Untracked key never reports.
    plat.pressKey(0x41 /* 'A' */);
    router.latch();
    CHECK(!router.keyDown(0x41));
}

TEST(InputRouterUnit, TopmostWindowWinsOverlap) {
    resetGui();
    int wa[1], wb[1], ga = -1, gb = -1;
    // Two overlapping windows; the LATER (higher slot) one should win the hit-test.
    int winA = buildButtonRow(0, 0, 100, 100, 1, wa, &ga);
    int winB = buildButtonRow(20, 20, 60, 60, 1, wb, &gb);
    CHECK(winB > winA);

    shim::ScriptedPlatform plat;
    play::InputRouter router(plat);

    // Point (40,40) is inside both; the topmost (winB) child should resolve.
    plat.setMouse(40, 40, true);
    play::RouteResult r = router.pump();
    CHECK_EQ(r.hoverWindow, winB);
    CHECK_EQ(r.hoverObject, wb[0]);
}
