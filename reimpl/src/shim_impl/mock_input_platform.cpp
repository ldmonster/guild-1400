#include "shim_impl/mock_input_platform.h"

namespace guild::shim {

bool MockInputPlatform::createMainWindow(const char* /*title*/, int /*w*/,
                                         int /*h*/, bool /*fullscreen*/) {
    return true;
}

void MockInputPlatform::destroyMainWindow() {
    // no-op
}

bool MockInputPlatform::pumpMessages() {
    // Determine whether the quit frame has been reached for the current frame,
    // then advance. quitAfter(N) makes this return false starting at frame N.
    bool keep_running = true;
    if (has_quit_frame_ && frame_ >= quit_frame_) {
        keep_running = false;
    }
    ++frame_;
    return keep_running;
}

std::uint32_t MockInputPlatform::timeMs() {
    return static_cast<std::uint32_t>(frame_) * 16u;
}

void MockInputPlatform::sleepMs(std::uint32_t /*ms*/) {
    // no-op
}

void MockInputPlatform::getMouse(MouseState& out) {
    out = mouse_;
    // A click queued for the current frame forces a left-down at its coords.
    auto it = clicks_.find(frame_);
    if (it != clicks_.end()) {
        out.x = it->second.x;
        out.y = it->second.y;
        out.left = true;
    } else if (clicks_.find(frame_ - 1) != clicks_.end()) {
        // Auto-up the frame after a queued click (unless setMouse said otherwise).
        out.left = false;
    }
}

bool MockInputPlatform::keyDown(int vkey) {
    auto it = keys_.find(vkey);
    return it != keys_.end() && it->second;
}

void MockInputPlatform::setMouse(int x, int y, bool left) {
    mouse_.x = x;
    mouse_.y = y;
    mouse_.left = left;
}

void MockInputPlatform::setKey(int vkey, bool down) {
    keys_[vkey] = down;
}

void MockInputPlatform::queueClickAt(int frame, int x, int y) {
    clicks_[frame] = ClickEvent{x, y};
}

void MockInputPlatform::quitAfter(int frame) {
    has_quit_frame_ = true;
    quit_frame_ = frame;
}

} // namespace guild::shim
