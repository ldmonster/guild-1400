#pragma once
// Headless scriptable IPlatform for driving the play-layer loop tests (mode_fsm,
// input_router, hud_binder, determinism, interactive). No window/OS — mouse,
// keyboard, and a "quit after N pumps" are scripted, so a deterministic frame
// sequence can be fed to the reconstructed menu/input/HUD dispatch.
//
// Portable (no SDL/Vulkan): always available. See also MockInputPlatform
// (queued-click-by-frame variant); ScriptedPlatform is the level-state variant
// (setMouse/pressKey/releaseKey persist until changed) the play-layer tests use.
#include "shim/IPlatform.h"

#include <set>

namespace guild::shim {

class ScriptedPlatform : public IPlatform {
public:
    ScriptedPlatform() = default;

    bool createMainWindow(const char* /*title*/, int /*w*/, int /*h*/,
                          bool /*fullscreen*/) override { return true; }
    void destroyMainWindow() override {}

    // Returns true for the first `quitPumps_` calls, then false (window closed).
    bool pumpMessages() override {
        if (quitPumps_ >= 0 && pumps_ >= quitPumps_)
            return false;
        ++pumps_;
        return true;
    }

    std::uint32_t timeMs() override { return static_cast<std::uint32_t>(pumps_) * 16u; }
    void sleepMs(std::uint32_t /*ms*/) override {}

    void getMouse(MouseState& out) override { out = mouse_; }
    bool keyDown(int vkey) override { return keys_.count(vkey) != 0; }

    // --- scripting API (used by the play-layer tests) -----------------------
    void setMouse(int x, int y, bool left) {
        mouse_.x = x; mouse_.y = y; mouse_.left = left;
    }
    void pressKey(int vkey)   { keys_.insert(vkey); }
    void releaseKey(int vkey) { keys_.erase(vkey); }
    void quitAfterPumps(int n) { quitPumps_ = n; }
    int  pumps() const { return pumps_; }

private:
    MouseState   mouse_;
    std::set<int> keys_;
    int pumps_ = 0;
    int quitPumps_ = -1;   // <0 = never quit
};

} // namespace guild::shim
