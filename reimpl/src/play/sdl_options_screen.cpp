// guild::play — native Game/Gfx/Sfx OPTIONS screens. See sdl_options_screen.h.
//
// 1:1 VIEW reconstruction of VIBE_Menu_RunOptionsGfx/Sfx/Game
// (0x56c21c / 0x56c808 / 0x56cc44). The originals are forms.BIN-driven panels;
// this renderer reproduces that REAL view: the `_OPTIONEN_PIC` full-screen
// background, the form windows at their native FRM2 geometry (center-translated
// only, never scaled — VIBE_Window_PositionAtCoord mode-2/3 @0x41d7e0), the
// `_TOOL_TIP_BIG` parchment panels, one row per setting with its real localized
// `_OPTIONEN_*` label + a `_SLIDER_GOLD_WAAGERECHT` gold slider track whose thumb
// sits at the engine's value->pixel mapping (VIBE_Scrollbar_DragThumb @0x4208ac:
// thumbX = 4*(value-min)/ComputeStep(range), ComputeStep @0x41df08), the cycle
// value text on the right, the centered title, and the `_BUTTON_RED` OK/Cancel
// row (Hud_BuildButtonRow @0x4bcdfc, labels `_OPTIONEN_BUTTONS+0/+1`).
//
// The DATA/behavior (OptionsRowsFor value/range/step table, RowsToConfig
// save-back, SaveSettings persistence, the apply sinks, the input/hit-test
// semantics and the OptionsResult contract) is disasm-pinned and preserved
// exactly; only the VIEW (geometry + assets + labels) and the hit-test rects are
// reconstructed here. When assets/font are absent (headless/asset-less) a simple
// flat fallback render runs so existing non-asset tests still pass.
#include "play/sdl_options_screen.h"

#include "play/settings_io.h"
#include "shim/IGraphicsDevice.h"
#include "shim/IPlatform.h"

#include "gui/menu_render.h"   // MenuRenderTarget, MenuFillRect, MenuPalette
#include "play/menu_assets.h"  // MenuAssets — real gilde.gfx art + ResolveOptionLabels
#include "gui/gui_surface_render.h"  // GuiSurface — 1:1 SHAPBANK blit-to-surface
#include "gui/gui_render_iface.h"    // RenderHSlider — Entity_InteractionLogic @0x41078c
#include "render/text_raster.h"
#include "render/font.h"
#include "render/types.h"
#include "render/bmp.h"
#include "shim_impl/disk_filesystem.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace guild::play {
namespace {

constexpr int kVkEscape = 0x1B;

// gilde.exe authors the whole GUI at an 800x600 "design" canvas and only
// CENTER-TRANSLATES it for the screen resolution (it does NOT scale widget
// sizes). VIBE_Window_PositionAtCoord @0x41d7e0 mode-2/3 resolves a window's
// design x/y to x + (screenW-800)/2, y + (screenH-600)/2.
constexpr int kDesignW = 800, kDesignH = 600;

// ---------------------------------------------------------------------------
// Form geometry per page (Resources/forms.BIN, FRM2; native window x/y/w/h).
// WIN0 = outer panel window, WIN1 = body window (parent of the slider widgets +
// the _TOOL_TIP_BIG parchment sprites). Slider widgets are at x=208 relative to
// WIN1; their y offsets are the per-page lists below. (MENU-SUBSCREENS-GROUNDTRUTH.)
// ---------------------------------------------------------------------------
struct FormGeom {
    int win0X, win0Y, win0W, win0H;   // outer panel window
    int win1X, win1Y, win1W, win1H;   // body window (slider parent)
    int sliderX;                      // slider x relative to WIN1
    const int* rowY;                  // slider y offsets relative to WIN1 (one per row)
    int rowCount;                     // number of slider y entries
    int parchY0, parchY1;             // the two _TOOL_TIP_BIG trim sprite y's (rel WIN1)
    int parchX;                       // _TOOL_TIP_BIG x (rel WIN1)
};

// OPTIONS_GFX.form  WIN0 (112,120,452,574); WIN1 (146,171,328,509);
//   9 sliders x=208 y={16,48,96,160,128,192,240,272,304} (BUILD order),
//   2x _TOOL_TIP_BIG+1 at (64,80)/(64,224).
const int kGfxRowY[9]  = { 16, 48, 96, 160, 128, 192, 240, 272, 304 };
// OPTIONS_SFX.form  WIN0 (112,120,449,575); WIN1 (144,168,340,514);
//   5 sliders x=208 y={32,120,160,200,280}, 2x _TOOL_TIP_BIG+1 (64,80)/(64,248).
const int kSfxRowY[5]  = { 32, 120, 160, 200, 280 };
// OPTIONS_GAME.form WIN0 (104,120,449,575); WIN1 (136,168,344,508);
//   11 sliders x=208 y={8,72,96,120,144,168,208,232,256,280,304},
//   2x _TOOL_TIP_BIG+1 (72,56)/(72,192).
const int kGameRowY[11] = { 8, 72, 96, 120, 144, 168, 208, 232, 256, 280, 304 };

FormGeom GeomFor(OptionsPage p) {
    switch (p) {
        case OptionsPage::kGfx:
            return { 112,120,452,574, 146,171,328,509, 208, kGfxRowY,  9, 80, 224, 64 };
        case OptionsPage::kSfx:
            return { 112,120,449,575, 144,168,340,514, 208, kSfxRowY,  5, 80, 248, 64 };
        case OptionsPage::kGame:
        default:
            return { 104,120,449,575, 136,168,344,508, 208, kGameRowY, 11, 56, 192, 72 };
    }
}

// ---------------------------------------------------------------------------
// VIBE_Slider_ComputeStep @0x41df08 — the integer step for a slider whose value
// range is `range` (= max - min). Drives both the 25-step clamp and the thumb
// pixel mapping (thumbX = 4*(value-min)/step). 1:1.
// ---------------------------------------------------------------------------
int SliderComputeStep(int range) {
    if (range >= 125000) return 10000;
    if (range >= 100000) return 5000;
    if (range >= 75000)  return 4000;
    if (range >= 50000)  return 3000;
    if (range >= 37500)  return 2000;
    if (range >= 25000)  return 1500;
    if (range >= 12500)  return 1000;
    if (range >= 5000)   return 500;
    if (range >= 2500)   return 200;
    if (range >= 1000)   return 100;
    if (range >= 250)    return 50;
    if (range >= 100)    return 10;
    return 1;
}

// VIBE_Scrollbar_DragThumb @0x4208ac: the thumb's pixel x within the track =
// 4 * (value - min) / ComputeStep(max - min), clamped to the 25-step span
// (max 100px, since VIBE_Object_SetValueOrText clamps v19/v15 to 25).
int SliderThumbPx(int value, int minV, int maxV) {
    const int range = maxV - minV;
    if (range <= 0) return 0;
    const int step = SliderComputeStep(range);
    int v = value;
    if (v < minV) v = minV;
    if (v > maxV) v = maxV;
    int px = 4 * (v - minV) / step;
    if (px < 0) px = 0;
    if (px > 100) px = 100;   // 25-step clamp (4px per step)
    return px;
}

// _SLIDER_GOLD_WAAGERECHT (#106) geometry: the 100px-wide fill bars [1][2][4]
// define a 100px thumb travel; the track plate is drawn that wide.
constexpr int kSliderTrackW = 100;
constexpr int kSliderTrackH = 18;
constexpr int kSliderThumbW = 14;   // visible thumb cap width on the track

// ---- shared draw helpers (mirror native_main_menu.cpp) ----
const std::uint8_t* GlyphMap() {
    static std::uint8_t table[256];
    static bool init = false;
    if (!init) { render::FontInitGlyphTable(table); init = true; }
    return table;
}

// Fallback (asset-less) ASCII label via the built-in raster font.
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

void FillRect(std::uint32_t* px, int w, int h, int x, int y, int bw, int bh,
              std::uint32_t col) {
    for (int Y = y; Y < y + bh; ++Y) {
        if (Y < 0 || Y >= h) continue;
        for (int X = x; X < x + bw; ++X) {
            if (X < 0 || X >= w) continue;
            px[(std::size_t)Y * w + X] = col;
        }
    }
}

// Per-page slider geometry from the real Menu/OPTIONS_*.form (type-69 objects in
// row order; objX=208 in the body window WIN1, per-slider track length `range`,
// per-row objY). Shared by the renderer and the hit-test so clicks land exactly on
// what is drawn. yByRow is in OptionsRowsFor / GetChildObjectId child order.
struct OptGeom { int win1X, win1Y, sliderObjX, range; const int* yByRow; int yCount; };
OptGeom OptGeomFor(OptionsPage page) {
    static const int kGameY[11] = {8,72,96,120,144,208,232,168,256,280,304};
    static const int kGfxY[9]   = {16,48,96,160,128,192,240,272,304};
    static const int kSfxY[5]   = {32,120,160,200,280};
    switch (page) {
        case OptionsPage::kGfx:  return {146,171,208,160,kGfxY,9};
        case OptionsPage::kSfx:  return {144,168,208,140,kSfxY,5};
        case OptionsPage::kGame: default: return {136,168,208,140,kGameY,11};
    }
}

// Full-screen background blit (the real _OPTIONEN_PIC at native size, scaled to
// the framebuffer like native_main_menu's DrawBackground — point-sampled).
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

// Nearest-neighbour scale-blit of a decoded sprite (A==0 skips) into a dest rect.
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

// Blit a sprite at native size, top-left (dx,dy), clipped.
void Blit(std::uint32_t* dst, int W, int H, const render::DecodedShape& sh, int dx, int dy) {
    if (sh.width <= 0 || sh.height <= 0) return;
    for (int y = 0; y < sh.height; ++y) {
        const int Y = dy + y;
        if (Y < 0 || Y >= H) continue;
        const std::uint32_t* srow = sh.argb.data() + (std::size_t)y * sh.width;
        std::uint32_t* drow = dst + (std::size_t)Y * W;
        for (int x = 0; x < sh.width; ++x) {
            const int X = dx + x;
            if (X < 0 || X >= W) continue;
            const std::uint32_t p = srow[x];
            if (p & 0xFF000000u) drow[X] = p;
        }
    }
}

// _BUTTON_RED (#174) 3-slice at native size: cap(12) L + stretched centre + cap(12) R.
// `sel` swaps to the hover frame triple {3=L,5=C,4=R}.
constexpr int kBtnCapW = 12, kBtnH = 33;
void DrawButton3Slice(std::uint32_t* dst, int W, int H, MenuAssets& a,
                      int rx, int ry, int rw, int rh, bool sel) {
    const render::DecodedShape* L = a.buttonFrame(sel ? 3 : 0);
    const render::DecodedShape* C = a.buttonFrame(sel ? 5 : 2);
    const render::DecodedShape* R = a.buttonFrame(sel ? 4 : 1);
    if (!L || !C || !R) return;
    int lw = kBtnCapW, rcw = kBtnCapW;
    int mw = rw - lw - rcw;
    if (mw < 0) { mw = 0; rcw = rw - lw; if (rcw < 0) { rcw = 0; lw = rw; } }
    ScaledBlit(dst, W, H, *L, rx,            ry, lw,  rh);
    ScaledBlit(dst, W, H, *C, rx + lw,       ry, mw,  rh);
    ScaledBlit(dst, W, H, *R, rx + lw + mw,  ry, rcw, rh);
}

// Centre a real-_FONT label inside a rect (CP1251 glyphs).
void DrawFontCentered(std::uint32_t* dst, int W, int H, const MenuFont& fnt,
                      int x, int y, int boxW, int boxH, const char* s,
                      std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    if (!s || !*s) return;
    const int tw = fnt.MeasureWidth(s);
    const int th = fnt.lineHeight() > 0 ? fnt.lineHeight() : 17;
    fnt.DrawText(dst, W, H, x + (boxW - tw) / 2, y + (boxH - th) / 2, s, 1, r, g, b);
}

// Left-align a real-_FONT label at (x, baseline-box y).
void DrawFontLeft(std::uint32_t* dst, int W, int H, const MenuFont& fnt,
                  int x, int y, int boxH, const char* s,
                  std::uint8_t r, std::uint8_t g, std::uint8_t b) {
    if (!s || !*s) return;
    const int th = fnt.lineHeight() > 0 ? fnt.lineHeight() : 17;
    fnt.DrawText(dst, W, H, x, y + (boxH - th) / 2, s, 1, r, g, b);
}

// ---- page titles (fallback English; the real title is resolved from textbin) ----
const char* PageTitleFallback(OptionsPage p) {
    switch (p) {
        case OptionsPage::kGame: return "GAME OPTIONS";  // VIBE_Text_RenderRichString(0x1866)
        case OptionsPage::kGfx:  return "GFX OPTIONS";   // ... 0x1867
        case OptionsPage::kSfx:  return "SFX OPTIONS";   // ... 0x1868
    }
    return "OPTIONS";
}

// _OPTIONEN_GFX / _SFX / _GAME label keys (per-row), in BUILD order. One per row.
const char* const kGfxKeys[9] = {
    "_OPTIONEN_GFX+0", "_OPTIONEN_GFX+1", "_OPTIONEN_GFX+2", "_OPTIONEN_GFX+3",
    "_OPTIONEN_GFX+4", "_OPTIONEN_GFX+5", "_OPTIONEN_GFX+6", "_OPTIONEN_GFX+7",
    "_OPTIONEN_GFX+8",
};
const char* const kSfxKeys[5] = {
    "_OPTIONEN_SFX+0", "_OPTIONEN_SFX+1", "_OPTIONEN_SFX+2", "_OPTIONEN_SFX+3",
    "_OPTIONEN_SFX+4",
};
const char* const kGameKeys[11] = {
    "_OPTIONEN_GAME+0", "_OPTIONEN_GAME+1", "_OPTIONEN_GAME+2", "_OPTIONEN_GAME+3",
    "_OPTIONEN_GAME+4", "_OPTIONEN_GAME+5", "_OPTIONEN_GAME+6", "_OPTIONEN_GAME+7",
    "_OPTIONEN_GAME+8", "_OPTIONEN_GAME+9", "_OPTIONEN_GAME+10",
};

void RowLabelKeys(OptionsPage p, const char* const** keys, int* n) {
    switch (p) {
        case OptionsPage::kGfx:  *keys = kGfxKeys;  *n = 9;  break;
        case OptionsPage::kSfx:  *keys = kSfxKeys;  *n = 5;  break;
        case OptionsPage::kGame: *keys = kGameKeys; *n = 11; break;
        default: *keys = nullptr; *n = 0; break;
    }
}

// The options-page title. The engine renders it via VIBE_Text_RenderRichString
// (0x1866/0x1867/0x1868 -> _OPTIONEN_UEBERSCHRIFT+2/+3/+4 = "$Z$[Настройки X$]"),
// i.e. centered rich markup. The IDENTICAL visible text exists markup-free as
// _OPTIONEN_MENUE_{GAME,GFX,SFX}+0 ("Настройки игры/графики/звука"); use those so
// no markup parser is needed and every page gets its CORRECT localized title.
// (The old _OPTIONEN_UEBERSCHRIFT+0/1/2 mapping was wrong: those are the
//  Load/Save/Settings headings, so Gfx showed "Сохранить игру" and Sfx "Настройки
//  игры", with the raw $Z$[ ]$ control codes drawn as glyphs.)
const char* const kTitleKeys[3] = {
    "_OPTIONEN_MENUE_GAME+0",  // "Настройки игры"
    "_OPTIONEN_MENUE_GFX+0",   // "Настройки графики"
    "_OPTIONEN_MENUE_SFX+0",   // "Настройки звука"
};
int TitleKeyIndex(OptionsPage p) {
    switch (p) {
        case OptionsPage::kGame: return 0;
        case OptionsPage::kGfx:  return 1;
        case OptionsPage::kSfx:  return 2;
    }
    return 0;
}

} // namespace

// ---------------------------------------------------------------------------
// Row table — exactly which real gilde.INI keys each page exposes, in the
// original forms' BUILD ORDER (VIBE_Form_GetChildObjectId child index order)
// with the exact (min, max) SetValueOrText ranges. All keys are REAL:
// VIBE_Config_WriteGfxSettings persists every one of them.
// (PRESERVED EXACTLY — pinned by oneone_screens_wave14_test.)
// ---------------------------------------------------------------------------
std::vector<OptionRow> OptionsRowsFor(OptionsPage page,
                                      const config::GfxSettings& g,
                                      const config::SoundSettings& s,
                                      const config::GameSettings& m,
                                      const OptionsRowCaps& caps) {
    std::vector<OptionRow> r;
    auto add = [&](const char* label, const char* key, OptionKind k,
                   int value, int minV, int maxV, int step, const char* optList = "",
                   bool hidden = false) {
        OptionRow row; row.label = label; row.key = key; row.kind = k;
        row.value = value; row.minV = minV; row.maxV = maxV; row.step = step;
        row.hidden = hidden; row.optList = optList;
        r.push_back(row);
    };
    // optList = the VIBE_Text_AppendWideLines list the original appends per row (the
    // text shown ON the slider): _OPTIONEN_STUFEN (low/med/high), _STUFEN_RES
    // (resolutions), _STUFEN_PANEL, _STUFEN_FREQ, _STUFEN_AN_AUS (on/off bool). ""
    // = numeric ("%i"). A row whose list is _STUFEN_AN_AUS is a true bool (square
    // toggle); others with a list are option-text sliders.
    switch (page) {
        case OptionsPage::kGfx: {
            int resMax = 0;
            if (caps.resCap1024) resMax = 1;
            if (caps.resCap1280) resMax = 2;
            add("Resolution",      "cur_res",          OptionKind::kCycle, g.curRes,          0, resMax, 1, "_OPTIONEN_STUFEN_RES", caps.inGame);
            add("Detail Level",    "details",          OptionKind::kCycle, g.details,         0, 2, 1, "_OPTIONEN_STUFEN");      // child 1
            add("Texture Scale",   "texture_scale",    OptionKind::kCycle, g.textureScale,    0, 2, 1, "_OPTIONEN_STUFEN");      // child 2
            add("Floor LOD",       "floor_lod",        OptionKind::kCycle, g.floorLod,        0, 2, 1, "_OPTIONEN_STUFEN");      // child 3
            add("Floor Mipmaps",   "floor_mipmapping", OptionKind::kToggle,g.floorMipmapping, 0, 1, 1, "_OPTIONEN_STUFEN_AN_AUS"); // child 4
            add("LOD Handling",    "lod_handling",     OptionKind::kToggle,g.lodHandling,     0, 1, 1, "_OPTIONEN_STUFEN");      // child 5 (2-opt STUFEN, not on/off)
            add("Shadow Detail",   "shadow_detail",    OptionKind::kCycle, g.shadowDetail,    0, 2, 1, "_OPTIONEN_STUFEN");      // child 6
            add("Gamma",           "fog_plane",        OptionKind::kStep,  100 - g.fogPlane,  50, 100, 10);                       // numeric slider
            add("Camera Limits",   "camera_limits",    OptionKind::kCycle, g.cameraLimits,    0, 2, 1, "_OPTIONEN_STUFEN");      // child 8
            break;
        }
        case OptionsPage::kSfx:
            add("Master Volume",   "master_vol", OptionKind::kStep,  s.masterVol, 0, 127, 16);
            add("Effects Volume",  "sfx_vol",    OptionKind::kStep,  s.sfxVol,    0, 127, 16);
            add("Music Volume",    "msx_vol",    OptionKind::kStep,  s.msxVol,    0, 127, 16);
            add("Speech Volume",   "speech_vol", OptionKind::kStep,  s.speechVol, 0, 127, 16);
            add("Music Quality",   "msx_freq",   OptionKind::kCycle, s.msxFreq,   0, 4,   1, "_OPTIONEN_STUFEN_FREQ");
            break;
        case OptionsPage::kGame:
            add("Game Speed",      "speed",          OptionKind::kStep,  m.speed,         0, 160,16); // child 0  dword_1233558
            add("Mouse Speed",     "mouse_speed",    OptionKind::kStep,  m.mouseSpeed,    0, 500,50); // child 1  dword_1233560
            add("Scroll Speed",    "scroll_speed",   OptionKind::kStep,  m.scrollSpeed,   0, 100,10); // child 2  dword_1233564
            add("Camera Speed",    "camera_speed",   OptionKind::kStep,  m.cameraSpeed,   0, 100,10); // child 3  byte_123355C
            add("Invert Mouse",    "invert_mouse",   OptionKind::kToggle,m.invertMouse,   0, 1,  1, "_OPTIONEN_STUFEN_AN_AUS", /*hidden=*/true);
            add("Show Cursor Text","show_cursor_txt",OptionKind::kToggle,m.showCursorTxt, 0, 1,  1, "_OPTIONEN_STUFEN_AN_AUS"); // child 5
            add("Show Building Info","show_geb_info",OptionKind::kToggle,m.showGebInfo,   0, 1,  1, "_OPTIONEN_STUFEN_AN_AUS"); // child 6
            add("Panel Mode",      "panel_mode",     OptionKind::kCycle, m.panelMode,     0, 4,  1, "_OPTIONEN_STUFEN_PANEL");  // child 9
            add("Help Events",     "help_events",    OptionKind::kToggle,m.helpEvents,    0, 1,  1, "_OPTIONEN_STUFEN_AN_AUS"); // child 10
            // Row 9 (_OPTIONEN_GAME+9) resolves to the localized caption "Уровень
            // сложности" (difficulty). Hidden per request — the slot is skipped in
            // layout/draw/hit-test (like Invert Mouse), but the vector index stays
            // so RowsToConfig's getU8(9) still reads the seed value back unchanged.
            add("Hints",           "hints",          OptionKind::kToggle,m.hints,         0, 1,  1, "_OPTIONEN_STUFEN_AN_AUS", /*hidden=*/true); // child 11
            add("Panel Help",      "panel_help",     OptionKind::kToggle,m.panelHelp,     0, 1,  1, "_OPTIONEN_STUFEN_AN_AUS"); // child 12
            break;
    }
    return r;
}

// Process-wide apply sinks (see header). Installed by app wiring; default no-op.
OptionsApplySinks& OptionsApplyHooks() {
    static OptionsApplySinks sinks;
    return sinks;
}

namespace {

// Apply a click on a control: cycle/step/toggle the value within [minV,maxV].
// Superseded by the +/- button + drag interaction (kept for reference / tests).
[[maybe_unused]] int ActuateValue(const OptionRow& row) {
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

// Pretty-print a row's current value for the panel. The four SFX volume sliders
// display as a PERCENTAGE of their max (gilde.exe SFX screen shows "100%", "90%",
// ... — master 127 -> 100%), not the raw 0..127 amplitude. All other numeric
// sliders show the raw value ("%i", VIBE_Text default).
std::string ValueText(const OptionRow& row) {
    char buf[16];
    const bool isVol =
        std::strcmp(row.key, "master_vol") == 0 || std::strcmp(row.key, "sfx_vol") == 0 ||
        std::strcmp(row.key, "msx_vol")   == 0 || std::strcmp(row.key, "speech_vol") == 0;
    if (isVol && row.maxV > 0) {
        const int pct = (row.value * 100 + row.maxV / 2) / row.maxV;
        std::snprintf(buf, sizeof buf, "%d%%", pct);
    } else {
        std::snprintf(buf, sizeof buf, "%d", row.value);
    }
    return buf;
}

// Write the row list back into the config structs — the reconstructed
// equivalent of the original's OK-path "copy widget DataPtr -> settings
// global" blocks (Gfx 0x56c735.., Sfx 0x56cbee.., Game 0x56d184..).
// (PRESERVED EXACTLY.)
void RowsToConfig(OptionsPage page, const std::vector<OptionRow>& rows,
                  config::GfxSettings& g, config::SoundSettings& s,
                  config::GameSettings& m) {
    auto getU8 = [&](std::size_t i) { return (guild::u8)rows[i].value; };
    switch (page) {
        case OptionsPage::kGfx:
            g.details = getU8(1); g.textureScale = getU8(2); g.floorLod = getU8(3);
            g.floorMipmapping = getU8(4); g.lodHandling = getU8(5);
            g.shadowDetail = getU8(6);
            g.fogPlane = (guild::u8)(50 - (rows[7].value - 50));
            g.cameraLimits = getU8(8);
            break;
        case OptionsPage::kSfx:
            s.masterVol = getU8(0); s.sfxVol = getU8(1); s.msxVol = getU8(2);
            s.speechVol = getU8(3); s.msxFreq = getU8(4);
            break;
        case OptionsPage::kGame:
            m.speed = rows[0].value; m.mouseSpeed = rows[1].value; m.scrollSpeed = rows[2].value;
            m.cameraSpeed = getU8(3);
            m.invertMouse = 0;
            m.showCursorTxt = getU8(5);
            m.showGebInfo = getU8(6); m.panelMode = getU8(7); m.helpEvents = getU8(8);
            m.hints = getU8(9); m.panelHelp = getU8(10);
            break;
    }
}

// ---------------------------------------------------------------------------
// Layout — every screen rect is derived from the real FRM2 geometry,
// center-translated only. The slider track is the control hit area; OK/Cancel
// are at the bottom of the outer panel (form window 3 / the button row).
// ---------------------------------------------------------------------------
struct Layout {
    int ox, oy;                       // center-translate offsets
    FormGeom g;                       // form geometry for this page
    int win1ScrX, win1ScrY;           // body window screen origin
    int trackX;                       // slider track screen x (constant column)
    int labelX;                       // label column screen x
    int valueX;                       // value-text column screen x (right of track)
    // OK / Cancel button rects.
    int okX, okY, okW, okH;
    int cancelX, cancelY, cancelW, cancelH;
};

constexpr int kBtnW = 96;             // OK/Cancel button width (measured from original)

Layout BuildLayout(OptionsPage page, int fbW, int fbH) {
    Layout L;
    L.g = GeomFor(page);
    L.ox = (fbW - kDesignW) / 2;
    L.oy = (fbH - kDesignH) / 2;
    L.win1ScrX = L.ox + L.g.win1X;
    L.win1ScrY = L.oy + L.g.win1Y;
    L.trackX   = L.win1ScrX + L.g.sliderX;
    L.labelX   = L.win1ScrX + 8;
    L.valueX   = L.trackX + kSliderTrackW + 10;

    // OK/Cancel: the button row sits in the outer panel near the bottom (form
    // window 3 / Hud_BuildButtonRow @0x4bcdfc; frida button-row window = 128,512,
    // 524,41). GeomFor stores win0W/win0H swapped, so panelW=win0H, panelH=win0W.
    // The panel is screen-centered (matches the frame), button row centered in it.
    const int panelW   = L.g.win0H;                  // 575 (game)
    const int panelH   = L.g.win0W;                  // 449 (game)
    const int win0ScrX = (fbW - panelW) / 2;         // centered
    const int win0ScrY = L.oy + L.g.win0Y;
    const int btnY     = win0ScrY + panelH - kBtnH - 21;   // -> ~515 (measured top)
    const int gap      = 78;                          // measured gap between the two buttons
    const int totalW   = kBtnW * 2 + gap;
    const int firstX   = win0ScrX + (panelW - totalW) / 2;
    L.okX = firstX;            L.okY = btnY; L.okW = kBtnW; L.okH = kBtnH;
    L.cancelX = firstX + kBtnW + gap; L.cancelY = btnY; L.cancelW = kBtnW; L.cancelH = kBtnH;
    return L;
}

// Slider track screen rect for row i.
void RowTrackRect(const Layout& L, int i, int* x, int* y, int* w, int* h) {
    *x = L.trackX;
    *y = L.win1ScrY + L.g.rowY[i] - kSliderTrackH / 2;
    *w = kSliderTrackW;
    *h = kSliderTrackH;
}

// Hit-test a control row: the slider track is the actuation area. Hidden rows
// keep their layout slot but are never hit. (Semantics preserved: click in the
// control column actuates the row's value.)
[[maybe_unused]] int HitRow(int mx, int my, const Layout& L, const std::vector<OptionRow>& rows) {
    for (int i = 0; i < (int)rows.size() && i < L.g.rowCount; ++i) {
        if (rows[i].hidden) continue;
        int x, y, w, h; RowTrackRect(L, i, &x, &y, &w, &h);
        // Generous vertical band (the original row height) so a click anywhere on
        // the row's slider line actuates it.
        if (mx >= x && mx < x + w && my >= y - 6 && my < y + h + 6) return i;
    }
    return -1;
}

bool HitOk(int mx, int my, const Layout& L) {
    return mx >= L.okX && mx < L.okX + L.okW && my >= L.okY && my < L.okY + L.okH;
}
bool HitCancel(int mx, int my, const Layout& L) {
    return mx >= L.cancelX && mx < L.cancelX + L.cancelW &&
           my >= L.cancelY && my < L.cancelY + L.cancelH;
}

// One-shot framebuffer dump for visual verification (GUILD_OPTIONS_DUMP=path).
// Debug aid only; env-gated, never on the 1:1 path.
void MaybeDump(const std::uint32_t* px, int W, int H) {
    const char* mp = std::getenv("GUILD_OPTIONS_DUMP");
    if (!mp) return;
    static bool dumped = false;
    if (dumped) return;
    dumped = true;
    std::vector<std::uint8_t> rgb((std::size_t)W * H * 3);
    for (std::size_t i = 0, n = (std::size_t)W * H; i < n; ++i) {
        const std::uint32_t c = px[i];
        rgb[i*3+0] = (std::uint8_t)((c >> 16) & 0xFF);
        rgb[i*3+1] = (std::uint8_t)((c >> 8) & 0xFF);
        rgb[i*3+2] = (std::uint8_t)(c & 0xFF);
    }
    std::vector<std::uint8_t> bmp = render::BmpSave24Bit(W, H, rgb.data());
    if (FILE* f = std::fopen(mp, "wb")) {
        std::fwrite(bmp.data(), 1, bmp.size(), f); std::fclose(f);
        std::printf("  --options: frame dumped -> %s (%dx%d)\n", mp, W, H);
    }
}

} // namespace

OptionsResult RunOptionsScreen(shim::IGraphicsDevice& device, shim::IPlatform& plat,
                               const OptionsConfig& cfg) {
    OptionsResult res;
    res.gfx = cfg.gfx; res.sound = cfg.sound; res.game = cfg.game;

    // The screen draws the game's own leather-hand cursor (below). Hide the OS
    // arrow so it never appears here (the menu already hides it; this also covers
    // the standalone GUILD_OPEN_OPTIONS path). Harmless if already hidden.
    plat.showSystemCursor(false);

    // ---- live INI binding: seed from the real gilde.INI (PRESERVED) -------
    const std::string iniRoot = cfg.iniDir.empty() ? cfg.gameDir : cfg.iniDir;
    shim::DiskFileSystem iniFs(iniRoot);
    bool bound = false;
    if (cfg.bindIni && !iniRoot.empty() && iniFs.exists(cfg.iniName.c_str())) {
        SettingsBundle seed;
        if (LoadSettings(iniFs, cfg.iniName, seed)) {
            res.gfx = seed.gfx; res.sound = seed.sound; res.game = seed.game;
            bound = true;
        }
    }

    const int W = cfg.fbW, H = cfg.fbH;
    std::vector<std::uint32_t> scratch((std::size_t)W * H, 0u);

    OptionsRowCaps caps;
    caps.resCap1024 = cfg.resCap1024; caps.resCap1280 = cfg.resCap1280;
    caps.inGame = cfg.inGame;
    std::vector<OptionRow> rows = OptionsRowsFor(cfg.page, res.gfx, res.sound, res.game, caps);
    const int rowCount = (int)rows.size();

    const Layout L = BuildLayout(cfg.page, W, H);

    // ---- real menu artwork + localized labels ----------------------------
    shim::DiskFileSystem assetFs(cfg.gameDir);
    MenuAssets assets;
    // Background: from the MAIN MENU, the options sub-screen keeps the live title
    // backdrop (the city _MENUE_BACKGROUND) underneath (doc 08 §1.6 — the 3D title
    // scene stays live under sub-forms; confirmed against the original: the options
    // panel sits over the city, NOT the _OPTIONEN_PIC bookshelf). _OPTIONEN_PIC is
    // the IN-GAME (pause) options background. Pick per context.
    const char* bgName = cfg.inGame
        ? (W >= 1152 ? "_OPTIONEN_PIC_1152" : W >= 1024 ? "_OPTIONEN_PIC_1024" : "_OPTIONEN_PIC")
        : (W >= 1152 ? "_MENUE_BACKGROUND_1152" : W >= 1024 ? "_MENUE_BACKGROUND_1024" : "_MENUE_BACKGROUND");
    const bool haveAssets = !cfg.gameDir.empty() &&
                            assets.Load(assetFs, "gfx/gilde.gfx", bgName);
    const bool haveFont = haveAssets && assets.font().loaded();

    // Resolve the localized row labels, the title, the value-cycle texts and the
    // OK/Cancel button labels (CP1251 from the textbin). Empty -> built-in fallback.
    std::vector<std::string> rowLabels((std::size_t)rowCount);
    std::string title;
    std::string okLabel, cancelLabel;
    if (haveAssets && !cfg.gameDir.empty()) {
        const char* const* keys = nullptr; int n = 0;
        RowLabelKeys(cfg.page, &keys, &n);
        if (keys && n > 0) {
            std::vector<std::string> tmp((std::size_t)n);
            ResolveOptionLabels(cfg.gameDir, keys, n, tmp.data());
            for (int i = 0; i < n && i < rowCount; ++i) rowLabels[i] = tmp[i];
        }
        // Title (one of _OPTIONEN_UEBERSCHRIFT+0/1/2).
        {
            const char* tk[1] = { kTitleKeys[TitleKeyIndex(cfg.page)] };
            std::string t[1];
            if (ResolveOptionLabels(cfg.gameDir, tk, 1, t)) title = t[0];
        }
        // OK / Cancel.
        {
            const char* bk[2] = { "_OPTIONEN_BUTTONS+0", "_OPTIONEN_BUTTONS+1" };
            std::string b[2];
            ResolveOptionLabels(cfg.gameDir, bk, 2, b);
            okLabel = b[0]; cancelLabel = b[1];
        }
    }
    // Bool on/off option text — what a discrete toggle slider shows at its thumb.
    // gilde.exe appends `_OPTIONEN_STUFEN_AN_AUS` (AppendWideLines(2,..)) to each
    // boolean child; the slider draws option[value] at the thumb (Entity_-
    // InteractionLogic @0x41078c). out[0] = "off", out[1] = "on".
    std::string anAus[2];
    if (haveAssets && !cfg.gameDir.empty()) {
        const char* ak[2] = { "_OPTIONEN_STUFEN_AN_AUS+0", "_OPTIONEN_STUFEN_AN_AUS+1" };
        ResolveOptionLabels(cfg.gameDir, ak, 2, anAus);
    }
    // Per-row option-list strings (optList+0..maxV) — the text the original draws ON
    // each discrete slider (cycles + 2-option STUFEN rows). Resolved ONCE from the
    // textbin (one mount) and indexed by the row's current value. Numeric (kStep)
    // rows have an empty optList and show "%i".
    std::vector<std::vector<std::string>> rowOpts((std::size_t)rowCount);
    if (haveAssets && !cfg.gameDir.empty()) {
        std::vector<std::string> keyStore;
        std::vector<std::pair<int,int>> spanOf(rowCount, {0,0});  // (start, count)
        for (int i = 0; i < rowCount; ++i) {
            if (!rows[i].optList || !rows[i].optList[0]) continue;
            const int start = (int)keyStore.size();
            for (int v = rows[i].minV; v <= rows[i].maxV; ++v)
                keyStore.push_back(std::string(rows[i].optList) + "+" + std::to_string(v));
            spanOf[i] = { start, (int)keyStore.size() - start };
        }
        if (!keyStore.empty()) {
            std::vector<const char*> keys(keyStore.size());
            for (std::size_t k = 0; k < keyStore.size(); ++k) keys[k] = keyStore[k].c_str();
            std::vector<std::string> out(keyStore.size());
            ResolveOptionLabels(cfg.gameDir, keys.data(), (int)keys.size(), out.data());
            for (int i = 0; i < rowCount; ++i) {
                const auto sp = spanOf[i];
                if (sp.second <= 0) continue;
                rowOpts[i].assign(out.begin() + sp.first, out.begin() + sp.first + sp.second);
            }
        }
    }

    const gui::MenuPalette pal;
    int frame = 0;
    bool prevLeft = false;
    int  dragRow = -1;    // row whose slider thumb is being dragged (-1 = none)
    // Left/right cap widths (the +/- buttons) — from the slider gfx, else nominal.
    const int sCapL = (haveAssets && assets.SliderCapLeft()  && assets.SliderCapLeft()->width  > 0)
                          ? assets.SliderCapLeft()->width  : 68;
    const int sCapR = (haveAssets && assets.SliderCapRight() && assets.SliderCapRight()->width > 0)
                          ? assets.SliderCapRight()->width : 67;

    // Set a slider row's value from a cursor X over its track (snapped to step) —
    // the engine's drag/click positioning (Slider_UpdateFromMouse @0x420a04).
    const OptGeom ig = OptGeomFor(cfg.page);
    auto trackOriginX = [&]() { return L.ox + ig.win1X + ig.sliderObjX + sCapL; };
    auto setFromTrack = [&](OptionRow& row, int clickX) {
        const int span = row.maxV - row.minV;
        if (span <= 0 || ig.range <= 0) return;
        int rel = clickX - trackOriginX();
        if (rel < 0) rel = 0;
        if (rel > ig.range) rel = ig.range;
        int v = row.minV + (int)((double)rel / (double)ig.range * span + 0.5);
        if (row.step > 0) v = ((v - row.minV + row.step / 2) / row.step) * row.step + row.minV;
        if (v < row.minV) v = row.minV;
        if (v > row.maxV) v = row.maxV;
        row.value = v;
    };
    // The row whose interactive band contains (mx,my), or -1.
    auto rowAt = [&](int mx, int my) -> int {
        for (int i = 0; i < rowCount; ++i) {
            if (rows[i].hidden) continue;
            const int sx = L.ox + ig.win1X + ig.sliderObjX;
            const int sy = L.oy + ig.win1Y + (i < ig.yCount ? ig.yByRow[i] : i * 24);
            if (my < sy - 2 || my >= sy + 21) continue;
            const int x1 = sx + sCapL + ig.range + sCapR + 80;   // caps+track+text band
            if (mx >= sx - 4 && mx < x1) return i;
        }
        return -1;
    };

    for (;;) {
        if (cfg.maxFrames >= 0 && frame >= cfg.maxFrames) break;

        // ---- render ----
        std::fill(scratch.begin(), scratch.end(), 0u);
        gui::MenuRenderTarget tgt = gui::MenuRenderTarget::Wrap(scratch.data(), W, H, W * 4);

        if (haveAssets) {
            // Real _OPTIONEN_PIC background, full-screen.
            DrawBackground(scratch.data(), W, H, assets.background().data(),
                           assets.backgroundWidth(), assets.backgroundHeight());
        } else {
            gui::MenuFillRect(&tgt.surf, 0, 0, W, H, pal.bgR, pal.bgG, pal.bgB);
        }

        // Main-menu options panel, reconstructed from the LIVE original (frida + pixel
        // sampling). The Window_Create reg map is (x@eax,y@edx,w@ebx,h@ecx) — the green
        // title bar slot (w=510) proves w=EBX — so WIN0 is 575x449 (the reimpl GeomFor
        // stores win0W/win0H swapped, hence width=win0H, height=win0W). Elements:
        //   * dark translucent fill   (sampled interior ~(41,40,24))
        //   * tan border frame        (sampled ~(165,146,115))
        //   * green title bar         (frida slot3 x=136,y=128,w=510,h=38; ~(16,50,0))
        //   * centered title text     "Настройки игры" (font 66, ~(255,255,214))
        // In-game (pause) options use the _OPTIONEN_PIC bg instead — skip all this.
        if (haveAssets && !cfg.inGame) {
            // The REAL options panel frame: GREEN MARBLE title bar + ornate gold
            // jeweled-corner border, transparent centre. _TOOL_TIP_GREEN* family (the
            // main-menu counterpart of the in-game _TOOL_TIP_BIG parchment); the game
            // page (11 rows) uses the BIGGER 574x450 variant.
            // ALL three option panels are the SAME size — the form WIN0 is ~574x450
            // for game/gfx/sfx alike (GeomFor: 575x449 / 574x452 / 575x449), which is
            // exactly _TOOL_TIP_GREEN_BIGGER (574x450). The smaller _TOOL_TIP_GREEN
            // (475x301) / _BIG (475x441) are OTHER in-game tooltips, not the options
            // frame — using them made the Sfx/Gfx panel too small (and pushed the
            // OK/Cancel row below the frame). Use the big jeweled green frame for all.
            const char* frameName = "_TOOL_TIP_GREEN_BIGGER";
            const render::DecodedShape* frame = assets.SpriteByName(frameName, 0);
            if (frame && frame->width > 0 && frame->height > 0) {
                const int fw = frame->width, fh = frame->height;
                // Panel is horizontally CENTERED (measured: original green-bar centre ==
                // screen centre); top at win0Y (center-translated). The asset's interior
                // (transparent centre) is x[25..fw-25], below the green bar (~y42) to
                // above the bottom border (~fh-25).
                const int px0 = (W - fw) / 2;
                const int py0 = L.oy + L.g.win0Y;
                // The dark interior must follow the gold border EXACTLY on every side.
                // A per-row "between first/last opaque" fill bleeds below the bottom
                // border because the jeweled corner tips stick out under the bottom gold
                // line. So flood-fill the ENCLOSED transparent region from the centre
                // (bounded by the continuous gold frame + green bar) — that is precisely
                // the interior, with no bleed past any border. (Mask cached per frame
                // pointer; recomputed only if the asset changes.)
                const std::uint32_t* fa = frame->argb.data();
                static const render::DecodedShape* s_maskFor = nullptr;
                static std::vector<unsigned char> s_interior;
                if (s_maskFor != frame) {
                    s_maskFor = frame;
                    s_interior.assign((std::size_t)fw * fh, 0);
                    std::vector<int> stk;
                    const int seed = (fh / 2) * fw + (fw / 2);
                    if (!(fa[seed] & 0xFF000000u)) { s_interior[seed] = 1; stk.push_back(seed); }
                    while (!stk.empty()) {
                        const int p = stk.back(); stk.pop_back();
                        const int x = p % fw, y = p / fw;
                        const int nb[4] = { (x + 1 < fw) ? p + 1 : -1,
                                            (x > 0)      ? p - 1 : -1,
                                            (y + 1 < fh) ? p + fw : -1,
                                            (y > 0)      ? p - fw : -1 };
                        for (int k = 0; k < 4; ++k) {
                            const int q = nb[k];
                            if (q < 0 || s_interior[q]) continue;
                            if (fa[q] & 0xFF000000u) continue;   // gold/green frame = boundary
                            s_interior[q] = 1; stk.push_back(q);
                        }
                    }
                }
                for (int ry = 0; ry < fh; ++ry) {
                    const int dy = py0 + ry; if (dy < 0 || dy >= H) continue;
                    std::uint32_t* drow = scratch.data() + (std::size_t)dy * W;
                    for (int rx = 0; rx < fw; ++rx) {
                        const int dx = px0 + rx; if (dx < 0 || dx >= W) continue;
                        const std::size_t fi = (std::size_t)ry * fw + rx;
                        const std::uint32_t ap = fa[fi];
                        if (ap & 0xFF000000u) {
                            drow[dx] = ap;                       // gold frame / green bar
                        } else if (s_interior[fi]) {             // enclosed interior -> dark
                            const std::uint32_t c = drow[dx];
                            const int r = ((((c >> 16) & 0xFF) * 82) + 22 * 174) >> 8;
                            const int g = ((((c >> 8) & 0xFF) * 82) + 26 * 174) >> 8;
                            const int b = (((c & 0xFF) * 82) + 16 * 174) >> 8;
                            drow[dx] = 0xFF000000u | ((std::uint32_t)r << 16) | ((std::uint32_t)g << 8) | (std::uint32_t)b;
                        }
                    }
                }
                // Group delimiters, rendered 1:1 from the original game page: a 3px
                // horizontal rule (dark / gold / dark) across the enclosed interior, with
                // the centre 4-bead ornament reproduced from the original's EXACT pixels
                // (there is no delimiter sprite in gilde.gfx — the engine draws it). The
                // two delimiters are at frame-y 107 and 243 (screen y 227/363 @800x600).
                // Per-page group-separator Y positions (frame-relative), measured
                // from the original: Game splits {money/speeds | toggles | help},
                // Sfx splits {master | channel volumes | music quality}. Each is the
                // SAME 3px gold rule + 4-bead ornament.
                int delimFy[2] = { -1, -1 }; int nDelim = 0;
                switch (cfg.page) {
                    case OptionsPage::kGame: delimFy[0] = 107; delimFy[1] = 243; nDelim = 2; break;
                    case OptionsPage::kSfx:  delimFy[0] = 131; delimFy[1] = 299; nDelim = 2; break;
                    case OptionsPage::kGfx:  delimFy[0] = 134; delimFy[1] = 278; nDelim = 2; break;
                    default: break;
                }
                if (nDelim > 0) {
                    // The 4-bead ornament, 27x7, captured pixel-for-pixel from the original
                    // (0 == transparent; the dark line passes through it at row index 3).
                    static const int kBeadW = 27, kBeadH = 7;
                    static const std::uint32_t kBeadPx[189] = {
  0,0x080000u,0x080000u,0x080000u,0x524531u,0x4A3C21u,0x423821u,0x423821u,0,0x080000u,0x080000u,0,0,0,0,0,0,0x080000u,0x080000u,0,0x423829u,0,0,0,0x080000u,0x080000u,0x080000u,
  0x100000u,0x84694Au,0xA59273u,0x73614Au,0x080000u,0x423821u,0,0,0x080000u,0x847152u,0x6B5D42u,0x080000u,0,0,0,0,0x080000u,0x6B6139u,0x7B714Au,0x080000u,0,0,0,0x080000u,0x635D31u,0xA59663u,0x84794Au,
  0x847139u,0xCEB67Bu,0xFFFBC6u,0xE7D7A5u,0x84714Au,0x080000u,0x080000u,0x080000u,0x847142u,0xF7E7B5u,0xE7D7A5u,0xA59663u,0x080000u,0x080000u,0x080000u,0x080000u,0xA5966Bu,0xE7D7A5u,0xF7E7B5u,0x847142u,0x080000u,0x080000u,0x080000u,0x7B7152u,0xE7DBA5u,0xFFFBBDu,0xC6B673u,
  0xA59252u,0xFFFBB5u,0xFFFBB5u,0xFFFFC6u,0xE7D3A5u,0xE7D7B5u,0xC6B694u,0xA5966Bu,0x736129u,0xFFFBC6u,0xFFFFC6u,0xE7D7A5u,0xBDB28Cu,0xA59273u,0xA59273u,0xBDB28Cu,0xE7D7A5u,0xFFFFC6u,0xFFFBC6u,0x736129u,0xA5966Bu,0xC6B694u,0xE7D7B5u,0xE7D3A5u,0xFFFFC6u,0xFFFBB5u,0xFFFBB5u,
  0x847539u,0xC6B673u,0xFFFBBDu,0xE7DBA5u,0x7B7152u,0x080000u,0x080000u,0x080000u,0x847142u,0xF7E7B5u,0xE7D7A5u,0xA5966Bu,0x080000u,0x080000u,0x080000u,0x080000u,0xA59663u,0xE7D7A5u,0xF7E7B5u,0x847142u,0x080000u,0x080000u,0x080000u,0x84714Au,0xE7D7A5u,0xFFFBC6u,0xCEB67Bu,
  0x080000u,0x84794Au,0xA59663u,0x635D31u,0x080000u,0,0,0,0x080000u,0x7B714Au,0x6B6139u,0x080000u,0,0,0,0,0x080000u,0x6B5D42u,0x847152u,0x080000u,0,0,0,0x080000u,0x73614Au,0xA59273u,0x84694Au,
  0,0x080000u,0x080000u,0x080000u,0,0,0,0,0,0x080000u,0x080000u,0,0,0,0,0,0,0x080000u,0x080000u,0,0,0,0,0,0x080000u,0x080000u,0x080000u,
                    };
                    const std::uint32_t LINE = 0xFFA59273u, DARK = 0xFF080000u;
                    for (int di = 0; di < nDelim; ++di) {
                        const int fy = delimFy[di];
                        if (fy < 1 || fy + 1 >= fh) continue;
                        // 3px rule (dark / gold / dark), clipped to the interior mask.
                        for (int dyo = -1; dyo <= 1; ++dyo) {
                            const int row = fy + dyo, Y = py0 + row;
                            if (Y < 0 || Y >= H) continue;
                            std::uint32_t* drow = scratch.data() + (std::size_t)Y * W;
                            const std::uint32_t col = (dyo == 0) ? LINE : DARK;
                            for (int rx = 0; rx < fw; ++rx) {
                                if (!s_interior[(std::size_t)row * fw + rx]) continue;
                                const int dx = px0 + rx; if (dx >= 0 && dx < W) drow[dx] = col;
                            }
                        }
                        // 1:1 bead ornament (its line row index 3 aligns to fy).
                        const int bx = px0 + 281;          // original cluster x394 - frame x113
                        const int by = py0 + fy - 3;
                        for (int yy = 0; yy < kBeadH; ++yy) {
                            const int Y = by + yy; if (Y < 0 || Y >= H) continue;
                            std::uint32_t* drow = scratch.data() + (std::size_t)Y * W;
                            for (int xx = 0; xx < kBeadW; ++xx) {
                                const std::uint32_t px = kBeadPx[yy * kBeadW + xx];
                                if (!px) continue;
                                const int X = bx + xx; if (X >= 0 && X < W) drow[X] = px;
                            }
                        }
                    }
                }
                // Title centered on the GREEN MARBLE BAR. The bar occupies frame-y
                // 11..41 in _TOOL_TIP_GREEN_BIGGER (measured from the asset; vertical
                // centre = frame-y 26). Centre the title box on exactly that band so
                // the text is centred on the bar both horizontally (in the frame) and
                // vertically (matches the original, whose ink centre is y~145.5).
                if (haveFont) {
                    const char* tt = !title.empty() ? title.c_str()
                                                     : PageTitleFallback(cfg.page);
                    DrawFontCentered(scratch.data(), W, H, assets.font(), px0, py0 + 11, fw, 31,
                                     tt, 255, 255, 214);
                }
            }
        }

        shim::MouseState ms{};
        plat.getMouse(ms);
        res.hoveredRow = rowAt(ms.x, ms.y);

        if (haveAssets) {
            // ---- 1:1 view: panel + sliders + title + buttons -----
            // The _TOOL_TIP_BIG parchment is the IN-GAME options body; the main-menu
            // options uses the dark WIN0 fill drawn above (no parchment).
            if (cfg.inGame) {
                if (const render::DecodedShape* parch = assets.SpriteByName("_TOOL_TIP_BIG", 0)) {
                    Blit(scratch.data(), W, H, *parch,
                         L.win1ScrX - 24, L.win1ScrY - 24);
                }
                if (const render::DecodedShape* trim = assets.SpriteByName("_TOOL_TIP_BIG", 1)) {
                    Blit(scratch.data(), W, H, *trim, L.win1ScrX + L.g.parchX, L.win1ScrY + L.g.parchY0);
                    Blit(scratch.data(), W, H, *trim, L.win1ScrX + L.g.parchX, L.win1ScrY + L.g.parchY1);
                }
            }

            // Title: the main-menu path draws it in the green title bar above; the
            // in-game (pause) path draws it here in the body band.
            if (cfg.inGame) {
                const std::string& titleStr = !title.empty() ? title : std::string();
                const char* titleC = !titleStr.empty() ? titleStr.c_str() : PageTitleFallback(cfg.page);
                const int titleY = L.oy + L.g.win0Y + 24;
                // Center on the PANEL center, not within win0W: GeomFor stores win0W/win0H
                // swapped (win0W=449 is really the panel HEIGHT), so centering within it
                // put the title ~64px left of center. The panel is screen-centered, so
                // center the title on the framebuffer center.
                if (haveFont)
                    DrawFontCentered(scratch.data(), W, H, assets.font(),
                                     0, titleY, W, 24, titleC, 255, 226, 150);
                else
                    DrawLabel(tgt.surf, W / 2 - 40, titleY, titleC, 255, 226, 150);
            }

            // Rows. EVERY options control is the SAME gold horizontal slider widget
            // (type 'E'), rendered by the 1:1 reconstruction of the engine draw
            // VIBE_Entity_InteractionLogic @0x41078c (gui::RenderHSlider) over the
            // SHAPBANK shape-blit-to-surface primitive gui::GuiSurface (Animation_-
            // Basic/Advanced/Velocity_Apply @0x5d85b8/89bc/883c). A value row (kStep)
            // is flags 0x882 with the number on the thumb; a bool/cycle (kToggle/
            // kCycle) is flags 0x82 with the current option text on the thumb. The
            // gold FILL, end caps (0/5), thumb (3), +/- hover (6/7) and value text all
            // come from RenderHSlider exactly as the binary draws them.
            // Exact slider geometry from Menu/OPTIONS_*.form (the type-69 slider
            // objects in row order, extracted via Form_ParseResourceFile). The body
            // window WIN1 holds the sliders at objX=208; each carries its own objY and
            // a track length (range). Screen pos = center-translate(ox,oy) + WIN1.xy +
            // obj.xy — the engine's PositionAtCoord(mode 2) + AddChildWindow placement.
            // (yByRow is in OptionsRowsFor / GetChildObjectId row order, incl. hidden.)
            const OptGeom og = OptGeomFor(cfg.page);
            const int win1X = og.win1X, win1Y = og.win1Y;
            const int sliderObjX = og.sliderObjX, sliderRange = og.range;
            const int* yByRow = og.yByRow; const int yCount = og.yCount;
            gui::GuiSurface gsurf(&tgt.surf, assets);
            gsurf.SetTextColor(60, 40, 16);   // ambient pen (State_Finalize colour)
            for (int i = 0; i < rowCount && i < L.g.rowCount; ++i) {
                if (rows[i].hidden) continue;
                const bool hov = (i == res.hoveredRow);
                // Slider widget x/y from the real form object; range = form track len.
                const int sliderX = L.ox + win1X + sliderObjX;
                const int sliderY = L.oy + win1Y + (i < yCount ? yByRow[i] : i * 24);
                const int th = 19;   // thumb (shape 3) height band

                // Label (the row caption) — the engine (flags & 0x80) right-aligns it
                // ending 24px left of the slider, at the slider Y (0x41148..0x41115f).
                const char* lbl = !rowLabels[i].empty() ? rowLabels[i].c_str() : rows[i].label;
                const int lblW = haveFont ? assets.font().MeasureWidth(lbl)
                                          : (int)std::strlen(lbl) * 6;
                const int labelX = sliderX - 24 - lblW;
                if (haveFont)
                    DrawFontLeft(scratch.data(), W, H, assets.font(),
                                 labelX, sliderY, th, lbl, hov ? 255 : 235, hov ? 240 : 220, hov ? 180 : 190);
                else
                    DrawLabel(tgt.surf, labelX, sliderY + 4, lbl, hov ? 255 : 220, hov ? 255 : 220, 200);

                // The text drawn ON the slider — the SAME as the original: discrete
                // rows (cycles + 2-option STUFEN) show option `value` of their
                // localized list (_OPTIONEN_STUFEN/_RES/_PANEL/_FREQ/_AN_AUS); numeric
                // rows show "%i". (VIBE_Text_AppendWideLines option lists @0x56cc44.)
                const bool isBool =
                    std::strcmp(rows[i].optList, "_OPTIONEN_STUFEN_AN_AUS") == 0;
                std::string vt;
                if (!rowOpts[i].empty() && rows[i].value >= 0 &&
                    rows[i].value < (int)rowOpts[i].size() && !rowOpts[i][rows[i].value].empty()) {
                    vt = rowOpts[i][rows[i].value];
                } else if (isBool) {
                    const int v = rows[i].value != 0 ? 1 : 0;
                    vt = !anAus[v].empty() ? anAus[v] : (v ? "ON" : "OFF");
                } else {
                    vt = ValueText(rows[i]);
                }

                // Boolean params (the _OPTIONEN_STUFEN_AN_AUS on/off rows) render as a
                // SQUARE TOGGLE from the `_AUSWAHL` asset (gfx #1210): shape 0 is the
                // empty square box (18x18, gold-bordered), shape 3 the small filled
                // square drawn inside when ON; the localized on/off caption sits to its
                // right. (Numeric + option-text rows stay the gold slider.)
                if (isBool && haveAssets) {
                    const render::DecodedShape* box  = assets.SpriteByName("_AUSWAHL", 0);
                    const render::DecodedShape* tick = assets.SpriteByName("_AUSWAHL", 3);
                    const bool on = rows[i].value != 0;
                    if (box && box->width > 0) {
                        const int bx = sliderX;
                        const int by = sliderY + (th - box->height) / 2;
                        BlitDecodedSprite(&tgt.surf, bx, by, *box);
                        if (on && tick && tick->width > 0)
                            BlitDecodedSprite(&tgt.surf,
                                              bx + (box->width  - tick->width)  / 2,
                                              by + (box->height - tick->height) / 2, *tick);
                        // on/off caption to the right of the box.
                        const int cx = bx + box->width + 8;
                        if (haveFont)
                            DrawFontLeft(scratch.data(), W, H, assets.font(),
                                         cx, sliderY, th, vt.c_str(), 230, 220, 180);
                        else
                            DrawLabel(tgt.surf, cx, sliderY + 4, vt.c_str(), 230, 220, 180);
                    } else {
                        // asset-less fallback: a drawn square box, filled when ON.
                        Outline(scratch.data(), W, H, sliderX, sliderY, 18, 18,
                                hov ? 0xFFFFE090u : 0xFFB08C46u);
                        if (on) FillRect(scratch.data(), W, H, sliderX + 3, sliderY + 3, 12, 12, 0xFFE0C060u);
                        DrawLabel(tgt.surf, sliderX + 26, sliderY + 4, vt.c_str(), 230, 220, 180);
                    }
                    continue;   // next row — no slider for bool params
                }

                gui::HSliderWidget wgt;
                wgt.gfxId = 0;                         // -> default record _SLIDER_GOLD_WAAGERECHT
                wgt.x = sliderX; wgt.y = sliderY; wgt.w = sliderRange + 140;
                wgt.value = rows[i].value; wgt.minV = rows[i].minV; wgt.maxV = rows[i].maxV;
                wgt.target = rows[i].value;
                wgt.range = sliderRange;               // form per-slider track length
                wgt.flags = (rows[i].kind == OptionKind::kStep) ? 0x882 : 0x82;
                wgt.hasOptionText = (rows[i].kind != OptionKind::kStep);
                wgt.optionText = wgt.hasOptionText ? vt.c_str() : nullptr;
                wgt.numberText = wgt.hasOptionText ? nullptr : vt.c_str();
                // +/- button hover (shapes 6/7): cursor over the left/right cap.
                wgt.hoverLeft  = hov && (ms.x >= sliderX && ms.x < sliderX + sCapL);
                wgt.hoverRight = hov && (ms.x >= sliderX + sCapL + sliderRange &&
                                         ms.x <  sliderX + sCapL + sliderRange + sCapR);
                if (haveFont)
                    gui::RenderHSlider(gsurf, wgt);
                else {
                    // no-font fallback: a flat bar + value text at the form position.
                    FillRect(scratch.data(), W, H, sliderX, sliderY + th / 2 - 2,
                             sliderRange, 4, 0xFF8C6A2Au);
                    DrawLabel(tgt.surf, sliderX + 4, sliderY + 4, vt.c_str(), 40, 30, 10);
                }
            }

            // OK / Cancel buttons (_BUTTON_RED 3-slice).
            const bool okHov  = HitOk(ms.x, ms.y, L);
            const bool canHov = HitCancel(ms.x, ms.y, L);
            DrawButton3Slice(scratch.data(), W, H, assets, L.okX, L.okY, L.okW, L.okH, okHov);
            DrawButton3Slice(scratch.data(), W, H, assets, L.cancelX, L.cancelY, L.cancelW, L.cancelH, canHov);
            const char* okC  = !okLabel.empty() ? okLabel.c_str() : "OK";
            const char* canC = !cancelLabel.empty() ? cancelLabel.c_str() : "Cancel";
            if (haveFont) {
                DrawFontCentered(scratch.data(), W, H, assets.font(), L.okX, L.okY, L.okW, L.okH, okC, 255, 235, 200);
                DrawFontCentered(scratch.data(), W, H, assets.font(), L.cancelX, L.cancelY, L.cancelW, L.cancelH, canC, 255, 235, 200);
            } else {
                DrawLabel(tgt.surf, L.okX + 12, L.okY + 10, okC, 255, 235, 200);
                DrawLabel(tgt.surf, L.cancelX + 8, L.cancelY + 10, canC, 255, 235, 200);
            }
        } else {
            // ---- asset-less fallback: simple flat panel (headless tests) -----
            const int win0X = L.ox + L.g.win0X, win0Y = L.oy + L.g.win0Y;
            FillRect(scratch.data(), W, H, win0X, win0Y, L.g.win0W, L.g.win0H, 0xFF181828u);
            DrawLabel(tgt.surf, win0X + 12, win0Y + 12, PageTitleFallback(cfg.page), 255, 226, 150);
            for (int i = 0; i < rowCount && i < L.g.rowCount; ++i) {
                if (rows[i].hidden) continue;
                const bool hov = (i == res.hoveredRow);
                int tx, ty, tw, th; RowTrackRect(L, i, &tx, &ty, &tw, &th);
                DrawLabel(tgt.surf, L.labelX, ty + 4, rows[i].label, hov ? 255 : 220, 220, 220);
                FillRect(scratch.data(), W, H, tx, ty + th / 2 - 2, tw, 4, 0xFF445566u);
                const int thumbPx = SliderThumbPx(rows[i].value, rows[i].minV, rows[i].maxV);
                FillRect(scratch.data(), W, H, tx + thumbPx - kSliderThumbW / 2, ty,
                         kSliderThumbW, th, hov ? 0xFFFFFF00u : 0xFFC0A060u);
                const std::string vt = ValueText(rows[i]);
                DrawLabel(tgt.surf, L.valueX, ty + 4, vt.c_str(), 160, 255, 160);
            }
            const bool okHov  = HitOk(ms.x, ms.y, L);
            const bool canHov = HitCancel(ms.x, ms.y, L);
            FillRect(scratch.data(), W, H, L.okX, L.okY, L.okW, L.okH, okHov ? 0xFF384858u : 0xFF283848u);
            FillRect(scratch.data(), W, H, L.cancelX, L.cancelY, L.cancelW, L.cancelH, canHov ? 0xFF384858u : 0xFF283848u);
            if (okHov)  Outline(scratch.data(), W, H, L.okX - 2, L.okY - 2, L.okW + 4, L.okH + 4, 0xFFFFFF00u);
            if (canHov) Outline(scratch.data(), W, H, L.cancelX - 2, L.cancelY - 2, L.cancelW + 4, L.cancelH + 4, 0xFFFFFF00u);
            DrawLabel(tgt.surf, L.okX + 12, L.okY + 10, "OK", 255, 255, 255);
            DrawLabel(tgt.surf, L.cancelX + 8, L.cancelY + 10, "Cancel", 255, 255, 255);
        }

        // ---- mouse cursor: the game's leather-hand pointer (_MOUSE_CURSOR), not
        // the Windows arrow. The main menu hides the OS cursor and blits this each
        // frame; the options screen must do the same or the cursor "changes to the
        // default Windows cursor" on entry. Hotspot (2,1) and the press dip match
        // native_main_menu's cursor model exactly.
        if (const render::DecodedShape* cur = assets.cursor()) {
            const int pdx = ms.left ? -1 : 0;
            const int pdy = ms.left ?  1 : 0;
            BlitDecodedSprite(&tgt.surf, ms.x - 2 + pdx, ms.y - 1 + pdy, *cur);
        }

        MaybeDump(scratch.data(), W, H);
        BlitToDevice(scratch.data(), W, H, device);
        device.present();
        ++res.framesPresented;

        // ---- input (PRESERVED semantics: cancel=ESC, OK=apply, click=actuate) ----
        if (!plat.pumpMessages()) { res.quitByWindow = true; res.cancelled = true; break; }
        plat.getMouse(ms);
        const bool leftEdge = ms.left && !prevLeft;
        prevLeft = ms.left;

        if (plat.keyDown(kVkEscape)) {
            res.quitByEsc = true; res.cancelled = true; break;   // ESC = cancel (discard)
        }

        // Continuous thumb drag: while held, the dragged slider tracks the cursor.
        if (ms.left && dragRow >= 0 && dragRow < rowCount) {
            setFromTrack(rows[dragRow], ms.x);
            res.lastToggledRow = dragRow;
        }
        if (!ms.left) dragRow = -1;

        if (leftEdge) {
            if (HitOk(ms.x, ms.y, L)) {
                res.cancelled = false; break;                    // OK = apply
            }
            if (HitCancel(ms.x, ms.y, L)) {
                res.cancelled = true; break;                     // Cancel = discard
            }
            const int hov = rowAt(ms.x, ms.y);
            if (hov >= 0) {
                OptionRow& row = rows[hov];
                const bool isBool =
                    std::strcmp(row.optList, "_OPTIONEN_STUFEN_AN_AUS") == 0;
                const int sx = L.ox + ig.win1X + ig.sliderObjX;
                if (isBool) {
                    // Square toggle: clicking the box flips the bool.
                    row.value = row.value ? 0 : 1;
                    res.lastToggledRow = hov;
                } else {
                    // Slider: left cap = MINUS (-step), right cap = PLUS (+step),
                    // track = set-by-position + start a drag. All clamped to range.
                    const int trackX0 = sx + sCapL;
                    const int trackX1 = trackX0 + ig.range;
                    if (ms.x >= sx && ms.x < trackX0) {
                        int v = row.value - row.step; if (v < row.minV) v = row.minV;
                        row.value = v;                           // minus button
                    } else if (ms.x >= trackX1 && ms.x < trackX1 + sCapR) {
                        int v = row.value + row.step; if (v > row.maxV) v = row.maxV;
                        row.value = v;                           // plus button
                    } else if (ms.x >= trackX0 && ms.x < trackX1) {
                        setFromTrack(row, ms.x);                 // click on track -> set
                        dragRow = hov;                           // and begin dragging
                    }
                    res.lastToggledRow = hov;
                }
            }
        }

        if (cfg.frameCapMs > 0) plat.sleepMs((std::uint32_t)cfg.frameCapMs);
        ++frame;
    }

    // The seed snapshot the edits are compared against. (PRESERVED.)
    std::vector<OptionRow> seed =
        OptionsRowsFor(cfg.page, bound ? res.gfx : cfg.gfx,
                       bound ? res.sound : cfg.sound,
                       bound ? res.game : cfg.game, caps);

    res.back = true;
    if (!res.cancelled) {
        // ---- OK path: save-back -> persist -> apply (PRESERVED order). ----
        RowsToConfig(cfg.page, rows, res.gfx, res.sound, res.game);
        SettingsBundle out; out.gfx = res.gfx; out.sound = res.sound; out.game = res.game;
        if (bound) res.persisted = SaveSettings(iniFs, cfg.iniName, out);

        OptionsApplySinks& apply = OptionsApplyHooks();
        switch (cfg.page) {
            case OptionsPage::kSfx:
                if (apply.applyVolumeSettings) apply.applyVolumeSettings(res.sound);
                break;
            case OptionsPage::kGfx: {
                if (apply.applyGfxSettings) apply.applyGfxSettings(res.gfx);
                if (!rows[0].hidden && rows[0].value != seed[0].value) {
                    res.resChanged = true;
                    res.gfx.curRes = (guild::u8)rows[0].value;
                    config::ResolutionForIndex(res.gfx.curRes, &res.gfx.resWidth,
                                               &res.gfx.resHeight);
                    out.gfx = res.gfx;
                    if (bound) res.persisted = SaveSettings(iniFs, cfg.iniName, out);
                }
                break;
            }
            case OptionsPage::kGame:
                if (apply.applyCameraScroll) apply.applyCameraScroll(res.game);
                break;
        }
    }
    for (int i = 0; i < rowCount && i < (int)seed.size(); ++i) {
        if (rows[i].value != seed[i].value) { res.changed = true; break; }
    }
    return res;
}

} // namespace guild::play
