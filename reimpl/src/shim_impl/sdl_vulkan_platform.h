#pragma once
// OPTIONAL Vulkan-capable SDL2 platform backend for IPlatform.
//
// This entire file is a no-op unless GUILD_HAVE_SDL2 is defined at compile time.
// It mirrors Sdl2Platform (window create/destroy, message pump, monotonic timer,
// mouse + keyboard polling) but creates the window with SDL_WINDOW_VULKAN and
// exposes the three things a Vulkan graphics backend needs to build a swapchain:
//
//   - requiredInstanceExtensions()  -> instance extensions SDL wants enabled
//   - createSurface(VkInstance)     -> a VkSurfaceKHR for the live window
//   - drawableSize()                -> the pixel (not logical) size of the window
//
// HEADLESS NOTE: this codebase is exercised under SDL_VIDEODRIVER=dummy/offscreen
// with no display. createMainWindow() therefore DEGRADES GRACEFULLY: if no video
// driver / Vulkan window can be created it returns false cleanly (it never
// asserts or crashes), and the timer/input/extension-query paths keep working.
// requiredInstanceExtensions() returns a sane list even with no window so tests
// can assert it: it queries SDL when possible and otherwise falls back to the
// standard {VK_KHR_surface, VK_KHR_xlib_surface} pair.
//
// Build (matches the GUILD_BACKEND CMake path):
//   g++ -std=c++17 -DGUILD_HAVE_SDL2 -DGUILD_HAVE_VULKAN $(pkg-config --cflags sdl2)
//       src/shim_impl/sdl_vulkan_platform.cpp ... $(pkg-config --libs sdl2) -lvulkan
//
// Without -DGUILD_HAVE_SDL2 the .cpp compiles to nothing and references no SDL or
// Vulkan symbols, so the default portable build still links with zero deps.
#ifdef GUILD_HAVE_SDL2

#include "shim/IPlatform.h"

#include <vulkan/vulkan.h>

#include <cstdint>
#include <vector>

struct SDL_Window;

namespace guild::shim {

// IPlatform over an SDL2 SDL_WINDOW_VULKAN window: real event pump, monotonic
// timer, real mouse/keyboard input, plus the Vulkan instance-extension /
// surface / drawable-size queries the Vulkan renderer consumes.
class SdlVulkanPlatform : public IPlatform {
public:
    SdlVulkanPlatform() = default;
    ~SdlVulkanPlatform() override { destroyMainWindow(); }

    // --- IPlatform -----------------------------------------------------------
    bool createMainWindow(const char* title, int w, int h, bool fullscreen) override;
    void destroyMainWindow() override;
    bool pumpMessages() override;
    std::uint32_t timeMs() override;
    void sleepMs(std::uint32_t ms) override;
    void getMouse(MouseState& out) override;
    bool keyDown(int vkey) override;
    std::string pollText() override;
    void showSystemCursor(bool show) override;

    // --- Vulkan integration --------------------------------------------------

    // Instance extensions SDL requires to present to this window. Works even
    // before/without a window: queries a transient hidden Vulkan window if it
    // can, otherwise returns the standard surface + xlib-surface fallback.
    std::vector<const char*> requiredInstanceExtensions();

    // Create a presentable surface for the live window. Returns VK_NULL_HANDLE
    // if there is no window or SDL_Vulkan_CreateSurface fails (e.g. headless).
    VkSurfaceKHR createSurface(VkInstance instance);

    // Pixel size of the drawable (high-DPI aware). Returns {0,0} with no window.
    void drawableSize(int& w, int& h);

    SDL_Window* window() const { return window_; }

    // Maps a Win32 virtual-key code to an SDL_Scancode (value cast to int).
    // Returns -1 (SDL_SCANCODE_UNKNOWN) for unmapped keys. Exposed for testing
    // the mapping table without a window or keyboard.
    static int vkeyToScancode(int vkey);

private:
    SDL_Window* window_ = nullptr;
    std::uint32_t start_ = 0;
    bool started_ = false;
    bool quit_ = false;
    std::string textBuf_;   // accumulated SDL_TEXTINPUT since the last pollText()
    int wheelAccum_ = 0;    // SDL_MOUSEWHEEL notches since the last getMouse()
};

} // namespace guild::shim

#endif // GUILD_HAVE_SDL2
