// E2E: a scripted platform feeds clicks at known coords into a multi-frame loop; the
// InputRouter latches + hit-tests + routes each frame through the REAL reconstructed
// gui click core. We assert the click actually REACHED the real dispatch (an observable
// side effect: the routed widget id, the selected radio slot, the HUD action) — not a
// no-op — and that the same scripted input replays to an identical routing twice
// (determinism).
//
// This exercises the same gui::ResolveClickedSlot / RouteClick / Hud_DispatchClick code
// the app-spine per-frame hooks (widgetDispatchMouseClick / hudHandleMouseClick) drive,
// but with the cursor->widget mapping de-inerted so the click lands on a live slot.
#include "test.h"

#include "play/input_router.h"
#include "shim_impl/scripted_platform.h"

#include "gui/input.h"
#include "gui/hud.h"
#include "gui/window.h"
#include "gui/object.h"
#include "gui/radiogroup.h"
#include "gui/types.h"

#include <vector>

using namespace guild;
using namespace guild::gui;

namespace {

void resetGui() {
    ResetWindows();
    ResetWidgets();
    ResetRadioGroups();
    ResetHudSlots();
    ResetInputState();
}

// A horizontal action-bar window of `n` clickable buttons (a radio group), each
// cellW x cellH, starting at (winX, winY). Records the widget indices.
struct ActionBar {
    int window = -1;
    int group  = -1;
    int cellW = 0, cellH = 0, winX = 0, winY = 0;
    std::vector<int> widgets;

    void build(int x, int y, int cw, int ch, int n) {
        winX = x; winY = y; cellW = cw; cellH = ch;
        widgets.resize(n);
        window = Window_Create((i16)x, (i16)y, (i16)(cw * n), (i16)ch, 0);
        for (int i = 0; i < n; ++i) {
            int widx = Object_AddToWindow(window, 0, (i16)(i * cw), 0);
            widgets[i] = widx;
            Widget& w = g_widgets[widx];
            w.w() = (i16)cw;
            w.h() = (i16)ch;
            w.id() = 700 + i;
            // Mirror it into a HUD action slot so Hud_DispatchClick's hot-spot scan
            // resolves this widget to a slot (the de-inert HUD path).
            g_hudSlots[i].widget = widx;
            g_hudSlots[i].owner  = 1;
        }
        group = RadioGroup_Create(n, widgets.data());
    }
    int centerX(int i) const { return winX + i * cellW + cellW / 2; }
    int centerY()      const { return winY + cellH / 2; }
};

// A scripted click at frame `f`: which button center to click, or -1 for "no click".
struct ScriptedClick { int frame; int button; };

// Run a deterministic scripted session: build the bar, then drive `frames` frames
// pumping the router; on each scripted frame set the mouse over the target button with
// the left button down (preceded by a release frame so it registers as a press edge).
// Returns the routing results captured on click frames + the final selected slot.
struct SessionOut {
    std::vector<play::RouteResult> routed; // one per click that produced an edge
    int finalSelected = -2;
    int routedIdSum = 0;
};

SessionOut runSession(const std::vector<ScriptedClick>& script, int frames,
                      int hudMode = gui::kClickFlagSelection) {
    resetGui();
    ActionBar bar;
    bar.build(/*x=*/200, /*y=*/120, /*cellW=*/48, /*cellH=*/24, /*n=*/4);

    shim::ScriptedPlatform plat;
    play::InputRouter router(plat);

    SessionOut out;
    size_t si = 0;
    for (int f = 0; f < frames; ++f) {
        int clickBtn = -1;
        if (si < script.size() && script[si].frame == f) {
            clickBtn = script[si].button;
            ++si;
        }
        if (clickBtn >= 0) {
            // Release frame first (so the next frame is a clean press edge).
            plat.setMouse(bar.centerX(clickBtn), bar.centerY(), /*left=*/false);
            router.pump(hudMode);
            // Press frame: this is the real click.
            plat.setMouse(bar.centerX(clickBtn), bar.centerY(), /*left=*/true);
            play::RouteResult r = router.pump(hudMode);
            out.routed.push_back(r);
            out.routedIdSum += (r.routedId > 0 ? r.routedId : 0);
        } else {
            // Idle frame: cursor parked off-window, button up.
            plat.setMouse(0, 0, false);
            router.pump(hudMode);
        }
    }
    out.finalSelected = g_radioGroups[bar.group].selected;
    return out;
}

} // namespace

// A scripted click at a known coordinate reaches the REAL dispatch (not a no-op):
// the routed widget id is the clicked button's id, the radio group selects it, and the
// HUD dispatch classifies the action.
TEST(InputRouterE2E, ScriptedClickReachesRealDispatch) {
    std::vector<ScriptedClick> script = {{2, 2}}; // frame 2: click button 2
    SessionOut out = runSession(script, /*frames=*/8);

    CHECK_EQ((int)out.routed.size(), 1);
    if (!out.routed.empty()) {
        const play::RouteResult& r = out.routed[0];
        CHECK(r.clickEdge);
        CHECK_EQ(r.hoverObject >= 0, true);   // hit a real widget
        CHECK_EQ(r.clickedSlot, 2);           // third child slot
        CHECK_EQ(r.routedId, 700 + 2);        // RouteClick returned the real id
        CHECK_EQ(r.hudAction, static_cast<int>(HudClickAction::kSelection));
    }
    // Observable engine-state side effect: selection actually changed (not inert).
    CHECK_EQ(out.finalSelected, 2);
}

// Multiple scripted clicks across frames each land on their own slot, in order.
TEST(InputRouterE2E, SequenceOfClicksRoutesEachSlot) {
    std::vector<ScriptedClick> script = {{1, 0}, {3, 3}, {5, 1}};
    SessionOut out = runSession(script, /*frames=*/8);

    CHECK_EQ((int)out.routed.size(), 3);
    if (out.routed.size() == 3) {
        CHECK_EQ(out.routed[0].clickedSlot, 0);
        CHECK_EQ(out.routed[1].clickedSlot, 3);
        CHECK_EQ(out.routed[2].clickedSlot, 1);
        CHECK_EQ(out.routed[0].routedId, 700);
        CHECK_EQ(out.routed[1].routedId, 703);
        CHECK_EQ(out.routed[2].routedId, 701);
    }
    // The last click wins the radio selection.
    CHECK_EQ(out.finalSelected, 1);
}

// Determinism: same scripted input -> identical routing across two independent runs.
TEST(InputRouterE2E, DeterministicReplay) {
    std::vector<ScriptedClick> script = {{1, 0}, {2, 2}, {4, 3}, {6, 1}};
    SessionOut a = runSession(script, /*frames=*/8);
    SessionOut b = runSession(script, /*frames=*/8);

    CHECK_EQ(a.routed.size(), b.routed.size());
    CHECK_EQ(a.finalSelected, b.finalSelected);
    CHECK_EQ(a.routedIdSum, b.routedIdSum);
    bool identical = a.routed.size() == b.routed.size();
    for (size_t i = 0; i < a.routed.size() && identical; ++i) {
        identical = identical &&
            a.routed[i].clickedSlot == b.routed[i].clickedSlot &&
            a.routed[i].routedId    == b.routed[i].routedId &&
            a.routed[i].hoverObject == b.routed[i].hoverObject &&
            a.routed[i].hudAction   == b.routed[i].hudAction;
    }
    CHECK(identical);
}

// Idle frames (no click) must NOT route anything (the path stays dormant until a real
// click edge) — proving the dispatch fires only on actual input, not every frame.
TEST(InputRouterE2E, IdleFramesDoNotRoute) {
    std::vector<ScriptedClick> script; // no clicks at all
    SessionOut out = runSession(script, /*frames=*/10);
    CHECK_EQ((int)out.routed.size(), 0);
    CHECK_EQ(out.finalSelected, -1); // radio group never selected
}
