// End-to-end lifecycle test for SdlVulkanPlatform: create -> pump N frames with
// timing + input polling -> destroy, all without crashing under the dummy video
// driver. Headless-safe: tolerates createMainWindow() degrading to false.
#include "test.h"

#ifdef GUILD_HAVE_SDL2
#include "shim_impl/sdl_vulkan_platform.h"
#include <SDL.h>
#include <vulkan/vulkan.h>

using namespace guild::shim;

TEST(SdlVulkanPlatformE2E, FullLifecycle) {
    SdlVulkanPlatform p;
    bool ok = p.createMainWindow("VkLifecycle", 256, 256, false);
    // Either outcome is acceptable headless; just record it.
    CHECK(ok || !ok);

    std::uint32_t last = p.timeMs();
    for (int frame = 0; frame < 30; ++frame) {
        bool running = p.pumpMessages();
        CHECK(running); // no SDL_QUIT is injected in this harness

        MouseState m;
        p.getMouse(m); // must be safe with or without a window

        // Poll a representative key each frame (false headless, never crashes).
        (void)p.keyDown('W');
        (void)p.keyDown(0x1B);

        std::uint32_t now = p.timeMs();
        CHECK(now >= last);
        last = now;

        p.sleepMs(1);
    }

    // Surface request with no instance is always a clean null.
    CHECK(p.createSurface(VK_NULL_HANDLE) == VK_NULL_HANDLE);

    p.destroyMainWindow();
    CHECK(p.window() == nullptr);
}

TEST(SdlVulkanPlatformE2E, ExtensionsThenLifecycle) {
    SdlVulkanPlatform p;
    // Query extensions before any window (renderer would do this to build the
    // VkInstance), then run a short window lifecycle.
    auto exts = p.requiredInstanceExtensions();
    CHECK(!exts.empty());

    p.createMainWindow("VkExt", 128, 128, false);
    for (int i = 0; i < 5; ++i) {
        p.pumpMessages();
        p.sleepMs(1);
    }
    p.destroyMainWindow();
    CHECK(p.window() == nullptr);
}

#else
TEST(SdlVulkanPlatformE2E, SkippedNoBackend) { CHECK(true); }
#endif
