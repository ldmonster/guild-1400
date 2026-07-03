// =============================================================================
// guild::gui::GuiSurface — see gui_surface_render.h for the 1:1 provenance.
// =============================================================================
#include "gui/gui_surface_render.h"

#include "render/types.h"          // render::Surface
#include "render/gfx_archive.h"    // render::DecodedShape
#include "play/menu_assets.h"      // play::MenuAssets (GfxArchive + MenuFont)

#include <cstdint>
#include <cstring>

namespace guild::gui {

namespace {

// Per-pixel destination transforms, keyed by an opaque source pixel.
// gilde.exe VIBE_FrameTable_Next/Validate inner loops + VIBE_FrameData_Interpolate:
//   mode 0: dst = src
//   mode 2: dst = (dst >> 1) & mask        (the 16bpp "& word_1406944" half-step is
//           exactly per-channel >>1 once channels are byte-aligned, so in 32bpp it
//           is dst.rgb >>= 1 per channel — byte-faithful, see header)
//   mode 3: dst = LightTable[dst], LightTable built by VIBE_Shape_BuildLightTable
//           @0x5d49a0 with amount 0x30: each RGB channel += 48 saturating at 255.
inline std::uint32_t ApplyNormal(std::uint32_t /*dst*/, std::uint32_t src) {
    return src;
}
inline std::uint32_t ApplyVelocity(std::uint32_t dst, std::uint32_t /*src*/) {
    // 50% darken of the destination, preserve full alpha (opaque write).
    const std::uint32_t r = (dst >> 16) & 0xFF, g = (dst >> 8) & 0xFF, b = dst & 0xFF;
    return 0xFF000000u | ((r >> 1) << 16) | ((g >> 1) << 8) | (b >> 1);
}
inline std::uint32_t ApplyAdvanced(std::uint32_t dst, std::uint32_t /*src*/) {
    // Per-channel saturating add of kAdvancedBrighten (0x30) to the destination.
    auto add = [](std::uint32_t c) -> std::uint32_t {
        std::uint32_t v = c + (std::uint32_t)kAdvancedBrighten;
        return v > 255u ? 255u : v;
    };
    const std::uint32_t r = add((dst >> 16) & 0xFF);
    const std::uint32_t g = add((dst >> 8) & 0xFF);
    const std::uint32_t b = add(dst & 0xFF);
    return 0xFF000000u | (r << 16) | (g << 8) | b;
}

} // namespace

GuiSurface::GuiSurface(render::Surface* target, play::MenuAssets& assets)
    : target_(target), assets_(&assets),
      defaultRecord_(play::MenuAssets::kSliderGoldName) {
    // Default clip = the whole surface. The engine seeds the clip from the screen
    // extent via VIBE_Coord_Push; here we use the target's pixel bounds. SetClip
    // stores the box exactly as Coord_Push (left/top/right/bottom).
    if (target_) {
        const int W = target_->widthPx ? target_->widthPx : target_->width;
        // X test is strict (`> left` / `< right`): to allow column 0..W-1, left=-1,
        // right=W. Y test is half-open (`>= top` / `< bottom`): top=0, bottom=H.
        clipL_ = -1;
        clipT_ = 0;
        clipR_ = W;
        clipB_ = target_->height;
    }
}

GuiSurface& GuiSurface::MapGfxId(int gfxId, const char* recordName) {
    if (recordName) {
        for (auto& kv : idMap_) {
            if (kv.first == gfxId) { kv.second = recordName; return *this; }
        }
        idMap_.emplace_back(gfxId, recordName);
    }
    return *this;
}

GuiSurface& GuiSurface::SetDefaultRecord(const char* recordName) {
    if (recordName) defaultRecord_ = recordName;
    return *this;
}

const char* GuiSurface::ResolveName(int gfxId) const {
    for (const auto& kv : idMap_) {
        if (kv.first == gfxId) return kv.second.c_str();
    }
    return defaultRecord_.c_str();
}

void GuiSurface::SetClip(int x0, int y0, int x1, int y1) {
    // VIBE_Coord_Push @0x5d8ae8: dword_64A1B4=x0(left), 64A1B8=y0(top),
    // 64A1BC=x1(right), 64A1C0=y1(bottom). The per-pixel blit tests are
    // strict on X (X>left && X<right) and half-open on Y (Y>=top && Y<bottom).
    clipL_ = x0;
    clipT_ = y0;
    clipR_ = x1;
    clipB_ = y1;
}

void GuiSurface::GetClip(int* x0, int* y0, int* x1, int* y1) const {
    if (x0) *x0 = clipL_;
    if (y0) *y0 = clipT_;
    if (x1) *x1 = clipR_;
    if (y1) *y1 = clipB_;
}

bool GuiSurface::ShapeSize(int gfxId, int shapeNr, int* w, int* h) {
    if (w) *w = 0;
    if (h) *h = 0;
    if (!assets_) return false;
    const render::DecodedShape* sh = assets_->SpriteByName(ResolveName(gfxId), shapeNr);
    if (!sh || sh->width <= 0 || sh->height <= 0) return false;
    if (w) *w = sh->width;
    if (h) *h = sh->height;
    return true;
}

bool GuiSurface::BlitShape(int gfxId, int shapeNr, int x, int y, ShapeMode mode) {
    // VIBE_Animation_Basic @0x5d85b8: returns 0 when the shape record is absent
    // (used by the slider right-cap fallback). Coord_Transform shape lookup +
    // depth-2 decode are inside SpriteByName.
    if (!target_ || target_->bpp != 32 || !target_->pixels || !assets_) return false;
    const render::DecodedShape* sh = assets_->SpriteByName(ResolveName(gfxId), shapeNr);
    if (!sh || sh->width <= 0 || sh->height <= 0) return false;

    const int sw = sh->width, shgt = sh->height;
    const int W = target_->widthPx ? target_->widthPx : target_->width;
    const int H = target_->height;
    auto* base = reinterpret_cast<std::uint32_t*>(target_->pixels);

    // Surface bounds intersected with the engine clip box. X is strict
    // (left<X<right), Y half-open (top<=Y<bottom) — exactly the FrameTable tests.
    for (int row = 0; row < shgt; ++row) {
        const int dy = y + row;
        if (dy < 0 || dy >= H) continue;
        if (dy < clipT_ || dy >= clipB_) continue;   // Y>=top && Y<bottom
        const std::uint32_t* src = sh->argb.data() + (std::size_t)row * sw;
        std::uint32_t* dst = base + (std::size_t)dy * W;
        for (int col = 0; col < sw; ++col) {
            const std::uint32_t px = src[col];
            if ((px & 0xFF000000u) == 0u) continue;   // transparent (engine: *src==0)
            const int dx = x + col;
            if (dx < 0 || dx >= W) continue;
            if (dx <= clipL_ || dx >= clipR_) continue; // X>left && X<right (strict)
            std::uint32_t out;
            switch (mode) {
                case ShapeMode::kVelocity: out = ApplyVelocity(dst[dx], px); break;
                case ShapeMode::kAdvanced: out = ApplyAdvanced(dst[dx], px); break;
                case ShapeMode::kNormal:
                default:                   out = ApplyNormal(dst[dx], px);   break;
            }
            dst[dx] = out;
        }
    }
    return true;
}

int GuiSurface::DrawText(int x, int y, const char* s, ShapeMode /*shadow*/,
                         u8 r, u8 g, u8 b) {
    // VIBE_Property_Set @0x4159dc — delegate to the 1:1 font pen (MenuFont). Scale 1
    // (engine draws at native size). MenuFont clips to the surface bounds; it does
    // not consult the GUI clip box (the engine's text pen path is a separate leaf).
    if (!target_ || target_->bpp != 32 || !target_->pixels || !assets_) return 0;
    // Slider value/option text uses the SMALL engine font (_FONT+1 / record 67) per
    // the original (frida: font id 67). RenderHSlider passes the vertical CENTRE anchor
    // (H3/2 + node.y); the original offsets up by lineHeight/2-1 to centre the glyph row
    // on the track — apply that here (the seam owns the font line-height).
    const play::MenuFont& font = assets_->smallFont();
    if (!font.loaded()) return 0;
    const int W = target_->widthPx ? target_->widthPx : target_->width;
    const int H = target_->height;
    auto* base = reinterpret_cast<std::uint32_t*>(target_->pixels);
    if (ambientSet_) { r = ambR_; g = ambG_; b = ambB_; }
    const int lh = font.lineHeight() > 0 ? font.lineHeight() : 11;
    font.DrawText(base, W, H, x, y - lh / 2 + 1, s, /*scale=*/1, r, g, b);
    return font.MeasureWidth(s);
}

int GuiSurface::TextWidth(const char* s) {
    // VIBE_Property_Get @0x4152cc — slider text uses the small font (record 67).
    if (!assets_) return 0;
    const play::MenuFont& font = assets_->smallFont();
    if (!font.loaded()) return 0;
    return font.MeasureWidth(s);
}

} // namespace guild::gui
