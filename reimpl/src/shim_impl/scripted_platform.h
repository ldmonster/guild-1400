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
#include <string>
#include <vector>

namespace guild::shim {

class ScriptedPlatform : public IPlatform {
public:
    ScriptedPlatform() = default;

    bool createMainWindow(const char* /*title*/, int /*w*/, int /*h*/,
                          bool /*fullscreen*/) override { return true; }
    void destroyMainWindow() override {}

    // Returns true for the first `quitPumps_` calls, then false (window closed).
    // When a timeline is scripted (scriptAt), the step whose pump index equals the
    // current pump count REPLACES the whole input state just before the pump
    // returns, so the state a screen reads after its p-th pumpMessages() call is
    // exactly timeline state[p] (states persist until the next step fires).
    bool pumpMessages() override {
        if (quitPumps_ >= 0 && pumps_ >= quitPumps_)
            return false;
        applyTimeline(pumps_);
        ++pumps_;
        return true;
    }

    std::uint32_t timeMs() override { return static_cast<std::uint32_t>(pumps_) * 16u; }
    void sleepMs(std::uint32_t /*ms*/) override {}

    void getMouse(MouseState& out) override {
        out = mouse_;
        out.wheel = wheelAccum_;   // notches since the previous getMouse()
        wheelAccum_ = 0;           // read-and-clear (matches the SDL backends)
    }
    bool keyDown(int vkey) override { return keys_.count(vkey) != 0; }
    std::string pollText() override { std::string s; s.swap(textBuf_); return s; }

    // --- scripting API (used by the play-layer tests) -----------------------
    void setMouse(int x, int y, bool left) {
        mouse_.x = x; mouse_.y = y; mouse_.left = left;
    }
    // Queue wheel notches: the next getMouse() reports them once, then clears
    // (the same read-and-clear contract as the SDL backends' SDL_MOUSEWHEEL
    // accumulation). ADDITIVE: with no setWheel calls every read returns 0.
    void setWheel(int notches) { wheelAccum_ += notches; }
    void pressKey(int vkey)   { keys_.insert(vkey); }
    void releaseKey(int vkey) { keys_.erase(vkey); }
    void queueText(const std::string& s) { textBuf_ += s; }   // next pollText() returns it
    void quitAfterPumps(int n) { quitPumps_ = n; }
    int  pumps() const { return pumps_; }

    // --- pump-indexed TIMELINE scripting (multi-screen flows) ----------------
    // Every play-layer screen pumps exactly once per frame, so one global
    // pump-indexed script can drive a whole multi-screen flow (menu -> city pick
    // -> wizard -> ...) deterministically: scriptAt(p, ...) installs the full
    // input state that becomes visible right after the p-th pumpMessages() call
    // (0-based) and persists until a later step fires. `keys` is the complete
    // set of vkeys held down from that pump on (it REPLACES the held set);
    // `textOnce` is appended to the pollText() stream once, when the step fires.
    // `wheelOnce` queues wheel notches once when the step fires (reported by the
    // next getMouse(), then cleared). Additive: with no timeline steps the
    // level-state API above is unchanged.
    void scriptAt(int pump, int x, int y, bool left,
                  std::vector<int> keys = {}, std::string textOnce = std::string(),
                  int wheelOnce = 0) {
        TimelineStep st;
        st.pump = pump; st.x = x; st.y = y; st.left = left;
        st.keys = std::move(keys); st.text = std::move(textOnce);
        st.wheel = wheelOnce;
        timeline_.push_back(std::move(st));
    }

private:
    struct TimelineStep {
        int pump = 0;
        int x = 0, y = 0;
        bool left = false;
        std::vector<int> keys;   // the full held-key set from this pump on
        std::string text;        // queued into pollText() once when fired
        int wheel = 0;           // wheel notches queued once when fired
    };

    void applyTimeline(int pump) {
        for (const TimelineStep& st : timeline_) {
            if (st.pump != pump) continue;
            mouse_.x = st.x; mouse_.y = st.y; mouse_.left = st.left;
            keys_.clear();
            keys_.insert(st.keys.begin(), st.keys.end());
            textBuf_ += st.text;
            wheelAccum_ += st.wheel;
        }
    }

    MouseState   mouse_;
    std::set<int> keys_;
    std::string  textBuf_;
    int wheelAccum_ = 0;   // queued wheel notches (consumed by getMouse)
    std::vector<TimelineStep> timeline_;
    int pumps_ = 0;
    int quitPumps_ = -1;   // <0 = never quit
};

} // namespace guild::shim
