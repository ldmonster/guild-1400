// =============================================================================
// gui_surface_render_test — golden-vector + smoke tests for guild::gui::GuiSurface
// (the SHAPBANK shape-blit-to-surface primitive, the rule-3 2D-GUI boundary).
//
// Two layers:
//   1) SYNTHETIC golden vectors (no assets): a hand-built gilde.gfx image with
//      solid-colour RLE shapes, exercising the three blit modes (kNormal copy,
//      kVelocity 50%-darken of dst, kAdvanced +0x30 saturating-add to dst) and
//      the clip rect (strict X, half-open Y — the FrameTable_Next/Validate tests).
//   2) REAL assets (guarded on GUILD_GAME_DIR): load gfx/gilde.gfx, blit the gold
//      slider shapes 0/1/3/5 (`_SLIDER_GOLD_WAAGERECHT`) — non-blank + clipping —
//      and a DrawText / TextWidth smoke test through the real `_FONT`.
// =============================================================================
#include "test.h"

#include "gui/gui_surface_render.h"
#include "gui/gui_render_iface.h"
#include "play/menu_assets.h"
#include "render/surface.h"
#include "render/types.h"
#include "shim_impl/mem_filesystem.h"
#include "shim_impl/disk_filesystem.h"

#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using guild::gui::GuiSurface;
using guild::gui::ShapeMode;
using guild::gui::kAdvancedBrighten;

namespace {

// ---- little-endian writers ------------------------------------------------
void PutU16(std::vector<std::uint8_t>& b, std::size_t off, std::uint16_t v) {
    b[off] = (std::uint8_t)(v & 0xFF);
    b[off + 1] = (std::uint8_t)(v >> 8);
}
void PutU32(std::vector<std::uint8_t>& b, std::size_t off, std::uint32_t v) {
    b[off] = (std::uint8_t)(v & 0xFF);
    b[off + 1] = (std::uint8_t)((v >> 8) & 0xFF);
    b[off + 2] = (std::uint8_t)((v >> 16) & 0xFF);
    b[off + 3] = (std::uint8_t)((v >> 24) & 0xFF);
}

// SHAPBANK header / shape-header offsets (must match render/gfx_archive.cpp).
constexpr std::size_t kBankShapeCount = 0x2A;
constexpr std::size_t kBankOffTable   = 0x45;
constexpr std::size_t kShWidth   = 6;
constexpr std::size_t kShHeight  = 0x0A;
constexpr std::size_t kShFullFlag = 0x26;
constexpr std::size_t kShRowTab  = 0x2A;
constexpr std::size_t kShHeaderSz = 0x32;

// Build one SHAPBANK blob holding `shapes.size()` solid-colour RLE rectangles.
// shapes[i] = {w, h, {R,G,B}}. Each shape is a fully-opaque rect of its colour.
struct ShapeSpec { int w; int h; std::uint8_t r, g, b; };

std::vector<std::uint8_t> BuildBank(const std::vector<ShapeSpec>& shapes) {
    const int n = (int)shapes.size();
    // Header: enough to hold the shape-count u16 (@0x2A) and the offset table
    // (@0x45, n u32s). Round the header up to where shapes start.
    const std::size_t hdrSz = kBankOffTable + (std::size_t)n * 4;
    std::vector<std::uint8_t> blob(hdrSz, 0);
    PutU16(blob, kBankShapeCount, (std::uint16_t)n);

    for (int i = 0; i < n; ++i) {
        const ShapeSpec& s = shapes[i];
        const std::size_t shapeOff = blob.size();
        PutU32(blob, kBankOffTable + (std::size_t)i * 4, (std::uint32_t)shapeOff);

        // Reserve the 0x32-byte shape header.
        blob.resize(shapeOff + kShHeaderSz, 0);
        PutU16(blob, shapeOff + kShWidth, (std::uint16_t)s.w);
        PutU16(blob, shapeOff + kShHeight, (std::uint16_t)s.h);
        PutU32(blob, shapeOff + kShFullFlag, 0);  // RLE (not full bitmap)

        // Row table: h u32 offsets (relative to shape) immediately after header.
        const std::size_t rowTabRel = kShHeaderSz;
        PutU32(blob, shapeOff + kShRowTab, (std::uint32_t)rowTabRel);
        blob.resize(shapeOff + rowTabRel + (std::size_t)s.h * 4, 0);

        // Each row: runCount=1, run {skipBytes=0, lenPixels=w, w*3 RGB bytes}.
        for (int row = 0; row < s.h; ++row) {
            const std::size_t rowOff = blob.size();
            PutU32(blob, shapeOff + rowTabRel + (std::size_t)row * 4,
                   (std::uint32_t)(rowOff - shapeOff));
            // runCount
            std::size_t base = blob.size();
            blob.resize(base + 4, 0);
            PutU32(blob, base, 1);
            // run header
            base = blob.size();
            blob.resize(base + 8, 0);
            PutU32(blob, base, 0);            // skipBytes
            PutU32(blob, base + 4, (std::uint32_t)s.w);  // lenPixels
            // pixels
            for (int x = 0; x < s.w; ++x) {
                blob.push_back(s.r);
                blob.push_back(s.g);
                blob.push_back(s.b);
            }
        }
    }
    return blob;
}

// 84-byte directory record fields.
constexpr std::size_t kRecSize    = 84;
constexpr std::size_t kRecDataOff = 48;
constexpr std::size_t kRecDataSz  = 56;
constexpr std::size_t kRecWidth   = 80;
constexpr std::size_t kRecHeight  = 82;

// Assemble a gilde.gfx image from named (record, bank-blob) pairs.
struct RecSpec { std::string name; std::vector<std::uint8_t> blob; int w, h; };

std::vector<std::uint8_t> BuildGfx(const std::vector<RecSpec>& recs) {
    const std::uint32_t count = (std::uint32_t)recs.size();
    std::vector<std::uint8_t> img(4 + (std::size_t)count * kRecSize, 0);
    PutU32(img, 0, count);
    // Blobs concatenate after the directory.
    std::size_t dataPos = img.size();
    std::vector<std::size_t> offsets(count);
    for (std::uint32_t i = 0; i < count; ++i) {
        offsets[i] = dataPos;
        dataPos += recs[i].blob.size();
    }
    img.resize(dataPos, 0);
    for (std::uint32_t i = 0; i < count; ++i) {
        const std::size_t rb = 4 + (std::size_t)i * kRecSize;
        std::memset(img.data() + rb, 0, 48);
        std::memcpy(img.data() + rb, recs[i].name.c_str(),
                    recs[i].name.size() < 48 ? recs[i].name.size() : 48);
        PutU32(img, rb + kRecDataOff, (std::uint32_t)offsets[i]);
        PutU32(img, rb + kRecDataSz, (std::uint32_t)recs[i].blob.size());
        PutU16(img, rb + kRecWidth, (std::uint16_t)recs[i].w);
        PutU16(img, rb + kRecHeight, (std::uint16_t)recs[i].h);
        std::memcpy(img.data() + offsets[i], recs[i].blob.data(), recs[i].blob.size());
    }
    return img;
}

std::uint32_t Px(const guild::render::Surface* s, int x, int y) {
    const int W = s->widthPx ? s->widthPx : s->width;
    return reinterpret_cast<const std::uint32_t*>(s->pixels)[(std::size_t)y * W + x];
}

// Build a MenuAssets loaded from a synthetic gilde.gfx with a tiny bg + a TEST
// record holding `shapes`. `bgName` becomes the menu background (only needs w/h>0).
bool LoadSyntheticAssets(guild::play::MenuAssets& assets, guild::shim::MemFileSystem& fs,
                         const std::vector<ShapeSpec>& testShapes) {
    std::vector<RecSpec> recs;
    // Background: a 2x2 solid record so Load()'s "bg decoded w/h>0" passes.
    recs.push_back({"_BG_SYNTH", BuildBank({{2, 2, 10, 20, 30}}), 2, 2});
    recs.push_back({"TEST", BuildBank(testShapes), testShapes[0].w, testShapes[0].h});
    fs.put("gfx/gilde.gfx", BuildGfx(recs));
    return assets.Load(fs, "gfx/gilde.gfx", "_BG_SYNTH");
}

} // namespace

// ---------------------------------------------------------------------------
// 1) kNormal — opaque per-pixel copy of the shape colour (VIBE_Animation_Basic).
// ---------------------------------------------------------------------------
TEST(GuiSurfaceRender, NormalBlitCopiesShapeColour) {
    guild::shim::MemFileSystem fs;
    guild::play::MenuAssets assets;
    // shape 0 = 4x3 red rect.
    CHECK(LoadSyntheticAssets(assets, fs, {{4, 3, 0xC0, 0x10, 0x20}}));

    guild::render::Surface* surf =
        guild::render::SurfaceCreate(16, 8, 32, guild::render::Format8888());
    CHECK(surf != nullptr);
    std::memset(surf->pixels, 0, (std::size_t)surf->pitch * surf->height);

    GuiSurface gs(surf, assets);   // default record == _SLIDER_GOLD_WAAGERECHT
    gs.SetDefaultRecord("TEST");

    CHECK(gs.BlitShape(0, 0, 5, 2, ShapeMode::kNormal));
    // Every covered pixel == 0xFFC01020 (opaque red).
    CHECK_EQ(Px(surf, 5, 2), 0xFFC01020u);
    CHECK_EQ(Px(surf, 8, 4), 0xFFC01020u);
    // Outside the 4x3 rect stays clear.
    CHECK_EQ(Px(surf, 4, 2), 0u);
    CHECK_EQ(Px(surf, 9, 2), 0u);
    CHECK_EQ(Px(surf, 5, 5), 0u);

    // A shape that does not exist returns false (engine's "==0").
    CHECK(!gs.BlitShape(0, 9, 0, 0, ShapeMode::kNormal));

    guild::render::SurfaceDestroy(surf);
}

// ---------------------------------------------------------------------------
// 2) kVelocity — 50% darken of the DESTINATION, keyed by shape opacity.
//    (VIBE_Velocity_Apply -> draw-mode 2: dst = (dst>>1)&mask.)
// ---------------------------------------------------------------------------
TEST(GuiSurfaceRender, VelocityDarkensDestination) {
    guild::shim::MemFileSystem fs;
    guild::play::MenuAssets assets;
    CHECK(LoadSyntheticAssets(assets, fs, {{4, 2, 0x00, 0xFF, 0x00}}));  // green shape

    guild::render::Surface* surf =
        guild::render::SurfaceCreate(16, 8, 32, guild::render::Format8888());
    CHECK(surf != nullptr);
    // Pre-fill the surface with a known colour (0x80, 0x41, 0x21).
    auto* px = reinterpret_cast<std::uint32_t*>(surf->pixels);
    const int W = surf->widthPx;
    for (int i = 0; i < W * surf->height; ++i) px[i] = 0xFF804121u;

    GuiSurface gs(surf, assets);
    gs.SetDefaultRecord("TEST");
    CHECK(gs.BlitShape(0, 0, 2, 1, ShapeMode::kVelocity));

    // Covered: each channel halved: 0x80>>1=0x40, 0x41>>1=0x20, 0x21>>1=0x10.
    CHECK_EQ(Px(surf, 2, 1), 0xFF402010u);
    CHECK_EQ(Px(surf, 5, 2), 0xFF402010u);
    // Uncovered pixel unchanged.
    CHECK_EQ(Px(surf, 1, 1), 0xFF804121u);

    guild::render::SurfaceDestroy(surf);
}

// ---------------------------------------------------------------------------
// 3) kAdvanced — brighten the DESTINATION by +0x30 per channel, saturating.
//    (VIBE_Animation_Advanced -> draw-mode 3 -> LightTable built with amount 0x30.)
// ---------------------------------------------------------------------------
TEST(GuiSurfaceRender, AdvancedBrightensDestinationSaturating) {
    guild::shim::MemFileSystem fs;
    guild::play::MenuAssets assets;
    CHECK(LoadSyntheticAssets(assets, fs, {{3, 2, 0x11, 0x22, 0x33}}));

    guild::render::Surface* surf =
        guild::render::SurfaceCreate(16, 8, 32, guild::render::Format8888());
    CHECK(surf != nullptr);
    auto* px = reinterpret_cast<std::uint32_t*>(surf->pixels);
    const int W = surf->widthPx;
    // dst (0x10, 0xF0, 0xFF): +0x30 -> 0x40, sat 0xF0+0x30=0x120->0xFF, 0xFF->0xFF.
    for (int i = 0; i < W * surf->height; ++i) px[i] = 0xFF10F0FFu;

    GuiSurface gs(surf, assets);
    gs.SetDefaultRecord("TEST");
    CHECK_EQ(kAdvancedBrighten, 0x30);
    CHECK(gs.BlitShape(0, 0, 4, 1, ShapeMode::kAdvanced));

    CHECK_EQ(Px(surf, 4, 1), 0xFF40FFFFu);
    CHECK_EQ(Px(surf, 6, 2), 0xFF40FFFFu);
    CHECK_EQ(Px(surf, 3, 1), 0xFF10F0FFu);   // outside the rect

    guild::render::SurfaceDestroy(surf);
}

// ---------------------------------------------------------------------------
// 4) Clip rect — strict X (left<X<right), half-open Y (top<=Y<bottom), exactly
//    the FrameTable_Next/Validate per-pixel tests.
// ---------------------------------------------------------------------------
TEST(GuiSurfaceRender, ClipRectStrictXHalfOpenY) {
    guild::shim::MemFileSystem fs;
    guild::play::MenuAssets assets;
    CHECK(LoadSyntheticAssets(assets, fs, {{10, 10, 0x77, 0x88, 0x99}}));

    guild::render::Surface* surf =
        guild::render::SurfaceCreate(32, 32, 32, guild::render::Format8888());
    CHECK(surf != nullptr);
    std::memset(surf->pixels, 0, (std::size_t)surf->pitch * surf->height);

    GuiSurface gs(surf, assets);
    gs.SetDefaultRecord("TEST");
    // Clip box: left=4, top=6, right=10, bottom=12. (X: 5..9 inclusive; Y: 6..11.)
    gs.SetClip(4, 6, 10, 12);
    CHECK(gs.BlitShape(0, 0, 0, 0, ShapeMode::kNormal));  // 10x10 rect at origin

    const std::uint32_t col = 0xFF778899u;
    // Rect covers cols 0..9, rows 0..9; clip keeps 5..9 x 6..9.
    // Inside the clip box AND the rect -> drawn.
    CHECK_EQ(Px(surf, 5, 6), col);
    CHECK_EQ(Px(surf, 9, 9), col);
    // X boundary is strict: X==left(4) is NOT drawn.
    CHECK_EQ(Px(surf, 4, 6), 0u);
    // X==right(10) is outside both the clip and the 10-wide rect.
    CHECK_EQ(Px(surf, 10, 6), 0u);
    // Y boundary half-open: Y==top(6) drawn (checked above), Y<top not drawn.
    CHECK_EQ(Px(surf, 5, 5), 0u);    // above top
    // Y rows 10,11 are inside the clip band but outside the rect -> clear.
    CHECK_EQ(Px(surf, 5, 10), 0u);

    guild::render::SurfaceDestroy(surf);
}

// ---------------------------------------------------------------------------
// 5) ShapeSize reports the decoded shape metrics.
// ---------------------------------------------------------------------------
TEST(GuiSurfaceRender, ShapeSizeReportsMetrics) {
    guild::shim::MemFileSystem fs;
    guild::play::MenuAssets assets;
    CHECK(LoadSyntheticAssets(assets, fs, {{7, 5, 1, 2, 3}, {9, 4, 4, 5, 6}}));

    guild::render::Surface* surf =
        guild::render::SurfaceCreate(8, 8, 32, guild::render::Format8888());
    GuiSurface gs(surf, assets);
    gs.SetDefaultRecord("TEST");

    int w = -1, h = -1;
    CHECK(gs.ShapeSize(0, 0, &w, &h));
    CHECK_EQ(w, 7);
    CHECK_EQ(h, 5);
    CHECK(gs.ShapeSize(0, 1, &w, &h));
    CHECK_EQ(w, 9);
    CHECK_EQ(h, 4);
    CHECK(!gs.ShapeSize(0, 7, &w, &h));   // absent
    CHECK_EQ(w, 0);
    CHECK_EQ(h, 0);

    guild::render::SurfaceDestroy(surf);
}

// ---------------------------------------------------------------------------
// 6) gfxId -> record-name mapping.
// ---------------------------------------------------------------------------
TEST(GuiSurfaceRender, GfxIdMapsToRecordName) {
    guild::shim::MemFileSystem fs;
    guild::play::MenuAssets assets;
    CHECK(LoadSyntheticAssets(assets, fs, {{2, 2, 0xAB, 0xCD, 0xEF}}));

    guild::render::Surface* surf =
        guild::render::SurfaceCreate(8, 8, 32, guild::render::Format8888());
    std::memset(surf->pixels, 0, (std::size_t)surf->pitch * surf->height);
    GuiSurface gs(surf, assets);
    gs.MapGfxId(4242, "TEST");

    CHECK(gs.BlitShape(4242, 0, 0, 0, ShapeMode::kNormal));
    CHECK_EQ(Px(surf, 0, 0), 0xFFABCDEFu);
    // An unmapped id falls back to the default (the slider, absent here) -> false.
    CHECK(!gs.BlitShape(7, 0, 0, 0, ShapeMode::kNormal));

    guild::render::SurfaceDestroy(surf);
}

// ===========================================================================
// REAL assets (guarded on GUILD_GAME_DIR) — the gold slider + the real font.
// ===========================================================================
namespace {
bool CountNonBlank(const guild::render::Surface* s) {
    const int W = s->widthPx ? s->widthPx : s->width;
    const auto* px = reinterpret_cast<const std::uint32_t*>(s->pixels);
    for (int i = 0; i < W * s->height; ++i)
        if ((px[i] & 0xFF000000u) != 0u) return true;
    return false;
}
} // namespace

TEST(GuiSurfaceRender, RealSliderShapesNonBlankAndClipped) {
    const char* dir = std::getenv("GUILD_GAME_DIR");
    if (!dir || !*dir) { std::printf("    (skipped: GUILD_GAME_DIR not set)\n"); return; }

    guild::shim::DiskFileSystem fs(dir);
    guild::play::MenuAssets assets;
    if (!assets.Load(fs)) {
        std::printf("    (skipped: gfx/gilde.gfx absent or did not load)\n");
        return;
    }

    guild::render::Surface* surf =
        guild::render::SurfaceCreate(256, 64, 32, guild::render::Format8888());
    CHECK(surf != nullptr);
    std::memset(surf->pixels, 0, (std::size_t)surf->pitch * surf->height);

    GuiSurface gs(surf, assets);  // default record == _SLIDER_GOLD_WAAGERECHT

    // shapes 0 (left cap), 1 (track), 3 (thumb), 5 (right cap) at staggered x.
    bool any = false;
    any |= gs.BlitShape(0, 0, 4,  20, ShapeMode::kNormal);
    any |= gs.BlitShape(0, 1, 76, 26, ShapeMode::kNormal);
    any |= gs.BlitShape(0, 3, 90, 20, ShapeMode::kNormal);
    any |= gs.BlitShape(0, 5, 160, 20, ShapeMode::kNormal);
    CHECK(any);
    CHECK(CountNonBlank(surf));

    // Clip test: clip to a 1px-tall band that the thumb does not reach -> clear.
    std::memset(surf->pixels, 0, (std::size_t)surf->pitch * surf->height);
    gs.SetClip(0, 0, 256, 1);   // only row 0 may draw; shapes are below it
    gs.BlitShape(0, 0, 4, 20, ShapeMode::kNormal);
    CHECK(!CountNonBlank(surf));

    // Advanced (selected) draw must still mark pixels (brighten of the cleared
    // dst is just +0x30 -> non-zero alpha set) when in-bounds.
    std::memset(surf->pixels, 0, (std::size_t)surf->pitch * surf->height);
    gs.SetClip(0, 0, 256, 64);
    const bool drewAdv = gs.BlitShape(0, 6, 4, 20, ShapeMode::kAdvanced) ||
                         gs.BlitShape(0, 0, 4, 20, ShapeMode::kAdvanced);
    CHECK(drewAdv);
    CHECK(CountNonBlank(surf));

    guild::render::SurfaceDestroy(surf);
}

TEST(GuiSurfaceRender, RealDrawTextSmoke) {
    const char* dir = std::getenv("GUILD_GAME_DIR");
    if (!dir || !*dir) { std::printf("    (skipped: GUILD_GAME_DIR not set)\n"); return; }

    guild::shim::DiskFileSystem fs(dir);
    guild::play::MenuAssets assets;
    if (!assets.Load(fs) || !assets.font().loaded()) {
        std::printf("    (skipped: gfx/gilde.gfx or _FONT absent)\n");
        return;
    }

    guild::render::Surface* surf =
        guild::render::SurfaceCreate(256, 32, 32, guild::render::Format8888());
    CHECK(surf != nullptr);
    std::memset(surf->pixels, 0, (std::size_t)surf->pitch * surf->height);

    GuiSurface gs(surf, assets);
    const int w = gs.TextWidth("100");
    CHECK(w > 0);
    const int drawn = gs.DrawText(8, 8, "100", ShapeMode::kNormal, 0xFF, 0xE0, 0x40);
    CHECK_EQ(drawn, w);   // DrawText returns MeasureWidth
    CHECK(CountNonBlank(surf));

    guild::render::SurfaceDestroy(surf);
}
