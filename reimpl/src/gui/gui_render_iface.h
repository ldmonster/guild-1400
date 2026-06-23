#pragma once
// =============================================================================
// guild::gui — shared interface for the 1:1 retained-mode GUI render-to-surface
// pipeline (the options/menu screens render byte-for-byte like gilde.exe instead
// of the native compositor approximation). The engine renders SHAPBANK shapes to
// a DirectDraw surface via Animation_Basic/Advanced/Velocity_Apply over a clip
// stack (Coord_Push); rule 3 swaps that pixel boundary to a software
// render::Surface (later uploaded to Vulkan). This header is the seam both the
// pixel primitive (gui_surface_render) and the widget draws (slider_render_1to1)
// code against, so they compose without ODR clashes.
// =============================================================================
#include "guild/common/types.h"

namespace guild::render { struct Surface; class GfxArchive; }

namespace guild::gui {

// Shape blit mode — the three engine draw leaves:
//   kNormal   = VIBE_Animation_Basic    @0x5d85b8  (opaque shape blit)
//   kAdvanced = VIBE_Animation_Advanced @0x5d89bc  (selected/brightened variant)
//   kVelocity = VIBE_Velocity_Apply     @0x5d883c  (shadow / recolor pass)
enum class ShapeMode { kNormal = 0, kAdvanced = 1, kVelocity = 2 };

// The SHAPBANK shape blit-to-surface primitive (gui_surface_render). Blits shape
// `shapeNr` of gilde.gfx record `gfxId` at (x,y) into `surf`, clipped to the
// current clip rect, in `mode`. Returns true if the shape existed and drew (the
// engine's Animation_Basic returns the drawn flag — used by the slider right-cap
// fallback). `Coord_Transform` shape lookup + depth-2 decode are inside.
struct IGuiSurface {
    virtual ~IGuiSurface() = default;
    // VIBE_Coord_Push @0x5d8ae8 — set the clip rect (x0,y0,x1,y1) for subsequent blits.
    virtual void SetClip(int x0, int y0, int x1, int y1) = 0;
    // Blit shape; returns false if the shape record is absent (engine's "==0").
    virtual bool BlitShape(int gfxId, int shapeNr, int x, int y, ShapeMode mode) = 0;
    // Shape metrics (Coord_Transform fields +6 width, +0x0A height). Returns false
    // if absent; w/h set to 0 then.
    virtual bool ShapeSize(int gfxId, int shapeNr, int* w, int* h) = 0;
    // VIBE_Property_Set @0x4159dc — the bitmap-font pen (right-advancing). Draw `s`
    // (CP1251) with the active font at (x,y) into the surface; `mode` mirrors the
    // a5 flags (1 outline, 2 advanced, 4 shadow). Returns the drawn pixel width.
    virtual int DrawText(int x, int y, const char* s, ShapeMode shadow,
                         u8 r, u8 g, u8 b) = 0;
    // VIBE_Property_Get @0x4152cc — measured width of `s` in the active font.
    virtual int TextWidth(const char* s) = 0;
};

// Geometry + state of one horizontal slider widget, as Entity_InteractionLogic
// @0x41078c reads it from the 740-byte widget record (all in screen pixels).
struct HSliderWidget {
    int gfxId   = 0;     // node+0x0C (the _SLIDER_GOLD_WAAGERECHT record)
    int x = 0, y = 0;    // node x/y (word@0x10 / word@0x12)
    int w = 0;           // node w (word@0x14)
    int value = 0;       // node+0x78
    int minV = 0;        // node+0x7C
    int maxV = 0;        // node+0x80
    int range = 0;       // node+0x88 (track travel length in px)
    int target = 0;      // node+0x8C (target marker; == value when none)
    u16 flags = 0;       // node+0x84 (v149): bit1 horiz, bit3(8), bit4(0x10), bit5(0x20)...
    bool hoverLeft = false;   // +/- left button hovered
    bool hoverRight = false;  // +/- right button hovered
    bool hasOptionText = false;   // node+0xD8 (discrete picker -> draw option text)
    const char* optionText = nullptr;  // current option string (Interaction_Handler result)
    const char* numberText = nullptr;  // "%i" of value when !hasOptionText
    const char* minText = nullptr;     // "%i" of min (drawn at left, flags&4)
    const char* maxText = nullptr;     // "%i" of max (drawn at right, flags&4)
};

// VIBE_Entity_InteractionLogic @0x41078c (horizontal branch) — draw one slider to
// the surface, 1:1. Reconstructed from raw x86 (see /tmp/guild_options1to1/brief.md).
void RenderHSlider(IGuiSurface& gs, const HSliderWidget& wgt);

} // namespace guild::gui
