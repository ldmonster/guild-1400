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

// Nearest-neighbour scaled blit of a decoded shape (alpha-keyed), mirroring
// native_main_menu's ScaledBlit — used for the _BUTTON_RED 3-slice caps/centre.
void ScaledShapeBlit(std::uint32_t* dst, int W, int H, const render::DecodedShape& s,
                     int rx, int ry, int rw, int rh) {
    if (s.width <= 0 || s.height <= 0 || rw <= 0 || rh <= 0) return;
    for (int y = 0; y < rh; ++y) {
        const int dy = ry + y; if (dy < 0 || dy >= H) continue;
        const int sy = y * s.height / rh;
        for (int x = 0; x < rw; ++x) {
            const int dx = rx + x; if (dx < 0 || dx >= W) continue;
            const int sx = x * s.width / rw;
            const std::uint32_t p = s.argb[(std::size_t)sy * s.width + sx];
            if (p & 0xFF000000u) dst[(std::size_t)dy * W + dx] = p;
        }
    }
}

// Draw a full _BUTTON_RED (gfx 174) 3-slice — left cap (shape 0/3) + stretched
// centre (shape 2/5) + right cap (shape 1/4) — exactly like native_main_menu's
// DrawButton3Slice.  This replaces the previous single-centre-slice blit (which
// dropped the rounded end caps and ignored the label width).
void DrawMenuButton(std::uint32_t* dst, int W, int H, MenuAssets& a,
                    int rx, int ry, int rw, int rh, bool sel) {
    const render::DecodedShape* L = a.buttonFrame(sel ? 3 : 0);
    const render::DecodedShape* C = a.buttonFrame(sel ? 5 : 2);
    const render::DecodedShape* R = a.buttonFrame(sel ? 4 : 1);
    if (!L || !C || !R) return;
    const int capW = MenuFont::kCapW;            // 12 native (shapes 0,1 width)
    int lw = capW, rcw = capW, mw = rw - lw - rcw;
    if (mw < 0) { mw = 0; rcw = rw - lw; if (rcw < 0) { rcw = 0; lw = rw; } }
    ScaledShapeBlit(dst, W, H, *L, rx,           ry, lw,  rh);
    ScaledShapeBlit(dst, W, H, *C, rx + lw,      ry, mw,  rh);
    ScaledShapeBlit(dst, W, H, *R, rx + lw + mw, ry, rcw, rh);
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

    // Resolve the eight localized (CP1251) main-menu captions from the shipped
    // textbin (the same _OPTIONEN_MENUE_* strings the engine draws); indexed by
    // button row. Empty entries fall back to the built-in English caption.
    std::string menuLabels[8];
    const bool haveLabels = haveAssets && ResolveMainMenuLabels(cfg.gameDir, menuLabels);

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
            // Real artwork: draw the shipped background, then paint each menu row
            // as the real _BUTTON_RED (gfx 174) 3-slice sized to its caption, with
            // the localized caption rendered in the real engine _FONT (record 66).
            DrawBackground(scratch.data(), W, H, assets.background().data(),
                           assets.backgroundWidth(), assets.backgroundHeight());
            const MenuFont& fnt = assets.font();
            const int rh = 33;   // _BUTTON_RED native height (shape 0..5 are all *x33)
            for (int i = 0; i < gui::kMainMenuButtonCount; ++i) {
                const int by = gui::MainMenu_ButtonY(i);
                const int rx = gui::kMainMenuButtonX;
                // Localized caption (CP1251); fall back to the built-in English one.
                const char* lab = (haveLabels && !menuLabels[i].empty())
                                      ? menuLabels[i].c_str()
                                      : gui::kMainMenuButtons[i].label;
                if (fnt.loaded()) {
                    // Button width = caption width + both 12px caps + 4 (the engine's
                    // Object_RecomputeSize @0x41b164 sprite-kind-9 sizing).
                    const int btnW = fnt.ButtonWidth(lab);
                    DrawMenuButton(scratch.data(), W, H, assets, rx, by, btnW, rh, false);
                    // Centre the real-glyph caption inside the button face.
                    const int textW = fnt.MeasureWidth(lab);
                    const int textH = fnt.lineHeight() > 0 ? fnt.lineHeight() : 17;
                    fnt.DrawText(scratch.data(), W, H,
                                 rx + (btnW - textW) / 2, by + (rh - textH) / 2,
                                 lab, 1, 255, 235, 200);
                } else {
                    // Font missing: still draw the real 3-slice, ASCII caption on top.
                    const int btnW = 124;
                    DrawMenuButton(scratch.data(), W, H, assets, rx, by, btnW, rh, false);
                    DrawLabel(tgt.surf, rx + 8, by + 12, lab, 255, 235, 200);
                }
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
