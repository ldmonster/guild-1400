// guild::play — native character-creation screen(s). See sdl_charcreate_screen.h.
//
// Two real sprite grids: PROFESSION (0x52c50c, gfx beruf[i]+1349) and WAPPEN
// (0x52ccd8, gfx 1342+i). Renders into a 32bpp scratch, blits to the device
// backbuffer (any bpp), presents, hit-tests the grid with the REAL geometry
// helpers (gui::Profession_ButtonX/Y), and produces a filled gui::NewGameParams.
#include "play/sdl_charcreate_screen.h"

#include "shim/IGraphicsDevice.h"
#include "shim/IPlatform.h"

#include "gui/newgame_setup.h"   // Profession_ButtonX/Y/Gfx, Wappen_ButtonGfx, Apply*
#include "gui/menu_render.h"     // MenuRenderTarget, MenuFillRect, MenuPalette
#include "play/menu_assets.h"    // MenuAssets, BlitDecodedSprite
#include "render/text_raster.h"
#include "render/font.h"
#include "render/types.h"
#include "render/gfx_archive.h"
#include "shim/IFileSystem.h"
#include "shim_impl/disk_filesystem.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace guild::play {
namespace {

constexpr int kVkEscape = 0x1B;

// The REAL profession byte table (dword_527604 @0x527604): {1,2,3,4,5,6,7,11}.
// gfx id = beruf + 1349 -> {1350,1351,1352,1353,1354,1355,1356,1360}.
constexpr int kBerufTable[gui::kProfessionCount] = {1, 2, 3, 4, 5, 6, 7, 11};

// Grid-cell visual size for the hit box / fallback rect (the sprite is centred in
// it). The real cells step 96px in x and 80px in y; leave a small gutter.
constexpr int kCellW = 84;
constexpr int kCellH = 68;

// The wappen grid laid out like the profession grid (4 columns x 2 rows) so both
// phases share one geometry; gfx = 1342 + i.
constexpr int kWappenCols = 4;
constexpr int kWappenCellW = 96;
constexpr int kWappenCellH = 80;
constexpr int kWappenOriginX = 100;
constexpr int kWappenOriginY = 130;

int WappenCellX(int i) { return kWappenCellW * (i % kWappenCols) + kWappenOriginX; }
int WappenCellY(int i) { return kWappenCellH * (i / kWappenCols) + kWappenOriginY; }

// Confirm button hit box (bottom of the screen).
constexpr int kConfirmX = 520, kConfirmY = 520, kConfirmW = 220, kConfirmH = 44;

// ---- helpers copied from sdl_menu.cpp (some duplication is fine) -------------

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

// Hit-test a grid: cells at (cellX(i),cellY(i)) of size kCellW x kCellH.
int HitGrid(int mx, int my, int count, int (*cx)(int), int (*cy)(int)) {
    for (int i = 0; i < count; ++i) {
        const int x = cx(i), y = cy(i);
        if (mx >= x && mx < x + kCellW && my >= y && my < y + kCellH) return i;
    }
    return -1;
}

bool HitConfirm(int mx, int my) {
    return mx >= kConfirmX && mx < kConfirmX + kConfirmW &&
           my >= kConfirmY && my < kConfirmY + kConfirmH;
}

// Draw one grid cell: real sprite (centred) when assets present, else a labelled
// rect.  Returns true iff a real decoded sprite was drawn.
bool DrawCell(render::Surface& surf, std::uint32_t* px, int W, int H,
              int cellX, int cellY, int gfxId, const char* fallbackLabel,
              bool selected, MenuAssets* assets) {
    gui::MenuPalette pal;
    // Cell backdrop (so an unselected/empty cell is visible over the background).
    gui::MenuFillRect(&surf, cellX, cellY, kCellW, kCellH,
                      selected ? 90 : pal.btnR, selected ? 70 : pal.btnG,
                      selected ? 40 : pal.btnB);
    bool real = false;
    if (assets && assets->loaded()) {
        const render::DecodedShape* spr = assets->SpriteForGfxId(gfxId);
        if (spr && spr->width > 0 && spr->height > 0) {
            int dx = cellX + (kCellW - spr->width) / 2;
            int dy = cellY + (kCellH - spr->height) / 2;
            BlitDecodedSprite(&surf, dx, dy, *spr);
            real = true;
        }
    }
    if (!real) {
        DrawLabel(surf, cellX + 6, cellY + kCellH / 2 - 3, fallbackLabel,
                  pal.textR, pal.textG, pal.textB);
    }
    if (selected)
        Outline(px, W, H, cellX - 2, cellY - 2, kCellW + 4, kCellH + 4, 0x0000FF00u);
    return real;
}

const char* ProfLabel(int i) {
    static const char* names[gui::kProfessionCount] = {
        "PATRIZIER", "HANDWERKER", "GASTWIRT", "DIEB",
        "KAEMPFER",  "GAUKLER",    "PRIESTER", "MEDICUS"};
    return (i >= 0 && i < gui::kProfessionCount) ? names[i] : "?";
}

} // namespace

CharCreateResult RunCharCreateScreen(shim::IGraphicsDevice& device,
                                     shim::IPlatform& plat,
                                     const CharCreateConfig& cfg) {
    CharCreateResult res;
    res.params = cfg.in;  // carry city/history/network forward.

    const int W = cfg.fbW, H = cfg.fbH;
    std::vector<std::uint32_t> scratch((std::size_t)W * H, 0u);

    // Real artwork (optional).
    shim::DiskFileSystem assetFs(cfg.gameDir);
    MenuAssets assets;
    const bool haveAssets = !cfg.gameDir.empty() && assets.Load(assetFs);

    int phase = 0;          // 0 = profession, 1 = wappen
    int selProf = -1;       // selected profession index (0..7), -1 = none
    int selWappen = 0;      // selected wappen index (default coat 0)
    int frame = 0;
    bool prevLeft = false;
    bool drewSprite = false;

    for (;;) {
        if (cfg.maxFrames >= 0 && frame >= cfg.maxFrames) break;

        // ---- render ----
        std::fill(scratch.begin(), scratch.end(), 0u);
        gui::MenuRenderTarget tgt = gui::MenuRenderTarget::Wrap(scratch.data(), W, H, W * 4);
        gui::MenuPalette pal;

        if (haveAssets) {
            DrawBackground(scratch.data(), W, H, assets.background().data(),
                           assets.backgroundWidth(), assets.backgroundHeight());
        } else {
            gui::MenuFillRect(&tgt.surf, 0, 0, W, H, pal.bgR, pal.bgG, pal.bgB);
        }

        shim::MouseState ms{};
        plat.getMouse(ms);

        if (phase == 0) {
            DrawLabel(tgt.surf, gui::kProfessionOriginX, 60, "CHOOSE PROFESSION",
                      255, 255, 255);
            const int hov = HitGrid(ms.x, ms.y, gui::kProfessionCount,
                                    gui::Profession_ButtonX, gui::Profession_ButtonY);
            res.hoveredProfession = hov;
            for (int i = 0; i < gui::kProfessionCount; ++i) {
                const int gfx = gui::Profession_ButtonGfx(kBerufTable[i]);
                bool r = DrawCell(tgt.surf, scratch.data(), W, H,
                                  gui::Profession_ButtonX(i), gui::Profession_ButtonY(i),
                                  gfx, ProfLabel(i), i == selProf,
                                  haveAssets ? &assets : nullptr);
                drewSprite = drewSprite || r;
                if (i == hov && i != selProf)
                    Outline(scratch.data(), W, H, gui::Profession_ButtonX(i) - 2,
                            gui::Profession_ButtonY(i) - 2, kCellW + 4, kCellH + 4,
                            0x00FFFF00u);
            }
            DrawLabel(tgt.surf, 60, 560, "ESC: back", 180, 180, 180);
        } else {
            res.phaseReached = 1;
            DrawLabel(tgt.surf, 100, 60, "CHOOSE COAT OF ARMS", 255, 255, 255);
            const int hov = HitGrid(ms.x, ms.y, gui::kWappenCount,
                                    WappenCellX, WappenCellY);
            res.hoveredWappen = hov;
            for (int i = 0; i < gui::kWappenCount; ++i) {
                const int gfx = gui::Wappen_ButtonGfx(i);
                char lbl[16]; std::snprintf(lbl, sizeof lbl, "WAPPEN %d", i);
                bool r = DrawCell(tgt.surf, scratch.data(), W, H,
                                  WappenCellX(i), WappenCellY(i), gfx, lbl,
                                  i == selWappen, haveAssets ? &assets : nullptr);
                drewSprite = drewSprite || r;
                if (i == hov && i != selWappen)
                    Outline(scratch.data(), W, H, WappenCellX(i) - 2,
                            WappenCellY(i) - 2, kCellW + 4, kCellH + 4, 0x00FFFF00u);
            }
            // Confirm button.
            const bool overConfirm = HitConfirm(ms.x, ms.y);
            gui::MenuFillRect(&tgt.surf, kConfirmX, kConfirmY, kConfirmW, kConfirmH,
                              overConfirm ? 60 : 30, overConfirm ? 110 : 70,
                              overConfirm ? 60 : 30);
            DrawLabel(tgt.surf, kConfirmX + 40, kConfirmY + 16, "CONFIRM",
                      255, 235, 200);
            if (overConfirm)
                Outline(scratch.data(), W, H, kConfirmX - 2, kConfirmY - 2,
                        kConfirmW + 4, kConfirmH + 4, 0x00FFFF00u);
            DrawLabel(tgt.surf, 60, 560, "ESC: back", 180, 180, 180);
        }

        BlitToDevice(scratch.data(), W, H, device);
        device.present();
        ++res.framesPresented;

        // ---- input ----
        if (!plat.pumpMessages()) { res.quitByWindow = true; res.back = true; break; }
        plat.getMouse(ms);
        const bool leftEdge = ms.left && !prevLeft;
        prevLeft = ms.left;

        if (phase == 0) {
            if (plat.keyDown(kVkEscape)) { res.back = true; break; }
            if (leftEdge) {
                const int hit = HitGrid(ms.x, ms.y, gui::kProfessionCount,
                                        gui::Profession_ButtonX, gui::Profession_ButtonY);
                if (hit >= 0) { selProf = hit; phase = 1; }
            }
        } else { // wappen phase
            if (plat.keyDown(kVkEscape)) { phase = 0; ++frame; continue; }
            if (leftEdge) {
                const int hit = HitGrid(ms.x, ms.y, gui::kWappenCount,
                                        WappenCellX, WappenCellY);
                if (hit >= 0) {
                    selWappen = hit;
                } else if (HitConfirm(ms.x, ms.y) && selProf >= 0) {
                    res.confirmed = true;
                    break;
                }
            }
        }

        if (cfg.frameCapMs > 0) plat.sleepMs((std::uint32_t)cfg.frameCapMs);
        ++frame;
    }

    res.usedRealSprites = drewSprite;

    // ---- produce the filled params via the REAL model ----
    if (res.confirmed && selProf >= 0) {
        // Player identity block (RunChoosePlayer commit) — name/family/gender/faith
        // defaulted, wappen from the grid.
        gui::NewGame_ApplyPlayer(res.params, cfg.defaultName, cfg.defaultFamily,
                                 selWappen, cfg.defaultGender, cfg.defaultFaith);
        // Profession pick (ChooseProfession commit): real beruf byte -> variant.
        gui::NewGame_ApplyProfession(res.params, kBerufTable[selProf]);
        // Arm the session-start command (dword_122F528 = 1555, dword_631614 = 1).
        gui::NewGame_Commit(res.params);
    } else if (selProf >= 0) {
        // Backed out after picking: still reflect the partial selection so the
        // caller / tests can read what was chosen (no Commit, started stays false).
        res.params.wappen = selWappen;
        res.params.profession = kBerufTable[selProf];
    }
    return res;
}

} // namespace guild::play
