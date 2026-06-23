// OPTIONAL SDL2 backends — compiled to nothing unless GUILD_HAVE_SDL2 is set.
// See sdl2_backend.h for the enable/build instructions.
#ifdef GUILD_HAVE_SDL2

#include "shim_impl/sdl2_backend.h"

#include <SDL.h>

namespace guild::shim {

// ----------------------------- graphics -----------------------------------

bool Sdl2GraphicsDevice::init(int width, int height, int bpp, bool fullscreen) {
    if (width <= 0 || height <= 0 || (bpp != 8 && bpp != 16 && bpp != 32))
        return false;
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
        return false;

    Uint32 flags = fullscreen ? SDL_WINDOW_FULLSCREEN : SDL_WINDOW_SHOWN;
    window_ = SDL_CreateWindow("Guild", SDL_WINDOWPOS_CENTERED,
                               SDL_WINDOWPOS_CENTERED, width, height, flags);
    if (!window_)
        return false;
    renderer_ = SDL_CreateRenderer(window_, -1, SDL_RENDERER_ACCELERATED);
    if (!renderer_)
        renderer_ = SDL_CreateRenderer(window_, -1, 0);
    if (!renderer_)
        return false;
    texture_ = SDL_CreateTexture(renderer_, SDL_PIXELFORMAT_ARGB8888,
                                 SDL_TEXTUREACCESS_STREAMING, width, height);
    if (!texture_)
        return false;

    const int bpx = bpp / 8;
    framebuffer_.assign(static_cast<std::size_t>(width) * height * bpx, 0);
    convert_.assign(static_cast<std::size_t>(width) * height, 0);

    surface_.pixels = framebuffer_.data();
    surface_.width = width;
    surface_.height = height;
    surface_.pitch = width * bpx;
    surface_.bpp = bpp;
    inited_ = true;
    return true;
}

void Sdl2GraphicsDevice::shutdown() {
    if (texture_) { SDL_DestroyTexture(texture_); texture_ = nullptr; }
    if (renderer_) { SDL_DestroyRenderer(renderer_); renderer_ = nullptr; }
    if (window_) { SDL_DestroyWindow(window_); window_ = nullptr; }
    framebuffer_.clear();
    convert_.clear();
    surface_ = Surface{};
    inited_ = false;
}

Surface* Sdl2GraphicsDevice::backbuffer() {
    if (!inited_)
        return nullptr;
    surface_.pixels = framebuffer_.data();
    return &surface_;
}

void Sdl2GraphicsDevice::setPalette(const std::uint32_t* argb256) {
    if (!argb256)
        return;
    for (int i = 0; i < 256; ++i)
        palette_[i] = argb256[i];
}

void Sdl2GraphicsDevice::present() {
    if (!inited_)
        return;
    const int w = surface_.width, h = surface_.height, bpp = surface_.bpp;
    const std::uint8_t* px = framebuffer_.data();
    std::uint32_t* out = convert_.data();

    for (int i = 0; i < w * h; ++i) {
        std::uint32_t argb;
        if (bpp == 8) {
            argb = palette_[px[i]] | 0xFF000000u;
        } else if (bpp == 16) {
            std::uint16_t v = static_cast<std::uint16_t>(px[i * 2] | (px[i * 2 + 1] << 8));
            std::uint32_t r5 = (v >> 11) & 0x1F, g6 = (v >> 5) & 0x3F, b5 = v & 0x1F;
            std::uint32_t r = (r5 << 3) | (r5 >> 2);
            std::uint32_t g = (g6 << 2) | (g6 >> 4);
            std::uint32_t b = (b5 << 3) | (b5 >> 2);
            argb = 0xFF000000u | (r << 16) | (g << 8) | b;
        } else { // 32bpp XRGB
            const std::uint8_t* p = px + i * 4;
            argb = 0xFF000000u | (static_cast<std::uint32_t>(p[2]) << 16) |
                   (static_cast<std::uint32_t>(p[1]) << 8) | p[0];
        }
        out[i] = argb;
    }

    SDL_UpdateTexture(texture_, nullptr, out, w * static_cast<int>(sizeof(std::uint32_t)));
    SDL_RenderClear(renderer_);
    SDL_RenderCopy(renderer_, texture_, nullptr, nullptr);
    SDL_RenderPresent(renderer_);
}

// ----------------------------- platform ------------------------------------

bool Sdl2Platform::createMainWindow(const char* title, int w, int h, bool fullscreen) {
    if (SDL_InitSubSystem(SDL_INIT_VIDEO) != 0)
        return false;
    if (!started_) { start_ = SDL_GetTicks(); started_ = true; }
    Uint32 flags = fullscreen ? SDL_WINDOW_FULLSCREEN : SDL_WINDOW_SHOWN;
    window_ = SDL_CreateWindow(title ? title : "Guild", SDL_WINDOWPOS_CENTERED,
                               SDL_WINDOWPOS_CENTERED, w, h, flags);
    return window_ != nullptr;
}

void Sdl2Platform::destroyMainWindow() {
    if (window_) { SDL_DestroyWindow(window_); window_ = nullptr; }
}

bool Sdl2Platform::pumpMessages() {
    SDL_Event e;
    while (SDL_PollEvent(&e)) {
        if (e.type == SDL_QUIT)
            quit_ = true;
        else if (e.type == SDL_MOUSEWHEEL) {
            // SDL_MOUSEWHEEL -> MouseState::wheel notches (the WM_MOUSEWHEEL
            // 120-unit stream the original fed into dword_672254). Respect the
            // "natural"/flipped direction SDL reports.
            int n = e.wheel.y;
            if (e.wheel.direction == SDL_MOUSEWHEEL_FLIPPED)
                n = -n;
            wheelAccum_ += n;
        }
    }
    return !quit_;
}

std::uint32_t Sdl2Platform::timeMs() {
    if (!started_) { start_ = SDL_GetTicks(); started_ = true; }
    return SDL_GetTicks() - start_;
}

void Sdl2Platform::sleepMs(std::uint32_t ms) {
    if (ms)
        SDL_Delay(ms);
}

void Sdl2Platform::getMouse(MouseState& out) {
    int x = 0, y = 0;
    Uint32 b = SDL_GetMouseState(&x, &y);
    out.x = x;
    out.y = y;
    out.left = (b & SDL_BUTTON(SDL_BUTTON_LEFT)) != 0;
    out.right = (b & SDL_BUTTON(SDL_BUTTON_RIGHT)) != 0;
    out.middle = (b & SDL_BUTTON(SDL_BUTTON_MIDDLE)) != 0;
    out.wheel = wheelAccum_;     // notches since the previous getMouse()
    wheelAccum_ = 0;
}

// Map a Win32 virtual-key code to an SDL_Scancode for the common keys the
// engine polls. Unknown codes return false. (The original used GetAsyncKeyState
// with VK_* constants; this gives those callers a working real-input path.)
bool Sdl2Platform::keyDown(int vkey) {
    SDL_Scancode sc = SDL_SCANCODE_UNKNOWN;
    if (vkey >= 'A' && vkey <= 'Z')
        sc = static_cast<SDL_Scancode>(SDL_SCANCODE_A + (vkey - 'A'));
    else if (vkey >= '0' && vkey <= '9')
        sc = static_cast<SDL_Scancode>(SDL_SCANCODE_0 + (vkey - '0'));
    else {
        switch (vkey) {
            case 0x1B: sc = SDL_SCANCODE_ESCAPE; break; // VK_ESCAPE
            case 0x20: sc = SDL_SCANCODE_SPACE; break;  // VK_SPACE
            case 0x0D: sc = SDL_SCANCODE_RETURN; break; // VK_RETURN
            case 0x25: sc = SDL_SCANCODE_LEFT; break;   // VK_LEFT
            case 0x26: sc = SDL_SCANCODE_UP; break;     // VK_UP
            case 0x27: sc = SDL_SCANCODE_RIGHT; break;  // VK_RIGHT
            case 0x28: sc = SDL_SCANCODE_DOWN; break;   // VK_DOWN
            case 0x10: sc = SDL_SCANCODE_LSHIFT; break; // VK_SHIFT
            case 0x11: sc = SDL_SCANCODE_LCTRL; break;  // VK_CONTROL
            default: return false;
        }
    }
    const Uint8* state = SDL_GetKeyboardState(nullptr);
    return state && state[sc] != 0;
}

} // namespace guild::shim

#endif // GUILD_HAVE_SDL2
