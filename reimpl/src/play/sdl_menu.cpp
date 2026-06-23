// guild::play — native main-menu screen. See sdl_menu.h.
//
// Renders the REAL menu column (gui::RenderMainMenu) + a city-pick sub-screen into
// a 32bpp scratch via the reconstructed gfx/text leaves, converts to the device
// backbuffer (any bpp), presents through the device (Vulkan on the host), and runs
// the REAL gui::MainMenu_Dispatch transition on a click. Backend-agnostic.
#include "play/sdl_menu.h"

#include "shim/IGraphicsDevice.h"
#include "shim/IPlatform.h"

#include "gui/menu_render.h"   // MenuRenderTarget, RenderMainMenu, MenuFillRect, MenuPalette
#include "gui/main_menu.h"     // MainMenu_ButtonY, MainMenu_Select, MainMenuItem, kMainMenu*
#include "play/menu_assets.h"  // MenuAssets — real gilde.gfx background + buttons
#include "render/text_raster.h"
#include "render/font.h"
#include "render/perf_overlay.h"
#include "render/types.h"
#include "shim/IFileSystem.h"
#include "shim_impl/disk_filesystem.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace guild::play {
namespace {

constexpr int kVkEscape = 0x1B;

// Main-menu button hit box. The real column is x=32, sprite gfx 174, y-table with
// a ~43px step (53-10); use a label-width hit row.
constexpr int kBtnHitW = 280;
constexpr int kBtnHitH = 40;

// City-pick list layout (a simple vertical list of the shipped cities).
int HitMainMenu(int mx, int my) {
    if (mx < gui::kMainMenuButtonX || mx >= gui::kMainMenuButtonX + kBtnHitW) return -1;
    for (int i = 0; i < gui::kMainMenuButtonCount; ++i) {
        const int by = gui::MainMenu_ButtonY(i);
        if (my >= by && my < by + kBtnHitH) return i;
    }
    return -1;
}


// The lazily-initialised 5x7 GUI glyph map (render::FontInitGlyphTable / byte_75FB50),
// mirroring gui::menu_render's GlyphMap().
const std::uint8_t* GlyphMap() {
    static std::uint8_t table[256];
    static bool init = false;
    if (!init) { render::FontInitGlyphTable(table); init = true; }
    return table;
}

// Drive render::DrawText straight into a 32bpp render::Surface (Lock-copy mode),
// mirroring gui::menu_render's SurfacePresent()+DrawLabel().
void DrawLabel(render::Surface& s, int x, int y, const char* text,
               std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    if (!text || !*text) return;
    render::PresentGlobals pg;
    pg.mode         = render::PresentBackend::DDrawLockBlt;
    pg.ppvBits      = reinterpret_cast<std::uintptr_t>(s.pixels);
    pg.dibPitch     = s.pitch;
    pg.dibStride    = s.widthPx;
    pg.screenHeight = s.height;
    pg.pitchExtra   = 4;
    pg.lockBitDepth = 32;
    pg.primary      = nullptr;
    render::DrawText(x, y, reinterpret_cast<const std::uint8_t*>(text), r, g, b,
                     GlyphMap(), pg, s.fmt);
}

// Bright rect outline (hover highlight) on the 32bpp scratch.
void Outline(std::uint32_t* px, int w, int h, int x, int y, int bw, int bh,
             std::uint32_t col) {
    auto put = [&](int X, int Y) { if (X >= 0 && X < w && Y >= 0 && Y < h) px[Y * w + X] = col; };
    for (int X = x; X < x + bw; ++X) { put(X, y); put(X, y + bh - 1); }
    for (int Y = y; Y < y + bh; ++Y) { put(x, Y); put(x + bw - 1, Y); }
}

// Draw the decoded 800x600 (any size) menu background into the 32bpp scratch,
// scaling by nearest-neighbour when the framebuffer size differs.  Transparent
// (A==0) background pixels fall through to whatever was already cleared.
void DrawBackground(std::uint32_t* dst, int W, int H,
                    const std::uint32_t* bg, int bw, int bh) {
    if (!bg || bw <= 0 || bh <= 0) return;
    for (int y = 0; y < H; ++y) {
        const int sy = (int)((long long)y * bh / H);
        const std::uint32_t* srow = bg + (std::size_t)sy * bw;
        std::uint32_t* drow = dst + (std::size_t)y * W;
        for (int x = 0; x < W; ++x) {
            const int sx = (int)((long long)x * bw / W);
            const std::uint32_t px = srow[sx];
            if (px & 0xFF000000u) drow[x] = px;
        }
    }
}

// Convert the 32bpp XRGB scratch into the device backbuffer (16/32bpp).
void BlitToDevice(const std::uint32_t* src, int w, int h, shim::IGraphicsDevice& dev) {
    shim::Surface* bb = dev.backbuffer();
    if (!bb || !bb->pixels) return;
    const int W = bb->width < w ? bb->width : w;
    const int H = bb->height < h ? bb->height : h;
    auto* base = static_cast<std::uint8_t*>(bb->pixels);
    if (bb->bpp == 32) {
        for (int y = 0; y < H; ++y)
            std::memcpy(base + (std::size_t)y * bb->pitch, src + (std::size_t)y * w, (std::size_t)W * 4);
    } else if (bb->bpp == 16) {
        for (int y = 0; y < H; ++y) {
            auto* row = reinterpret_cast<std::uint16_t*>(base + (std::size_t)y * bb->pitch);
            const std::uint32_t* s = src + (std::size_t)y * w;
            for (int x = 0; x < W; ++x) {
                const std::uint32_t c = s[x];
                const std::uint32_t r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
                row[x] = (std::uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            }
        }
    } else if (bb->bpp == 8) {
        // grayscale fallback (paletted devices set their own palette)
        for (int y = 0; y < H; ++y) {
            auto* row = base + (std::size_t)y * bb->pitch;
            const std::uint32_t* s = src + (std::size_t)y * w;
            for (int x = 0; x < W; ++x) {
                const std::uint32_t c = s[x];
                row[x] = (std::uint8_t)(((c >> 16 & 0xFF) * 30 + (c >> 8 & 0xFF) * 59 + (c & 0xFF) * 11) / 100);
            }
        }
    }
}

} // namespace

SdlMenuResult RunSdlMenu(shim::IGraphicsDevice& device, shim::IPlatform& plat,
                         const SdlMenuConfig& cfg) {
    SdlMenuResult res;
    const int W = cfg.fbW, H = cfg.fbH;
    std::vector<std::uint32_t> scratch((std::size_t)W * H, 0u);

    // Try to load the REAL menu artwork from gfx/gilde.gfx under the game dir.
    // When present we draw the shipped background + install the sprite hook that
    // paints the real gfx-174 buttons; when absent everything below falls back to
    // the asset-less reconstructed rects/font (the menu stays non-blank).
    shim::DiskFileSystem assetFs(cfg.gameDir);
    MenuAssets assets;
    const bool haveAssets = !cfg.gameDir.empty() && assets.Load(assetFs);
    if (haveAssets) assets.InstallHooks();

    int frame = 0;
    bool prevLeft = false;
    bool prevF11 = false;

    for (;;) {
        if (cfg.maxFrames >= 0 && frame >= cfg.maxFrames) break;

        // ---- render ----
        std::fill(scratch.begin(), scratch.end(), 0u);
        gui::MenuRenderTarget tgt = gui::MenuRenderTarget::Wrap(scratch.data(), W, H, W * 4);

        shim::MouseState ms{};
        plat.getMouse(ms);

        if (haveAssets) {
            // Real artwork: draw the shipped background, then paint the real
            // gfx-174 button sprite at each menu row via the installed hook
            // (this bypasses RenderMainMenu's flat-fill backdrop so the town
            // banner stays visible), with the caption text on top.
            DrawBackground(scratch.data(), W, H, assets.background().data(),
                           assets.backgroundWidth(), assets.backgroundHeight());
            for (int i = 0; i < gui::kMainMenuButtonCount; ++i) {
                const int by = gui::MainMenu_ButtonY(i);
                gui::GetMenuRenderHooks().drawSprite(
                    &tgt.surf, gui::kMainMenuButtonX, by,
                    gui::kMainMenuButtonSprite, gui::GetMenuRenderHooks().userData);
                DrawLabel(tgt.surf, gui::kMainMenuButtonX + 8, by + 12,
                          gui::kMainMenuButtons[i].label, 255, 235, 200);
            }
        } else {
            gui::RenderMainMenu(tgt);
        }
        const int hov = HitMainMenu(ms.x, ms.y);
        res.hoveredItem = hov;
        if (hov >= 0)
            Outline(scratch.data(), W, H, gui::kMainMenuButtonX - 3,
                    gui::MainMenu_ButtonY(hov) - 3, kBtnHitW + 6, kBtnHitH + 6,
                    0x00FFFF00u);

        // In-game performance overlay (Steam-style); off unless GUILD_PERF_OVERLAY / F11.
        render::GlobalPerfOverlay().Frame(plat.timeMs());
        render::GlobalPerfOverlay().Draw(scratch.data(), W, H);

        BlitToDevice(scratch.data(), W, H, device);
        device.present();
        ++res.framesPresented;

        // ---- input ----
        if (!plat.pumpMessages()) { res.quitByWindow = true; res.choice = SdlMenuChoice::kQuit; break; }
        plat.getMouse(ms);
        const bool leftEdge = ms.left && !prevLeft;
        prevLeft = ms.left;

        const bool f11 = plat.keyDown(0x7A);   // F11 cycles the performance overlay
        if (f11 && !prevF11) render::GlobalPerfOverlay().CycleDetail();
        prevF11 = f11;

        if (plat.keyDown(kVkEscape)) { res.quitByEsc = true; res.choice = SdlMenuChoice::kQuit; break; }
        bool decided = false;
        if (leftEdge) {
            const int clicked = HitMainMenu(ms.x, ms.y);
            if (clicked >= 0) {
                const gui::MainMenuItem item = gui::MainMenu_Select(clicked);
                // Run the REAL dispatch (session-flag / close bookkeeping) so the
                // transition matches the binary even though we branch on the item.
                (void)gui::MainMenu_Dispatch(clicked);
                decided = true;
                switch (item) {
                    case gui::MainMenuItem::kQuit:        res.choice = SdlMenuChoice::kQuit; break;
                    case gui::MainMenuItem::kNewGame:     res.choice = SdlMenuChoice::kNewGame; break;
                    case gui::MainMenuItem::kLoad:        res.choice = SdlMenuChoice::kLoadGame; break;
                    case gui::MainMenuItem::kGameOptions: res.choice = SdlMenuChoice::kOptions; res.optionsPage = 0; break;
                    case gui::MainMenuItem::kGfxOptions:  res.choice = SdlMenuChoice::kOptions; res.optionsPage = 1; break;
                    case gui::MainMenuItem::kSfxOptions:  res.choice = SdlMenuChoice::kOptions; res.optionsPage = 2; break;
                    case gui::MainMenuItem::kCredits:     res.choice = SdlMenuChoice::kCredits; break;
                    default: decided = false; break;   // Multiplayer: not implemented natively -> stay
                }
            }
        }
        if (decided) break;

        if (cfg.frameCapMs > 0) plat.sleepMs((std::uint32_t)cfg.frameCapMs);
        ++frame;
    }
    // Restore the inert default hooks so a later asset-less render falls back.
    if (haveAssets) MenuAssets::ClearHooks();
    return res;
}

} // namespace guild::play
