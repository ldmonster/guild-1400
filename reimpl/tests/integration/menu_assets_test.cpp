// Integration: build a small in-memory gilde.gfx with a button-like sprite,
// have play::MenuAssets decode it + install the gui::MenuRenderHooks, then render
// the main-menu form and assert the real sprite painted into the button rect
// (distinct from the asset-less fallback fill).
#include "test.h"

#include "play/menu_assets.h"
#include "gui/menu_render.h"
#include "gui/main_menu.h"
#include "render/types.h"
#include "shim_impl/mem_filesystem.h"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <vector>

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using u32 = std::uint32_t;

namespace {

void PutU16(std::vector<u8>& b, std::size_t at, u16 v) {
    if (at + 2 > b.size()) b.resize(at + 2);
    b[at] = (u8)v; b[at + 1] = (u8)(v >> 8);
}
void PutU32(std::vector<u8>& b, std::size_t at, u32 v) {
    if (at + 4 > b.size()) b.resize(at + 4);
    b[at] = (u8)v; b[at + 1] = (u8)(v >> 8);
    b[at + 2] = (u8)(v >> 16); b[at + 3] = (u8)(v >> 24);
}

// One SHAPBANK blob, one shape: every pixel a solid colour (R,G,B), w*h.
std::vector<u8> BuildSolidBlob(int w, int h, u8 R, u8 G, u8 B) {
    const u32 shapeOff = 0x80;
    std::vector<u8> blob(shapeOff, 0);
    PutU16(blob, 0x2A, 1);
    PutU32(blob, 0x45, shapeOff);

    std::size_t shBase = blob.size();
    blob.resize(shBase + 0x32, 0);
    PutU16(blob, shBase + 6, (u16)w);
    PutU16(blob, shBase + 0x0A, (u16)h);

    std::vector<u32> rowOffsets(h, 0);
    for (int r = 0; r < h; ++r) {
        rowOffsets[r] = (u32)(blob.size() - shBase);
        u32 at = (u32)blob.size();
        PutU32(blob, at, 1); at += 4;          // 1 run
        PutU32(blob, at, 0); at += 4;          // skip 0
        PutU32(blob, at, (u32)w); at += 4;     // w pixels
        for (int x = 0; x < w; ++x) {
            blob.resize(at + 3);
            blob[at] = R; blob[at + 1] = G; blob[at + 2] = B; at += 3;
        }
    }
    u32 rowTabRel = (u32)(blob.size() - shBase);
    PutU32(blob, shBase + 0x2A, rowTabRel);
    for (int r = 0; r < h; ++r) PutU32(blob, blob.size(), rowOffsets[r]);
    return blob;
}

// Build a gilde.gfx where record #174 == "_BUTTON_RED" decodes to a solid colour.
// (Record index == gfx id, so we pad records 0..173 with empty names.)
std::vector<u8> BuildArchiveWithButton(int btnW, int btnH, u8 R, u8 G, u8 B,
                                       int bgW, int bgH) {
    const u32 count = 1774;  // need index 174 (button) and 1773 (background)
    std::vector<u8> file;
    file.resize(4 + count * 84, 0);
    PutU32(file, 0, count);

    auto writeRec = [&](u32 idx, const char* name, u32 off, u32 sz, u16 w, u16 h) {
        std::size_t base = 4 + idx * 84;
        std::memcpy(file.data() + base, name, std::strlen(name));
        PutU32(file, base + 48, off);
        PutU32(file, base + 56, sz);
        PutU16(file, base + 80, w);
        PutU16(file, base + 82, h);
    };

    std::vector<u8> btn = BuildSolidBlob(btnW, btnH, R, G, B);
    std::vector<u8> bg  = BuildSolidBlob(bgW, bgH, 30, 40, 60);

    u32 offBtn = (u32)file.size();
    file.insert(file.end(), btn.begin(), btn.end());
    u32 offBg = (u32)file.size();
    file.insert(file.end(), bg.begin(), bg.end());

    writeRec(174,  "_BUTTON_RED",       offBtn, (u32)btn.size(), (u16)btnW, (u16)btnH);
    writeRec(1773, "_MENUE_BACKGROUND", offBg,  (u32)bg.size(),  (u16)bgW,  (u16)bgH);
    return file;
}

u32 PixAt(const std::vector<u32>& fb, int W, int x, int y) {
    return fb[(std::size_t)y * W + x];
}

} // namespace

TEST(MenuAssets, DecodeAndInstallHook) {
    auto archive = BuildArchiveWithButton(100, 33, 200, 20, 20, /*bg*/ 800, 600);

    guild::shim::MemFileSystem fs;
    fs.put("gfx/gilde.gfx", archive);

    guild::play::MenuAssets assets;
    CHECK(assets.Load(fs));
    CHECK(assets.loaded());
    CHECK_EQ(assets.backgroundWidth(), 800);
    CHECK_EQ(assets.backgroundHeight(), 600);
    CHECK(assets.buttonFrameCount() >= 1);

    const guild::render::DecodedShape* f0 = assets.buttonFrame(0);
    CHECK(f0 != nullptr);
    CHECK_EQ(f0->width, 100);
    CHECK_EQ(f0->argb[0], 0xFFC81414u);   // 200,20,20

    // Render the main menu into a 32bpp framebuffer.  Without the hook the button
    // gets the fallback fill (MenuPalette btn colour 96,96,160 => 0xFF6060A0).
    const int W = 320, H = 400;
    std::vector<u32> fb((std::size_t)W * H, 0u);
    auto tgt = guild::gui::MenuRenderTarget::Wrap(fb.data(), W, H, W * 4);

    guild::play::MenuAssets::ClearHooks();
    guild::gui::RenderMainMenu(tgt);
    // Fallback button face colour at the first button (x=32, y=10): MenuFillRect
    // packs via the Argb8888 format which has no alpha field, so the fallback fill
    // is the RGB-only 0x006060A0 (palette btn = 96,96,160).
    const u32 fallback = PixAt(fb, W, guild::gui::kMainMenuButtonX + 4,
                               guild::gui::MainMenu_ButtonY(0) + 4);
    CHECK_EQ(fallback, 0x006060A0u);

    // Now install the real hook and re-render: the button rect must show the real
    // sprite colour (red), not the fallback fill.
    std::fill(fb.begin(), fb.end(), 0u);
    assets.InstallHooks();
    guild::gui::RenderMainMenu(tgt);
    const u32 real = PixAt(fb, W, guild::gui::kMainMenuButtonX + 4,
                           guild::gui::MainMenu_ButtonY(0) + 4);
    CHECK_EQ(real, 0xFFC81414u);
    CHECK(real != fallback);

    guild::play::MenuAssets::ClearHooks();
}

TEST(MenuAssets, AbsentArchiveStaysInert) {
    guild::shim::MemFileSystem fs;     // no gilde.gfx put
    guild::play::MenuAssets assets;
    CHECK(!assets.Load(fs));
    CHECK(!assets.loaded());

    // InstallHooks is a no-op when not loaded; the render falls back cleanly.
    guild::play::MenuAssets::ClearHooks();
    assets.InstallHooks();
    CHECK(guild::gui::GetMenuRenderHooks().drawSprite == nullptr);
}
