// Integration tier: the REAL Vulkan swapchain present path, driven headlessly
// through a VK_EXT_headless_surface (lavapipe). This exercises the exact code an
// SDL_WINDOW_VULKAN surface would: createSwapchain -> per-frame vkAcquireNextImageKHR
// -> vkCmdBlitImage(target -> swapchain image) -> vkQueuePresentKHR. The surface
// factory is the only thing that differs from the SDL path (SdlVulkanPlatform::
// createSurface would replace vkCreateHeadlessSurfaceEXT).
#include "test.h"

#if defined(GUILD_HAVE_VULKAN)
#include "shim_impl/vulkan_backend.h"
#include <vulkan/vulkan.h>
#include <vector>

using namespace guild::shim;

namespace {
// Build a headless VkSurfaceKHR from the device's instance (the test-only stand-in
// for a real window surface). Returns VK_NULL_HANDLE if the loader/ICD lacks the
// extension, in which case the device falls back to offscreen and the test skips.
VkSurfaceKHR MakeHeadlessSurface(VkInstance inst) {
    auto fn = (PFN_vkCreateHeadlessSurfaceEXT)vkGetInstanceProcAddr(
        inst, "vkCreateHeadlessSurfaceEXT");
    if (!fn) return VK_NULL_HANDLE;
    VkHeadlessSurfaceCreateInfoEXT ci{};
    ci.sType = VK_STRUCTURE_TYPE_HEADLESS_SURFACE_CREATE_INFO_EXT;
    VkSurfaceKHR s = VK_NULL_HANDLE;
    if (fn(inst, &ci, nullptr, &s) != VK_SUCCESS) return VK_NULL_HANDLE;
    return s;
}
std::vector<const char*> HeadlessExts() {
    return {"VK_KHR_surface", "VK_EXT_headless_surface"};
}
} // namespace

TEST(VulkanSwapchainItest, BuildsSwapchainAndPresents) {
    VulkanGraphicsDevice gfx;
    gfx.configureSwapchain(MakeHeadlessSurface, HeadlessExts());
    if (!gfx.init(64, 48, 32, false)) {
        std::printf("  [skip] VulkanSwapchainItest: no Vulkan ICD\n");
        CHECK(true);
        return;
    }
    if (!gfx.swapchainActive()) {
        std::printf("  [skip] VulkanSwapchainItest: headless surface/swapchain unavailable "
                    "(device=%s)\n", gfx.deviceName().c_str());
        gfx.shutdown();
        CHECK(true);
        return;
    }

    std::printf("  swapchain: device=%s images=%d\n", gfx.deviceName().c_str(),
                gfx.swapchainImageCount());
    CHECK(gfx.swapchainImageCount() >= 1);

    // Paint a known 32bpp pattern into the engine framebuffer and present it.
    if (Surface* s = gfx.backbuffer()) {
        auto* px = static_cast<std::uint32_t*>(s->pixels);
        const int pw = s->pitch / 4;
        for (int y = 0; y < s->height; ++y)
            for (int x = 0; x < s->width; ++x)
                // engine stores B,G,R,X in memory -> 0x00RRGGBB here
                px[y * pw + x] = (std::uint32_t(x & 0xFF)) |
                                 (std::uint32_t(y & 0xFF) << 8) | (0x40u << 16);
    }

    gfx.present();
    CHECK_EQ(gfx.swapchainPresentCount(), 1);   // vkQueuePresentKHR actually ran

    // The target image (blit source) holds the presented pixels; verify round-trip.
    std::vector<std::uint32_t> img = gfx.readback();
    CHECK_EQ((int)img.size(), 64 * 48);
    if (img.size() == 64u * 48u) {
        // pixel (10, 5): R=0x40, G=5, B=10 -> 0xFF400A05... assert channels.
        std::uint32_t p = img[5 * 64 + 10];
        CHECK_EQ((p >> 16) & 0xFF, 0x40u); // R
        CHECK_EQ((p >> 8) & 0xFF, 5u);     // G
        CHECK_EQ(p & 0xFF, 10u);           // B
    }
    gfx.shutdown();
    CHECK(!gfx.swapchainActive());
}

#else
TEST(VulkanSwapchainItest, SkippedNoBackend) { CHECK(true); }
#endif
