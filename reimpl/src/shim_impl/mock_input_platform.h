#pragma once
// Scriptable headless IPlatform for driving the interactive loop deterministically
// in tests. NOT a translation of any gilde.exe function; it is a clean test double
// for the OS boundary (window/timing/input) the game was decoupled from. Portable:
// no SDL/Win32, so no GUILD_HAVE_* guards needed.
#include "shim/IPlatform.h"
#include <unordered_map>

namespace guild::shim {

// MockInputPlatform: a frame-driven IPlatform whose entire behaviour is scripted up
// front. pumpMessages() advances an internal frame counter by 1 and returns false
// once the scripted quit frame is reached. timeMs() is frame*16 (~60fps).
// getMouse()/keyDown() report the scripted state, plus any clicks queued for the
// current frame.
class MockInputPlatform : public IPlatform {
public:
    MockInputPlatform() = default;

    // IPlatform
    bool createMainWindow(const char* title, int w, int h, bool fullscreen) override;
    void destroyMainWindow() override;
    bool pumpMessages() override;
    std::uint32_t timeMs() override;
    void sleepMs(std::uint32_t ms) override;
    void getMouse(MouseState& out) override;
    bool keyDown(int vkey) override;

    // --- scripting API (not part of the interface) ---
    void setMouse(int x, int y, bool left);  // script the persistent mouse state
    void setKey(int vkey, bool down);         // script a key state
    void queueClickAt(int frame, int x, int y); // left-down on frame, auto-up next
    void quitAfter(int frame);                // pumpMessages()==false from this frame
    int  frame() const { return frame_; }

private:
    struct ClickEvent { int x = 0, y = 0; };

    int frame_ = 0;
    bool has_quit_frame_ = false;
    int quit_frame_ = 0;
    MouseState mouse_;
    std::unordered_map<int, bool> keys_;
    // Frame on which a queued left-down fires. The auto-up happens the next frame,
    // so a click queued at frame F reports left=true at F and (unless overridden)
    // left=false at F+1.
    std::unordered_map<int, ClickEvent> clicks_;
};

} // namespace guild::shim
