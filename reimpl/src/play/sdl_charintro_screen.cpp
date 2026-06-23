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

void Outline(std::uint32_t* px, int W, int H, int x, int y, int w, int h, std::uint32_t c) {
    auto put = [&](int X, int Y){ if (X >= 0 && X < W && Y >= 0 && Y < H) px[(std::size_t)Y * W + X] = c; };
    for (int X = x; X < x + w; ++X) { put(X, y); put(X, y + h - 1); }
    for (int Y = y; Y < y + h; ++Y) { put(x, Y); put(x + w - 1, Y); }
}

int TextWidthCp1251(const std::string& s) { return (int)s.size() * 6; }  // 6px advance/char

void DrawCenteredCp1251(render::Surface* s, int cx, int y, const std::string& t, int r, int g, int b) {
    render::DrawTextCp1251(s, cx - TextWidthCp1251(t) / 2, y, t.c_str(), (guild::u8)r, (guild::u8)g, (guild::u8)b);
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
            i += 2;                                             // skip a "$X" control token
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
    rx = rowX; ry = rowsY0 + i * rowH; rw = rowW; rh = rowH - 8;
}
int CharIntroLayout::HitRow(int mx, int my) const {
    for (int i = 0; i < rowCount; ++i) {
        int rx, ry, rw, rh; RowRect(i, rx, ry, rw, rh);
        if (mx >= rx && mx < rx + rw && my >= ry && my < ry + rh) return i;
    }
    return -1;
}
CharIntroLayout CharIntroComputeLayout(int W, int H, int rowCount) {
    CharIntroLayout L;
    L.rowCount = rowCount;
    L.px = 128 * W / 800; L.py = 72 * H / 600;
    L.pw = 441 * W / 800; L.ph = 490 * H / 600;
    L.cx = L.px + L.pw / 2;
    L.titleY  = L.py + 24;
    L.promptY = L.py + 56;
    L.rowsY0  = L.py + 96;
    L.rowH    = (L.ph - 120) / (rowCount > 0 ? rowCount : 1);
    L.rowW    = L.pw - 60;
    L.rowX    = L.px + 30;
    return L;
}

// ---- one frame -------------------------------------------------------------
void RenderCharIntroFrame(std::uint32_t* scratch, int W, int H, const CharIntroContent& content,
                          int hoveredRow, int seedVariant, MenuAssets* assets) {
    const int rowCount = (int)content.options.size();
    const CharIntroLayout L = CharIntroComputeLayout(W, H, rowCount);
    std::fill(scratch, scratch + (std::size_t)W * H, 0u);
    gui::MenuRenderTarget tgt = gui::MenuRenderTarget::Wrap(scratch, W, H, W * 4);
    if (assets && assets->loaded())
        DrawBackground(scratch, W, H, assets->background().data(),
                       assets->backgroundWidth(), assets->backgroundHeight());
    else {
        const gui::MenuPalette pal;
        gui::MenuFillRect(&tgt.surf, 0, 0, W, H, pal.bgR, pal.bgG, pal.bgB);
    }
    // Parchment-style panel plate + border (the form window backing).
    gui::MenuFillRect(&tgt.surf, L.px, L.py, L.pw, L.ph, 34, 26, 16);
    Outline(scratch, W, H, L.px, L.py, L.pw, L.ph, 0xFFB08C46u);

    DrawCenteredCp1251(&tgt.surf, L.cx, L.titleY, content.heading, 255, 226, 150);
    DrawCenteredCp1251(&tgt.surf, L.cx, L.promptY, content.prompt, 220, 210, 190);

    for (int i = 0; i < rowCount; ++i) {
        int rx, ry, rw, rh; L.RowRect(i, rx, ry, rw, rh);
        const bool hov  = (i == hoveredRow);
        const bool seed = content.selectable.size() > (std::size_t)i && content.selectable[i]
                          && (i == seedVariant);
        if (seed)      gui::MenuFillRect(&tgt.surf, rx, ry, rw, rh, 90, 60, 28);
        else if (hov)  gui::MenuFillRect(&tgt.surf, rx, ry, rw, rh, 64, 50, 30);
        else           gui::MenuFillRect(&tgt.surf, rx, ry, rw, rh, 44, 36, 24);
        Outline(scratch, W, H, rx, ry, rw, rh, (hov || seed) ? 0xFFFFD060u : 0xFF6A5630u);
        const int tr = (hov || seed) ? 255 : 226, tg = (hov || seed) ? 240 : 210,
                  tb = (hov || seed) ? 170 : 180;
        DrawCenteredCp1251(&tgt.surf, L.cx, ry + rh / 2 - 3, content.options[i], tr, tg, tb);
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

    int frame = 0;
    bool prevLeft = false;
    for (;;) {
        if (cfg.maxFrames >= 0 && frame >= cfg.maxFrames) break;

        shim::MouseState ms{};
        plat.getMouse(ms);
        res.hoveredRow = L.HitRow(ms.x, ms.y);

        RenderCharIntroFrame(scratch.data(), W, H, content, res.hoveredRow, res.variant,
                             haveAssets ? &assets : nullptr);
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

} // namespace guild::play
