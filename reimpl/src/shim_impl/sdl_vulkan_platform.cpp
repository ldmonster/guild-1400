// OPTIONAL Vulkan-capable SDL2 platform — compiled to nothing unless
// GUILD_HAVE_SDL2 is set. See sdl_vulkan_platform.h for build/enable details.
#ifdef GUILD_HAVE_SDL2

#include "shim_impl/sdl_vulkan_platform.h"

#include <SDL.h>
#include <SDL_vulkan.h>

#include <cstdio>

namespace guild::shim {

// ----------------------------- IPlatform -----------------------------------

bool SdlVulkanPlatform::createMainWindow(const char* title, int w, int h,
                                         bool fullscreen) {
    // Start the timer regardless of whether the window comes up, so headless
    // timing still works after a failed (degraded) window creation.
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0) {
        std::fprintf(stderr, "[window] SDL_InitSubSystem(VIDEO) failed: %s\n", SDL_GetError());
        if (!started_) { start_ = SDL_GetTicks(); started_ = true; }
        return false; // No video driver available — degrade cleanly.
    }
    if (!started_) { start_ = SDL_GetTicks(); started_ = true; }
    std::fprintf(stderr, "[window] SDL video driver = %s\n",
                 SDL_GetCurrentVideoDriver() ? SDL_GetCurrentVideoDriver() : "(none)");

    if (w <= 0 || h <= 0)
        return false;

    // Loading the Vulkan loader can fail under some headless drivers; treat it
    // as a clean degrade rather than proceeding to create a doomed window.
    if (SDL_Vulkan_LoadLibrary(nullptr) != 0) {
        std::fprintf(stderr, "[window] SDL_Vulkan_LoadLibrary failed: %s\n", SDL_GetError());
        return false;
    }

    Uint32 flags = SDL_WINDOW_VULKAN;
    flags |= fullscreen ? SDL_WINDOW_FULLSCREEN : SDL_WINDOW_SHOWN;
    window_ = SDL_CreateWindow(title ? title : "Guild", SDL_WINDOWPOS_CENTERED,
                               SDL_WINDOWPOS_CENTERED, w, h, flags);
    if (!window_)
        std::fprintf(stderr, "[window] SDL_CreateWindow(VULKAN %dx%d) failed: %s\n",
                     w, h, SDL_GetError());
    // Under the dummy/offscreen video drivers a SDL_WINDOW_VULKAN window cannot
    // be realized; SDL returns null. That is the documented headless behavior:
    // return false, no crash.
    if (window_) SDL_StartTextInput();   // enable SDL_TEXTINPUT for the name-entry pages
    return window_ != nullptr;
}

void SdlVulkanPlatform::destroyMainWindow() {
    if (window_) { SDL_DestroyWindow(window_); window_ = nullptr; }
}

bool SdlVulkanPlatform::pumpMessages() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT)
            quit_ = true;
        else if (e.type == SDL_TEXTINPUT)
            textBuf_ += e.text.text;   // UTF-8 typed chars (the WM_CHAR stream)
        else if (e.type == SDL_MOUSEWHEEL) {
            // SDL_MOUSEWHEEL -> MouseState::wheel notches (the WM_MOUSEWHEEL
            // stream the original fed into the dword_672254 wheel accumulator
            // consumed by VIBE_Camera_UpdateMovement @0x4b41a8's zoom branch).
            int n = e.wheel.y;
            if (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
                n = -n;
            wheelAccum_ += n;
        }
    }
    return !quit_;
}

std::string SdlVulkanPlatform::pollText() {
    std::string s;
    s.swap(textBuf_);
    return s;
}

std::uint32_t SdlVulkanPlatform::timeMs() {
    if (!started_) { start_ = SDL_GetTicks(); started_ = true; }
    return SDL_GetTicks() - start_;
}

void SdlVulkanPlatform::sleepMs(std::uint32_t ms) {
    if (ms)
        SDL_Delay(ms);
}

void SdlVulkanPlatform::getMouse(MouseState& out) {
    int x = 0, y = 0;
    Uint32 b = SDL_GetMouseState(&x, &y);
    out.x = x;
    out.y = y;
    out.left = (b & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
    out.right = (b & SDL_BUTTON(SDL_BUTTON_RIGHT)) != 0;
    out.middle = (b & SDL_BUTTON(SDL_BUTTON_MIDDLE)) != 0;
    out.wheel = wheelAccum_;     // notches since the previous getMouse()
    wheelAccum_ = 0;
}

void SdlVulkanPlatform::showSystemCursor(bool show) {
    SDL_ShowCursor(show ? SDL_ENABLE : SDL_DISABLE);
}

// Map a Win32 virtual-key code to an SDL_Scancode for the common keys the
// engine polls (arrows, WASD, ESC, SPACE, ENTER, SHIFT, CTRL, letters, digits).
// Mouse buttons are handled via getMouse(). Returns SDL_SCANCODE_UNKNOWN for
// unmapped codes. Pure function: no window/keyboard required.
int SdlVulkanPlatform::vkeyToScancode(int vkey) {
    if (vkey >= 'A' && vkey <= 'Z')
        return static_cast<int>(SDL_SCANCODE_A) + (vkey - 'A');
    // SDL scancodes for digits are NOT contiguous from SDL_SCANCODE_0:
    // '1'..'9' run SDL_SCANCODE_1..SDL_SCANCODE_9, and '0' is SDL_SCANCODE_0
    // (which sits *after* SDL_SCANCODE_9). Map accordingly.
    if (vkey == '0')
        return SDL_SCANCODE_0;
    if (vkey >= '1' && vkey <= '9')
        return static_cast<int>(SDL_SCANCODE_1) + (vkey - '1');
    switch (vkey) {
        case 0x1B: return SDL_SCANCODE_ESCAPE; // VK_ESCAPE
        case 0x20: return SDL_SCANCODE_SPACE;  // VK_SPACE
        case 0x0D: return SDL_SCANCODE_RETURN; // VK_RETURN
        case 0x25: return SDL_SCANCODE_LEFT;   // VK_LEFT
        case 0x26: return SDL_SCANCODE_UP;     // VK_UP
        case 0x27: return SDL_SCANCODE_RIGHT;  // VK_RIGHT
        case 0x28: return SDL_SCANCODE_DOWN;   // VK_DOWN
        case 0x10: return SDL_SCANCODE_LSHIFT; // VK_SHIFT
        case 0x11: return SDL_SCANCODE_LCTRL;  // VK_CONTROL
        case 0x09: return SDL_SCANCODE_TAB;    // VK_TAB
        case 0x08: return SDL_SCANCODE_BACKSPACE; // VK_BACK
        default: return SDL_SCANCODE_UNKNOWN;
    }
}

bool SdlVulkanPlatform::keyDown(int vkey) {
    int sc = vkeyToScancode(vkey);
    if (sc == SDL_SCANCODE_UNKNOWN)
        return false;
    const Uint8* state = SDL_GetKeyboardState(nullptr);
    return state && state[sc] != 0;
}

// --------------------------- Vulkan integration ----------------------------

std::vector<const char*> SdlVulkanPlatform::requiredInstanceExtensions() {
    std::vector<const char*> exts;

    // SDL_Vulkan_GetInstanceExtensions can query against a real window, or
    // (SDL >= 2.0.8) with a null window. We try the live window first, then a
    // null query, then a transient hidden window, then a hardcoded fallback so
    // callers always get a usable list — even fully headless with no window.
    auto query = [&](SDL_Window* w) -> bool {
        unsigned count = 0;
        if (SDL_Vulkan_GetInstanceExtensions(w, &count, nullptr) != SDL_TRUE)
            return false;
        if (count == 0)
            return false;
        std::vector<const char*> names(count);
        if (SDL_Vulkan_GetInstanceExtensions(w, &count, names.data()) != SDL_TRUE)
            return false;
        // SDL returns pointers to internally-owned static strings, safe to keep.
        exts.assign(names.begin(), names.begin() + count);
        return true;
    };

    // Ensure the video subsystem + Vulkan loader are up so the query can work.
    // SDL_Vulkan_GetInstanceExtensions dispatches through the loaded Vulkan
    // library; if SDL_Vulkan_LoadLibrary fails (common under the dummy/offscreen
    // video drivers) the dispatch pointer is null and calling the query would
    // crash — so we only query when the loader genuinely came up.
    bool video_ok = (SDL_WasInit(SDL_INIT_VIDEO) != 0) ||
                    (SDL_InitSubSystem(SDL_INIT_VIDEO) == 0);
    bool vk_ok = video_ok && (SDL_Vulkan_LoadLibrary(nullptr) == 0);

    if (vk_ok && window_ && query(window_))
        return exts;
    if (vk_ok && query(nullptr))
        return exts;

    // Fallback: the canonical extensions for an Xlib-backed Linux presentation.
    // This keeps tests assertable when no driver can answer the query.
    exts.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
    exts.push_back("VK_KHR_xlib_surface");
    return exts;
}

VkSurfaceKHR SdlVulkanPlatform::createSurface(VkInstance instance) {
    if (!window_ || instance == VK_NULL_HANDLE)
        return VK_NULL_HANDLE;
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (SDL_Vulkan_CreateSurface(window_, instance, &surface) != SDL_TRUE)
        return VK_NULL_HANDLE;
    return surface;
}

void SdlVulkanPlatform::drawableSize(int& w, int& h) {
    w = 0;
    h = 0;
    if (window_)
        SDL_Vulkan_GetDrawableSize(window_, &w, &h);
}

} // namespace guild::shim

#endif // GUILD_HAVE_SDL2
