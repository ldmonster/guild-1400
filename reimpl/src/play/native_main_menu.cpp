// guild::play — native bridge for the byte-faithful main menu. See header.
#include "play/native_main_menu.h"

#include "shim/IGraphicsDevice.h"
#include "shim/IPlatform.h"
#include "shim/IFileSystem.h"

#include "gui/main_menu_run.h"     // Menu_RunMainMenu, MainMenuRunHooks/State/Record, Menu_SetRunHooks
#include "gui/main_menu.h"         // kMainMenuButtons, MainMenu_ButtonY (captions by y)
#include "gui/window.h"            // Window_Create (the real form-backing build window)
#include "gui/menu_render.h"       // MenuRenderTarget, MenuPalette
#include "play/menu_assets.h"      // MenuAssets — real gilde.gfx background + gfx-174 buttons
#include "render/text_raster.h"
#include "render/font.h"
#include "render/types.h"

#include "play/sdl_city_screen.h"
#include "play/sdl_charcreate_screen.h"
#include "play/sdl_options_screen.h"
#include "play/sdl_credits_screen.h"

#include <cstdint>
#include <cstring>
#include <vector>

namespace guild::play {
namespace {

constexpr int kBtnHitW = 280;
constexpr int kBtnHitH = 40;

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

    void recomputeHover() {
        hoverWidget = -1; hoverIndex = -1;
        if (!rec) return;
        if (mouse.x < gui::kMainMenuButtonX || mouse.x >= gui::kMainMenuButtonX + kBtnHitW) return;
        for (int i = 0; i < rec->buttonCount; ++i) {
            const int by = rec->buttons[i].y;
            if (mouse.y >= by && mouse.y < by + kBtnHitH) { hoverIndex = i; hoverWidget = rec->buttons[i].widgetId; return; }
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
        if (art)
            DrawBackground(scratch.data(), W, H, assets->background().data(),
                           assets->backgroundWidth(), assets->backgroundHeight());
        else {
            gui::MenuPalette pal;
            gui::MenuFillRect(&tgt.surf, 0, 0, W, H, pal.bgR, pal.bgG, pal.bgB);
        }
        if (rec) {
            for (int i = 0; i < rec->buttonCount; ++i) {
                const int by = rec->buttons[i].y;
                if (art) gui::GetMenuRenderHooks().drawSprite(&tgt.surf, gui::kMainMenuButtonX, by,
                                                             gui::kMainMenuButtonSprite,
                                                             gui::GetMenuRenderHooks().userData);
                DrawLabel(tgt.surf, gui::kMainMenuButtonX + 8, by + 12, CaptionForY(by), 255, 235, 200);
                if (i == hoverIndex)
                    Outline(scratch.data(), W, H, gui::kMainMenuButtonX - 3, by - 3,
                            kBtnHitW + 6, kBtnHitH + 6, 0x00FFFF00u);
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
        CityScreenConfig csc; csc.gameDir = gameDir; csc.fbW = fbW; csc.fbH = fbH;
        csc.frameCapMs = frameCapMs; if (cities) csc.cities = *cities;
        CityScreenResult city = RunCityScreen(*dev, *plat, csc);
        if (city.backByWindow) { windowOpen = false; return false; }
        if (!city.confirmed) return false;
        CharCreateConfig ccc; ccc.gameDir = gameDir; ccc.fbW = fbW; ccc.fbH = fbH; ccc.frameCapMs = frameCapMs;
        CharCreateResult cc = RunCharCreateScreen(*dev, *plat, ccc);
        if (cc.quitByWindow) { windowOpen = false; return false; }
        if (!cc.confirmed) return false;
        if (result) { result->action = NativeMenuResult::kPlayCity; result->cityPath = city.cityPath; }
        return true;   // arms word_63C740|=1 + dword_631614=1 -> menu loop exits
    }
    bool RunLoadGame() override {
        // No shipped saves -> stand in with the first city.
        if (result && cities && !cities->empty()) {
            result->action = NativeMenuResult::kPlayCity; result->cityPath = cities->front().second;
            return true;
        }
        return false;
    }
    void RunOptionsGfx() override { OptionsConfig o; o.page = OptionsPage::kGfx;  o.gameDir = gameDir; o.fbW = fbW; o.fbH = fbH; o.frameCapMs = frameCapMs; RunOptionsScreen(*dev, *plat, o); }
    void RunOptionsSfx() override { OptionsConfig o; o.page = OptionsPage::kSfx;  o.gameDir = gameDir; o.fbW = fbW; o.fbH = fbH; o.frameCapMs = frameCapMs; RunOptionsScreen(*dev, *plat, o); }
    void RunOptionsGame() override{ OptionsConfig o; o.page = OptionsPage::kGame; o.gameDir = gameDir; o.fbW = fbW; o.fbH = fbH; o.frameCapMs = frameCapMs; RunOptionsScreen(*dev, *plat, o); }
    void RunCreditsScroll() override { CreditsScreenConfig c; c.gameDir = gameDir; c.fbW = fbW; c.fbH = fbH; c.frameCapMs = frameCapMs; RunCreditsScreen(*dev, *plat, c); }
    void RunCreditsWindow() override { CreditsScreenConfig c; c.gameDir = gameDir; c.fbW = fbW; c.fbH = fbH; c.frameCapMs = frameCapMs; RunCreditsScreen(*dev, *plat, c); }
};

} // namespace

NativeMenuResult RunNativeMainMenu(shim::IGraphicsDevice& device, shim::IPlatform& plat,
                                   shim::IFileSystem& fs, const std::string& gameDir,
                                   const std::vector<std::pair<std::string, std::string>>& cities,
                                   int fbW, int fbH, int frameCapMs, int maxFrames) {
    NativeMenuResult result;

    MenuAssets assets;
    // Pick the background whose native size matches the window (the original ships
    // 800x600 / 1024x768 / 1152x864 menu backdrops).
    const char* bgName = fbW >= 1152 ? "_MENUE_BACKGROUND_1152"
                       : fbW >= 1024 ? "_MENUE_BACKGROUND_1024"
                                     : "_MENUE_BACKGROUND";
    const bool haveArt = !gameDir.empty() && assets.Load(fs, "gfx/gilde.gfx", bgName);
    if (haveArt) assets.InstallHooks();   // wires gui::MenuRenderHooks.drawSprite (gfx 174)

    gui::MainMenuRunState st;
    gui::MainMenuRunRecord rec;

    NativeMenuHooks h;
    h.dev = &device; h.plat = &plat; h.fs = &fs; h.gameDir = gameDir; h.cities = &cities;
    h.fbW = fbW; h.fbH = fbH; h.frameCapMs = frameCapMs; h.assets = &assets;
    h.st = &st; h.rec = &rec; h.result = &result;
    h.scratch.assign((std::size_t)fbW * fbH, 0u);

    gui::MainMenuRunHooks* prev = gui::Menu_SetRunHooks(&h);
    gui::Menu_RunMainMenu(st, &rec, maxFrames);
    gui::Menu_SetRunHooks(prev);

    if (haveArt) MenuAssets::ClearHooks();
    return result;
}

} // namespace guild::play
