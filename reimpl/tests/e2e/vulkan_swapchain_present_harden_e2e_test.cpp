// E2E hardening tier for the on-screen Vulkan present path (swapchain), driven
// HEADLESS through VK_EXT_headless_surface on the lavapipe ICD. Complements
// vulkan_swapchain_e2e_test.cpp (8-frame loop) by proving the present path is:
//   (1) stable over a LONG animated loop (30 presents) with a STABLE per-frame
//       presentCount and a DETERMINISTIC final readback (matching the formula),
//   (2) survives a shutdown + re-init cycle on the SAME device object (simulates
//       a window recreate) and keeps presenting correctly afterward,
//   (3) reports the actual backend it ran on (lavapipe/llvmpipe vs real GPU).
// GUARDED: if no Vulkan ICD or no headless surface is available, it skips clean.
#include "test.h"

#if defined(GUILD_HAVE_VULKAN)
#include "shim_impl/vulkan_backend.h"
#include <vulkan/vulkan.h>

#include <cstdint>
#include <cstring>
#include <vector>

using namespace guild::shim;

namespace {
// The same headless surface factory the integration tier uses; this is the only
// thing that differs from a real SdlVulkanPlatform::createSurface() window path.
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

// Deterministic animated pattern written into the engine framebuffer. The frame
// number drives the red channel ramp so the readback is exactly predictable.
void PaintFrame(VulkanGraphicsDevice& gfx, int frame) {
    Surface* s = gfx.backbuffer();
    if (!s) return;
    auto* px = static_cast<std::uint32_t*>(s->pixels);
    const int pw = s->pitch / 4;
    for (int y = 0; y < s->height; ++y)
        for (int x = 0; x < s->width; ++x)
            // engine stores B,G,R,X -> here 0x00RRGGBB
            px[y * pw + x] = (std::uint32_t((x + frame) & 0xFF) << 16) |
                             (std::uint32_t(y & 0xFF) << 8) |
                             std::uint32_t((frame * 3) & 0xFF);
}
} // namespace

// 30-present animated loop: every frame must reach vkQueuePresentKHR, the present
// count must equal the frame count exactly, and the final readback must match the
// PaintFrame formula byte-for-byte (deterministic content through the swapchain).
TEST(VulkanSwapchainHardenE2E, LongAnimatedLoopDeterministic) {
    const int W = 96, H = 72, kFrames = 30;
    VulkanGraphicsDevice gfx;
    gfx.configureSwapchain(MakeHeadlessSurface, HeadlessExts());
    if (!gfx.init(W, H, 32, false)) {
        std::printf("  [skip] LongAnimatedLoop: no Vulkan ICD\n");
        CHECK(true);
        return;
    }
    if (!gfx.swapchainActive()) {
        std::printf("  [skip] LongAnimatedLoop: headless swapchain unavailable (device=%s)\n",
                    gfx.deviceName().c_str());
        gfx.shutdown();
        CHECK(true);
        return;
    }

    const bool isLavapipe = gfx.deviceName().find("llvmpipe") != std::string::npos ||
                            gfx.deviceName().find("lavapipe") != std::string::npos;
    std::printf("  [vk] present backend: \"%s\" api=%s (%s) images=%d\n",
                gfx.deviceName().c_str(), gfx.apiVersion().c_str(),
                isLavapipe ? "lavapipe/llvmpipe SOFTWARE" : "REAL GPU",
                gfx.swapchainImageCount());

    for (int f = 0; f < kFrames; ++f) {
        PaintFrame(gfx, f);
        gfx.present();
        // presentCount must advance lock-step every single frame.
        CHECK_EQ(gfx.presentCount(), f + 1);
        CHECK_EQ(gfx.swapchainPresentCount(), f + 1);
    }
    CHECK_EQ(gfx.presentCount(), kFrames);
    CHECK_EQ(gfx.swapchainPresentCount(), kFrames);
    CHECK(gfx.swapchainActive()); // swapchain survived the whole loop, no rebuild needed

    // Deterministic final-frame content: rebuild the expected XRGB and compare.
    const int last = kFrames - 1;
    std::vector<std::uint32_t> got = gfx.readback();
    CHECK_EQ((int)got.size(), W * H);
    bool exact = (int)got.size() == W * H;
    for (int y = 0; y < H && exact; ++y) {
        for (int x = 0; x < W; ++x) {
            std::uint32_t r = std::uint32_t((x + last) & 0xFF);
            std::uint32_t g = std::uint32_t(y & 0xFF);
            std::uint32_t b = std::uint32_t((last * 3) & 0xFF);
            std::uint32_t want = 0xFF000000u | (r << 16) | (g << 8) | b;
            if (got[y * W + x] != want) { exact = false; break; }
        }
    }
    CHECK(exact); // every presented texel matches the formula -> deterministic

    // Re-running readback (no new present) yields byte-identical pixels.
    std::vector<std::uint32_t> got2 = gfx.readback();
    CHECK(got == got2);

    std::printf("  [vk] presented %d frames headless; final readback deterministic (%dx%d)\n",
                kFrames, W, H);
    gfx.shutdown();
    CHECK(!gfx.inited());
    CHECK_EQ(gfx.swapchainPresentCount(), 0); // counters reset on shutdown
}

// Shutdown + re-init on the SAME device object (simulates window destroy/recreate).
// The second life of the device must build a fresh swapchain and present cleanly,
// proving destroyAll()/destroySwapchain() fully release everything (no stale state).
TEST(VulkanSwapchainHardenE2E, ReinitCyclePresentsAgain) {
    const int W = 48, H = 32;
    VulkanGraphicsDevice gfx;
    gfx.configureSwapchain(MakeHeadlessSurface, HeadlessExts());
    if (!gfx.init(W, H, 32, false)) {
        std::printf("  [skip] ReinitCycle: no Vulkan ICD\n");
        CHECK(true);
        return;
    }
    if (!gfx.swapchainActive()) {
        std::printf("  [skip] ReinitCycle: headless swapchain unavailable\n");
        gfx.shutdown();
        CHECK(true);
        return;
    }

    for (int cycle = 0; cycle < 3; ++cycle) {
        const int kFrames = 5;
        for (int f = 0; f < kFrames; ++f) {
            PaintFrame(gfx, f + cycle * 10);
            gfx.present();
        }
        CHECK_EQ(gfx.presentCount(), kFrames);
        CHECK_EQ(gfx.swapchainPresentCount(), kFrames);
        CHECK(gfx.swapchainActive());

        // Tear the whole device down and stand it back up (configureSwapchain
        // state persists across init() per the contract, so no reconfigure).
        gfx.shutdown();
        CHECK(!gfx.inited());
        if (cycle < 2) {
            bool ok = gfx.init(W, H, 32, false);
            CHECK(ok);
            if (!ok) return;
            CHECK(gfx.swapchainActive()); // fresh swapchain built each re-init
        }
    }
    std::printf("  [vk] survived 3 shutdown/re-init cycles, swapchain rebuilt each time\n");
}

#else
TEST(VulkanSwapchainHardenE2E, SkippedNoBackend) { CHECK(true); }
#endif
