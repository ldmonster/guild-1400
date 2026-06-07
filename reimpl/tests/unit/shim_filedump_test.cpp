// Unit tests for FileDumpGraphicsDevice (portable visible-output backend).
// Renders known patterns into the framebuffer, present()s to a temp dir, reads
// the dumped BMP/PPM back and verifies the pixels (including 8bpp palette
// expansion) and the numbered-sequence naming.
#include "shim_impl/filedump_graphics.h"
#include "tests/framework/test.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

using namespace guild::shim;

namespace {

std::string makeTempDir() {
    char templ[] = "/tmp/guild_filedump_XXXXXX";
    char* d = mkdtemp(templ);
    return d ? std::string(d) : std::string();
}

std::vector<std::uint8_t> readFile(const std::string& path) {
    std::vector<std::uint8_t> data;
    std::FILE* f = std::fopen(path.c_str(), "rb");
    if (!f) return data;
    std::fseek(f, 0, SEEK_END);
    long n = std::ftell(f);
    std::fseek(f, 0, SEEK_SET);
    if (n > 0) {
        data.resize(static_cast<std::size_t>(n));
        std::size_t got = std::fread(data.data(), 1, data.size(), f);
        data.resize(got);
    }
    std::fclose(f);
    return data;
}

} // namespace

// Encode/decode round-trip for the standard BMP writer (independent of device).
TEST(ShimFileDump, Bmp24EncodeDecodeRoundTrip) {
    const int w = 5, h = 3;
    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(w) * h * 3);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x) {
            std::uint8_t* p = &rgb[(static_cast<std::size_t>(y) * w + x) * 3];
            p[0] = static_cast<std::uint8_t>(x * 50);
            p[1] = static_cast<std::uint8_t>(y * 80);
            p[2] = static_cast<std::uint8_t>(x * 10 + y * 5);
        }
    auto file = FileDumpGraphicsDevice::EncodeBmp24(w, h, rgb.data());
    auto img = FileDumpGraphicsDevice::DecodeBmp24(file);
    CHECK(img.ok);
    CHECK_EQ(img.width, w);
    CHECK_EQ(img.height, h);
    for (std::size_t i = 0; i < rgb.size(); ++i)
        CHECK_EQ(img.rgb[i], rgb[i]);
}

// PPM round-trip including a comment line in the header.
TEST(ShimFileDump, PpmEncodeDecodeRoundTrip) {
    const int w = 4, h = 4;
    std::vector<std::uint8_t> rgb(static_cast<std::size_t>(w) * h * 3);
    for (std::size_t i = 0; i < rgb.size(); ++i)
        rgb[i] = static_cast<std::uint8_t>(i * 3 + 1);
    auto file = FileDumpGraphicsDevice::EncodePpm(w, h, rgb.data());
    auto img = FileDumpGraphicsDevice::DecodePpm(file);
    CHECK(img.ok);
    CHECK_EQ(img.width, w);
    CHECK_EQ(img.height, h);
    for (std::size_t i = 0; i < rgb.size(); ++i)
        CHECK_EQ(img.rgb[i], rgb[i]);
}

// 32bpp framebuffer: draw a pattern, present() -> BMP, read back, verify.
TEST(ShimFileDump, Present32bppDumpsBmp) {
    std::string dir = makeTempDir();
    CHECK(!dir.empty());

    FileDumpGraphicsDevice dev;
    dev.configureDump(dir, "f", FileDumpGraphicsDevice::kBmp);
    CHECK(dev.init(8, 6, 32, false));

    Surface* s = dev.backbuffer();
    CHECK(s != nullptr);
    auto* px = static_cast<std::uint8_t*>(s->pixels);
    for (int y = 0; y < s->height; ++y)
        for (int x = 0; x < s->width; ++x) {
            std::uint8_t* p = px + static_cast<std::size_t>(y) * s->pitch + x * 4;
            p[0] = static_cast<std::uint8_t>(x * 30); // B
            p[1] = static_cast<std::uint8_t>(y * 40); // G
            p[2] = static_cast<std::uint8_t>(x + y);  // R
            p[3] = 0xFF;
        }
    dev.present();
    CHECK_EQ(dev.presentCount(), 1);

    auto file = readFile(dev.framePath(0, FileDumpGraphicsDevice::kBmp));
    auto img = FileDumpGraphicsDevice::DecodeBmp24(file);
    CHECK(img.ok);
    CHECK_EQ(img.width, 8);
    CHECK_EQ(img.height, 6);
    for (int y = 0; y < 6; ++y)
        for (int x = 0; x < 8; ++x) {
            CHECK_EQ(img.at(x, y, 0), static_cast<std::uint8_t>(x + y));      // R
            CHECK_EQ(img.at(x, y, 1), static_cast<std::uint8_t>(y * 40));     // G
            CHECK_EQ(img.at(x, y, 2), static_cast<std::uint8_t>(x * 30));     // B
        }
}

// 8bpp framebuffer with a custom palette: present() -> PPM, verify the indices
// were expanded through the palette to RGB.
TEST(ShimFileDump, Present8bppPaletteExpansion) {
    std::string dir = makeTempDir();
    CHECK(!dir.empty());

    std::uint32_t pal[256];
    for (int i = 0; i < 256; ++i) {
        std::uint8_t r = static_cast<std::uint8_t>(255 - i);
        std::uint8_t g = static_cast<std::uint8_t>(i);
        std::uint8_t b = static_cast<std::uint8_t>((i * 2) & 0xFF);
        pal[i] = 0xFF000000u | (static_cast<std::uint32_t>(r) << 16) |
                 (static_cast<std::uint32_t>(g) << 8) | b;
    }

    FileDumpGraphicsDevice dev;
    dev.configureDump(dir, "pal", FileDumpGraphicsDevice::kPpm);
    CHECK(dev.init(4, 4, 8, false));
    dev.setPalette(pal);

    Surface* s = dev.backbuffer();
    auto* px = static_cast<std::uint8_t*>(s->pixels);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x)
            px[static_cast<std::size_t>(y) * s->pitch + x] =
                static_cast<std::uint8_t>(y * 4 + x); // indices 0..15
    dev.present();

    auto file = readFile(dev.framePath(0, FileDumpGraphicsDevice::kPpm));
    auto img = FileDumpGraphicsDevice::DecodePpm(file);
    CHECK(img.ok);
    for (int y = 0; y < 4; ++y)
        for (int x = 0; x < 4; ++x) {
            int idx = y * 4 + x;
            CHECK_EQ(img.at(x, y, 0), static_cast<std::uint8_t>(255 - idx));
            CHECK_EQ(img.at(x, y, 1), static_cast<std::uint8_t>(idx));
            CHECK_EQ(img.at(x, y, 2), static_cast<std::uint8_t>((idx * 2) & 0xFF));
        }
}

// 8bpp without a palette falls back to a grayscale ramp.
TEST(ShimFileDump, Present8bppGrayscaleFallback) {
    std::string dir = makeTempDir();
    FileDumpGraphicsDevice dev;
    dev.configureDump(dir, "g", FileDumpGraphicsDevice::kBmp);
    CHECK(dev.init(3, 2, 8, false));
    Surface* s = dev.backbuffer();
    auto* px = static_cast<std::uint8_t*>(s->pixels);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 3; ++x)
            px[static_cast<std::size_t>(y) * s->pitch + x] =
                static_cast<std::uint8_t>(40 + y * 3 + x);
    dev.present();
    auto img = FileDumpGraphicsDevice::DecodeBmp24(
        readFile(dev.framePath(0, FileDumpGraphicsDevice::kBmp)));
    CHECK(img.ok);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 3; ++x) {
            std::uint8_t v = static_cast<std::uint8_t>(40 + y * 3 + x);
            CHECK_EQ(img.at(x, y, 0), v);
            CHECK_EQ(img.at(x, y, 1), v);
            CHECK_EQ(img.at(x, y, 2), v);
        }
}

// Multiple presents produce a numbered sequence with distinct content.
TEST(ShimFileDump, MultipleFramesNumberedSequence) {
    std::string dir = makeTempDir();
    FileDumpGraphicsDevice dev;
    dev.configureDump(dir, "frame", FileDumpGraphicsDevice::kBmp);
    CHECK(dev.init(2, 2, 32, false));
    Surface* s = dev.backbuffer();
    auto* px = static_cast<std::uint8_t*>(s->pixels);

    for (int f = 0; f < 3; ++f) {
        for (int i = 0; i < 4; ++i) {
            std::uint8_t* p = px + i * 4;
            p[0] = 0; p[1] = 0; p[2] = static_cast<std::uint8_t>(f * 50 + 10); p[3] = 0xFF;
        }
        dev.present();
    }
    CHECK_EQ(dev.presentCount(), 3);

    // Expected names: frame0000.bmp, frame0001.bmp, frame0002.bmp
    for (int f = 0; f < 3; ++f) {
        auto img = FileDumpGraphicsDevice::DecodeBmp24(
            readFile(dev.framePath(f, FileDumpGraphicsDevice::kBmp)));
        CHECK(img.ok);
        CHECK_EQ(img.at(0, 0, 0), static_cast<std::uint8_t>(f * 50 + 10)); // R
    }
    // The path for frame 1 must literally contain "frame0001.bmp".
    std::string p1 = dev.framePath(1, FileDumpGraphicsDevice::kBmp);
    CHECK(p1.find("frame0001.bmp") != std::string::npos);
}
