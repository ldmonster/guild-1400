#pragma once
// Portable, dependency-free backend for IGraphicsDevice that makes the
// reconstructed renderer's framebuffer *visible*: every present() snapshots the
// software framebuffer and writes it to a numbered image file (BMP and/or PPM)
// in a chosen directory, so frames can be inspected with any image viewer.
//
// NOT a translation of gilde.exe; a clean implementation of the interface
// contract. It composes a MemoryGraphicsDevice for the in-memory framebuffer
// and adds the file-output stage on top of present().
//
// The pixel snapshot is expanded to 24-bit RGB on dump:
//   8bpp  -> looked up through the 256-entry ARGB palette set via setPalette()
//            (identity grayscale ramp if no palette was supplied).
//   16bpp -> treated as RGB565 (little-endian).
//   32bpp -> treated as 0xAARRGGBB (alpha ignored).
//
// The on-disk files use a clean, standard, round-trippable layout (NOT the
// quirky gilde.exe bmp.cpp codec): a 54-byte 24-bit BI_RGB BMP and/or a binary
// P6 PPM, both inspectable by standard tools. A matching decoder is provided so
// tests can read frames back without external libraries.
//
// File naming: "<prefix>NNNN.<ext>" where NNNN is the zero-based present index,
// e.g. "frame0000.bmp", "frame0001.bmp", ...
//
// ---------------------------------------------------------------------------
// Enabling the optional SDL2 backend (see sdl2_backend.h): this file-dump
// device plus the headless platform are the always-available defaults and need
// no third-party libraries. The SDL2 window backend is separate and only built
// when GUILD_HAVE_SDL2 is defined (see sdl2_backend.h for build/link flags).
// ---------------------------------------------------------------------------
#include "shim/IGraphicsDevice.h"
#include "shim_impl/memory_graphics.h"
#include <cstdint>
#include <string>
#include <vector>

namespace guild::shim {

// A decoded RGB image (top-down, 3 bytes/pixel R,G,B). Used by the readback
// helpers so tests can verify dumped files without external image libraries.
struct RgbImage {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> rgb; // width*height*3, top-down, R,G,B
    bool ok = false;
    std::uint8_t at(int x, int y, int c) const {
        return rgb[(static_cast<std::size_t>(y) * width + x) * 3 + c];
    }
};

class FileDumpGraphicsDevice : public IGraphicsDevice {
public:
    enum Format {
        kBmp = 1,
        kPpm = 2,
        kBoth = kBmp | kPpm,
    };

    FileDumpGraphicsDevice() = default;
    ~FileDumpGraphicsDevice() override { shutdown(); }

    // Where dumps go and how they're named. Call before present() (may be called
    // any time; affects subsequent presents). `dir` should already exist.
    void configureDump(const std::string& dir, const std::string& prefix = "frame",
                       int formats = kBmp);

    // IGraphicsDevice
    bool init(int width, int height, int bpp, bool fullscreen) override;
    void shutdown() override;
    Surface* backbuffer() override;
    void present() override;
    void setPalette(const std::uint32_t* argb256) override;

    // --- test/inspection hooks (not part of the interface) ---
    int presentCount() const { return mem_.presentCount(); }
    const std::vector<std::uint8_t>& lastPresented() const { return mem_.lastPresented(); }
    const std::uint32_t* palette() const { return mem_.palette(); }
    // Path of the file written for present index `frame` in `fmt` (kBmp/kPpm).
    std::string framePath(int frame, Format fmt) const;
    // The current snapshot expanded to top-down 24-bit RGB (what gets dumped).
    RgbImage snapshotRgb() const;

    // --- static helpers: standard 24-bit BMP / P6 PPM encode + decode ---
    static std::vector<std::uint8_t> EncodeBmp24(int w, int h, const std::uint8_t* rgb);
    static std::vector<std::uint8_t> EncodePpm(int w, int h, const std::uint8_t* rgb);
    static RgbImage DecodeBmp24(const std::vector<std::uint8_t>& file);
    static RgbImage DecodePpm(const std::vector<std::uint8_t>& file);

private:
    bool dumpToFile(const std::string& path, const std::vector<std::uint8_t>& bytes) const;

    MemoryGraphicsDevice mem_;
    std::string dir_;
    std::string prefix_ = "frame";
    int formats_ = kBmp;
    bool configured_ = false;
};

} // namespace guild::shim
