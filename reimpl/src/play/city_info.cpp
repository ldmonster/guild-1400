// guild::play — ChooseCity info window (real per-city card). See city_info.h.
#include "play/city_info.h"

#include "gui/text_load.h"           // gui::text::BuildTextArray
#include "io/archive_mount.h"
#include "render/surface.h"
#include "render/text_cp1251.h"
#include "render/types.h"
#include "shim/IFileSystem.h"

#include <cctype>
#include <cstdint>
#include <string>

namespace guild::play {
namespace {

std::string Upper(std::string s) {
    for (auto& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}

inline void PutPx(render::Surface* s, int x, int y, u8 r, u8 g, u8 b) {
    const int W = s->widthPx ? s->widthPx : s->width;
    if (x < 0 || y < 0 || x >= W || y >= s->height) return;
    if (s->bpp == 32) {
        auto* px = reinterpret_cast<std::uint32_t*>(
            static_cast<std::uint8_t*>(s->pixels) + (std::size_t)y * s->pitch) + x;
        *px = 0xFF000000u | ((std::uint32_t)r << 16) | ((std::uint32_t)g << 8) | b;
    } else {
        render::SurfaceSetPixelRgb(s, x, y, r, g, b);
    }
}
void FillRect(render::Surface* s, int x, int y, int w, int h, u8 r, u8 g, u8 b) {
    for (int j = 0; j < h; ++j) for (int i = 0; i < w; ++i) PutPx(s, x + i, y + j, r, g, b);
}
void FrameRect(render::Surface* s, int x, int y, int w, int h, u8 r, u8 g, u8 b) {
    for (int i = 0; i < w; ++i) { PutPx(s, x + i, y, r, g, b); PutPx(s, x + i, y + h - 1, r, g, b); }
    for (int j = 0; j < h; ++j) { PutPx(s, x, y + j, r, g, b); PutPx(s, x + w - 1, y + j, r, g, b); }
}

} // namespace

bool CityInfoText::Load(shim::IFileSystem* fs, const char* archive) {
    if (loaded_) return true;
    if (!fs || !fs->exists(archive)) return false;
    io::ArchiveMount mount;
    if (!mount.Mount(fs, archive, /*caseInsensitive=*/true)) return false;
    // Every ".res" member tiles the global text array by its declared baseIndex
    // (BuildTextArray), exactly as the engine loads the localized DB at boot.
    for (const io::ArchiveMember& m : mount.members()) {
        const std::string& n = m.name;
        if (n.size() < 4) continue;
        std::string ext = n.substr(n.size() - 4);
        for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
        if (ext != ".res") continue;
        std::vector<u8> bytes;
        if (!mount.OpenMember(n.c_str(), bytes) || bytes.empty()) continue;
        gui::text::BuildTextArray(bytes.data(), bytes.size(), db_);
    }
    loaded_ = db_.Count() > 0;
    return loaded_;
}

std::string CityInfoText::Beschr(const std::string& city) const {
    const std::string key = "_STADTAUSWAHL_" + Upper(city) + "_BESCHR+0";
    int i = db_.FindIndex(key.c_str());
    return (i >= 0) ? db_.Text(i) : std::string();
}

std::string CityInfoText::Info(const std::string& city) const {
    const std::string key = "_STADTAUSWAHL_" + Upper(city) + "_INFO+0";
    int i = db_.FindIndex(key.c_str());
    return (i >= 0) ? db_.Text(i) : std::string();
}

// ---- real gilde.gfx art -----------------------------------------------------
bool CityInfoGfx::Load(shim::IFileSystem* fs, const char* archive) {
    if (loaded_) return true;
    if (!fs || !fs->exists(archive)) return false;
    loaded_ = arc_.LoadFromFile(*fs, archive) && arc_.ok();
    return loaded_;
}
const render::DecodedShape* CityInfoGfx::Decode(const std::string& name, int shape) {
    auto key = name + "#" + std::to_string(shape);
    auto it = cache_.find(key);
    if (it != cache_.end()) return it->second.get();
    auto out = std::make_unique<render::DecodedShape>();
    if (!loaded_ || !arc_.DecodeShapeByName(name.c_str(), shape, *out) || out->width <= 0) {
        cache_[key] = nullptr; return nullptr;
    }
    const render::DecodedShape* p = out.get();
    cache_[key] = std::move(out);
    return p;
}
const render::DecodedShape* CityInfoGfx::Panel()  { return Decode("_PERGAMENT_MB", 0); }
const render::DecodedShape* CityInfoGfx::ButtonShape(int shape) { return Decode("_BUTTON_RED", shape); }
const render::DecodedShape* CityInfoGfx::Crest(const std::string& city) {
    std::string c = Upper(city);
    return Decode("_STADTWAPPEN_" + c, 0);
}

namespace {
// Nearest-neighbour blit of a decoded shape into (dx,dy,dw,dh); A==0 transparent.
void BlitShape(render::Surface* s, int dx, int dy, int dw, int dh,
               const render::DecodedShape& sh) {
    if (sh.width <= 0 || sh.height <= 0 || dw <= 0 || dh <= 0) return;
    for (int j = 0; j < dh; ++j) {
        const int sy = j * sh.height / dh;
        for (int i = 0; i < dw; ++i) {
            const int sx = i * sh.width / dw;
            const std::uint32_t px = sh.argb[(std::size_t)sy * sh.width + sx];
            if ((px >> 24) == 0) continue;            // transparent
            PutPx(s, dx + i, dy + j, (px >> 16) & 0xFF, (px >> 8) & 0xFF, px & 0xFF);
        }
    }
}
// The markup card text, drawn dark on parchment within (tx,ty,tw) starting at line0.
void DrawBeschr(render::Surface* fb, int tx, int ty, int colUnit, int lineH,
                const std::string& beschr) {
    int line = 0, col = 0; bool inTitle = false;
    auto draw = [&](const std::string& run, bool t) {
        if (run.empty()) return;
        u8 r = t ? 96 : 54, g = t ? 28 : 36, b = t ? 12 : 18;   // dark red title / dark text
        int endx = render::DrawTextCp1251(fb, tx + col, ty + line * lineH, run.c_str(), r, g, b);
        col = endx - tx;
    };
    std::string run;
    for (std::size_t i = 0; i < beschr.size();) {
        const unsigned char c = (unsigned char)beschr[i];
        if (c == '$' && i + 1 < beschr.size()) {
            draw(run, inTitle); run.clear();
            std::size_t j = i + 1; int arg = -1;
            if (std::isdigit((unsigned char)beschr[j])) { arg = beschr[j] - '0'; ++j; }
            const char code = (j < beschr.size()) ? beschr[j] : 0; ++j;
            switch (code) {
                case '[': inTitle = true; break;
                case ']': inTitle = false; break;
                case 'N': ++line; col = 0; break;
                case 'A': line += (arg > 0 ? arg : 1); col = 0; break;
                case '>': col = (arg > 0 ? arg : 0) * colUnit; break;
                case 'B': case 'L': col = 0; break;
                default: break;
            }
            i = j;
        } else { run += (char)c; ++i; }
    }
    draw(run, inTitle);
}
} // namespace

InfoWindowLayout RenderCityInfoWindow(render::Surface* fb, int fbW, int fbH,
                                      const CityInfoText& text, CityInfoGfx& gfx,
                                      const std::string& cityName) {
    InfoWindowLayout L;
    if (!fb || !fb->pixels) return L;
    // Menu/CHOOSECITY form rects (800x600 layout), scaled to the framebuffer.
    const float sx = fbW / 800.0f, sy = fbH / 600.0f;
    const int px = (int)(120 * sx), py = (int)(392 * sy);
    const int pw = (int)(548 * sx), ph = (int)(204 * sy);
    L.panelX = px; L.panelY = py; L.panelW = pw; L.panelH = ph; L.valid = true;

    const render::DecodedShape* panel = gfx.Panel();
    if (panel) BlitShape(fb, px, py, pw, ph, *panel);
    else FillRect(fb, px, py, pw, ph, 196, 170, 120);   // parchment fallback

    const std::string beschr = text.Beschr(cityName);

    // City crest on the left (native aspect, fit to the panel's upper area).
    int crestRight = px + 12;
    if (const render::DecodedShape* crest = gfx.Crest(cityName)) {
        const int ch = (int)(120 * sy), cw = crest->width * ch / (crest->height ? crest->height : 1);
        const int cx = px + (int)(14 * sx), cy = py + (int)(20 * sy);
        BlitShape(fb, cx, cy, cw, ch, *crest);
        crestRight = cx + cw + (int)(12 * sx);
    }

    // Title (ASCII city name) + the BESCHR markup card, dark on parchment.
    const int tx = crestRight, ty = py + (int)(14 * sy);
    const int lineH = 12, colUnit = 34;
    render::DrawTextCp1251(fb, tx, ty, cityName.c_str(), 110, 30, 12);
    DrawBeschr(fb, tx, ty + lineH + 2, colUnit, lineH, beschr);

    // The choose button on the bottom strip (128,545,529,43), right-aligned: the red
    // main-menu button (_BUTTON_RED) composed left-cap + stretched centre + right-cap,
    // like RenderMainMenu. Its rect is returned for the caller's click hit-test.
    const int stripX = (int)(128 * sx), stripY = (int)(545 * sy);
    const int stripW = (int)(529 * sx), stripH = (int)(43 * sy);
    const render::DecodedShape* capL = gfx.ButtonShape(0);
    const render::DecodedShape* capR = gfx.ButtonShape(1);
    const render::DecodedShape* face = gfx.ButtonShape(2);
    int bh = (face && face->height > 0) ? face->height : 33;
    if (bh > stripH - 4) bh = stripH - 4;
    int bw = (int)(150 * sx);
    const int bx = stripX + stripW - bw - (int)(6 * sx), by = stripY + (stripH - bh) / 2;
    if (face) {
        const int capW = (capL && capL->width > 0) ? (capL->width * bh / (face->height ? face->height : bh)) : 0;
        const int capWR = (capR && capR->width > 0) ? (capR->width * bh / (face->height ? face->height : bh)) : 0;
        BlitShape(fb, bx + capW, by, bw - capW - capWR, bh, *face);          // stretched centre
        if (capL) BlitShape(fb, bx, by, capW, bh, *capL);                    // left cap
        if (capR) BlitShape(fb, bx + bw - capWR, by, capWR, bh, *capR);      // right cap
        // Choose caption (light text on the red button).
        render::DrawTextCp1251(fb, bx + capW + (int)(8 * sx), by + (bh - 7) / 2, "OK", 250, 240, 210);
    } else {
        FillRect(fb, bx, by, bw, bh, 120, 30, 24); FrameRect(fb, bx, by, bw, bh, 230, 200, 90);
    }
    L.btnX = bx; L.btnY = by; L.btnW = bw; L.btnH = bh;
    return L;
}

void RenderCityInfoCard(render::Surface* fb, int px, int py, int pw, int ph,
                        const std::string& title, const std::string& beschr) {
    if (!fb || !fb->pixels || pw <= 0 || ph <= 0) return;

    // Framed parchment-dark panel (the info window).
    FillRect(fb, px, py, pw, ph, 28, 22, 14);
    FrameRect(fb, px, py, pw, ph, 150, 120, 70);
    FrameRect(fb, px + 1, py + 1, pw - 2, ph - 2, 70, 54, 30);

    const int padx = 7, pady = 6, lineH = 11, colUnit = 30;
    int line = 0, col = 0;
    bool inTitle = false;

    // ASCII caption (the engine's $C city name) on the first line, if given.
    if (!title.empty()) {
        render::DrawTextCp1251(fb, px + padx, py + pady, title.c_str(), 255, 230, 120);
        line = 1;
    }

    auto draw = [&](const std::string& run, bool t) {
        if (run.empty()) return;
        const int tx = px + padx + col;
        const int ty = py + pady + line * lineH;
        u8 r = t ? 255 : 214, g = t ? 224 : 206, b = t ? 110 : 190;
        int endx = render::DrawTextCp1251(fb, tx, ty, run.c_str(), r, g, b);
        col = endx - (px + padx);
    };

    const std::string& s = beschr;
    std::string run;
    for (std::size_t i = 0; i < s.size();) {
        const unsigned char c = (unsigned char)s[i];
        if (c == '$' && i + 1 < s.size()) {
            draw(run, inTitle); run.clear();
            std::size_t j = i + 1;
            int arg = -1;
            if (std::isdigit((unsigned char)s[j])) { arg = s[j] - '0'; ++j; }
            const char code = (j < s.size()) ? s[j] : 0;
            ++j;
            switch (code) {
                case '[': inTitle = true; break;
                case ']': inTitle = false; break;
                case 'N': ++line; col = 0; break;                       // newline
                case 'A': line += (arg > 0 ? arg : 1); col = 0; break;  // line feed(s)
                case '>': col = (arg > 0 ? arg : 0) * colUnit; break;   // tab to column
                case 'B': col = 0; break;                               // row start
                case 'L': col = 0; break;                               // column reset
                default: break;                                         // $Z and others: no-op
            }
            i = j;
        } else {
            run += (char)c;
            ++i;
        }
    }
    draw(run, inTitle);
}

} // namespace guild::play
