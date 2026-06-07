#pragma once
// Portable default backend for IPlatform — a headless host with a monotonic
// std::chrono clock and a scriptable input queue. NOT a translation of any
// gilde.exe function; this is a clean implementation of the interface contract
// (the OS boundary the game was decoupled from), usable for headless runs/tests.
#include "shim/IPlatform.h"
#include <chrono>
#include <unordered_map>

namespace guild::shim {

// Headless IPlatform: no real window, no OS message loop. timeMs() is a real
// monotonic millisecond clock; sleepMs() actually sleeps. Input (mouse + keys)
// is scriptable so tests can drive the game deterministically. pumpMessages()
// returns true until postQuit() is called.
class NullPlatform : public IPlatform {
public:
    NullPlatform();

    // IPlatform
    bool createMainWindow(const char* title, int w, int h, bool fullscreen) override;
    void destroyMainWindow() override;
    bool pumpMessages() override;
    std::uint32_t timeMs() override;
    void sleepMs(std::uint32_t ms) override;
    void getMouse(MouseState& out) override;
    bool keyDown(int vkey) override;

    // --- test/scripting hooks (not part of the interface) ---
    void postQuit();                       // make pumpMessages() return false
    void setMouse(const MouseState& m);    // script the next mouse read
    void setKey(int vkey, bool down);      // script a key state
    bool windowCreated() const { return window_created_; }

private:
    std::chrono::steady_clock::time_point start_;
    MouseState mouse_;
    std::unordered_map<int, bool> keys_;
    bool quit_ = false;
    bool window_created_ = false;
};

// HeadlessPlatform is the conventional name for the default headless backend.
using HeadlessPlatform = NullPlatform;

} // namespace guild::shim
