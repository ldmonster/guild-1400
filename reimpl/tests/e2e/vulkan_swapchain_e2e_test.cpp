// E2E tier: a multi-frame swapchain present loop (acquire/blit/present per frame,
// reusing the binary semaphores), headless via VK_EXT_headless_surface on lavapipe.
// Proves the on-screen present path is stable across frames and tears down clean.
#include "test.h"

#if defined(GUILD_HAVE_VULKAN)
#include "shim_impl/vulkan_backend.h"
#include <vulkan/vulkan.h>
#include <vector>

using namespace guild::shim;

namespace {
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
} // namespace

TEST(VulkanSwapchainE2E, MultiFramePresentLoop) {
    VulkanGraphicsDevice gfx;
    gfx.configureSwapchain(MakeHeadlessSurface, {"VK_KHR_surface", "VK_EXT_headless_surface"});
    if (!gfx.init(128, 128, 32, false)) { std::printf("  [skip] no Vulkan ICD\n"); CHECK(true); return; }
    if (!gfx.swapchainActive()) {
        std::printf("  [skip] headless swapchain unavailable (device=%s)\n", gfx.deviceName().c_str());
        gfx.shutdown(); CHECK(true); return;
    }

    const int kFrames = 8;
    for (int f = 0; f < kFrames; ++f) {
        if (Surface* s = gfx.backbuffer()) {
            auto* px = static_cast<std::uint32_t*>(s->pixels);
            const int pw = s->pitch / 4;
            for (int y = 0; y < s->height; ++y)
                for (int x = 0; x < s->width; ++x)
                    px[y * pw + x] = std::uint32_t((x + f) & 0xFF) << 16; // animated red ramp
        }
        gfx.present();
    }
    CHECK_EQ(gfx.swapchainPresentCount(), kFrames); // every frame reached vkQueuePresentKHR
    CHECK_EQ(gfx.presentCount(), kFrames);

    // Last frame's content is observable via the target readback.
    std::vector<std::uint32_t> img = gfx.readback();
    CHECK_EQ((int)img.size(), 128 * 128);
    if (img.size() == 128u * 128u) {
        std::uint32_t p = img[0 * 128 + 10]; // x=10, last frame f=7 -> R=(10+7)=17
        CHECK_EQ((p >> 16) & 0xFF, 17u);
    }

    gfx.shutdown();
    CHECK_EQ(gfx.swapchainPresentCount(), 0); // reset on shutdown
    CHECK(!gfx.inited());
}

#else
TEST(VulkanSwapchainE2E, SkippedNoBackend) { CHECK(true); }
#endif
