// guild::play — the native CHOOSECITY screen. See sdl_city_screen.h.
//
// Faithful native front for VIBE_Menu_RunChooseCity @0x52e6d8: renders the real
// _MENUE_BACKGROUND (play::MenuAssets) + a framed list of the available cities
// (real names via render::DrawText), hover highlight, a Confirm + Back control.
// Left-click a city + Confirm (or double-click) returns it; ESC / Back returns
// {back=true}.  Backend-agnostic; drives real Vulkan+SDL unchanged.
//
// The original is a 3D scene (city-point markers picked by raycast); this is the
// 2D equivalent that drives the SAME model leaves (gui::ChooseCity_IsConfirm /
// ChooseCity_IsCityObject, the "stadt_" prefix, the "gamedata/cities" listing).
#include "play/sdl_city_screen.h"

#include "shim/IGraphicsDevice.h"
#include "shim/IPlatform.h"

#include "gui/menu_render.h"      // MenuRenderTarget, MenuFillRect, MenuPalette
#include "gui/newgame_setup.h"    // ChooseCity_IsConfirm / ChooseCity_IsCityObject / kCityNamePrefix
#include "play/menu_assets.h"     // MenuAssets — real gilde.gfx _MENUE_BACKGROUND
#include "render/text_raster.h"
#include "render/font.h"
#include "render/types.h"
#include "shim/IFileSystem.h"
#include "shim_impl/disk_filesystem.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

namespace guild::play {
namespace {

constexpr int kVkEscape = 0x1B;
constexpr int kVkReturn = 0x0D;   // Enter — confirm (mirrors enterKey 28 in the model)

// ---- CHOOSECITY 2D layout (the framed list + Confirm/Back row) --------------
// The original lays the cities out in 3D; the native screen frames them in a
// vertical list inside a panel, with the two action buttons at the bottom.
constexpr int kPanelX  = 220;     // list panel (centred-ish on an 800-wide fb)
constexpr int kPanelY  = 70;
constexpr int kPanelW  = 360;
constexpr int kRowH    = 40;
constexpr int kRowGap  = 8;
constexpr int kRowX    = kPanelX + 16;
constexpr int kRowW    = kPanelW - 32;
constexpr int kRowY0   = kPanelY + 56;     // below the header
// Action buttons (Confirm / Back) below the list.
constexpr int kBtnW    = 150;
constexpr int kBtnH    = 36;

int RowY(int i) { return kRowY0 + i * (kRowH + kRowGap); }

// Hit-test the city list; returns the row index under (mx,my) or -1.
int HitCityRow(int mx, int my, int cityCount) {
    if (mx < kRowX || mx >= kRowX + kRowW) return -1;
    for (int i = 0; i < cityCount; ++i) {
        const int ry = RowY(i);
        if (my >= ry && my < ry + kRowH) return i;
    }
    return -1;
}

// Confirm / Back button rows live just under the last list row.
int ActionRowY(int cityCount) {
    const int n = cityCount > 0 ? cityCount : 1;
    return RowY(n - 1) + kRowH + 24;
}
int ConfirmX() { return kPanelX + 24; }
int BackX()    { return kPanelX + kPanelW - 24 - kBtnW; }

bool HitConfirm(int mx, int my, int cityCount) {
    const int by = ActionRowY(cityCount), bx = ConfirmX();
    return mx >= bx && mx < bx + kBtnW && my >= by && my < by + kBtnH;
}
bool HitBack(int mx, int my, int cityCount) {
    const int by = ActionRowY(cityCount), bx = BackX();
    return mx >= bx && mx < bx + kBtnW && my >= by && my < by + kBtnH;
}

// ---- render helpers (mirrored from sdl_menu.cpp; duplication is fine) -------
const std::uint8_t* GlyphMap() {
    static std::uint8_t table[256];
    static bool init = false;
    if (!init) { render::FontInitGlyphTable(table); init = true; }
    return table;
}

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

void Outline(std::uint32_t* px, int w, int h, int x, int y, int bw, int bh,
             std::uint32_t col) {
    auto put = [&](int X, int Y) { if (X >= 0 && X < w && Y >= 0 && Y < h) px[Y * w + X] = col; };
    for (int X = x; X < x + bw; ++X) { put(X, y); put(X, y + bh - 1); }
    for (int Y = y; Y < y + bh; ++Y) { put(x, Y); put(x + bw - 1, Y); }
}

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

// A filled button face + label + outline (Confirm / Back).
void DrawButton(gui::MenuRenderTarget& tgt, std::uint32_t* scratch, int W, int H,
                int x, int y, const char* label, const gui::MenuPalette& pal,
                bool hot) {
    gui::MenuFillRect(&tgt.surf, x, y, kBtnW, kBtnH, pal.btnR, pal.btnG, pal.btnB);
    DrawLabel(tgt.surf, x + 14, y + 14, label, pal.textR, pal.textG, pal.textB);
    Outline(scratch, W, H, x, y, kBtnW, kBtnH,
            hot ? 0x00FFFF00u : 0x00C8C8DCu);
}

} // namespace

CityScreenResult RunCityScreen(shim::IGraphicsDevice& device, shim::IPlatform& plat,
                               const CityScreenConfig& cfg) {
    CityScreenResult res;
    const int W = cfg.fbW, H = cfg.fbH;
    const int n = (int)cfg.cities.size();
    std::vector<std::uint32_t> scratch((std::size_t)W * H, 0u);

    // Load the REAL menu artwork (gfx/gilde.gfx) for the backdrop when present;
    // absent -> a flat panel fill keeps the frame non-blank (asset-less tests).
    shim::DiskFileSystem assetFs(cfg.gameDir);
    MenuAssets assets;
    const bool haveAssets = !cfg.gameDir.empty() && assets.Load(assetFs);

    int selected = -1;      // the currently picked city row (click selects, Confirm commits)
    int frame = 0;
    bool prevLeft = false;
    int prevHov = -1;
    std::uint32_t lastClickMs = 0;
    int lastClickRow = -1;

    gui::MenuPalette pal;

    for (;;) {
        if (cfg.maxFrames >= 0 && frame >= cfg.maxFrames) break;

        // ---- render ----
        std::fill(scratch.begin(), scratch.end(), 0u);
        gui::MenuRenderTarget tgt = gui::MenuRenderTarget::Wrap(scratch.data(), W, H, W * 4);

        shim::MouseState ms{};
        plat.getMouse(ms);

        if (haveAssets) {
            DrawBackground(scratch.data(), W, H, assets.background().data(),
                           assets.backgroundWidth(), assets.backgroundHeight());
            res.sawBackground = true;
        }

        // The list panel (semi-opaque frame over the backdrop so the rows read).
        gui::MenuFillRect(&tgt.surf, kPanelX, kPanelY, kPanelW,
                          ActionRowY(n) + kBtnH + 24 - kPanelY,
                          pal.bgR, pal.bgG, pal.bgB);
        Outline(scratch.data(), W, H, kPanelX, kPanelY, kPanelW,
                ActionRowY(n) + kBtnH + 24 - kPanelY, 0x00C8C8DCu);
        DrawLabel(tgt.surf, kPanelX + 16, kPanelY + 18, "CHOOSE A CITY", 255, 255, 255);

        const int hov = HitCityRow(ms.x, ms.y, n);
        res.hoveredItem = hov;

        if (n == 0) {
            DrawLabel(tgt.surf, kRowX, kRowY0 + 8, "(no cities available)", 200, 180, 180);
        }
        for (int i = 0; i < n; ++i) {
            const int ry = RowY(i);
            const bool sel = (i == selected);
            gui::MenuFillRect(&tgt.surf, kRowX, ry, kRowW, kRowH,
                              sel ? (std::uint8_t)(pal.btnR + 48) : pal.btnR,
                              sel ? (std::uint8_t)(pal.btnG + 48) : pal.btnG,
                              sel ? (std::uint8_t)(pal.btnB + 48) : pal.btnB);
            DrawLabel(tgt.surf, kRowX + 14, ry + 14, cfg.cities[i].first.c_str(),
                      pal.textR, pal.textG, pal.textB);
            if (i == hov)
                Outline(scratch.data(), W, H, kRowX - 2, ry - 2, kRowW + 4, kRowH + 4,
                        0x00FFFF00u);
            else if (sel)
                Outline(scratch.data(), W, H, kRowX - 2, ry - 2, kRowW + 4, kRowH + 4,
                        0x0000FF00u);
        }

        // Confirm + Back action buttons.
        DrawButton(tgt, scratch.data(), W, H, ConfirmX(), ActionRowY(n), "CONFIRM",
                   pal, HitConfirm(ms.x, ms.y, n));
        DrawButton(tgt, scratch.data(), W, H, BackX(), ActionRowY(n), "BACK",
                   pal, HitBack(ms.x, ms.y, n));

        BlitToDevice(scratch.data(), W, H, device);
        device.present();
        ++res.framesPresented;
        prevHov = hov;
        (void)prevHov;

        // ---- input ----
        if (!plat.pumpMessages()) { res.back = true; res.backByWindow = true; break; }
        plat.getMouse(ms);
        const bool leftEdge = ms.left && !prevLeft;
        prevLeft = ms.left;

        if (plat.keyDown(kVkEscape)) { res.back = true; res.backByEsc = true; break; }

        // Enter confirms (the model's enterKey 28 / ChooseCity_IsConfirm path).
        if (plat.keyDown(kVkReturn) && selected >= 0) {
            res.confirmed = true;
            res.cityIndex = selected;
            res.cityName = cfg.cities[selected].first;
            res.cityPath = cfg.cities[selected].second;
            break;
        }

        if (leftEdge) {
            const int row = HitCityRow(ms.x, ms.y, n);
            if (row >= 0) {
                // Double-click a row confirms directly (the "double-pattern").
                const std::uint32_t now = plat.timeMs();
                const bool dbl = (row == selected) && (row == lastClickRow) &&
                                 (now - lastClickMs <= 400u);
                selected = row;
                lastClickRow = row;
                lastClickMs = now;
                if (dbl) {
                    res.confirmed = true;
                    res.cityIndex = row;
                    res.cityName = cfg.cities[row].first;
                    res.cityPath = cfg.cities[row].second;
                    break;
                }
            } else if (HitConfirm(ms.x, ms.y, n) && selected >= 0) {
                res.confirmed = true;
                res.cityIndex = selected;
                res.cityName = cfg.cities[selected].first;
                res.cityPath = cfg.cities[selected].second;
                break;
            } else if (HitBack(ms.x, ms.y, n)) {
                res.back = true;
                break;
            }
        }

        if (cfg.frameCapMs > 0) plat.sleepMs((std::uint32_t)cfg.frameCapMs);
        ++frame;
    }
    return res;
}

} // namespace guild::play
