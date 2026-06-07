#pragma once
// Portable default backend for IGraphicsDevice — a pure system-memory framebuffer.
// NOT a translation of gilde.exe; a clean implementation of the interface contract.
// The engine's software rasterizer draws into backbuffer(); present() captures the
// frame into an inspectable buffer so tests can assert on rendered output. No GPU.
#include "shim/IGraphicsDevice.h"
#include <cstdint>
#include <vector>

namespace guild::shim {

class MemoryGraphicsDevice : public IGraphicsDevice {
public:
    ~MemoryGraphicsDevice() override { shutdown(); }

    // IGraphicsDevice
    bool init(int width, int height, int bpp, bool fullscreen) override;
    void shutdown() override;
    Surface* backbuffer() override;
    void present() override;
    void setPalette(const std::uint32_t* argb256) override;

    // --- test/inspection hooks (not part of the interface) ---
    // Copy of the framebuffer taken at the last present() (empty before first).
    const std::vector<std::uint8_t>& lastPresented() const { return presented_; }
    int presentCount() const { return present_count_; }
    // 256-entry ARGB palette as set by setPalette.
    const std::uint32_t* palette() const { return palette_; }

private:
    std::vector<std::uint8_t> framebuffer_; // live backbuffer pixels
    std::vector<std::uint8_t> presented_;   // snapshot from last present()
    Surface surface_;
    std::uint32_t palette_[256] = {};
    int present_count_ = 0;
    bool inited_ = false;
};

} // namespace guild::shim
