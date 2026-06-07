#include "shim_impl/memory_graphics.h"

namespace guild::shim {

bool MemoryGraphicsDevice::init(int width, int height, int bpp, bool /*fullscreen*/) {
    if (width <= 0 || height <= 0 || (bpp != 8 && bpp != 16 && bpp != 32))
        return false;

    const int bytes_per_pixel = bpp / 8;
    const int pitch = width * bytes_per_pixel;
    framebuffer_.assign(static_cast<std::size_t>(pitch) * height, 0);
    presented_.clear();

    surface_.pixels = framebuffer_.data();
    surface_.width = width;
    surface_.height = height;
    surface_.pitch = pitch;
    surface_.bpp = bpp;

    inited_ = true;
    present_count_ = 0;
    return true;
}

void MemoryGraphicsDevice::shutdown() {
    framebuffer_.clear();
    presented_.clear();
    surface_ = Surface{};
    inited_ = false;
}

Surface* MemoryGraphicsDevice::backbuffer() {
    if (!inited_)
        return nullptr;
    // pixels may have moved if framebuffer_ reallocated; keep it current.
    surface_.pixels = framebuffer_.data();
    return &surface_;
}

void MemoryGraphicsDevice::present() {
    if (!inited_)
        return;
    presented_ = framebuffer_; // keep the last frame for capture
    ++present_count_;
}

void MemoryGraphicsDevice::setPalette(const std::uint32_t* argb256) {
    if (!argb256)
        return;
    for (int i = 0; i < 256; ++i)
        palette_[i] = argb256[i];
}

} // namespace guild::shim
