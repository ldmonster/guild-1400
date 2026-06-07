// Integration tier: real VkDevice on the system ICD (lavapipe headless),
// present a known Surface through the offscreen path, read back, assert the
// exact pixels round-tripped.
#include "test.h"

#if defined(GUILD_HAVE_VULKAN)
#include "shim_impl/vulkan_backend.h"

#include <cstdint>
#include <cstdio>
#include <vector>

using namespace guild::shim;

TEST(VulkanBackendIT, DeviceInitAndIntrospect) {
    VulkanGraphicsDevice dev;
    CHECK(dev.init(16, 8, 32, false));
    if (!dev.inited())
        return; // guard: no usable Vulkan device in this env
    CHECK(!dev.deviceName().empty());
    CHECK(!dev.apiVersion().empty());
    std::printf("    [vk] device=\"%s\" api=%s\n", dev.deviceName().c_str(),
                dev.apiVersion().c_str());
    Surface* s = dev.backbuffer();
    CHECK(s != nullptr);
    if (s) {
        CHECK_EQ(s->width, 16);
        CHECK_EQ(s->height, 8);
        CHECK_EQ(s->bpp, 32);
    }
    dev.shutdown();
    CHECK(!dev.inited());
}

TEST(VulkanBackendIT, Present32bppRoundTrip) {
    const int W = 16, H = 8;
    VulkanGraphicsDevice dev;
    CHECK(dev.init(W, H, 32, false));
    if (!dev.inited())
        return;

    Surface* s = dev.backbuffer();
    CHECK(s != nullptr);
    if (!s) { dev.shutdown(); return; }

    // Fill the CPU framebuffer (B,G,R,X bytes) with a deterministic pattern.
    auto* px = static_cast<std::uint8_t*>(s->pixels);
    std::vector<std::uint32_t> expect(static_cast<std::size_t>(W) * H);
    for (int i = 0; i < W * H; ++i) {
        std::uint8_t b = static_cast<std::uint8_t>(i * 3 + 1);
        std::uint8_t g = static_cast<std::uint8_t>(i * 5 + 2);
        std::uint8_t r = static_cast<std::uint8_t>(i * 7 + 3);
        px[i * 4 + 0] = b;
        px[i * 4 + 1] = g;
        px[i * 4 + 2] = r;
        px[i * 4 + 3] = 0;
        expect[i] = 0xFF000000u | (static_cast<std::uint32_t>(r) << 16) |
                    (static_cast<std::uint32_t>(g) << 8) | b;
    }

    dev.present();
    CHECK_EQ(dev.presentCount(), 1);

    std::vector<std::uint32_t> got = dev.readback();
    CHECK_EQ(got.size(), expect.size());
    bool exact = got.size() == expect.size();
    for (std::size_t i = 0; i < got.size() && i < expect.size(); ++i)
        if (got[i] != expect[i]) { exact = false; break; }
    CHECK(exact);
    dev.shutdown();
}

TEST(VulkanBackendIT, Present8bppPaletteRoundTrip) {
    const int W = 8, H = 8;
    VulkanGraphicsDevice dev;
    CHECK(dev.init(W, H, 8, false));
    if (!dev.inited())
        return;

    std::uint32_t pal[256];
    for (int i = 0; i < 256; ++i)
        pal[i] = 0x00000000u | static_cast<std::uint32_t>((i << 16) | (i << 8) | i);
    dev.setPalette(pal);

    Surface* s = dev.backbuffer();
    if (!s) { dev.shutdown(); return; }
    auto* px = static_cast<std::uint8_t*>(s->pixels);
    std::vector<std::uint32_t> expect(static_cast<std::size_t>(W) * H);
    for (int i = 0; i < W * H; ++i) {
        std::uint8_t idx = static_cast<std::uint8_t>(i * 4);
        px[i] = idx;
        expect[i] = pal[idx] | 0xFF000000u;
    }

    dev.present();
    std::vector<std::uint32_t> got = dev.readback();
    CHECK_EQ(got.size(), expect.size());
    bool exact = got.size() == expect.size();
    for (std::size_t i = 0; i < got.size() && i < expect.size(); ++i)
        if (got[i] != expect[i]) { exact = false; break; }
    CHECK(exact);
    dev.shutdown();
}

#else
TEST(VulkanBackendIT, SkippedNoBackend) { CHECK(true); }
#endif
