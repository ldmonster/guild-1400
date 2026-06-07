// guild::play — native Game/Gfx/Sfx OPTIONS screens. See sdl_options_screen.h.
//
// Faithful reconstruction of VIBE_Menu_RunOptionsGame/Gfx/Sfx (0x56cc44 /
// 0x56c21c / 0x56c808). The originals are forms.BIN-driven panels; this is the
// backend-agnostic native equivalent: a vertical column of control rows, one per
// real gilde.INI setting that page exposes, rendered over the real menu
// background when present. Each row's value, range and step mirror the 3rd arg
// (max) the original passes to VIBE_Object_SetValueOrText and the cycle option
// lists; on apply the values are written straight into the config structs (the
// reconstructed equivalent of VIBE_Config_WriteGfxSettings @0x56af54 writing the
// [Gfx]/[Sound]/[Game] keys).
#include "play/sdl_options_screen.h"

#include "shim/IGraphicsDevice.h"
#include "shim/IPlatform.h"

#include "gui/menu_render.h"   // MenuRenderTarget, MenuFillRect, MenuPalette
#include "play/menu_assets.h"  // MenuAssets — real gilde.gfx background
#include "render/text_raster.h"
#include "render/font.h"
#include "render/types.h"
#include "shim_impl/disk_filesystem.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace guild::play {
namespace {

constexpr int kVkEscape = 0x1B;

// Panel geometry.
constexpr int kPanelX   = 60;
constexpr int kTitleY   = 40;
constexpr int kRowY0    = 90;
constexpr int kRowH     = 30;
constexpr int kLabelX   = 76;
constexpr int kValueX   = 360;   // the control hit column (click to actuate)
constexpr int kCtrlW    = 200;   // width of the control hit area
constexpr int kPanelW   = 560;

// ---- shared draw helpers (copied from sdl_menu.cpp; some duplication is fine) ----
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

// ---- page titles (the rich-string ids the originals select) ----
const char* PageTitle(OptionsPage p) {
    switch (p) {
        case OptionsPage::kGame: return "GAME OPTIONS";  // VIBE_Text_RenderRichString(0x1866)
        case OptionsPage::kGfx:  return "GFX OPTIONS";   // ... 0x1867
        case OptionsPage::kSfx:  return "SFX OPTIONS";   // ... 0x1868
    }
    return "OPTIONS";
}

} // namespace

// ---------------------------------------------------------------------------
// Row table — exactly which real gilde.INI keys each page exposes, with the same
// value ranges/cycles as the original forms (the 3rd "max" arg to
// VIBE_Object_SetValueOrText, and the cycle option lists). All keys here are
// REAL: VIBE_Config_WriteGfxSettings persists every one of them.
// ---------------------------------------------------------------------------
std::vector<OptionRow> OptionsRowsFor(OptionsPage page,
                                      const config::GfxSettings& g,
                                      const config::SoundSettings& s,
                                      const config::GameSettings& m) {
    std::vector<OptionRow> r;
    auto add = [&](const char* label, const char* key, OptionKind k,
                   int value, int minV, int maxV, int step) {
        OptionRow row; row.label = label; row.key = key; row.kind = k;
        row.value = value; row.minV = minV; row.maxV = maxV; row.step = step;
        r.push_back(row);
    };
    switch (page) {
        case OptionsPage::kGfx:
            // VIBE_Menu_RunOptionsGfx widget order -> byte block @0x1233514..
            add("Detail Level",    "details",          OptionKind::kCycle, g.details,         0, 2, 1);
            add("Texture Scale",   "texture_scale",    OptionKind::kCycle, g.textureScale,    0, 2, 1);
            add("LOD Handling",    "lod_handling",     OptionKind::kCycle, g.lodHandling,     0, 2, 1);
            add("Shadow Detail",   "shadow_detail",    OptionKind::kCycle, g.shadowDetail,    0, 2, 1);
            add("Floor Mipmaps",   "floor_mipmapping", OptionKind::kToggle,g.floorMipmapping, 0, 1, 1);
            add("Camera Limits",   "camera_limits",    OptionKind::kToggle,g.cameraLimits,    0, 1, 1);
            add("Floor LOD",       "floor_lod",        OptionKind::kCycle, g.floorLod,        0, 2, 1);
            add("Character Detail","character_detail", OptionKind::kCycle, g.characterDetail, 0, 2, 1);
            add("Fog Plane",       "fog_plane",        OptionKind::kStep,  g.fogPlane,        0, 100,10);
            add("Resolution",      "cur_res",          OptionKind::kCycle, g.curRes,          0, 2, 1);
            break;
        case OptionsPage::kSfx:
            // VIBE_Menu_RunOptionsSfx widget order -> byte block @0x1233550..
            add("Master Volume",   "master_vol", OptionKind::kStep,  s.masterVol, 0, 127, 16);
            add("Effects Volume",  "sfx_vol",    OptionKind::kStep,  s.sfxVol,    0, 127, 16);
            add("Music Volume",    "msx_vol",    OptionKind::kStep,  s.msxVol,    0, 127, 16);
            add("Speech Volume",   "speech_vol", OptionKind::kStep,  s.speechVol, 0, 127, 16);
            add("Music Quality",   "msx_freq",   OptionKind::kCycle, s.msxFreq,   0, 4,   1);
            break;
        case OptionsPage::kGame:
            // VIBE_Menu_RunOptionsGame widget order -> byte/dword block @0x1233558..
            add("Game Speed",      "speed",          OptionKind::kStep,  m.speed,         0, 160,16);
            add("Mouse Speed",     "mouse_speed",    OptionKind::kStep,  m.mouseSpeed,    0, 500,50);
            add("Scroll Speed",    "scroll_speed",   OptionKind::kStep,  m.scrollSpeed,   0, 100,10);
            add("Camera Speed",    "camera_speed",   OptionKind::kStep,  m.cameraSpeed,   0, 100,10);
            add("Invert Mouse",    "invert_mouse",   OptionKind::kToggle,m.invertMouse,   0, 1,  1);
            add("Show Cursor Text","show_cursor_txt",OptionKind::kToggle,m.showCursorTxt, 0, 1,  1);
            add("Show Building Info","show_geb_info",OptionKind::kToggle,m.showGebInfo,   0, 1,  1);
            add("Panel Mode",      "panel_mode",     OptionKind::kCycle, m.panelMode,     0, 4,  1);
            add("Help Events",     "help_events",    OptionKind::kToggle,m.helpEvents,    0, 1,  1);
            add("Hints",           "hints",          OptionKind::kToggle,m.hints,         0, 1,  1);
            add("Panel Help",      "panel_help",     OptionKind::kToggle,m.panelHelp,     0, 1,  1);
            break;
    }
    return r;
}

namespace {

// Apply a click on a control: cycle/step/toggle the value within [minV,maxV].
int ActuateValue(const OptionRow& row) {
    int v = row.value;
    switch (row.kind) {
        case OptionKind::kToggle:
            v = (v != 0) ? 0 : 1;
            break;
        case OptionKind::kCycle:
            v += row.step;
            if (v > row.maxV) v = row.minV;   // wrap
            break;
        case OptionKind::kStep:
            v += row.step;
            if (v > row.maxV) v = row.minV;   // wrap back to min for testability
            break;
    }
    if (v < row.minV) v = row.minV;
    if (v > row.maxV) v = row.maxV;
    return v;
}

// Pretty-print a row's current value for the panel.
std::string ValueText(const OptionRow& row) {
    if (row.kind == OptionKind::kToggle)
        return row.value ? "ON" : "OFF";
    if (row.kind == OptionKind::kCycle) {
        char buf[16];
        std::snprintf(buf, sizeof buf, "%d", row.value);
        return buf;
    }
    char buf[16];
    std::snprintf(buf, sizeof buf, "%d", row.value);
    return buf;
}

// Write the row list back into the config structs (the reconstructed equivalent
// of the original's "copy widget DataPtr -> settings global" + WriteGfxSettings).
void RowsToConfig(OptionsPage page, const std::vector<OptionRow>& rows,
                  config::GfxSettings& g, config::SoundSettings& s,
                  config::GameSettings& m) {
    auto getU8 = [&](std::size_t i) { return (guild::u8)rows[i].value; };
    switch (page) {
        case OptionsPage::kGfx:
            g.details = getU8(0); g.textureScale = getU8(1); g.lodHandling = getU8(2);
            g.shadowDetail = getU8(3); g.floorMipmapping = getU8(4); g.cameraLimits = getU8(5);
            g.floorLod = getU8(6); g.characterDetail = getU8(7); g.fogPlane = getU8(8);
            g.curRes = getU8(9);
            config::ResolutionForIndex(g.curRes, &g.resWidth, &g.resHeight);
            break;
        case OptionsPage::kSfx:
            s.masterVol = getU8(0); s.sfxVol = getU8(1); s.msxVol = getU8(2);
            s.speechVol = getU8(3); s.msxFreq = getU8(4);
            break;
        case OptionsPage::kGame:
            m.speed = rows[0].value; m.mouseSpeed = rows[1].value; m.scrollSpeed = rows[2].value;
            m.cameraSpeed = getU8(3); m.invertMouse = getU8(4); m.showCursorTxt = getU8(5);
            m.showGebInfo = getU8(6); m.panelMode = getU8(7); m.helpEvents = getU8(8);
            m.hints = getU8(9); m.panelHelp = getU8(10);
            break;
    }
}

// Hit-test a control row (the right-side actuation column). Returns row index or -1.
int HitRow(int mx, int my, int rowCount) {
    if (mx < kValueX || mx >= kValueX + kCtrlW) return -1;
    for (int i = 0; i < rowCount; ++i) {
        const int ry = kRowY0 + i * kRowH;
        if (my >= ry && my < ry + kRowH - 4) return i;
    }
    return -1;
}

// Back/OK control hit box (bottom of the panel).
struct BackBox { int x, y, w, h; };
BackBox BackControl(int rowCount) {
    const int y = kRowY0 + rowCount * kRowH + 16;
    return { kPanelX, y, 120, 26 };
}
bool HitBack(int mx, int my, int rowCount) {
    BackBox b = BackControl(rowCount);
    return mx >= b.x && mx < b.x + b.w && my >= b.y && my < b.y + b.h;
}

} // namespace

OptionsResult RunOptionsScreen(shim::IGraphicsDevice& device, shim::IPlatform& plat,
                               const OptionsConfig& cfg) {
    OptionsResult res;
    res.gfx = cfg.gfx; res.sound = cfg.sound; res.game = cfg.game;

    const int W = cfg.fbW, H = cfg.fbH;
    std::vector<std::uint32_t> scratch((std::size_t)W * H, 0u);

    // Live working copy of the rows (value edits accumulate here).
    std::vector<OptionRow> rows = OptionsRowsFor(cfg.page, res.gfx, res.sound, res.game);
    const int rowCount = (int)rows.size();

    // Real menu artwork backdrop when assets are present.
    shim::DiskFileSystem assetFs(cfg.gameDir);
    MenuAssets assets;
    const bool haveAssets = !cfg.gameDir.empty() && assets.Load(assetFs);

    const gui::MenuPalette pal;
    int frame = 0;
    bool prevLeft = false;

    for (;;) {
        if (cfg.maxFrames >= 0 && frame >= cfg.maxFrames) break;

        // ---- render ----
        std::fill(scratch.begin(), scratch.end(), 0u);
        gui::MenuRenderTarget tgt = gui::MenuRenderTarget::Wrap(scratch.data(), W, H, W * 4);

        if (haveAssets) {
            DrawBackground(scratch.data(), W, H, assets.background().data(),
                           assets.backgroundWidth(), assets.backgroundHeight());
        } else {
            gui::MenuFillRect(&tgt.surf, 0, 0, W, H, pal.bgR, pal.bgG, pal.bgB);
        }
        // A translucent-ish panel plate behind the rows (drawn always, for legibility).
        gui::MenuFillRect(&tgt.surf, kPanelX - 12, kTitleY - 8, kPanelW,
                          rowCount * kRowH + 80, 20, 24, 40);

        shim::MouseState ms{};
        plat.getMouse(ms);
        res.hoveredRow = HitRow(ms.x, ms.y, rowCount);

        DrawLabel(tgt.surf, kPanelX, kTitleY, PageTitle(cfg.page), 255, 230, 160);
        for (int i = 0; i < rowCount; ++i) {
            const int ry = kRowY0 + i * kRowH;
            const bool hov = (i == res.hoveredRow);
            DrawLabel(tgt.surf, kLabelX, ry + 6, rows[i].label,
                      hov ? 255 : 220, hov ? 255 : 220, hov ? 200 : 220);
            const std::string vt = ValueText(rows[i]);
            DrawLabel(tgt.surf, kValueX + 8, ry + 6, vt.c_str(), 160, 255, 160);
            // control box outline
            Outline(scratch.data(), W, H, kValueX, ry, kCtrlW, kRowH - 6,
                    hov ? 0xFFFFFF00u : 0xFF445566u);
        }
        // Back / OK control.
        BackBox b = BackControl(rowCount);
        const bool backHov = HitBack(ms.x, ms.y, rowCount);
        gui::MenuFillRect(&tgt.surf, b.x, b.y, b.w, b.h, 40, 50, 70);
        DrawLabel(tgt.surf, b.x + 12, b.y + 8, "BACK (apply)", 255, 255, 255);
        if (backHov) Outline(scratch.data(), W, H, b.x - 2, b.y - 2, b.w + 4, b.h + 4, 0xFFFFFF00u);

        BlitToDevice(scratch.data(), W, H, device);
        device.present();
        ++res.framesPresented;

        // ---- input ----
        if (!plat.pumpMessages()) { res.quitByWindow = true; res.cancelled = true; break; }
        plat.getMouse(ms);
        const bool leftEdge = ms.left && !prevLeft;
        prevLeft = ms.left;

        if (plat.keyDown(kVkEscape)) {
            res.quitByEsc = true; res.cancelled = true; break;   // ESC = cancel (discard)
        }
        if (leftEdge) {
            if (HitBack(ms.x, ms.y, rowCount)) {
                res.cancelled = false; break;                    // Back = apply
            }
            const int hov = HitRow(ms.x, ms.y, rowCount);
            if (hov >= 0) {
                rows[hov].value = ActuateValue(rows[hov]);
                res.lastToggledRow = hov;
            }
        }

        if (cfg.frameCapMs > 0) plat.sleepMs((std::uint32_t)cfg.frameCapMs);
        ++frame;
    }

    res.back = true;
    if (!res.cancelled) {
        // Apply: copy the edited rows back into the config structs.
        RowsToConfig(cfg.page, rows, res.gfx, res.sound, res.game);
    }
    // changed = any value differs from the seed (regardless of apply/cancel, the
    // caller usually only commits res.* when !cancelled — but report the truth).
    std::vector<OptionRow> seed = OptionsRowsFor(cfg.page, cfg.gfx, cfg.sound, cfg.game);
    for (int i = 0; i < rowCount && i < (int)seed.size(); ++i) {
        if (rows[i].value != seed[i].value) { res.changed = true; break; }
    }
    return res;
}

} // namespace guild::play
