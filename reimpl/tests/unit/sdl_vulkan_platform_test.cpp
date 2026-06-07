// Unit tests for SdlVulkanPlatform: the Win32-vkey -> SDL_Scancode mapping
// table, monotonic timer, and the instance-extension query WITHOUT a window.
// All runnable headless under SDL_VIDEODRIVER=dummy. Compiles in both builds.
#include "test.h"

#ifdef GUILD_HAVE_SDL2
#include "shim_impl/sdl_vulkan_platform.h"
#include <SDL.h>
#include <cstring>
#include <string>
#include <vector>

using namespace guild::shim;

TEST(SdlVulkanPlatformUnit, VkeyScancodeMapping) {
    // Letters + digits map onto SDL's contiguous scancode ranges.
    CHECK_EQ(SdlVulkanPlatform::vkeyToScancode('A'), (int)SDL_SCANCODE_A);
    CHECK_EQ(SdlVulkanPlatform::vkeyToScancode('Z'), (int)SDL_SCANCODE_Z);
    CHECK_EQ(SdlVulkanPlatform::vkeyToScancode('W'), (int)SDL_SCANCODE_W);
    CHECK_EQ(SdlVulkanPlatform::vkeyToScancode('S'), (int)SDL_SCANCODE_S);
    CHECK_EQ(SdlVulkanPlatform::vkeyToScancode('D'), (int)SDL_SCANCODE_D);
    CHECK_EQ(SdlVulkanPlatform::vkeyToScancode('0'), (int)SDL_SCANCODE_0);
    CHECK_EQ(SdlVulkanPlatform::vkeyToScancode('9'), (int)SDL_SCANCODE_9);

    // Named keys (Win32 VK_* hex codes).
    CHECK_EQ(SdlVulkanPlatform::vkeyToScancode(0x1B), (int)SDL_SCANCODE_ESCAPE);
    CHECK_EQ(SdlVulkanPlatform::vkeyToScancode(0x20), (int)SDL_SCANCODE_SPACE);
    CHECK_EQ(SdlVulkanPlatform::vkeyToScancode(0x0D), (int)SDL_SCANCODE_RETURN);
    CHECK_EQ(SdlVulkanPlatform::vkeyToScancode(0x25), (int)SDL_SCANCODE_LEFT);
    CHECK_EQ(SdlVulkanPlatform::vkeyToScancode(0x26), (int)SDL_SCANCODE_UP);
    CHECK_EQ(SdlVulkanPlatform::vkeyToScancode(0x27), (int)SDL_SCANCODE_RIGHT);
    CHECK_EQ(SdlVulkanPlatform::vkeyToScancode(0x28), (int)SDL_SCANCODE_DOWN);
    CHECK_EQ(SdlVulkanPlatform::vkeyToScancode(0x10), (int)SDL_SCANCODE_LSHIFT);
    CHECK_EQ(SdlVulkanPlatform::vkeyToScancode(0x11), (int)SDL_SCANCODE_LCTRL);

    // Unmapped codes fall through to UNKNOWN.
    CHECK_EQ(SdlVulkanPlatform::vkeyToScancode(0x00), (int)SDL_SCANCODE_UNKNOWN);
    CHECK_EQ(SdlVulkanPlatform::vkeyToScancode(0xFE), (int)SDL_SCANCODE_UNKNOWN);
}

TEST(SdlVulkanPlatformUnit, KeyDownUnmappedIsFalse) {
    SdlVulkanPlatform p;
    // No key is held in a headless run; unmapped vkey is always false.
    CHECK(!p.keyDown(0x00));
    CHECK(!p.keyDown('A'));
}

TEST(SdlVulkanPlatformUnit, TimerMonotonic) {
    SdlVulkanPlatform p;
    std::uint32_t t0 = p.timeMs();
    p.sleepMs(5);
    std::uint32_t t1 = p.timeMs();
    p.sleepMs(5);
    std::uint32_t t2 = p.timeMs();
    CHECK(t1 >= t0);
    CHECK(t2 >= t1);
}

TEST(SdlVulkanPlatformUnit, RequiredInstanceExtensionsNoWindow) {
    SdlVulkanPlatform p; // no createMainWindow() call
    std::vector<const char*> exts = p.requiredInstanceExtensions();
    CHECK(!exts.empty());
    // Whatever the source (live SDL query or fallback), VK_KHR_surface must be
    // present — it is mandatory for any window-system presentation.
    bool has_surface = false;
    for (const char* e : exts) {
        CHECK(e != nullptr);
        if (e && std::strcmp(e, VK_KHR_SURFACE_EXTENSION_NAME) == 0)
            has_surface = true;
    }
    CHECK(has_surface);
}

#else
TEST(SdlVulkanPlatformUnit, SkippedNoBackend) { CHECK(true); }
#endif
