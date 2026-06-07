#pragma once
// =============================================================================
// guild::play — load + decode the REAL main-menu artwork from gfx/gilde.gfx and
// install it behind gui::MenuRenderHooks so the native menu (sdl_menu.cpp) draws
// the shipped background + buttons instead of the asset-less reconstruction.
//
// The menu uses these gfx records (gfx id == archive record index):
//   #1773 _MENUE_BACKGROUND 800x600   — the backdrop ("DIE GILDE" town banner)
//   #174  _BUTTON_RED       (6 shapes) — the 8 menu buttons (3-slice caps)
//   #1776 _MAIN_MENU_RAHMEN 300x320   — the menu frame/border
// All are depth-2 shapes; render/gfx_archive.h DecodeShape reconstructs them
// (the VIBE_FrameTable_Index @0x5fbb24 codec).
//
// MenuAssets is inert when gilde.gfx is absent: load() returns false and the
// caller falls back to the flat-rect/font path.  When loaded, InstallHooks()
// wires a drawSprite hook that blits the decoded gfx-id sprite into the 32bpp
// menu framebuffer; the background bitmap is exposed for the backdrop blit.
// =============================================================================
#include "guild/common/types.h"
#include "render/gfx_archive.h"

#include <string>
#include <vector>

namespace guild::shim { class IFileSystem; }
namespace guild::render { struct Surface; }

namespace guild::play {

class MenuAssets {
public:
    // Load gfx/gilde.gfx under the mounted game dir via `fs` and decode the menu
    // records.  `archivePath` defaults to "gfx/gilde.gfx".  Returns true iff the
    // archive loaded AND the background decoded to a sane 800x600 image.  On
    // failure the object stays !loaded() and InstallHooks() is a no-op.
    bool Load(shim::IFileSystem& fs, const char* archivePath = "gfx/gilde.gfx",
              const char* bgName = "_MENUE_BACKGROUND");

    bool loaded() const { return loaded_; }

    // The decoded background (#1773): 0xAARRGGBB, row-major, w*h entries.
    const std::vector<u32>& background() const { return bg_.argb; }
    int backgroundWidth()  const { return bg_.width; }
    int backgroundHeight() const { return bg_.height; }

    // The button sprite-bank record (#174). frameCount() shapes are decoded.
    int buttonFrameCount() const { return (int)buttons_.size(); }
    const render::DecodedShape* buttonFrame(int i) const {
        return (i >= 0 && i < (int)buttons_.size()) ? &buttons_[i] : nullptr;
    }

    // Decode an arbitrary record-by-gfx-id sprite (shape 0) on demand; returns
    // nullptr on failure.  Cached so the render hook is allocation-free per frame.
    const render::DecodedShape* SpriteForGfxId(int gfxId);

    // Install this MenuAssets behind gui::SetMenuRenderHooks so RenderMenuForm /
    // RenderMainMenu paint the real gfx-id sprites.  Idempotent.  Pass the same
    // instance you keep alive for the whole menu loop (the hook holds a pointer).
    void InstallHooks();

    // Restore the inert default MenuRenderHooks (so tests / fallback are clean).
    static void ClearHooks();

private:
    bool loaded_ = false;
    render::GfxArchive archive_;
    render::DecodedShape bg_;
    std::vector<render::DecodedShape> buttons_;

    // gfxId -> decoded shape0 cache (for the generic sprite hook).
    std::vector<std::pair<int, render::DecodedShape>> spriteCache_;
};

// Blit a decoded sprite (0xAARRGGBB, A==0 transparent) into a 32bpp render::Surface
// at (x,y) with per-pixel transparency and clipping.  Returns pixels written.
// This is the asset-side counterpart of the engine's depth-2 shape blit.
int BlitDecodedSprite(render::Surface* s, int x, int y,
                      const render::DecodedShape& sprite);

} // namespace guild::play
