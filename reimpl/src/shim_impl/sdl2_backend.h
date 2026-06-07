#pragma once
// OPTIONAL real-window backends for IGraphicsDevice + IPlatform using SDL2.
//
// This entire file is a no-op unless GUILD_HAVE_SDL2 is defined at compile time.
// The portable defaults (FileDumpGraphicsDevice / MemoryGraphicsDevice +
// NullPlatform) require no third-party libraries and are always available; the
// SDL2 backends here add an actual on-screen window that blits the software
// framebuffer, plus real frame timing and keyboard/mouse input.
//
// ---------------------------------------------------------------------------
// How to enable SDL2
// ---------------------------------------------------------------------------
//   1. Install SDL2 dev headers (e.g. `apt install libsdl2-dev`).
//   2. Detect it with pkg-config:   pkg-config --exists sdl2
//   3. Compile with the macro + SDL2 flags, e.g.:
//
//        g++ -std=c++17 -DGUILD_HAVE_SDL2  (cont.)
//            $(pkg-config --cflags sdl2)   (cont.)
//            src/shim_impl/sdl2_backend.cpp ...  (cont.)
//            $(pkg-config --libs sdl2)
//
//   Without -DGUILD_HAVE_SDL2 the .cpp compiles to nothing and no SDL symbols
//   are referenced, so the default build links cleanly with zero deps.
//
// Pixel formats blitted: 8bpp is expanded through the palette to 32-bit XRGB
// each present(); 16bpp is treated as RGB565; 32bpp is taken as XRGB8888.
// ---------------------------------------------------------------------------
#ifdef GUILD_HAVE_SDL2

#include "shim/IGraphicsDevice.h"
#include "shim/IPlatform.h"
#include <cstdint>
#include <vector>

struct SDL_Window;
struct SDL_Renderer;
struct SDL_Texture;

namespace guild::shim {

// IGraphicsDevice over an SDL2 window. init() creates a window + streaming
// texture; present() converts the software framebuffer to XRGB8888 and blits it.
class Sdl2GraphicsDevice : public IGraphicsDevice {
public:
    Sdl2GraphicsDevice() = default;
    ~Sdl2GraphicsDevice() override { shutdown(); }

    bool init(int width, int height, int bpp, bool fullscreen) override;
    void shutdown() override;
    Surface* backbuffer() override;
    void present() override;
    void setPalette(const std::uint32_t* argb256) override;

    SDL_Window* window() const { return window_; }

private:
    std::vector<std::uint8_t> framebuffer_;
    std::vector<std::uint32_t> convert_; // width*height XRGB8888 scratch
    Surface surface_;
    std::uint32_t palette_[256] = {};
    SDL_Window* window_ = nullptr;
    SDL_Renderer* renderer_ = nullptr;
    SDL_Texture* texture_ = nullptr;
    bool inited_ = false;
};

// IPlatform over SDL2: real window event pump, monotonic timer, real input.
// The window is owned by the paired Sdl2GraphicsDevice when one is used; this
// platform can also create its own hidden window if used standalone.
class Sdl2Platform : public IPlatform {
public:
    Sdl2Platform() = default;
    ~Sdl2Platform() override { destroyMainWindow(); }

    bool createMainWindow(const char* title, int w, int h, bool fullscreen) override;
    void destroyMainWindow() override;
    bool pumpMessages() override;
    std::uint32_t timeMs() override;
    void sleepMs(std::uint32_t ms) override;
    void getMouse(MouseState& out) override;
    bool keyDown(int vkey) override;

    SDL_Window* window() const { return window_; }

private:
    SDL_Window* window_ = nullptr;
    std::uint32_t start_ = 0;
    bool started_ = false;
    bool quit_ = false;
};

} // namespace guild::shim

#endif // GUILD_HAVE_SDL2
