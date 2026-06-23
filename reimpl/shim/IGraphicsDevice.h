#pragma once
// Host graphics boundary. The engine renders into a system-memory framebuffer
// (the software rasterizer) and presents it through this interface. The original
// had 5 present modes over GDI BitBlt / DirectDraw Blt / Flip; a backend may
// implement any equivalent (e.g. blit a surface to a window).
#include <cstdint>

namespace guild::render { struct Scene3DDrawList; struct Surface; }

namespace guild::shim {

struct Surface {
    void* pixels = nullptr; // top-left origin
    int   width = 0;
    int   height = 0;
    int   pitch = 0;        // bytes per row
    int   bpp = 0;          // bits per pixel (8/16/32)
};

class IGraphicsDevice {
public:
    virtual ~IGraphicsDevice() = default;
    virtual bool init(int width, int height, int bpp, bool fullscreen) = 0;
    virtual void shutdown() = 0;
    virtual Surface* backbuffer() = 0;     // lock/get the software framebuffer
    virtual void present() = 0;            // blit/flip backbuffer to screen
    virtual void setPalette(const std::uint32_t* argb256) = 0; // 8bpp paletted mode

    // Render a 3D scene draw list on the GPU into `target` (a CPU surface the
    // caller then composites 2D over + presents). The engine's fixed-function 3D
    // raster swapped to a Vulkan pipeline (rule 3); the transform/lighting math in
    // the draw list is unchanged. Returns false if this backend has no GPU 3D path
    // (headless/software) — the caller then falls back to render::RasterizeDrawList.
    virtual bool renderScene3D(const render::Scene3DDrawList& /*dl*/,
                               render::Surface* /*target*/) { return false; }
};

} // namespace guild::shim
