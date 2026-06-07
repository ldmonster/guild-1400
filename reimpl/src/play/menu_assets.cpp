// guild::play — real main-menu artwork loader + MenuRenderHooks install. See header.
#include "play/menu_assets.h"

#include "gui/menu_render.h"   // MenuRenderHooks, SetMenuRenderHooks
#include "render/types.h"      // render::Surface

#include "shim/IFileSystem.h"

#include <cstdint>

namespace guild::play {
namespace {

// gfx-id of the menu records (record index == gfx id in the archive).
constexpr int kGfxButtonRed     = 174;
constexpr const char* kButtonRedName  = "_BUTTON_RED";
constexpr const char* kBackgroundName = "_MENUE_BACKGROUND";

} // namespace

int BlitDecodedSprite(render::Surface* s, int x, int y,
                      const render::DecodedShape& sprite) {
    if (!s || !s->pixels || sprite.width <= 0 || sprite.height <= 0) return 0;
    // The menu framebuffer is 32bpp packed 0xAARRGGBB (gui::Argb8888): our decoded
    // sprite is the same packing, so copy through (honouring transparency + clip).
    if (s->bpp != 32) return 0;

    const int sw = sprite.width, sh = sprite.height;
    const int W = s->widthPx ? s->widthPx : s->width;
    const int H = s->height;
    int drawn = 0;
    auto* base = reinterpret_cast<std::uint32_t*>(s->pixels);
    for (int row = 0; row < sh; ++row) {
        const int dy = y + row;
        if (dy < 0 || dy >= H) continue;
        const std::uint32_t* src = sprite.argb.data() + (std::size_t)row * sw;
        std::uint32_t* dst = base + (std::size_t)dy * W;
        for (int col = 0; col < sw; ++col) {
            const std::uint32_t px = src[col];
            if ((px & 0xFF000000u) == 0u) continue;  // transparent
            const int dx = x + col;
            if (dx < 0 || dx >= W) continue;
            dst[dx] = px;
            ++drawn;
        }
    }
    return drawn;
}

bool MenuAssets::Load(shim::IFileSystem& fs, const char* archivePath, const char* bgName) {
    loaded_ = false;
    buttons_.clear();
    spriteCache_.clear();
    bg_ = render::DecodedShape{};

    if (!archivePath || !fs.exists(archivePath)) return false;
    if (!archive_.LoadFromFile(fs, archivePath)) return false;

    // Decode the requested background (#1773 _MENUE_BACKGROUND 800x600, or the
    // _1024 / _1152 resolution variants), shape 0. Fall back to the 800x600 record
    // when the requested name is absent so callers can always ask for the big one.
    if (!bgName || !*bgName) bgName = kBackgroundName;
    if (!archive_.DecodeShapeByName(bgName, 0, bg_))
        if (!archive_.DecodeShapeByName(kBackgroundName, 0, bg_)) return false;
    if (bg_.width <= 0 || bg_.height <= 0) return false;

    // Decode the button frames (#174 _BUTTON_RED — all shapes).
    const int btnRec = archive_.FindByName(kButtonRedName);
    if (btnRec >= 0) {
        const int n = archive_.ShapeCount(btnRec);
        for (int i = 0; i < n; ++i) {
            render::DecodedShape sh;
            if (archive_.DecodeShape(btnRec, i, sh)) buttons_.push_back(std::move(sh));
        }
    }

    loaded_ = true;
    return true;
}

const render::DecodedShape* MenuAssets::SpriteForGfxId(int gfxId) {
    if (!loaded_) return nullptr;
    for (auto& kv : spriteCache_)
        if (kv.first == gfxId) return &kv.second;

    // Fast path: the buttons are already decoded under gfx 174.
    if (gfxId == kGfxButtonRed && !buttons_.empty()) {
        // Use the 3rd frame (the 100-wide centre face) if present, else frame 0.
        const render::DecodedShape& src =
            buttons_.size() > 2 ? buttons_[2] : buttons_[0];
        spriteCache_.emplace_back(gfxId, src);
        return &spriteCache_.back().second;
    }

    render::DecodedShape sh;
    if (!archive_.DecodeShape(gfxId, 0, sh)) return nullptr;
    spriteCache_.emplace_back(gfxId, std::move(sh));
    return &spriteCache_.back().second;
}

namespace {
// The drawSprite hook: resolve the gfx-id sprite and blit it into the surface.
bool DrawSpriteHook(render::Surface* s, int x, int y, int gfxId, void* userData) {
    auto* self = static_cast<MenuAssets*>(userData);
    if (!self || !self->loaded()) return false;
    const render::DecodedShape* sprite = self->SpriteForGfxId(gfxId);
    if (!sprite) return false;
    return BlitDecodedSprite(s, x, y, *sprite) > 0;
}
} // namespace

void MenuAssets::InstallHooks() {
    if (!loaded_) return;
    gui::MenuRenderHooks hooks;
    hooks.drawSprite = &DrawSpriteHook;
    hooks.userData = this;
    gui::SetMenuRenderHooks(hooks);
}

void MenuAssets::ClearHooks() {
    gui::SetMenuRenderHooks(gui::MenuRenderHooks{});  // inert default
}

} // namespace guild::play
