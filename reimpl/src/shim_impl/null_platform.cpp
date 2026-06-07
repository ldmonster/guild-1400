#include "shim_impl/null_platform.h"
#include <thread>

namespace guild::shim {

NullPlatform::NullPlatform() : start_(std::chrono::steady_clock::now()) {}

bool NullPlatform::createMainWindow(const char*, int, int, bool) {
    window_created_ = true;
    return true; // no-op success
}

void NullPlatform::destroyMainWindow() {
    window_created_ = false;
}

bool NullPlatform::pumpMessages() {
    return !quit_;
}

std::uint32_t NullPlatform::timeMs() {
    auto now = std::chrono::steady_clock::now();
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now - start_).count();
    return static_cast<std::uint32_t>(ms); // matches winmm timeGetTime 32-bit wrap
}

void NullPlatform::sleepMs(std::uint32_t ms) {
    if (ms)
        std::this_thread::sleep_for(std::chrono::milliseconds(ms));
}

void NullPlatform::getMouse(MouseState& out) {
    out = mouse_;
}

bool NullPlatform::keyDown(int vkey) {
    auto it = keys_.find(vkey);
    return it != keys_.end() && it->second;
}

void NullPlatform::postQuit() {
    quit_ = true;
}

void NullPlatform::setMouse(const MouseState& m) {
    mouse_ = m;
}

void NullPlatform::setKey(int vkey, bool down) {
    keys_[vkey] = down;
}

} // namespace guild::shim
