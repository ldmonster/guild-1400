#pragma once
// =============================================================================
// guild::gui::GuiSurface — concrete IGuiSurface: the SHAPBANK shape-blit-to-
// surface pixel primitive (the rule-3 boundary of the retained-mode 2D GUI).
//
// 1:1 with the engine render leaves (gilde.exe, imagebase 0x400000):
//   VIBE_Coord_Transform   @0x5d8b00  shape-record lookup (bank + per-shape off)
//   VIBE_Animation_Basic   @0x5d85b8  ShapeMode::kNormal   — opaque per-pixel blit
//   VIBE_Animation_Advanced@0x5d89bc  ShapeMode::kAdvanced — draw-mode byte = 3
//   VIBE_Velocity_Apply    @0x5d883c  ShapeMode::kVelocity — draw-mode byte = 2
//   VIBE_Coord_Push        @0x5d8ae8  SetClip (clip-rect globals 64A1B4..)
//   VIBE_Property_Set/Get   @0x4159dc/@0x4152cc  text pen (delegated to MenuFont)
//
// THE DRAW-MODE BYTE (shape+0x0D) AND THE RECOLOR PASSES
// -----------------------------------------------------------------------------
// Animation_Advanced/Velocity_Apply temporarily set the shape's byte @+0x0D to
// 3 / 2, draw, then restore it. That byte is the per-pixel transform selected in
// the row-RLE blit inner loops (VIBE_FrameTable_Next @0x5fbc10 /
// VIBE_FrameTable_Validate @0x5fc200, the clip-edge cases) and in the full-bitmap
// blit (VIBE_FrameData_Interpolate @0x5d7420). All three transforms read/WRITE
// the DESTINATION pixel, keyed by the shape's opaque pixels — they do not use the
// shape's own colour:
//   mode 0 (kNormal)  : dst = src                       (plain opaque copy)
//   mode 2 (kVelocity): dst = (dst >> 1) & mask         (50% darken — the shadow)
//   mode 3 (kAdvanced): dst = LightTable[dst]           (brighten the destination)
//
// The brighten LUT is built by VIBE_Shape_BuildLightTable @0x5d49a0, called from
// VIBE_Shape_InitColorMasks @0x5d4ad4 with amount = 0x30 (48): for every colour it
// unpacks RGB, ADDS 48 to each channel saturating at 255, and repacks. So
// kAdvanced = per-channel saturating add of 48 to the destination.
//
// All of this is reconstructed against a 32bpp 0xAARRGGBB render::Surface (the
// engine ran 16bpp; the `& mask` half-step is exactly per-channel >>1 once the
// channels are byte-aligned, so the 32bpp form is byte-faithful — no gap).
//
// SHAPE DECODE: the depth-2 SHAPBANK codec already exists as
// render::GfxArchive::DecodeShape; we reach it (and its cache) through
// play::MenuAssets::SpriteByName(name, shapeNr). gfxId in IGuiSurface is mapped
// to a record NAME via a small registry (default: the gold slider record), since
// the engine keys shapes by the node's gfx record, which for the options slider
// is `_SLIDER_GOLD_WAAGERECHT`.
// =============================================================================
#include "guild/common/types.h"
#include "gui/gui_render_iface.h"

#include <string>
#include <vector>

namespace guild::render { struct Surface; }
namespace guild::play { class MenuAssets; }

namespace guild::gui {

class GuiSurface : public IGuiSurface {
public:
    // `target` is the 32bpp 0xAARRGGBB surface to render into (must outlive this).
    // `assets` supplies the loaded gilde.gfx (GfxArchive + DecodeShape cache) and
    // the real `_FONT` (MenuFont) for text. Both must outlive this object.
    GuiSurface(render::Surface* target, play::MenuAssets& assets);

    // Map a gfxId integer to a gilde.gfx record NAME (the engine's node+0x0C gfx
    // record). Calls without a registered id fall back to the default record name.
    // Returns this for chaining. (The options slider registers its id ->
    // `_SLIDER_GOLD_WAAGERECHT`.)
    GuiSurface& MapGfxId(int gfxId, const char* recordName);
    // Default record name used when a gfxId is not registered. Defaults to the
    // gold horizontal slider so a bare BlitShape(id, ...) draws the slider art.
    GuiSurface& SetDefaultRecord(const char* recordName);
    // Ambient text-pen colour (the engine's State_Finalize(font) colour). When set,
    // DrawText uses it (the widget draws pass 0,0,0 as a placeholder). Without it,
    // DrawText honours the per-call r/g/b (golden-vector tests).
    GuiSurface& SetTextColor(u8 r, u8 g, u8 b) {
        ambR_ = r; ambG_ = g; ambB_ = b; ambientSet_ = true; return *this;
    }

    // --- IGuiSurface ---
    void SetClip(int x0, int y0, int x1, int y1) override;
    bool BlitShape(int gfxId, int shapeNr, int x, int y, ShapeMode mode) override;
    bool ShapeSize(int gfxId, int shapeNr, int* w, int* h) override;
    int  DrawText(int x, int y, const char* s, ShapeMode shadow,
                  u8 r, u8 g, u8 b) override;
    int  TextWidth(const char* s) override;

    // Current clip rect (inclusive-exclusive box actually applied), for tests.
    void GetClip(int* x0, int* y0, int* x1, int* y1) const;

private:
    // Resolve gfxId -> record name (registry, else default).
    const char* ResolveName(int gfxId) const;

    render::Surface*   target_;
    play::MenuAssets*  assets_;
    std::string        defaultRecord_;
    std::vector<std::pair<int, std::string>> idMap_;

    // Clip rect (VIBE_Coord_Push globals dword_64A1B4=left, 64A1B8=top,
    // 64A1BC=right, 64A1C0=bottom). Initialised to the full surface.
    int clipL_ = 0, clipT_ = 0, clipR_ = 0, clipB_ = 0;
    u8  ambR_ = 0, ambG_ = 0, ambB_ = 0;
    bool ambientSet_ = false;
};

// The kAdvanced brighten amount (VIBE_Shape_InitColorMasks @0x5d4ad4 passes 0x30
// to VIBE_Shape_BuildLightTable @0x5d49a0). Exposed for the golden-vector test.
constexpr int kAdvancedBrighten = 0x30;

} // namespace guild::gui
