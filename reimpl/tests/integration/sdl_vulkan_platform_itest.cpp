// Integration tests for SdlVulkanPlatform: attempt to create a real
// SDL_WINDOW_VULKAN window, pump messages, query drawable size + surface, and
// keep the timer running. Under SDL_VIDEODRIVER=dummy the Vulkan window cannot
// be realized, so createMainWindow() returns false; we assert the DOCUMENTED
// behavior either way (degrade-false is OK) and that nothing crashes.
#include "test.h"

#ifdef GUILD_HAVE_SDL2
#include "shim_impl/sdl_vulkan_platform.h"
#include <SDL.h>
#include <vulkan/vulkan.h>

using namespace guild::shim;

TEST(SdlVulkanPlatformIntegration, CreateWindowDegradesOrSucceeds) {
    SdlVulkanPlatform p;
    bool ok = p.createMainWindow("VkTest", 320, 240, /*fullscreen=*/false);

    if (ok) {
        // A real Vulkan window came up (a display + driver was present).
        CHECK(p.window() != nullptr);
        int w = 0, h = 0;
        p.drawableSize(w, h);
        CHECK(w > 0);
        CHECK(h > 0);
        // No instance => surface creation must refuse cleanly.
        CHECK(p.createSurface(VK_NULL_HANDLE) == VK_NULL_HANDLE);
    } else {
        // Documented headless degrade: no window, queries are null-safe.
        CHECK(p.window() == nullptr);
        int w = -1, h = -1;
        p.drawableSize(w, h);
        CHECK_EQ(w, 0);
        CHECK_EQ(h, 0);
        CHECK(p.createSurface(VK_NULL_HANDLE) == VK_NULL_HANDLE);
    }

    // The message pump and timer must work regardless of window state.
    CHECK(p.pumpMessages());
    std::uint32_t t0 = p.timeMs();
    p.sleepMs(2);
    CHECK(p.timeMs() >= t0);

    // Extension query stays sane whether or not the window exists.
    auto exts = p.requiredInstanceExtensions();
    CHECK(!exts.empty());

    p.destroyMainWindow();
    CHECK(p.window() == nullptr);
}

TEST(SdlVulkanPlatformIntegration, RebuildAfterDestroy) {
    SdlVulkanPlatform p;
    p.createMainWindow("VkTest", 64, 64, false);
    p.destroyMainWindow();
    // Second create after destroy must not crash and timer keeps advancing.
    std::uint32_t t0 = p.timeMs();
    p.createMainWindow("VkTest2", 64, 64, false);
    p.pumpMessages();
    CHECK(p.timeMs() >= t0);
    p.destroyMainWindow();
    CHECK(p.window() == nullptr);
}

#else
TEST(SdlVulkanPlatformIntegration, SkippedNoBackend) { CHECK(true); }
#endif
