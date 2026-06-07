#include "play/input_router.h"

#include "gui/input.h"
#include "gui/hud.h"
#include "gui/window.h"
#include "gui/object.h"
#include "gui/types.h"

namespace guild::play {

using namespace guild::gui;

namespace {

// Absolute on-screen bounds of a child widget. Object_AddToWindow stores the child's
// absolute x/y (window.x + localX) at +16/+18 and its w/h at +20/+22.
bool widgetContains(const Widget& w, int x, int y) {
    int wx = w.at<i16>(16);
    int wy = w.at<i16>(18);
    int ww = w.at<i16>(20);
    int wh = w.at<i16>(22);
    if (ww <= 0 || wh <= 0)
        return false;
    return x >= wx && x < wx + ww && y >= wy && y < wy + wh;
}

bool windowContains(const Window& win, int x, int y) {
    int wx = win.at<i16>(4);
    int wy = win.at<i16>(6);
    int ww = win.at<i16>(8);
    int wh = win.at<i16>(10);
    if (ww <= 0 || wh <= 0)
        return false;
    return x >= wx && x < wx + ww && y >= wy && y < wy + wh;
}

} // namespace

void InputRouter::resetEdges() {
    leftDownPrev_ = false;
    leftDown_ = false;
    clickEdge_ = false;
    for (int i = 0; i < kTrackedKeys; ++i) {
        keyNow_[i] = false;
        keyPrev_[i] = false;
    }
}

int InputRouter::slotForVk(int vk) const {
    for (int i = 0; i < kTrackedKeys; ++i)
        if (trackedVk_[i] == vk)
            return i;
    return -1;
}

void InputRouter::latch() {
    // Mouse: read the raw platform cursor + buttons (the DirectInput latch the original
    // ran at the top of the frame). Compute the left-button click EDGE.
    shim::MouseState m{};
    plat_.getMouse(m);
    mouseX_ = m.x;
    mouseY_ = m.y;
    leftDownPrev_ = leftDown_;
    leftDown_ = m.left;
    clickEdge_ = leftDown_ && !leftDownPrev_;

    // Push into the engine input globals (dword_672220 held / dword_672228 click edge).
    gui::g_mouseDown  = leftDown_ ? 1 : 0;
    gui::g_mouseClick = clickEdge_ ? 1 : 0;

    // Keys: latch the tracked virtual-keys with edge detection.
    for (int i = 0; i < kTrackedKeys; ++i) {
        keyPrev_[i] = keyNow_[i];
        keyNow_[i] = (trackedVk_[i] != 0) && plat_.keyDown(trackedVk_[i]);
    }
}

bool InputRouter::keyDown(int vk) const {
    int s = slotForVk(vk);
    return s >= 0 && keyNow_[s];
}

bool InputRouter::keyPressed(int vk) const {
    int s = slotForVk(vk);
    return s >= 0 && keyNow_[s] && !keyPrev_[s];
}

int InputRouter::hitTest() {
    // Scan enabled windows in descending slot order (later-created windows draw on
    // top — the highest in-use slot wins a geometric overlap). Within a window, scan
    // its child id list for the first widget whose absolute bounds contain the cursor.
    int hoverWindow = -1;
    int hoverObject = -1;
    for (int s = kMaxWindows - 1; s >= 0; --s) {
        Window& win = g_windows[s];
        if (!win.enabled())
            continue;
        if (!windowContains(win, mouseX_, mouseY_))
            continue;
        hoverWindow = s;
        const i32* list = WindowChildList(s);
        int count = win.objCount();
        for (int i = 0; i < count; ++i) {
            int widx = list[i];
            if (widx < 0 || widx >= kMaxWidgets)
                continue;
            if (widgetContains(g_widgets[widx], mouseX_, mouseY_)) {
                hoverObject = widx;
                // First hit wins; later children in the list are earlier in z-order
                // in this model, so we keep scanning to prefer the LAST match (the
                // topmost child). Continue to honour z-order.
            }
        }
        break; // topmost containing window resolved
    }

    gui::g_hoverWindow = hoverWindow;
    gui::g_hoverObject = hoverObject;
    // The click-router resolves child slots relative to the last-clicked window; the
    // hovered window is that window for an in-flight click.
    gui::g_lastClickedWindow = hoverWindow;
    return hoverObject;
}

RouteResult InputRouter::route(int hudClickMode) {
    RouteResult r{};
    r.clickEdge   = clickEdge_;
    r.hoverWindow = gui::g_hoverWindow;
    r.hoverObject = gui::g_hoverObject;

    // The REAL widget click core: resolve the hovered widget's child slot within its
    // window, then route the click (records last-clicked id, toggles a button, runs
    // the radio-group exclusive select). Both reach reconstructed gui/input.cpp.
    r.clickedSlot = gui::ResolveClickedSlot(gui::g_lastClickedWindow, gui::g_hoverObject);
    r.routedId    = gui::RouteClick(gui::g_lastClickedWindow, gui::g_hoverObject);

    // The REAL HUD click core: hot-spot slot scan + flag-bit action classify/dispatch.
    HudClickAction action = gui::Hud_DispatchClick(hudClickMode, gui::g_hoverObject);
    r.hudAction = static_cast<int>(action);
    return r;
}

RouteResult InputRouter::pump(int hudClickMode) {
    latch();
    hitTest();
    RouteResult r{};
    if (clickEdge_) {
        r = route(hudClickMode);
    } else {
        r.clickEdge   = false;
        r.hoverWindow = gui::g_hoverWindow;
        r.hoverObject = gui::g_hoverObject;
    }
    return r;
}

} // namespace guild::play
