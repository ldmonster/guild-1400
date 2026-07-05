// guild::play — native bridge for the byte-faithful main menu. See header.
#include "play/native_main_menu.h"

#include "shim/IGraphicsDevice.h"
#include "shim/IPlatform.h"
#include "shim/IFileSystem.h"
#include "shim/IAudioDevice.h"
#include "play/mp3_decode.h"
#include "crt/rand.h"             // crt::RandNext — VIBE_Util_RandNext @0x5cb8bc

#include "gui/main_menu_run.h"     // Menu_RunMainMenu, MainMenuRunHooks/State/Record, Menu_SetRunHooks
#include "gui/main_menu.h"         // kMainMenuButtons, MainMenu_ButtonY (captions by y)
#include "gui/window.h"            // Window_Create (the real form-backing build window)
#include "gui/menu_render.h"       // MenuRenderTarget, MenuPalette
#include "play/menu_assets.h"      // MenuAssets — real gilde.gfx background + gfx-174 buttons
#include "render/text_raster.h"
#include "render/font.h"
#include "render/types.h"
#include "render/bmp.h"

#include "play/sdl_city_screen.h"
#include "play/sdl_city_screen3d.h"
#include "play/sdl_charcreate_screen.h"
#include "play/sdl_charintro_screen.h"   // VIBE_Menu_ChooseCharacterIntroVariant (difficulty)
#include "play/sdl_choosehistory_screen.h"  // VIBE_Menu_RunChooseHistory (perspective)
#include "play/sdl_chooseplayer_screen.h"   // VIBE_Menu_RunChoosePlayer (identity wizard)
#include "play/sdl_loadgame_screen.h"       // VIBE_Menu_RunLoadGame (save browser)
#include "play/sdl_options_screen.h"
#include "play/sdl_credits_screen.h"
#include "play/menu_recon_network_screens.h"  // RunNetworkScreen (Сетевая игра hub)

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace guild::play {
namespace {

// ---------------------------------------------------------------------------
// Menu layout — gilde.exe authors the whole GUI at an 800x600 "design" canvas and
// scales every coordinate by screenW/800 at render time (VIBE_Gui_LoadGfxFile sets
// flt_62D224 = resolution * (1/800); the menu form is positioned mode-3 = centred).
// We reproduce that: place the 300x320 MAIN_MENU window centred in the 800x600
// design space, lay the eight sprite buttons at x = kMainMenuButtonX(32) + windowX
// and the y-table, then scale the design rect to the framebuffer.
// ---------------------------------------------------------------------------
constexpr int kDesignW = 800, kDesignH = 600;
// MENU\MAIN_MENU.form (FRM2) window record: x=232, y=160, w=407, h=352, flags=0x10,
// 0 objects (decoded via Form_ParseResourceFile). flags=0x10 allocates only the
// window text buffer (Window_Create @0x419c38) — NO frame/background gfx is drawn
// (frame/corner gfx come from flags 0x8/0x20, which this window does not set), so
// the buttons float directly on the static _MENUE_BACKGROUND. The window is placed
// by Window_PositionAtCoord(mode 3) @0x41d7e0, which at base GUI scale resolves to
// the form's own x/y (the scale term A*(1-1/s) is the high-res correction).
constexpr int kWinW = 407, kWinH = 352;            // form window w/h
constexpr int kWinX = 232, kWinY = 160;            // form window x/y (mode-3 base)

// _BUTTON_RED (gfx 174) 3-slice: cap(12) + centre(100) + cap(12) = 124 x 33.
// Six shapes = two states of three parts: normal {0=left,2=centre,1=right},
// selected/hover {3=left,5=centre,4=right}.
constexpr int kBtnDesignW = 124, kBtnDesignH = 33;
constexpr int kBtnCapW = 12, kBtnMidW = 100;
// Live-binary ground truth (frida @800x600): menu buttons are a fixed 300px wide
// at screen x=258 (the full MENU\MAIN_MENU window width), label centred inside.
constexpr int kBtnScreenX0 = 258, kBtnFullW = 300;

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
    pg.mode = render::PresentBackend::DDrawLockBlt;
    pg.ppvBits = reinterpret_cast<std::uintptr_t>(s.pixels);
    pg.dibPitch = s.pitch; pg.dibStride = s.widthPx; pg.screenHeight = s.height;
    pg.pitchExtra = 4; pg.lockBitDepth = 32; pg.primary = nullptr;
    render::DrawText(x, y, reinterpret_cast<const std::uint8_t*>(text), r, g, b, GlyphMap(), pg, s.fmt);
}
void Outline(std::uint32_t* px, int w, int h, int x, int y, int bw, int bh, std::uint32_t col) {
    auto put = [&](int X, int Y) { if (X >= 0 && X < w && Y >= 0 && Y < h) px[Y * w + X] = col; };
    for (int X = x; X < x + bw; ++X) { put(X, y); put(X, y + bh - 1); }
    for (int Y = y; Y < y + bh; ++Y) { put(x, Y); put(x + bw - 1, Y); }
}
// Nearest-neighbour scale-blit of a decoded sprite into the dest rect (A==0 skips),
// mirroring the engine's point-sampled UI upscale.
void ScaledBlit(std::uint32_t* dst, int W, int H, const render::DecodedShape& sh,
                int dx, int dy, int dw, int dh) {
    if (sh.width <= 0 || sh.height <= 0 || dw <= 0 || dh <= 0) return;
    for (int y = 0; y < dh; ++y) {
        const int Y = dy + y;
        if (Y < 0 || Y >= H) continue;
        const int sy = y * sh.height / dh;
        const std::uint32_t* srow = sh.argb.data() + (std::size_t)sy * sh.width;
        std::uint32_t* drow = dst + (std::size_t)Y * W;
        for (int x = 0; x < dw; ++x) {
            const int X = dx + x;
            if (X < 0 || X >= W) continue;
            const std::uint32_t p = srow[x * sh.width / dw];
            if (p & 0xFF000000u) drow[X] = p;
        }
    }
}
// Draw a full _BUTTON_RED 3-slice (left cap + stretched centre + right cap) into the
// scaled dest rect.  `sel` swaps to the selected/hover frame triple {3,5,4}.
void DrawButton3Slice(std::uint32_t* dst, int W, int H, MenuAssets& a,
                      int rx, int ry, int rw, int rh, bool sel) {
    const render::DecodedShape* L = a.buttonFrame(sel ? 3 : 0);
    const render::DecodedShape* C = a.buttonFrame(sel ? 5 : 2);
    const render::DecodedShape* R = a.buttonFrame(sel ? 4 : 1);
    if (!L || !C || !R) return;
    // _BUTTON_RED 3-slice at NATIVE size: the two 12px end caps are drawn at their
    // native width and the 100px centre shape is STRETCHED to fill the gap (the
    // button is sized to the label by Object_RecomputeSize @0x41b164: width =
    // textWidth + cap(12) + cap(12) + 4). Caps do not scale.
    int lw = kBtnCapW;                       // 12 native
    int rcw = kBtnCapW;                       // 12 native
    int mw = rw - lw - rcw;                   // centre fills the remainder
    if (mw < 0) { mw = 0; rcw = rw - lw; if (rcw < 0) { rcw = 0; lw = rw; } }
    ScaledBlit(dst, W, H, *L, rx,          ry, lw,  rh);
    ScaledBlit(dst, W, H, *C, rx + lw,     ry, mw,  rh);
    ScaledBlit(dst, W, H, *R, rx + lw + mw, ry, rcw, rh);
}

// ---------------------------------------------------------------------------
// Mouse cursor (gilde.exe hides the OS cursor and draws _MOUSE_CURSOR; that gfx
// record is in the general SHAPBANK codec our decoder doesn't yet handle, so we
// render a faithful arrow: white fill, black outline, hotspot at the top-left
// tip).  ' ' = transparent, 'o' = black outline, '.' = white fill.
// ---------------------------------------------------------------------------
const char* const kCursorArrow[] = {
    "o          ",
    "oo         ",
    "o.o        ",
    "o..o       ",
    "o...o      ",
    "o....o     ",
    "o.....o    ",
    "o......o   ",
    "o.......o  ",
    "o........o ",
    "o.....ooooo",
    "o..o..o    ",
    "o.o o..o   ",
    "oo  o..o   ",
    "o    o..o  ",
    "     o..o  ",
    "      o..o ",
    "      o..o ",
    "       oo  ",
};
constexpr int kCursorW = 11;
constexpr int kCursorH = (int)(sizeof(kCursorArrow) / sizeof(kCursorArrow[0]));

// Hotspot of the real _MOUSE_CURSOR hand (its fingertip = topmost opaque pixel).
constexpr int kCursorHotX = 2, kCursorHotY = 1;

// Menu fade-in length (gilde.exe VIBE_Fade_Register(...,"BLACK", 90, 10) -> 90 ticks).
constexpr int kFadeFrames = 90;

// Darken the whole frame by `factor` (0 -> all black, 1 -> untouched): the menu
// fade-in overlay (a full-screen black quad whose alpha runs 1.0 -> 0.0).
void FadeFromBlack(std::uint32_t* px, int W, int H, double factor) {
    if (factor >= 1.0) return;
    if (factor < 0.0) factor = 0.0;
    const int f = (int)(factor * 256.0);  // 0..256 brightness scale
    const std::size_t n = (std::size_t)W * H;
    for (std::size_t i = 0; i < n; ++i) {
        const std::uint32_t c = px[i];
        const int r = (((c >> 16) & 0xFF) * f) >> 8;
        const int g = (((c >> 8) & 0xFF) * f) >> 8;
        const int b = ((c & 0xFF) * f) >> 8;
        px[i] = 0xFF000000u | ((std::uint32_t)r << 16) | ((std::uint32_t)g << 8) | (std::uint32_t)b;
    }
}

// Draw the arrow at (mx,my) (the hotspot), point-scaled by `scale` (integer-ish).
void DrawCursor(std::uint32_t* dst, int W, int H, int mx, int my, int scale) {
    if (scale < 1) scale = 1;
    for (int gy = 0; gy < kCursorH; ++gy) {
        const char* row = kCursorArrow[gy];
        for (int gx = 0; gx < kCursorW; ++gx) {
            const char c = row[gx];
            if (c == ' ') continue;
            const std::uint32_t col = (c == 'o') ? 0xFF000000u : 0xFFFFFFFFu;
            for (int sy = 0; sy < scale; ++sy)
                for (int sx = 0; sx < scale; ++sx) {
                    const int X = mx + gx * scale + sx, Y = my + gy * scale + sy;
                    if (X >= 0 && X < W && Y >= 0 && Y < H) dst[(std::size_t)Y * W + X] = col;
                }
        }
    }
}
// While a sub-screen (options/city/credits) is showing, the menu's game-drawn
// cursor isn't running, so re-show the OS cursor for its lifetime, then re-hide it.
struct OsCursorScope {
    shim::IPlatform* p;
    explicit OsCursorScope(shim::IPlatform* p) : p(p) { if (p) p->showSystemCursor(true); }
    ~OsCursorScope() { if (p) p->showSystemCursor(false); }
};
void DrawBackground(std::uint32_t* dst, int W, int H, const std::uint32_t* bg, int bw, int bh) {
    if (!bg || bw <= 0 || bh <= 0) return;
    for (int y = 0; y < H; ++y) {
        const int sy = (int)((long long)y * bh / H);
        const std::uint32_t* srow = bg + (std::size_t)sy * bw;
        std::uint32_t* drow = dst + (std::size_t)y * W;
        for (int x = 0; x < W; ++x) {
            const std::uint32_t px = srow[(int)((long long)x * bw / W)];
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
                const std::uint32_t c = s[x], r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
                row[x] = (std::uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
            }
        }
    }
}
// The city FILE base name the original keeps in ReturnedString @0x122EE50 (the
// enumerated save-browser entry, extension stripped): the session start builds
// "gamedata/cities/<base>.cty" from it (gilde.exe 0x533d4b "%s/%s.cty"). Our
// city list carries full paths ("Resources/gamedata/Cities/AUGSBURG.cty"), so
// strip the directory and the extension.
std::string CityFileBaseName(const std::string& path) {
    std::size_t slash = path.find_last_of("/\\");
    std::string base = (slash == std::string::npos) ? path : path.substr(slash + 1);
    std::size_t dot = base.find_last_of('.');
    if (dot != std::string::npos) base.resize(dot);
    return base;
}

// Caption for a built button by its y row (matches the kMainMenuButtons y-table).
const char* CaptionForY(int y) {
    for (int i = 0; i < gui::kMainMenuButtonCount; ++i)
        if (gui::MainMenu_ButtonY(i) == y) return gui::kMainMenuButtons[i].label;
    return "";
}

// The native host hooks for Menu_RunMainMenu.
struct NativeMenuHooks : gui::MainMenuRunHooks {
    shim::IGraphicsDevice* dev = nullptr;
    shim::IPlatform* plat = nullptr;
    shim::IFileSystem* fs = nullptr;
    std::string gameDir;
    const std::vector<std::pair<std::string, std::string>>* cities = nullptr;
    int fbW = 800, fbH = 600, frameCapMs = 16;
    MenuAssets* assets = nullptr;
    gui::MainMenuRunState* st = nullptr;
    gui::MainMenuRunRecord* rec = nullptr;
    NativeMenuResult* result = nullptr;
    std::vector<std::uint32_t> scratch;

    shim::MouseState mouse{};
    bool prevLeft = false;
    bool windowOpen = true;
    bool escDown = false;
    int  hoverWidget = -1;
    int  hoverIndex = -1;
    int  fadeFrame = 0;   // menu fade-in progress (0..kFadeFrames)

    // Per-button resolved label (CP1251) + design-space width, indexed by the
    // gui::MainMenu button order (the y-table row index). Populated from the real
    // textbin (_OPTIONEN_MENUE_*) + the _FONT metrics; empty/124 falls back.
    const std::string* labelsByRow = nullptr;       // [8] resolved labels (may be empty)
    const int*         widthsByRow = nullptr;        // [8] design widths

    // Map a built button's design-y to its 0..7 row index (label/width lookup).
    int RowForY(int y) const {
        for (int i = 0; i < gui::kMainMenuButtonCount; ++i)
            if (gui::MainMenu_ButtonY(i) == y) return i;
        return -1;
    }
    int DesignWForY(int y) const {
        const int row = RowForY(y);
        return (widthsByRow && row >= 0) ? widthsByRow[row] : kBtnDesignW;
    }

    // Screen rect of the button at design-y `designY` (centred + scaled layout),
    // using the button's measured design width.
    MenuButtonRect ButtonRect(int designY) const {
        return MenuButtonScreenRect(designY, fbW, fbH, DesignWForY(designY));
    }

    void recomputeHover() {
        hoverWidget = -1; hoverIndex = -1;
        if (!rec) return;
        for (int i = 0; i < rec->buttonCount; ++i) {
            const MenuButtonRect r = ButtonRect(rec->buttons[i].y);
            if (mouse.x >= r.x && mouse.x < r.x + r.w &&
                mouse.y >= r.y && mouse.y < r.y + r.h) {
                hoverIndex = i; hoverWidget = rec->buttons[i].widgetId; return;
            }
        }
    }

    // ---- build leaf: a REAL Window_Create so the widget build runs (mirrors the default). ----
    int FormLoadMainMenu() override { return gui::Window_Create(0, 0, 300, 320, 0); }

    // ---- per-frame host pump (called first each loop iteration) ----
    int InitStateReader(int /*group*/) override {
        if (plat) {
            if (!plat->pumpMessages()) windowOpen = false;
            plat->getMouse(mouse);
            escDown = plat->keyDown(0x1B);
        }
        recomputeHover();
        return 0;
    }
    bool ClickEdge(int /*frame*/) override { return mouse.left && !prevLeft; }
    int  HoverId(int /*frame*/) override { return hoverWidget; }
    bool EscDown(int /*frame*/) override { return escDown; }

    // ---- render + present (the loop's RunFrameLoop tick) ----
    int RunFrameLoop() override {
        const int W = fbW, H = fbH;
        std::fill(scratch.begin(), scratch.end(), 0u);
        gui::MenuRenderTarget tgt = gui::MenuRenderTarget::Wrap(scratch.data(), W, H, W * 4);
        const bool art = assets && assets->loaded();
        // gilde.exe 0x529d08: the main-menu backdrop is the STATIC image
        // _MENUE_BACKGROUND (with _1024/_1152 resolution variants in gilde.gfx),
        // NOT a 3D scene. Blit it as the base layer; flat-fill only when the asset
        // is absent (headless/asset-less).
        if (art) {
            DrawBackground(scratch.data(), W, H, assets->background().data(),
                           assets->backgroundWidth(), assets->backgroundHeight());
        } else {
            gui::MenuPalette pal;
            gui::MenuFillRect(&tgt.surf, 0, 0, W, H, pal.bgR, pal.bgG, pal.bgB);
        }
        // NOTE: the MENU\MAIN_MENU window (flags=0x10) draws no frame/background gfx
        // (verified from Window_Create @0x419c38); the buttons render directly on the
        // static _MENUE_BACKGROUND, so there is no panel/frame blit here.
        if (rec) {
            for (int i = 0; i < rec->buttonCount; ++i) {
                const int by = rec->buttons[i].y;
                const MenuButtonRect r = ButtonRect(by);
                const bool sel = (i == hoverIndex);
                if (art) {
                    DrawButton3Slice(scratch.data(), W, H, *assets, r.x, r.y, r.w, r.h, sel);
                } else {
                    // Asset-less fallback: a flat scaled plate with a border.
                    gui::MenuFillRect(&tgt.surf, r.x, r.y, r.w, r.h,
                                      sel ? 120 : 90, 30, 30);
                    Outline(scratch.data(), W, H, r.x, r.y, r.w, r.h, 0x00D8C060u);
                }
                // Centre the caption inside the button.  When the real _FONT and a
                // resolved localized label are available, render the actual engine
                // glyphs (CP1251) at the design->framebuffer scale, measuring with
                // the real metrics (VIBE_Property_Get); else fall back to the small
                // built-in ASCII font + the English built-in caption.
                const int row = (i < (int)rec->buttonCount) ? RowForY(by) : -1;
                const std::string* lbl =
                    (labelsByRow && row >= 0 && !labelsByRow[row].empty())
                        ? &labelsByRow[row] : nullptr;
                if (art && lbl && assets->font().loaded()) {
                    const MenuFont& fnt = assets->font();
                    const double sx = (double)fbW / kDesignW;
                    int scale = (int)(sx + 0.5);
                    if (scale < 1) scale = 1;
                    const int textW = fnt.MeasureWidth(lbl->c_str()) * scale;
                    const int textH = (fnt.lineHeight() > 0 ? fnt.lineHeight() : 17) * scale;
                    const int tx = r.x + (r.w - textW) / 2;
                    const int ty = r.y + (r.h - textH) / 2;
                    fnt.DrawText(scratch.data(), W, H, tx, ty, lbl->c_str(),
                                 scale, 255, 255, 214);  // near-white (frida-sampled glyph colour)
                } else {
                    const char* cap = lbl ? lbl->c_str() : CaptionForY(by);
                    const int textW = (int)std::strlen(cap) * 6;
                    DrawLabel(tgt.surf, r.x + (r.w - textW) / 2, r.y + (r.h - 7) / 2,
                              cap, 255, 235, 200);
                }
            }
        }
        // Fade-in from black on menu open (gilde.exe 0x529d08:
        // VIBE_Fade_Register(...,"BLACK", 90, 10) — a 90-tick fade-in quad). We
        // darken the whole frame by (1 - frame/90), so frame 0 = black, 90 = full.
        if (fadeFrame < kFadeFrames) {
            FadeFromBlack(scratch.data(), W, H, (double)fadeFrame / kFadeFrames);
            ++fadeFrame;
        }
        // The game-rendered mouse cursor (OS cursor hidden) — drawn on top, after the
        // fade, so it stays crisp.  Real _MOUSE_CURSOR (gloved hand) when art loaded;
        // else a simple arrow.  Hotspot = the fingertip (2,1).
        //
        // Click reaction (gilde.exe VIBE_DragCursor_RenderMouse @0x41fc58): while the
        // button is held, the sprite is nudged 1px left + 1px down (--v4; ++v6) — the
        // "press" feedback; the hotspot (click point) does not move.
        const int pdx = mouse.left ? -1 : 0;
        const int pdy = mouse.left ? 1 : 0;
        const render::DecodedShape* cur = art ? assets->cursor() : nullptr;
        if (cur)
            ScaledBlit(scratch.data(), W, H, *cur,
                       mouse.x - kCursorHotX + pdx, mouse.y - kCursorHotY + pdy,
                       cur->width, cur->height);
        else
            DrawCursor(scratch.data(), W, H, mouse.x + pdx, mouse.y + pdy, W >= 1000 ? 2 : 1);
        // One-shot framebuffer dump for visual verification (GUILD_MENU_DUMP=path).
        // Captured after the fade completes so the menu is fully lit. Not 1:1 logic;
        // a debug aid only.
        if (const char* mp = std::getenv("GUILD_MENU_DUMP")) {
            static bool dumped = false;
            if (!dumped && fadeFrame >= kFadeFrames) {
                dumped = true;
                std::vector<std::uint8_t> rgb((std::size_t)W * H * 3);
                for (std::size_t i = 0, n = (std::size_t)W * H; i < n; ++i) {
                    const std::uint32_t c = scratch[i];
                    rgb[i*3+0] = (std::uint8_t)((c >> 16) & 0xFF);
                    rgb[i*3+1] = (std::uint8_t)((c >> 8) & 0xFF);
                    rgb[i*3+2] = (std::uint8_t)(c & 0xFF);
                }
                std::vector<std::uint8_t> bmp = render::BmpSave24Bit(W, H, rgb.data());
                if (FILE* f = std::fopen(mp, "wb")) {
                    std::fwrite(bmp.data(), 1, bmp.size(), f); std::fclose(f);
                    std::printf("  --play: menu frame dumped -> %s (%dx%d)\n", mp, W, H);
                }
            }
        }
        BlitToDevice(scratch.data(), W, H, *dev);
        dev->present();
        if (result) ++result->framesPresented;
        prevLeft = mouse.left;
        if (frameCapMs > 0 && plat) plat->sleepMs((std::uint32_t)frameCapMs);
        // Continue until the window closes or a transition armed close (dword_631614).
        if (!windowOpen) { if (result) result->quitByWindow = true; return 0; }
        return (st && st->close) ? 0 : 1;
    }

    // ---- sub-screen runners -> the native sdl_* screens ----
    bool EnterChooseCity() override {
        OsCursorScope cur(plat);
        CityScreenConfig csc; csc.gameDir = gameDir; csc.fbW = fbW; csc.fbH = fbH;
        csc.frameCapMs = frameCapMs; if (cities) csc.cities = *cities;
        // The REAL 3D city pick (Menu/ChooseCity.ed3 + sp_STADTTURM towers +
        // VIBE_Pick_FindNearestObjectAt). Falls back to the 2D list when the .ed3
        // scene assets are absent (asset-less / headless builds).
        CityScreenResult city = RunCityScreen3D(*dev, *plat, csc);
        if (city.backByWindow) { windowOpen = false; return false; }
        if (!city.confirmed) return false;
        // VIBE_Menu_ChooseCharacterIntroVariant @0x52e4e0 — the difficulty pick
        // (_M0_DIFFICULTY: very easy..very hard). The original chains it right after the
        // city confirm; a cancel/back (return 0) aborts the new-game chain back to the
        // menu (gui::Menu_RunChooseCity: `if (ChooseCharacterIntroVariant()) ...`).
        CharIntroConfig cic; cic.gameDir = gameDir; cic.fbW = fbW; cic.fbH = fbH; cic.frameCapMs = frameCapMs;
        CharIntroResult intro = RunCharIntroScreen(*dev, *plat, cic);
        if (intro.quitByWindow) { windowOpen = false; return false; }
        if (!intro.confirmed) return false;        // back / ESC -> chain aborts
        // VIBE_Menu_RunChooseHistory @0x52d684 — the historical-perspective pick
        // (_M0_HISTORIE: factual / personal / none -> History flag 1/2/0). Cancel aborts.
        ChooseHistoryConfig chc; chc.gameDir = gameDir; chc.fbW = fbW; chc.fbH = fbH; chc.frameCapMs = frameCapMs;
        ChooseHistoryScreenResult hist = RunChooseHistoryScreen(*dev, *plat, chc);
        if (hist.quitByWindow) { windowOpen = false; return false; }
        if (!hist.confirmed) return false;         // back / ESC -> chain aborts
        // After the FACTUAL perspective (flag 1) the original asks for the task/mission
        // difficulty ("Ваши задания" / _M0_AUFTRAEGE). The other perspectives skip it.
        int taskMode = -1;
        if (hist.historyFlag == 1) {
            ChooseHistoryConfig tcfg; tcfg.gameDir = gameDir; tcfg.fbW = fbW; tcfg.fbH = fbH; tcfg.frameCapMs = frameCapMs;
            ChooseTasksScreenResult tasks = RunChooseTasksScreen(*dev, *plat, tcfg);
            if (tasks.quitByWindow) { windowOpen = false; return false; }
            if (!tasks.confirmed) return false;    // back / ESC -> chain aborts
            taskMode = tasks.taskMode;
        }
        // VIBE_Menu_RunChoosePlayer @0x52ccd8 — the identity wizard (Vorname / Nachname /
        // Geschlecht / Glauben / Wappen). The first character-spine screen.
        ChoosePlayerConfig pcfg; pcfg.gameDir = gameDir; pcfg.fbW = fbW; pcfg.fbH = fbH; pcfg.frameCapMs = frameCapMs;
        ChoosePlayerScreenResult player = RunChoosePlayerScreen(*dev, *plat, pcfg);
        if (player.quitByWindow) { windowOpen = false; return false; }
        if (!player.confirmed) return false;       // back out of page 0 -> chain aborts
        CharCreateConfig ccc; ccc.gameDir = gameDir; ccc.fbW = fbW; ccc.fbH = fbH; ccc.frameCapMs = frameCapMs;
        // Carry the WHOLE upstream parameter block into the char-create step so
        // the produced NewGameParams is the original's full global block
        // (0x122F4A0..): city (RunChooseCity), difficulty (byte_12335BA),
        // perspective (History flag), and the wizard identity.
        // City: the original records the picked marker ("stadt_<file>") and the
        // matched city file base name (ReturnedString @0x122EE50, fed to the
        // "%s/%s.cty" load at 0x533d4b). cityPath -> base name without dir/ext.
        gui::NewGame_ApplyCity(ccc.in,
                               std::string(gui::kCityNamePrefix) + city.cityName,
                               CityFileBaseName(city.cityPath), /*network=*/false);
        ccc.in.difficulty = intro.variant;         // byte_12335BA (0..4)
        ccc.in.historyFlag = hist.historyFlag;     // carry the chosen perspective forward
        ccc.in.taskMode = taskMode;                // _M0_AUFTRAEGE pick (factual only; else -1)
        ccc.in.firstName = player.firstName;       // identity from the player wizard
        ccc.in.familyName = player.familyName;
        ccc.in.gender = player.gender; ccc.in.faith = player.faith; ccc.in.wappen = player.wappenIndex;
        ccc.defaultName = player.firstName; ccc.defaultFamily = player.familyName;
        ccc.defaultGender = player.gender; ccc.defaultFaith = player.faith;
        CharCreateResult cc = RunCharCreateScreen(*dev, *plat, ccc);
        if (cc.quitByWindow) { windowOpen = false; return false; }
        if (!cc.confirmed) return false;
        if (result) {
            result->action = NativeMenuResult::kPlayCity;
            result->cityPath = city.cityPath;
            // The full committed block (city + difficulty + history + identity +
            // profession, params.started armed by NewGame_Commit inside the
            // char-create step) — everything ApplyNewGameParams needs.
            result->params = cc.params;
        }
        return true;   // arms word_63C740|=1 + dword_631614=1 -> menu loop exits
    }
    bool RunLoadGame() override {
        // VIBE_Menu_RunLoadGame @0x56a270 — the native load-game screen drives the
        // 1:1 gui::Menu_RunLoadGame underneath (slot table from the save dir via
        // the reconstructed browser leaves). A confirmed pick returns 1 -> the
        // menu dispatch arms dword_631614 = 1 and the menu loop exits; a cancel
        // (ESC / back / no saves) returns 0 -> back to the menu (LABEL_55).
        OsCursorScope cur(plat);
        LoadGameScreenConfig lc;
        lc.gameDir = gameDir; lc.fs = fs;
        lc.fbW = fbW; lc.fbH = fbH; lc.frameCapMs = frameCapMs;
        LoadGameScreenResult lg = RunLoadGameScreen(*dev, *plat, lc);
        if (lg.quitByWindow) { windowOpen = false; return false; }
        if (!lg.confirmed) return false;
        if (result) {
            result->action       = NativeMenuResult::kLoadGame;
            result->savePath     = lg.savePath;       // real path for the session
            result->saveLoadPath = lg.loadPath;       // byte_122F530 (1:1 string)
            result->saveName     = lg.saveName;
        }
        // word_63C740 is ONE global in the original: RunLoadGame assigned 10 to it
        // (0x56a426) before returning — mirror it into the menu's model of that word.
        if (st && lg.sessionFlags) st->sessionFlags = lg.sessionFlags;
        return true;   // -> dword_631614 = 1 in the menu dispatch (0x52a841)
    }
    // VIBE_Menu_ChooseNetworkMode @0x529a64 — the "Сетевая игра" hub. Runs the
    // native network screen (hub + search/IP sub-views). The hub itself is 1:1 and
    // navigable; the actual multiplayer transport is rule-6 (networking) and not
    // wired — the screen returns to the menu without launching a session. Returns
    // false so the menu does NOT arm dword_631614 (no session started), matching the
    // original's "else word_63C740 = 0" when ChooseNetworkMode yields nothing.
    bool ChooseNetworkMode() override {
        OsCursorScope cur(plat);
        NetworkScreenConfig nc; nc.gameDir = gameDir; nc.fbW = fbW; nc.fbH = fbH;
        nc.frameCapMs = frameCapMs;
        NetworkScreenResult nr = RunNetworkScreen(*dev, *plat, nc);
        if (nr.quitByWindow) { windowOpen = false; return false; }
        return false;   // no real session launched (rule 6) -> back to the menu
    }
    // No OsCursorScope here: RunOptionsScreen draws the game leather-hand cursor
    // itself (_MOUSE_CURSOR), so the OS arrow must remain hidden (as in the menu).
    void RunOptionsGfx() override { OptionsConfig o; o.page = OptionsPage::kGfx;  o.gameDir = gameDir; o.fbW = fbW; o.fbH = fbH; o.frameCapMs = frameCapMs; RunOptionsScreen(*dev, *plat, o); }
    void RunOptionsSfx() override { OptionsConfig o; o.page = OptionsPage::kSfx;  o.gameDir = gameDir; o.fbW = fbW; o.fbH = fbH; o.frameCapMs = frameCapMs; RunOptionsScreen(*dev, *plat, o); }
    void RunOptionsGame() override{ OptionsConfig o; o.page = OptionsPage::kGame; o.gameDir = gameDir; o.fbW = fbW; o.fbH = fbH; o.frameCapMs = frameCapMs; RunOptionsScreen(*dev, *plat, o); }
    void RunCreditsScroll() override { OsCursorScope cur(plat); CreditsScreenConfig c; c.gameDir = gameDir; c.fbW = fbW; c.fbH = fbH; c.frameCapMs = frameCapMs; RunCreditsScreen(*dev, *plat, c); }
    void RunCreditsWindow() override { OsCursorScope cur(plat); CreditsScreenConfig c; c.gameDir = gameDir; c.fbW = fbW; c.fbH = fbH; c.frameCapMs = frameCapMs; RunCreditsScreen(*dev, *plat, c); }
};

} // namespace

MenuButtonRect MenuButtonScreenRect(int designY, int fbW, int fbH, int designW) {
    // gilde.exe draws the GUI at NATIVE 800x600 pixel size and only CENTER-TRANSLATES
    // it for the screen resolution — it does NOT scale widget sizes.
    // VIBE_Window_PositionAtCoord @0x41d7e0 (mode 3): v3 = x - A/s + A with
    // A = dword_69FFA4 = screenW/2 and s = flt_62D224 = screenW/800
    // (VIBE_Gui_LoadGfxFile @0x41b888: flt_62D224 = screenW * (1/800)=0x3aa3d70a),
    // so v3 = x - 400 + screenW/2 = x + (screenW-800)/2. Likewise y + (screenH-600)/2
    // (dword_69FFA0 = screenH/2). Widget sizes stay native (CreateSprite stores
    // native px; _BUTTON_RED record height +82 = 33).
    const int ox = (fbW - kDesignW) / 2;   // horizontal centering offset
    const int oy = (fbH - kDesignH) / 2;   // vertical centering offset
    // The sprite is AddSpriteToWindow(kMainMenuButtonX=32, designY, _BUTTON_RED)
    // inside the MENU\MAIN_MENU window at (232,160): screen x = 232+32 = 264.
    // VIBE_Window_NormalizeSpriteWidths @0x416658 sets the button WIDTH (widget
    // +20 = max(widestLabel+8, 128)) but does NOT move its x (+14) — so the
    // rect x is the AddSprite position, and the width is the caller's normalized
    // designW. (Native px; only center-translated for the screen resolution.)
    const int dx = kWinX + gui::kMainMenuButtonX + ox;   // 232 + 32 = 264
    const int dy = kWinY + designY + oy;                  // 160 + designY
    return { dx, dy, designW, kBtnDesignH };
}

NativeMenuResult RunNativeMainMenu(shim::IGraphicsDevice& device, shim::IPlatform& plat,
                                   shim::IFileSystem& fs, const std::string& gameDir,
                                   const std::vector<std::pair<std::string, std::string>>& cities,
                                   int fbW, int fbH, int frameCapMs, int maxFrames,
                                   shim::IAudioDevice* audio) {
    NativeMenuResult result;

    // ---- menu music: gilde.exe 0x529d08 (0x52a259) picks one of three CD tracks at
    // random: v22 = (int)VIBE_Util_RandNext() % 3; v22==0 -> cd1\Rittersleut.mp3,
    // v22==1 -> cd1\MauerUndTor.mp3, v22==2 -> cd2\KraeuterUndPhiolen.mp3; then plays
    // it looped (VIBE_Audio_LoadTrack(...,3)), stopping it on exit.  We decode the
    // shipped .mp3 to PCM and drive one looping device voice directly (the device
    // loops on a non-zero loop count).  The RNG draw is the *same* crt::RandNext the
    // 1:1 gui::Menu_RunMainMenu consumes, so the track choice is bit-faithful.
    std::vector<std::int16_t> musicPcm;
    shim::VoiceHandle musicVoice = -1;
    if (audio && !gameDir.empty()) {
        static const char* const kMenuTracks[3] = {
            "msx/CD1/Rittersleut.mp3",        // cd1\Rittersleut.mp3
            "msx/CD1/MauerUndTor.mp3",        // cd1\MauerUndTor.mp3
            "msx/Cd2/KraeuterUndPhiolen.mp3", // cd2\KraeuterUndPhiolen.mp3
        };
        // gilde.exe 0x52a259: v22 = (int)VIBE_Util_RandNext() % 3 (signed modulo;
        // RandNext() is non-negative [0,0x7FFF] so pick is in {0,1,2}).
        const int pick = crt::RandNext() % 3;
        int rate = 44100;
        if (DecodeMp3File(gameDir + "/" + kMenuTracks[pick], musicPcm, rate) &&
            !musicPcm.empty()) {
            musicVoice = audio->allocVoice();
            if (musicVoice >= 0) {
                audio->playSample(musicVoice, musicPcm.data(),
                                  musicPcm.size() * sizeof(std::int16_t), rate,
                                  /*loops=*/-1);   // loop forever
                audio->setVolume(musicVoice, 110);
            }
        }
    }

    // The original hides the OS cursor and draws its own; do the same for the menu.
    plat.showSystemCursor(false);

    MenuAssets assets;
    // Pick the background whose native size matches the window (the original ships
    // 800x600 / 1024x768 / 1152x864 menu backdrops).
    const char* bgName = fbW >= 1152 ? "_MENUE_BACKGROUND_1152"
                       : fbW >= 1024 ? "_MENUE_BACKGROUND_1024"
                                     : "_MENUE_BACKGROUND";
    const bool haveArt = !gameDir.empty() && assets.Load(fs, "gfx/gilde.gfx", bgName);
    if (haveArt) assets.InstallHooks();   // wires gui::MenuRenderHooks.drawSprite (gfx 174)

    // Resolve the eight localized button labels from the install's textbin
    // (_OPTIONEN_MENUE_*), then compute each button's design width from the real
    // _FONT metrics: width = VIBE_Property_Get(label) + cap(12) + cap(12) + 4
    // (VIBE_Object_RecomputeSize @0x41b164, sprite kind 9). Empty label / no font
    // leaves the nominal 124-wide button + built-in caption.
    // The original draws ALL menu buttons at ONE uniform width — equalized to the
    // WIDEST label (measured against the shipped menu: every _BUTTON_RED bar is the
    // same width, the label centred). Per-button width is VIBE_Object_RecomputeSize
    // @0x41b164 = Property_Get(label) + cap(12) + cap(12) + 4; the buttons are then
    // equalized to the maximum across the group, so e.g. "Réseau" is as wide as
    // "Options graphiques". (Confirmed against the original 1024x768 menu: 9 uniform
    // bars, width = widest label + 28.)
    std::string menuLabels[8];
    int menuWidths[8];
    for (int i = 0; i < 8; ++i) menuWidths[i] = 124;  // _BUTTON_RED nominal fallback
    ResolveMainMenuLabels(gameDir, menuLabels);
    if (haveArt && assets.font().loaded()) {
        int uniformW = 0;
        for (int i = 0; i < 8; ++i)
            if (!menuLabels[i].empty()) {
                const int w = assets.font().ButtonWidth(menuLabels[i].c_str());
                if (w > uniformW) uniformW = w;
            }
        // VIBE_Window_NormalizeSpriteWidths @0x416658: the common width is the
        // widest sprite + 8, clamped to a 128px minimum (NOT just the widest).
        if (uniformW > 0) {
            uniformW += 8;
            if (uniformW < 128) uniformW = 128;
            for (int i = 0; i < 8; ++i) menuWidths[i] = uniformW;
        }
    }

    gui::MainMenuRunState st;
    gui::MainMenuRunRecord rec;

    NativeMenuHooks h;
    h.dev = &device; h.plat = &plat; h.fs = &fs; h.gameDir = gameDir; h.cities = &cities;
    h.fbW = fbW; h.fbH = fbH; h.frameCapMs = frameCapMs; h.assets = &assets;
    h.st = &st; h.rec = &rec; h.result = &result;
    h.labelsByRow = menuLabels; h.widthsByRow = menuWidths;
    h.scratch.assign((std::size_t)fbW * fbH, 0u);

    gui::MainMenuRunHooks* prev = gui::Menu_SetRunHooks(&h);
    gui::Menu_RunMainMenu(st, &rec, maxFrames);
    gui::Menu_SetRunHooks(prev);

    if (haveArt) MenuAssets::ClearHooks();

    // Stop the menu track and restore the OS cursor (the original stops the track
    // and fades on menu exit; VIBE_Audio_StopTrack @0x43a2fc).
    if (audio && musicVoice >= 0) {
        audio->stop(musicVoice);
        audio->freeVoice(musicVoice);
    }
    plat.showSystemCursor(true);
    return result;
}

} // namespace guild::play
