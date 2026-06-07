// Unit tier: framebuffer -> XRGB8888 conversion golden vectors. No VkDevice.
#include "test.h"

#if defined(GUILD_HAVE_VULKAN)
#include "shim_impl/vulkan_backend.h"

#include <cstdint>
#include <vector>

using namespace guild::shim;

// 8bpp paletted: each index maps through the palette and gets opaque alpha.
TEST(VulkanBackend, Convert8bppPalette) {
    std::uint32_t pal[256];
    for (int i = 0; i < 256; ++i)
        pal[i] = 0x00000000u | static_cast<std::uint32_t>(i * 0x010101);
    pal[5] = 0x00123456u; // no alpha set in palette
    std::uint8_t src[4] = {0, 5, 255, 1};
    std::uint32_t out[4] = {};
    ConvertToXRGB8888(src, 4, 1, 8, pal, out);
    CHECK_EQ(out[0], 0xFF000000u);
    CHECK_EQ(out[1], 0xFF123456u); // alpha forced on
    CHECK_EQ(out[2], (0xFF000000u | static_cast<std::uint32_t>(255 * 0x010101)));
    CHECK_EQ(out[3], (0xFF000000u | static_cast<std::uint32_t>(1 * 0x010101)));
}

// 16bpp RGB565 expanded with bit-replication.
TEST(VulkanBackend, Convert16bpp565) {
    // Pure red (0xF800), pure green (0x07E0), pure blue (0x001F), white.
    std::uint16_t vals[4] = {0xF800, 0x07E0, 0x001F, 0xFFFF};
    std::uint8_t src[8];
    for (int i = 0; i < 4; ++i) {
        src[i * 2] = static_cast<std::uint8_t>(vals[i] & 0xFF);
        src[i * 2 + 1] = static_cast<std::uint8_t>(vals[i] >> 8);
    }
    std::uint32_t out[4] = {};
    ConvertToXRGB8888(src, 4, 1, 16, nullptr, out);
    CHECK_EQ(out[0], 0xFFFF0000u); // r5=31 -> 255
    CHECK_EQ(out[1], 0xFF00FF00u); // g6=63 -> (63<<2)|(63>>4) = 255
    CHECK_EQ(out[2], 0xFF0000FFu); // b5=31 -> 255
    CHECK_EQ(out[3], 0xFFFFFFFFu); // white
}

// 32bpp XRGB: memory order is B,G,R,X -> 0xFFRRGGBB.
TEST(VulkanBackend, Convert32bpp) {
    std::uint8_t src[8] = {
        0x11, 0x22, 0x33, 0x99, // B=11 G=22 R=33 X=99 -> 0xFF332211
        0xAA, 0xBB, 0xCC, 0x00, // -> 0xFFCCBBAA
    };
    std::uint32_t out[2] = {};
    ConvertToXRGB8888(src, 2, 1, 32, nullptr, out);
    CHECK_EQ(out[0], 0xFF332211u);
    CHECK_EQ(out[1], 0xFFCCBBAAu);
}

// Guards: null / degenerate args must not crash and leave output untouched.
TEST(VulkanBackend, ConvertGuards) {
    std::uint32_t out[1] = {0xDEADBEEFu};
    ConvertToXRGB8888(nullptr, 1, 1, 32, nullptr, out);
    CHECK_EQ(out[0], 0xDEADBEEFu);
    std::uint8_t src[4] = {1, 2, 3, 4};
    ConvertToXRGB8888(src, 0, 1, 32, nullptr, out);
    CHECK_EQ(out[0], 0xDEADBEEFu);
}

#else
TEST(VulkanBackend, SkippedNoBackend) { CHECK(true); }
#endif
