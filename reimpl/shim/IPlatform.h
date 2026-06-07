#pragma once
// Host platform boundary: top-level window, frame timing, raw input.
// Replaces the Win32/user32/winmm/DInput usage in the original.
#include <cstdint>

namespace guild::shim {

struct MouseState {
    int x = 0, y = 0;
    bool left = false, right = false, middle = false;
};

class IPlatform {
public:
    virtual ~IPlatform() = default;

    virtual bool createMainWindow(const char* title, int w, int h, bool fullscreen) = 0;
    virtual void destroyMainWindow() = 0;

    // Pump OS messages; returns false when a quit was requested.
    virtual bool pumpMessages() = 0;

    // winmm timeGetTime equivalent: milliseconds since start, monotonic.
    virtual std::uint32_t timeMs() = 0;
    virtual void sleepMs(std::uint32_t ms) = 0;

    virtual void getMouse(MouseState& out) = 0;
    virtual bool keyDown(int vkey) = 0;
};

} // namespace guild::shim
