#pragma once
// Host platform boundary: top-level window, frame timing, raw input.
// Replaces the Win32/user32/winmm/DInput usage in the original.
#include <cstdint>
#include <string>

namespace guild::shim {

struct MouseState {
    int x = 0, y = 0;
    bool left = false, right = false, middle = false;
    // Mouse-wheel notches accumulated since the previous getMouse() call
    // (+ = away from the user / zoom in). ADDITIVE (wave-3): replaces the
    // original's WM_MOUSEWHEEL stream (the dword_672254 wheel accumulator the
    // camera wheel-zoom branch of VIBE_Camera_UpdateMovement @0x4b41a8
    // consumes). Backends without a wheel always report 0.
    int wheel = 0;
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

    // Typed text accumulated since the last call (UTF-8), then cleared. The SDL backend
    // fills this from SDL_TEXTINPUT events — the equivalent of the Win32 WM_CHAR stream the
    // original's text-entry widgets consumed (rule 4: Win32 input -> SDL). Default: none
    // (headless/mock backends that don't support text entry).
    virtual std::string pollText() { return std::string(); }

    // Show/hide the OS mouse cursor. The original hides it and draws its own
    // _MOUSE_CURSOR sprite; default no-op for backends that don't have one.
    virtual void showSystemCursor(bool /*show*/) {}
};

} // namespace guild::shim
