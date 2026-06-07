// E2E tier: init -> present several gradient/pattern frames -> readback ->
// shutdown, headless, clean teardown, no Vulkan validation errors.
#include "test.h"

#if defined(GUILD_HAVE_VULKAN)
#include "shim_impl/vulkan_backend.h"

#include <cstdint>
#include <vector>

using namespace guild::shim;

TEST(VulkanBackendE2E, GradientFramesRoundTrip) {
    const int W = 32, H = 24;
    VulkanGraphicsDevice dev;
    CHECK(dev.init(W, H, 32, false));
    if (!dev.inited())
        return; // no usable device; skip gracefully

    Surface* s = dev.backbuffer();
    CHECK(s != nullptr);
    if (!s) { dev.shutdown(); return; }
    auto* px = static_cast<std::uint8_t*>(s->pixels);

    const int frames = 5;
    for (int f = 0; f < frames; ++f) {
        for (int y = 0; y < H; ++y) {
            for (int x = 0; x < W; ++x) {
                int i = y * W + x;
                px[i * 4 + 0] = static_cast<std::uint8_t>(x * 8 + f);       // B
                px[i * 4 + 1] = static_cast<std::uint8_t>(y * 10 + f);      // G
                px[i * 4 + 2] = static_cast<std::uint8_t>((x + y) * 4 + f); // R
                px[i * 4 + 3] = 0;
            }
        }
        dev.present();

        std::vector<std::uint32_t> got = dev.readback();
        CHECK_EQ(got.size(), static_cast<std::size_t>(W) * H);

        // Spot-check a few texels match the XRGB conversion of this frame.
        bool ok = got.size() == static_cast<std::size_t>(W) * H;
        if (ok) {
            for (int i : {0, W + 3, W * H / 2, W * H - 1}) {
                std::uint8_t b = px[i * 4 + 0], g = px[i * 4 + 1], r = px[i * 4 + 2];
                std::uint32_t exp = 0xFF000000u |
                                    (static_cast<std::uint32_t>(r) << 16) |
                                    (static_cast<std::uint32_t>(g) << 8) | b;
                if (got[static_cast<std::size_t>(i)] != exp) { ok = false; break; }
            }
        }
        CHECK(ok);
    }
    CHECK_EQ(dev.presentCount(), frames);

    dev.shutdown();
    CHECK(!dev.inited());

    // Re-init after teardown to prove clean reuse.
    CHECK(dev.init(8, 8, 16, false));
    if (dev.inited()) {
        dev.present();
        CHECK_EQ(dev.presentCount(), 1);
        dev.shutdown();
    }
}

#else
TEST(VulkanBackendE2E, SkippedNoBackend) { CHECK(true); }
#endif
