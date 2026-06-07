#pragma once
// guild::play — InputRouter (PLAYABLE_PLAN P1).
//
// The app-spine per-frame hooks widgetDispatchMouseClick / hudHandleMouseClick are
// wired "REAL" (src/app/wiring.cpp) but drive the reconstructed gui click core off
// the *current* GUI input globals — which, headless, are never populated from the
// platform mouse. g_hoverObject stays -1, so gui::ResolveClickedSlot / RouteClick /
// Hud_DispatchClick run but always resolve "nothing": an inert no-op path.
//
// InputRouter de-inerts that path. Given a shim::IPlatform it latches the raw mouse +
// keys into the engine input state each frame (the DirectInput latch the original ran
// at the head of VIBE_Widget_DispatchMouseClick @0x421594), HIT-TESTS the cursor
// against the live Window/Widget geometry to populate g_hoverWindow / g_hoverObject /
// g_lastClickedWindow, and on a left-button click EDGE routes the click through the
// REAL reconstructed gui core:
//
//   gui::ResolveClickedSlot  (the dword_75BF10 child-slot resolution loop)
//   gui::RouteClick          (the click-routing id bookkeeping + radio-group select)
//   gui::Hud_DispatchClick   (the HUD hot-spot scan + flag-bit action classify)
//
// So a scripted click at known coords reaches a real selected slot / routed widget id /
// HUD action — an observable side effect, not a no-op.
//
// State touched (all reconstructed gui globals, gui/input.h + gui/hud.h):
//   g_mouseDown / g_mouseClick (dword_672220 / dword_672228 — held + click edge),
//   g_hoverWindow / g_hoverObject (dword_62D290 / dword_62D22C),
//   g_lastClickedWindow / g_lastClickedId / g_lastClickedSlot (dword_75BF08/38/00).
//
// Reuses, does not modify, the shared gui modules; no edits to wiring.cpp / *_hooks*.

#include "shim/IPlatform.h"

namespace guild::play {

// Virtual-key codes the router latches (Win32 VK_* values the original DInput map
// produced; only the few the in-game UI consumes are named here).
inline constexpr int kVkEscape = 0x1B;
inline constexpr int kVkReturn = 0x0D;
inline constexpr int kVkSpace  = 0x20;

// Click-mode word handed to Hud_DispatchClick. The in-world left-click drives the
// status-banner / selection-flag path (the v21 0x8 bit at LABEL_45 of 0x4bc280).
inline constexpr int kDefaultHudClickMode = 0x8; // gui::kClickFlagStatusBanner

// Outcome of routing one frame's input through the real gui click core.
struct RouteResult {
    bool clickEdge   = false; // a left-button press edge occurred this frame
    int  hoverWindow = -1;    // window slot under the cursor (g_hoverWindow)
    int  hoverObject = -1;    // widget index under the cursor (g_hoverObject)
    int  clickedSlot = -1;    // child slot resolved by gui::ResolveClickedSlot
    int  routedId    = -1;    // widget id returned by gui::RouteClick (-1 = not clickable)
    int  hudAction   = 0;     // gui::HudClickAction the HUD dispatch classified (int cast)
};

class InputRouter {
public:
    explicit InputRouter(shim::IPlatform& plat) : plat_(plat) {}

    // Latch + hit-test + (on click edge) route, in one call. Returns what was routed.
    // `hudClickMode` selects which HUD action path the dispatch takes (default the
    // status-banner / selection bit). Call once per frame, after the platform pump.
    RouteResult pump(int hudClickMode = kDefaultHudClickMode);

    // Latch the raw platform mouse + keys into the engine input globals (no routing).
    // Computes the left-button click edge (down now && up last frame).
    void latch();

    // Resolve the window + widget under the current latched cursor, writing the gui
    // hover globals (g_hoverWindow / g_hoverObject / g_lastClickedWindow). Returns the
    // hovered widget index, or -1 when the cursor is over no clickable widget.
    int hitTest();

    // Route the current hover through the REAL gui click core (ResolveClickedSlot +
    // RouteClick + Hud_DispatchClick). Returns the result; does nothing meaningful when
    // there is no hover. Normally called by pump() only on a click edge.
    RouteResult route(int hudClickMode = kDefaultHudClickMode);

    // --- latched key state (edge-detected) ---------------------------------
    bool keyDown(int vk) const;      // held this frame
    bool keyPressed(int vk) const;   // down this frame && up last frame (press edge)

    // Last latched cursor + button state (for inspection / tests).
    int  mouseX() const { return mouseX_; }
    int  mouseY() const { return mouseY_; }
    bool mouseLeftDown() const { return leftDown_; }
    bool clickEdge() const { return clickEdge_; }

    // Reset the edge-detection history (call when re-seeding a deterministic run).
    void resetEdges();

private:
    shim::IPlatform& plat_;
    int  mouseX_ = 0, mouseY_ = 0;
    bool leftDown_ = false;
    bool leftDownPrev_ = false;
    bool clickEdge_ = false;

    // Edge state for the named keys (kVk* above) + a couple of generic slots.
    static constexpr int kTrackedKeys = 8;
    int  trackedVk_[kTrackedKeys] = {kVkEscape, kVkReturn, kVkSpace, 0, 0, 0, 0, 0};
    bool keyNow_[kTrackedKeys]  = {false};
    bool keyPrev_[kTrackedKeys] = {false};

    int slotForVk(int vk) const;
};

} // namespace guild::play
