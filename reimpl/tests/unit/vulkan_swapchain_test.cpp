// Unit tier for the Vulkan swapchain present path. No device here — the unit
// checks are the offscreen-fallback contract (a null surface factory keeps the
// device in offscreen mode) and the configure-before-init ordering. The real
// swapchain acquire/blit/present runs in the integration + e2e tiers against a
// headless surface (VK_EXT_headless_surface) on the lavapipe ICD.
#include "test.h"

#if defined(GUILD_HAVE_VULKAN)
#include "shim_impl/vulkan_backend.h"

using namespace guild::shim;

// A device with NO configureSwapchain() call stays offscreen-only.
TEST(VulkanSwapchainUnit, OffscreenByDefault) {
    VulkanGraphicsDevice gfx;
    if (!gfx.init(32, 24, 32, false)) { CHECK(true); return; } // no ICD -> skip
    CHECK(!gfx.swapchainActive());
    CHECK_EQ(gfx.swapchainImageCount(), 0);
    CHECK_EQ(gfx.swapchainPresentCount(), 0);
    gfx.shutdown();
    CHECK(!gfx.swapchainActive());
}

// A null surface factory configured but returning VK_NULL_HANDLE also falls back
// to offscreen (init still succeeds, just no swapchain).
TEST(VulkanSwapchainUnit, NullSurfaceFactoryFallsBackOffscreen) {
    VulkanGraphicsDevice gfx;
    gfx.configureSwapchain([](VkInstance) { return (VkSurfaceKHR)VK_NULL_HANDLE; },
                           {"VK_KHR_surface"});
    if (!gfx.init(16, 16, 32, false)) { CHECK(true); return; }
    CHECK(!gfx.swapchainActive());     // no surface -> offscreen
    gfx.present();                     // must not crash
    CHECK(gfx.presentCount() >= 1);
    gfx.shutdown();
}

#else
TEST(VulkanSwapchainUnit, SkippedNoBackend) { CHECK(true); }
#endif
