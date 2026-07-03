// guild::play — native difficulty screen (VIBE_Menu_ChooseCharacterIntroVariant @0x52e4e0).
// Renders the real `_M0_DIFFICULTY` markup (heading + prompt + the five difficulty rows +
// a back row) and runs the SDL frame loop, mirroring the 1:1 commit logic.
#include "play/sdl_charintro_screen.h"

#include "shim/IGraphicsDevice.h"
#include "shim/IPlatform.h"
#include "shim_impl/disk_filesystem.h"
#include "io/archive_mount.h"
#include "gui/text_load.h"
#include "gui/text/textdb.h"
#include "gui/menu_render.h"
#include "play/menu_assets.h"
#include "play/sdl_city_screen3d.h"   // RenderNewGameDeskBackdrop (shared desk scene)
#include "render/bmp.h"               // BmpLoadBuffer (parchment-form texture)
#include "render/gfx_archive.h"       // render::DecodedShape
#include "render/surface.h"           // SurfaceCreate / SurfaceDestroy (backdrop buffer)
#include "render/text_cp1251.h"
#include "render/types.h"

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>

namespace guild::play {

namespace {
constexpr int kVkEsc = 0x1B;

// Blit a 32bpp scratch to the device backbuffer (any bpp) — the shared screen idiom.
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
                row[x] = (std::uint16_t)((((c >> 16 & 0xFF) >> 3) << 11) |
                                         (((c >> 8 & 0xFF) >> 2) << 5) | ((c & 0xFF) >> 3));
            }
        }
    }
}

void DrawBackground(std::uint32_t* dst, int W, int H, const std::uint32_t* bg, int bw, int bh) {
    if (!bg || bw <= 0 || bh <= 0) return;
    for (int y = 0; y < H; ++y) {
        const int sy = (int)((std::int64_t)y * bh / H);
        for (int x = 0; x < W; ++x) {
            const int sx = (int)((std::int64_t)x * bw / W);
            dst[(std::size_t)y * W + x] = bg[(std::size_t)sy * bw + sx];
        }
    }
}

// _BUTTON_RED 3-slice geometry (gfx 174): cap(12) + stretched centre + cap(12).
constexpr int kBtnCapW = 12;

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

// Draw a full _BUTTON_RED 3-slice (left cap + stretched centre + right cap); `sel`
// swaps to the hover frame triple {3,5,4}. Caps stay native width; centre stretches.
void DrawButton3Slice(std::uint32_t* dst, int W, int H, MenuAssets& a,
                      int rx, int ry, int rw, int rh, bool sel) {
    const render::DecodedShape* L = a.buttonFrame(sel ? 3 : 0);
    const render::DecodedShape* C = a.buttonFrame(sel ? 5 : 2);
    const render::DecodedShape* R = a.buttonFrame(sel ? 4 : 1);
    if (!L || !C || !R) return;
    int lw = kBtnCapW, rcw = kBtnCapW;
    int mw = rw - lw - rcw;
    if (mw < 0) { mw = 0; rcw = rw - lw; if (rcw < 0) { rcw = 0; lw = rw; } }
    ScaledBlit(dst, W, H, *L, rx,           ry, lw,  rh);
    ScaledBlit(dst, W, H, *C, rx + lw,      ry, mw,  rh);
    ScaledBlit(dst, W, H, *R, rx + lw + mw, ry, rcw, rh);
}

// Draw a label centred at (cx, cy) with the real baked-gold _FONT (the main-menu
// button glyphs). y in DrawText is the glyph top, so offset by half the line height.
void DrawMenuFontCentered(std::uint32_t* dst, int W, int H, const MenuFont& fnt,
                          int cx, int cy, const std::string& s, int scale) {
    if (s.empty() || !fnt.loaded()) return;
    const int tw = fnt.MeasureWidth(s.c_str()) * scale;
    const int th = (fnt.lineHeight() > 0 ? fnt.lineHeight() : 17) * scale;
    fnt.DrawText(dst, W, H, cx - tw / 2, cy - th / 2, s.c_str(), scale, 255, 226, 150, false);
}

// Draw a label centred on cx with its glyph TOP at `topY` — for precise multi-line layout.
void DrawMenuFontTopCentered(std::uint32_t* dst, int W, int H, const MenuFont& fnt,
                             int cx, int topY, const std::string& s, int scale) {
    if (s.empty() || !fnt.loaded()) return;
    const int tw = fnt.MeasureWidth(s.c_str()) * scale;
    fnt.DrawText(dst, W, H, cx - tw / 2, topY, s.c_str(), scale, 255, 226, 150, false);
}

// Greedy word-wrap `text` to `maxW` pixels using `fnt` metrics (honours embedded '\n').
std::vector<std::string> WrapText(const MenuFont& fnt, const std::string& text, int maxW) {
    std::vector<std::string> lines;
    std::size_t start = 0;
    while (start <= text.size()) {
        const std::size_t nl = text.find('\n', start);
        const std::string seg = (nl == std::string::npos) ? text.substr(start)
                                                          : text.substr(start, nl - start);
        std::string line;
        std::size_t i = 0;
        while (i < seg.size()) {
            const std::size_t sp = seg.find(' ', i);
            const std::string word = (sp == std::string::npos) ? seg.substr(i) : seg.substr(i, sp - i);
            const std::string cand = line.empty() ? word : line + " " + word;
            if (line.empty() || fnt.MeasureWidth(cand.c_str()) <= maxW) line = cand;
            else { lines.push_back(line); line = word; }
            if (sp == std::string::npos) break;
            i = sp + 1;
        }
        lines.push_back(line);
        if (nl == std::string::npos) break;
        start = nl + 1;
    }
    return lines;
}

// The parchment titles use _FONT+2 (gfx record 68) — a NATIVELY BLACK thin gothic (the
// _FONT bold gold variant record 66 is for the red buttons). Same font the network /
// New-Game parchment forms title with. Cached per game dir.
const MenuFont* TitleFont(const std::string& gameDir) {
    static std::string s_dir;
    static MenuFont s_font;
    static bool s_tried = false;
    if (s_tried && s_dir == gameDir) return s_font.loaded() ? &s_font : nullptr;
    s_tried = true; s_dir = gameDir;
    if (gameDir.empty()) return nullptr;
    shim::DiskFileSystem fs(gameDir);
    s_font.Load(fs, "gfx/gilde.gfx", "_FONT+2");
    return s_font.loaded() ? &s_font : nullptr;
}

// The difficulty form's parchment sheet is the same burnt-edge texture the network /
// New-Game forms use — Pergamente/Pergament_Blatt_2_256_rev.bmp (256x256) from
// Resources/Textures.BIN, NOT a gilde.gfx record. Black (0,0,0) is the transparent
// color key (the torn corners), so the desk shows through. Decoded once, cached per dir.
const render::DecodedShape* FormParchment(const std::string& gameDir) {
    static std::string s_dir;
    static render::DecodedShape s_shape;
    static bool s_tried = false;
    if (s_tried && s_dir == gameDir) return s_shape.width > 0 ? &s_shape : nullptr;
    s_tried = true; s_dir = gameDir; s_shape = render::DecodedShape{};
    if (gameDir.empty()) return nullptr;
    shim::DiskFileSystem fs(gameDir);
    io::ArchiveMount mount;
    if (!mount.Mount(&fs, "Resources/Textures.BIN", /*caseInsensitive=*/true)) return nullptr;
    std::vector<u8> bytes;
    if (!mount.OpenMember("Pergamente/Pergament_Blatt_2_256_rev.bmp", bytes) || bytes.empty())
        return nullptr;
    int w = 0, h = 0;
    std::vector<u8> rgb = render::BmpLoadBuffer(bytes, 24, w, h);
    if (rgb.empty() || w <= 0 || h <= 0) return nullptr;
    s_shape.width = w; s_shape.height = h;
    s_shape.argb.resize((std::size_t)w * h);
    for (std::size_t i = 0; i < (std::size_t)w * h; ++i) {
        const u8 r = rgb[i*3], g = rgb[i*3+1], b = rgb[i*3+2];
        if (r == 0 && g == 0 && b == 0) { s_shape.argb[i] = 0u; continue; }
        s_shape.argb[i] = 0xFF000000u | ((u32)r << 16) | ((u32)g << 8) | (u32)b;
    }
    return &s_shape;
}
} // namespace

// ---- markup parser ---------------------------------------------------------
CharIntroContent ParseDifficultyMarkup(const std::string& s) {
    CharIntroContent c;
    std::string prompt;
    const std::size_t n = s.size();
    std::size_t i = 0;
    bool headingDone = false, seenOption = false;
    while (i < n) {
        const char ch = s[i];
        if (ch == '$') {
            if (i + 1 < n && s[i + 1] == '[') {                 // $[ heading ]
                i += 2;
                std::string h;
                while (i < n && !(s[i] == '$' && i + 1 < n && s[i + 1] == ']')) { h += s[i]; ++i; }
                if (i + 1 < n) i += 2;                           // skip "$]"
                c.heading = h; headingDone = true;
                continue;
            }
            // `$N` is a newline (paragraph break) in the rich-text markup — preserve it
            // as a real line break so the body keeps the original's sparse layout.
            if (i + 1 < n && (s[i + 1] == 'N' || s[i + 1] == 'n')) {
                if (headingDone && !seenOption) prompt += '\n';
                i += 2;
                continue;
            }
            i += 2;                                             // skip any other "$X" control token
            continue;
        }
        if (ch == '%' && i + 2 < n && s[i + 1] == 'i') {        // %i<sel>[label]
            const char sel = s[i + 2];
            std::size_t j = i + 3;
            if (j < n && s[j] == '[') {
                ++j;
                std::string lab;
                while (j < n && s[j] != ']') { lab += s[j]; ++j; }
                if (j < n) ++j;                                 // skip "]"
                c.options.push_back(lab);
                c.selectable.push_back(sel == 'a');
                seenOption = true;
                i = j;
                continue;
            }
            ++i;
            continue;
        }
        if (headingDone && !seenOption) prompt += ch;           // prompt = heading..first option
        ++i;
    }
    // trim the prompt
    std::size_t a = prompt.find_first_not_of(" \t\r\n");
    std::size_t b = prompt.find_last_not_of(" \t\r\n");
    c.prompt = (a == std::string::npos) ? std::string() : prompt.substr(a, b - a + 1);
    return c;
}

// ---- content load ----------------------------------------------------------
bool LoadCharIntroContent(const std::string& gameDir, CharIntroContent& out) {
    if (gameDir.empty()) return false;
    shim::DiskFileSystem fs(gameDir);
    const char* arch = "Resources/textbin_deutsch.BIN";
    if (!fs.exists(arch)) return false;
    io::ArchiveMount mount;
    if (!mount.Mount(&fs, arch, /*caseInsensitive=*/true)) return false;
    gui::text::TextDb db;
    for (const io::ArchiveMember& m : mount.members()) {
        const std::string& n = m.name;
        if (n.size() < 4) continue;
        std::string ext = n.substr(n.size() - 4);
        for (auto& ch : ext) ch = (char)std::tolower((unsigned char)ch);
        if (ext != ".res") continue;
        std::vector<u8> bytes;
        if (!mount.OpenMember(n.c_str(), bytes) || bytes.empty()) continue;
        gui::text::BuildTextArray(bytes.data(), bytes.size(), db);
    }
    const int idx = db.FindIndex("_M0_DIFFICULTY+0");   // by NAME -> index-order-independent
    if (idx < 0) return false;
    const char* markup = db.Text(idx);
    if (!markup || !*markup) return false;
    out = ParseDifficultyMarkup(markup);
    return !out.options.empty();
}

// ---- layout ----------------------------------------------------------------
void CharIntroLayout::RowRect(int i, int& rx, int& ry, int& rw, int& rh) const {
    rx = rowX; ry = rowsY0 + i * rowH; rw = rowW; rh = btnH;
}
int CharIntroLayout::HitRow(int mx, int my) const {
    for (int i = 0; i < rowCount; ++i) {
        int rx, ry, rw, rh; RowRect(i, rx, ry, rw, rh);
        if (mx >= rx && mx < rx + rw && my >= ry && my < ry + rh) return i;
    }
    return -1;
}
CharIntroLayout CharIntroComputeLayout(int W, int H, int rowCount) {
    // Ground truth from gilde.exe @800x600 (frida Window_Create form (128,72,490,441),
    // center-translated -> x=(800-490)/2=155, plus the measured red _BUTTON_RED column):
    // the screen is the shared New-Game desk scene with a 490x441 parchment FORM centred
    // over the map. Title/prompt are gold gothic centred; the 6 red buttons are a fixed
    // 138x22 column at screen centre, first top y=179, 40px pitch.
    CharIntroLayout L;
    L.rowCount = rowCount;
    L.pw = 490 * W / 800; L.ph = 441 * H / 600;  // parchment form size
    L.px = (W - L.pw) / 2; L.py = 72 * H / 600;  // center-translated form position
    L.cx = W / 2;                                 // buttons + titles centre on screen
    L.titleY  = 102 * H / 600;                    // "Уровень сложности" centre-y
    L.promptY = 146 * H / 600;                    // prompt sentence centre-y
    L.rowW    = 158 * W / 800;                    // full button width incl gold caps (measured 321..478)
    L.btnH    = 33  * H / 600;                    // native _BUTTON_RED height (measured 174..206)
    L.rowsY0  = 174 * H / 600;                    // top of button 0 (centre y=190, pitch 40)
    L.rowH    = 40  * H / 600;                    // row pitch
    L.rowX    = L.cx - L.rowW / 2;
    return L;
}

// ---- one frame -------------------------------------------------------------
void RenderCharIntroFrame(std::uint32_t* scratch, int W, int H, const CharIntroContent& content,
                          int hoveredRow, int seedVariant, MenuAssets* assets,
                          const std::uint32_t* backdrop, const std::string& gameDir) {
    (void)seedVariant;   // the radio pre-selection is not drawn as a distinct frame
    const int rowCount = (int)content.options.size();
    const CharIntroLayout L = CharIntroComputeLayout(W, H, rowCount);
    gui::MenuRenderTarget tgt = gui::MenuRenderTarget::Wrap(scratch, W, H, W * 4);

    const bool art = assets && assets->loaded();
    // Background: the pre-rendered New-Game desk scene if supplied, else the menu
    // backdrop / a flat fill (asset-less / headless).
    if (backdrop) {
        std::memcpy(scratch, backdrop, (std::size_t)W * H * 4);
    } else if (art) {
        DrawBackground(scratch, W, H, assets->background().data(),
                       assets->backgroundWidth(), assets->backgroundHeight());
    } else {
        std::fill(scratch, scratch + (std::size_t)W * H, 0u);
        const gui::MenuPalette pal;
        gui::MenuFillRect(&tgt.surf, 0, 0, W, H, pal.bgR, pal.bgG, pal.bgB);
    }

    // The parchment FORM: the burnt-edge sheet stretched to the center-translated form
    // rect, covering the desk's map (its torn corners are color-keyed so the desk shows).
    if (const render::DecodedShape* parch = FormParchment(gameDir))
        ScaledBlit(scratch, W, H, *parch, L.px, L.py, L.pw, L.ph);

    // Title + prompt: the NATIVELY-BLACK thin gothic _FONT+2 (the same parchment-title
    // font the network/New-Game forms use), centred on screen — NOT the gold button font.
    const int scale = 1;
    const MenuFont* titleFont = TitleFont(gameDir);
    if (titleFont) {
        DrawMenuFontCentered(scratch, W, H, *titleFont, L.cx, L.titleY,  content.heading, scale);
        DrawMenuFontCentered(scratch, W, H, *titleFont, L.cx, L.promptY, content.prompt,  scale);
    } else if (art) {
        const MenuFont& fnt = assets->font();
        DrawMenuFontCentered(scratch, W, H, fnt, L.cx, L.titleY,  content.heading, scale);
        DrawMenuFontCentered(scratch, W, H, fnt, L.cx, L.promptY, content.prompt,  scale);
    } else {
        // Asset-less fallback (CP1251 bitmap font) so headless still shows the text.
        const int tw0 = (int)content.heading.size() * 6;
        render::DrawTextCp1251(&tgt.surf, L.cx - tw0 / 2, L.titleY - 6, content.heading.c_str(), 255, 226, 150);
        const int tw1 = (int)content.prompt.size() * 6;
        render::DrawTextCp1251(&tgt.surf, L.cx - tw1 / 2, L.promptY - 6, content.prompt.c_str(), 220, 210, 190);
    }

    // The radio-of-6: real red _BUTTON_RED 3-slice bars with gold _FONT labels.
    for (int i = 0; i < rowCount; ++i) {
        int rx, ry, rw, rh; L.RowRect(i, rx, ry, rw, rh);
        const bool hov = (i == hoveredRow);
        if (art) {
            DrawButton3Slice(scratch, W, H, *assets, rx, ry, rw, rh, hov);
            DrawMenuFontCentered(scratch, W, H, assets->font(), L.cx, ry + rh / 2,
                                 content.options[i], scale);
        } else {
            gui::MenuFillRect(&tgt.surf, rx, ry, rw, rh, hov ? 150 : 110, hov ? 40 : 28, 16);
            const int tw = (int)content.options[i].size() * 6;
            render::DrawTextCp1251(&tgt.surf, L.cx - tw / 2, ry + rh / 2 - 6,
                                   content.options[i].c_str(), 255, 226, 150);
        }
    }
}

// ---- the screen ------------------------------------------------------------
CharIntroResult RunCharIntroScreen(shim::IGraphicsDevice& device, shim::IPlatform& plat,
                                   const CharIntroConfig& cfg) {
    CharIntroResult res;
    res.variant = cfg.seedVariant;

    const int W = cfg.fbW, H = cfg.fbH;
    std::vector<std::uint32_t> scratch((std::size_t)W * H, 0u);

    // Content: real markup when assets present; English fallback otherwise.
    CharIntroContent content;
    res.usedRealText = LoadCharIntroContent(cfg.gameDir, content);
    if (!res.usedRealText) {
        content.heading = "Difficulty";
        content.prompt  = "Please choose the difficulty level.";
        content.options = {"very easy", "easy", "normal", "hard", "very hard", "back"};
        content.selectable = {true, true, true, true, true, false};
    }
    const int rowCount = (int)content.options.size();
    const CharIntroLayout L = CharIntroComputeLayout(W, H, rowCount);

    shim::DiskFileSystem assetFs(cfg.gameDir);
    MenuAssets assets;
    const bool haveAssets = !cfg.gameDir.empty() && assets.Load(assetFs);

    // Render the shared New-Game desk scene ONCE (settled A2 camera) into a backdrop
    // buffer; the parchment form + buttons composite over it each frame.
    std::vector<std::uint32_t> backdrop;
    bool haveBackdrop = false;
    if (render::Surface* bg = render::SurfaceCreate(W, H, 32)) {
        if (RenderNewGameDeskBackdrop(device, cfg.gameDir, W, H, bg)) {
            backdrop.resize((std::size_t)W * H);
            for (int y = 0; y < H; ++y) {
                const auto* s = reinterpret_cast<const std::uint32_t*>(
                    static_cast<const std::uint8_t*>(bg->pixels) + (std::size_t)y * bg->pitch);
                std::memcpy(backdrop.data() + (std::size_t)y * W, s, (std::size_t)W * 4);
            }
            haveBackdrop = true;
        }
        render::SurfaceDestroy(bg);
    }

    int frame = 0;
    bool prevLeft = false;
    for (;;) {
        if (cfg.maxFrames >= 0 && frame >= cfg.maxFrames) break;

        shim::MouseState ms{};
        plat.getMouse(ms);
        res.hoveredRow = L.HitRow(ms.x, ms.y);

        RenderCharIntroFrame(scratch.data(), W, H, content, res.hoveredRow, res.variant,
                             haveAssets ? &assets : nullptr,
                             haveBackdrop ? backdrop.data() : nullptr, cfg.gameDir);
        BlitToDevice(scratch.data(), W, H, device);
        device.present();
        ++res.framesPresented;

        // ---- input ----
        if (!plat.pumpMessages()) { res.quitByWindow = true; res.back = true; break; }
        plat.getMouse(ms);
        const bool leftEdge = ms.left && !prevLeft;
        prevLeft = ms.left;

        if (plat.keyDown(kVkEsc)) { res.back = true; break; }   // ESC -> cancel
        if (leftEdge) {
            const int hov = L.HitRow(ms.x, ms.y);
            if (hov >= 0) {
                const bool selectable = content.selectable.size() > (std::size_t)hov
                                        && content.selectable[hov];
                if (selectable) { res.confirmed = true; res.variant = hov; break; }  // commit difficulty
                else            { res.back = true; break; }                          // back row -> cancel
            }
        }

        if (cfg.frameCapMs > 0) plat.sleepMs((std::uint32_t)cfg.frameCapMs);
        ++frame;
    }
    return res;
}

// Shared New-Game parchment chrome (used by history/tasks/player screens): composite the
// backdrop, draw the parchment sheet to the form rect, then an optional black _FONT+2 title
// + wrapped body. Page-specific widgets (inputs / rows / buttons) are drawn by the caller.
void RenderNewGameParchmentChrome(std::uint32_t* scratch, int W, int H,
                                  const std::uint32_t* backdrop, MenuAssets* assets,
                                  const std::string& gameDir, int parchTopDesign, int parchHDesign,
                                  int parchWDesign,
                                  const std::string& title, int titleCyDesign,
                                  const std::string& body, int bodyTopDesign,
                                  int bodyLineHDesign, int bodyWrapWDesign) {
    const bool art = assets && assets->loaded();
    gui::MenuRenderTarget tgt = gui::MenuRenderTarget::Wrap(scratch, W, H, W * 4);
    if (backdrop) {
        std::memcpy(scratch, backdrop, (std::size_t)W * H * 4);
    } else if (art) {
        DrawBackground(scratch, W, H, assets->background().data(),
                       assets->backgroundWidth(), assets->backgroundHeight());
    } else {
        std::fill(scratch, scratch + (std::size_t)W * H, 0u);
        const gui::MenuPalette pal; gui::MenuFillRect(&tgt.surf, 0, 0, W, H, pal.bgR, pal.bgG, pal.bgB);
    }
    const int pw = parchWDesign * W / 800, ph = parchHDesign * H / 600;
    const int px = (W - pw) / 2, py = parchTopDesign * H / 600;
    if (const render::DecodedShape* parch = FormParchment(gameDir))
        ScaledBlit(scratch, W, H, *parch, px, py, pw, ph);
    const int cx = W / 2, scale = 1;
    const MenuFont* tf = TitleFont(gameDir);
    if (tf && !title.empty())
        DrawMenuFontCentered(scratch, W, H, *tf, cx, titleCyDesign * H / 600, title, scale);
    if (tf && !body.empty()) {
        const std::vector<std::string> lines = WrapText(*tf, body, bodyWrapWDesign * W / 800);
        int ty = bodyTopDesign * H / 600;
        const int lh = bodyLineHDesign * H / 600;
        for (const std::string& ln : lines) { DrawMenuFontTopCentered(scratch, W, H, *tf, cx, ty, ln, scale); ty += lh; }
    }
}

// Draw one red _BUTTON_RED 3-slice bar with a centred gold _FONT label (the shared
// New-Game button look), for callers that place a single button (e.g. the player wizard's
// "Назад"). No-op if assets are absent.
void DrawNewGameButton(std::uint32_t* dst, int W, int H, MenuAssets& assets,
                       int x, int y, int w, int h, bool hover, const std::string& label) {
    if (!assets.loaded()) return;
    DrawButton3Slice(dst, W, H, assets, x, y, w, h, hover);
    DrawMenuFontCentered(dst, W, H, assets.font(), x + w / 2, y + h / 2, label, 1);
}

// Expose the parchment title font (_FONT+2, natively black) so other New-Game screens
// (the player wizard's input field) can render matching black gothic text.
const MenuFont* NewGameTitleFont(const std::string& gameDir) { return TitleFont(gameDir); }

// ---- choose-history screen -------------------------------------------------
// Ground truth (gilde.exe @800x600, measured): same desk + parchment form (155,72,490,441)
// as the difficulty screen, a black _FONT+2 title (centre y≈91) + a multi-line wrapped
// body paragraph (7 lines, first top y≈131, pitch 26), and N per-label red buttons
// centred on screen at cy = 340 + 40*i (top y≈324), height 33.
std::vector<NewGameButtonRect>
ChooseHistoryButtonRects(int W, int H, const CharIntroContent& content, MenuAssets* assets,
                         int btnTop0Design) {
    std::vector<NewGameButtonRect> out;
    const int cx = W / 2;
    const int btnH  = 33  * H / 600;
    const int top0  = btnTop0Design * H / 600;
    const int pitch = 40  * H / 600;
    const bool art = assets && assets->loaded();
    // The radio group equalizes every button to the WIDEST label's width (uniform bars,
    // text centred) — matching gilde.exe (and the difficulty screen). A little extra
    // padding so the bars read as wide as the original's (measured ~318px design).
    constexpr int kExtraPad = 28;
    int designW = 0;
    for (const std::string& opt : content.options) {
        const int w = art ? assets->font().ButtonWidth(opt.c_str())
                          : (int)opt.size() * 8 + 28;
        if (w > designW) designW = w;
    }
    const int w = (designW + kExtraPad) * W / 800;
    for (std::size_t i = 0; i < content.options.size(); ++i) {
        NewGameButtonRect r;
        r.w = w; r.h = btnH; r.x = cx - w / 2; r.y = top0 + (int)i * pitch;
        out.push_back(r);
    }
    return out;
}

int ChooseHistoryHitRow(const std::vector<NewGameButtonRect>& rects, int mx, int my) {
    for (std::size_t i = 0; i < rects.size(); ++i) {
        const NewGameButtonRect& r = rects[i];
        if (mx >= r.x && mx < r.x + r.w && my >= r.y && my < r.y + r.h) return (int)i;
    }
    return -1;
}

void RenderChooseHistoryFrame(std::uint32_t* scratch, int W, int H,
                              const CharIntroContent& content, int hoveredRow, int seedRow,
                              MenuAssets* assets, const std::uint32_t* backdrop,
                              const std::string& gameDir, int btnTop0Design) {
    (void)seedRow;
    const int cx = W / 2, scale = 1;
    // The history/tasks parchment is taller than the difficulty form so its longer button
    // column sits ON the sheet. Keep the form top at the frida y=72 (so the sheet reaches
    // high above the title like the original) and enlarge the height to ph=458 so the burnt
    // texture's opaque region lands at y≈76..519 — matching the measured original (≈82..521).
    const int pw = 490 * W / 800, ph = 458 * H / 600;
    const int px = (W - pw) / 2, py = 72 * H / 600;
    gui::MenuRenderTarget tgt = gui::MenuRenderTarget::Wrap(scratch, W, H, W * 4);

    const bool art = assets && assets->loaded();
    if (backdrop) {
        std::memcpy(scratch, backdrop, (std::size_t)W * H * 4);
    } else if (art) {
        DrawBackground(scratch, W, H, assets->background().data(),
                       assets->backgroundWidth(), assets->backgroundHeight());
    } else {
        std::fill(scratch, scratch + (std::size_t)W * H, 0u);
        const gui::MenuPalette pal;
        gui::MenuFillRect(&tgt.surf, 0, 0, W, H, pal.bgR, pal.bgG, pal.bgB);
    }

    if (const render::DecodedShape* parch = FormParchment(gameDir))
        ScaledBlit(scratch, W, H, *parch, px, py, pw, ph);

    // Title + wrapped body: the natively-black thin gothic _FONT+2.
    const MenuFont* tf = TitleFont(gameDir);
    if (tf) {
        DrawMenuFontCentered(scratch, W, H, *tf, cx, 102 * H / 600, content.heading, scale);
        const int wrapW = pw - 50 * W / 800;       // ~25px parchment margin each side
        const std::vector<std::string> lines = WrapText(*tf, content.prompt, wrapW);
        int topY = 124 * H / 600;
        const int lineH = 26 * H / 600;
        for (const std::string& ln : lines) {
            DrawMenuFontTopCentered(scratch, W, H, *tf, cx, topY, ln, scale);
            topY += lineH;
        }
    } else {
        const int tw0 = (int)content.heading.size() * 6;
        render::DrawTextCp1251(&tgt.surf, cx - tw0 / 2, 90 * H / 600, content.heading.c_str(), 30, 22, 14);
    }

    // The N equal-width red _BUTTON_RED bars with gold _FONT labels.
    const std::vector<NewGameButtonRect> rects =
        ChooseHistoryButtonRects(W, H, content, assets, btnTop0Design);
    for (std::size_t i = 0; i < rects.size(); ++i) {
        const NewGameButtonRect& r = rects[i];
        const bool hov = ((int)i == hoveredRow);
        if (art) {
            DrawButton3Slice(scratch, W, H, *assets, r.x, r.y, r.w, r.h, hov);
            DrawMenuFontCentered(scratch, W, H, assets->font(), cx, r.y + r.h / 2,
                                 content.options[i], scale);
        } else {
            gui::MenuFillRect(&tgt.surf, r.x, r.y, r.w, r.h, hov ? 150 : 110, hov ? 40 : 28, 16);
            const int tw = (int)content.options[i].size() * 6;
            render::DrawTextCp1251(&tgt.surf, cx - tw / 2, r.y + r.h / 2 - 6,
                                   content.options[i].c_str(), 255, 226, 150);
        }
    }
}

} // namespace guild::play
